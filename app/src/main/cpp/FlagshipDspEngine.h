#pragma once

#include <oboe/Oboe.h>
#include <signalsmith-stretch.h>
#include <vector>
#include <array>
#include <atomic>
#include <cmath>
#include <mutex>
#include <memory>
#include <algorithm>
#include <string>
#include <fstream>

namespace apexdj {

constexpr int NUM_STEMS = 4;        // 0:Drums, 1:Bass, 2:Other/Music, 3:Vocals
constexpr int NUM_DECKS = 3;        // 0:Deck A, 1:Deck B, 2:Background Deck (0-Vocal Instrumental BGM)
constexpr int NUM_HOT_CUES = 8;
constexpr int WAVEFORM_BINS = 256;
constexpr int MAX_TRACK_SAMPLES = 44100 * 360; // 6 minutes stereo buffer per deck

// 64-Bit Zero-Delay Feedback State-Variable Filter (Linkwitz-Riley & Biquad Precision)
struct CytomicSvf64 {
    double ic1eq = 0.0, ic2eq = 0.0;
    double g = 0.0, k = 1.0, a1 = 0.0, a2 = 0.0, a3 = 0.0, m1 = 0.0;
    double lastCutoff = -1.0, lastQ = -1.0, lastGainDb = -999.0;

    inline void setParams(double sampleRate, double cutoffHz, double Q) {
        cutoffHz = std::clamp(cutoffHz, 18.0, sampleRate * 0.47);
        if (std::abs(cutoffHz - lastCutoff) < 0.25 && std::abs(Q - lastQ) < 0.005) return;
        lastCutoff = cutoffHz; lastQ = Q;
        g = std::tan(M_PI * cutoffHz / sampleRate);
        k = 1.0 / std::max(Q, 0.1);
        a1 = 1.0 / (1.0 + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    inline void processSample(double v0, double& lp, double& bp, double& hp) {
        double v3 = v0 - ic2eq;
        double v1 = a1 * ic1eq + a2 * v3;
        double v2 = ic2eq + a2 * ic1eq + a3 * v3;
        ic1eq = 2.0 * v1 - ic1eq;
        ic2eq = 2.0 * v2 - ic2eq;
        if (!std::isfinite(ic1eq)) ic1eq = 0.0;
        if (!std::isfinite(ic2eq)) ic2eq = 0.0;
        lp = v2;
        bp = v1;
        hp = v0 - k * v1 - v2;
    }

    inline void setBellParams(double sampleRate, double centerHz, double Q, double gainDb) {
        if (std::abs(centerHz - lastCutoff) < 0.25 && std::abs(gainDb - lastGainDb) < 0.02) return;
        lastCutoff = centerHz; lastGainDb = gainDb;
        double A = std::pow(10.0, gainDb / 40.0);
        g = std::tan(M_PI * std::clamp(centerHz, 20.0, sampleRate * 0.47) / sampleRate);
        k = 1.0 / std::max(Q * A, 0.1);
        a1 = 1.0 / (1.0 + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
        m1 = k * (A * A - 1.0);
    }

    inline double processBellSample(double v0) {
        double v3 = v0 - ic2eq;
        double v1 = a1 * ic1eq + a2 * v3;
        double v2 = ic2eq + a2 * ic1eq + a3 * v3;
        ic1eq = 2.0 * v1 - ic1eq;
        ic2eq = 2.0 * v2 - ic2eq;
        return v0 + m1 * v1;
    }
};

// 5000W PA-Grade Phase-Coherent Channel Strip
struct FlagshipChannelStrip {
    std::vector<float> pcmLeft;
    std::vector<float> pcmRight;
    std::atomic<int> loadedSamples{0};
    std::atomic<int> totalExpectedSamples{1};

    std::array<float, WAVEFORM_BINS * 3> rgbWaveformFlat{};
    std::atomic<bool> playing{false};
    std::atomic<double> playheadSample{0.0};
    std::atomic<int> nativeTrackSampleRate{44100};

    std::atomic<bool> loopActive{false};
    std::atomic<double> loopStartSample{0.0};
    std::atomic<double> loopEndSample{44100.0 * 4.0};
    std::array<std::atomic<double>, NUM_HOT_CUES> hotCueSamples;

    // 100% Locked Pitch/Tempo (Never changes randomly!)
    std::atomic<float> tempoRatio{1.0f};
    std::atomic<float> jogScratchVelocity{0.0f};
    std::atomic<bool> jogTouching{false};
    std::atomic<float> detectedBpm{124.0f};
    std::atomic<int> camelotKeyIndex{8};

    // Vocal ON/OFF Switch (Default ON for Deck A/B, Default OFF [0 Vocals] for Background Deck)
    std::atomic<bool> vocalSwitchEnabled{true};

    // 4-Stem Gains (0:Drums, 1:Bass, 2:Other/Music, 3:Vocals)
    std::atomic<float> stemGain[NUM_STEMS] = {1.0f, 1.0f, 1.0f, 1.0f};
    std::atomic<float> aiBassMultiplier{1.0f};
    std::atomic<float> aiVocalMultiplier{1.0f};

    // Phase-Coherent Crossover Filters for Vocal Isolation & Bass Swap (Butterworth Q=0.7071)
    CytomicSvf64 subSplitL, subSplitR;     // 150 Hz Sub-Bass Crossover (Keeps 5000W Subs 100% solid)
    CytomicSvf64 airSplitL, airSplitR;     // 5800 Hz High-Air Crossover
    CytomicSvf64 eqLowBellL, eqLowBellR;   // 90 Hz Punch
    CytomicSvf64 eqLoMidBellL, eqLoMidBellR; // 320 Hz Boxiness Cut
    CytomicSvf64 eqHiMidBellL, eqHiMidBellR; // 2.2 kHz Clarity
    CytomicSvf64 eqHighBellL, eqHighBellR;   // 10 kHz Air
    CytomicSvf64 vcfFilterL, vcfFilterR;

    std::atomic<float> liveSubBassEnv{0.0f};
    std::atomic<float> liveVocalEnv{0.0f};
    std::atomic<float> liveRmsEnergy{0.0f};

    std::atomic<float> eqLow{0.0f}, eqLoMid{0.0f}, eqHiMid{0.0f}, eqHigh{0.0f};
    std::atomic<float> vcfCutoffKnob{0.5f}; // 0.5 = Bypassed/Neutral
    std::atomic<float> colorFxKnob{0.0f};   // 0.0 = Bypassed/Neutral
    std::atomic<float> channelFader{1.0f};
    std::atomic<float> vuMeterPeak{0.0f};

    FlagshipChannelStrip() {
        pcmLeft.resize(MAX_TRACK_SAMPLES, 0.0f);
        pcmRight.resize(MAX_TRACK_SAMPLES, 0.0f);
        for (int i = 0; i < NUM_HOT_CUES; ++i) hotCueSamples[i].store(-1.0);
    }

    void init(int sampleRate) {
        subSplitL.setParams(sampleRate, 150.0, 0.7071);
        subSplitR.setParams(sampleRate, 150.0, 0.7071);
        airSplitL.setParams(sampleRate, 5800.0, 0.7071);
        airSplitR.setParams(sampleRate, 5800.0, 0.7071);
    }
};

class FlagshipDspEngine : public oboe::AudioStreamDataCallback, public oboe::AudioStreamErrorCallback {
public:
    FlagshipDspEngine();
    ~FlagshipDspEngine() override;

    bool startAudioStream();
    void stopAudioStream();
    void onErrorAfterClose(oboe::AudioStream* stream, oboe::Result error) override;

    bool mountObbBrain(const std::string& obbPath);
    bool isObbBrainMounted() const { return mObbMapped.load(std::memory_order_acquire); }

    void beginTrackLoad(int deckIndex, int totalExpectedSamples, int sampleRate);
    void appendPcmChunk(int deckIndex, const float* leftChunk, const float* rightChunk, int chunkSamples, int sampleRate);

    bool startMasterRecording(const std::string& wavPath);
    void stopMasterRecording();
    bool isMasterRecording() const { return mRecordingActive.load(std::memory_order_acquire); }

    void triggerHotCue(int deckIndex, int cueIndex);
    void toggleBeatLoop(int deckIndex, double beats);
    void seekNormalized(int deckIndex, float normalizedPos);
    void setVocalSwitch(int deckIndex, bool enabled);
    bool getVocalSwitch(int deckIndex) const;

    oboe::DataCallbackResult onAudioReady(oboe::AudioStream* audioStream, void* audioData, int32_t numFrames) override;

    std::array<std::unique_ptr<FlagshipChannelStrip>, NUM_DECKS> channels;

    std::atomic<float> crossfader{-1.0f}; // Starts cleanly on Deck A (-1.0)
    std::atomic<float> bgmDeckLevel{0.38f};
    std::atomic<float> masterIsoLow{0.0f}, masterIsoMid{0.0f}, masterIsoHigh{0.0f};
    std::atomic<float> neveTransformerDrive{0.15f}; // Subtle studio warmth, zero harsh distortion
    std::atomic<bool> neuralAutoMixEnabled{false};

    std::string getBrainTelemetry() const;

private:
    std::shared_ptr<oboe::AudioStream> mStream;
    double mSampleRate = 48000.0;

    // 5000W PA Protection: 30Hz Subsonic High-Pass Filter + Master EQ
    CytomicSvf64 mSubsonicFilterL, mSubsonicFilterR;
    CytomicSvf64 mMasterLowBellL, mMasterLowBellR;
    CytomicSvf64 mMasterMidBellL, mMasterMidBellR;
    CytomicSvf64 mMasterHighBellL, mMasterHighBellR;

    // Existing OBB Brain Memory Map
    int mObbFd = -1;
    const float* mObbMappedFloats = nullptr;
    size_t mObbMappedBytes = 0;
    std::atomic<bool> mObbMapped{true}; // Unlocked immediately if existing OBB is found or bypassed

    // Unidirectional Studio Phrase Auto-Mix State Machine
    int mActiveLeadDeck = 0; // 0 = Deck A, 1 = Deck B
    bool mTransitionInProgress = false;
    double mTransitionProgress = 0.0; // 0.0 to 1.0 over 16 bars (zero back-and-forth oscillation!)
    double mCooldownSeconds = 0.0;
    std::string mActiveBrainStateText = "5000W PA Bit-Transparent Engine Ready";

    // Master WAV Recorder
    std::mutex mRecMutex;
    std::ofstream mRecFile;
    std::atomic<bool> mRecordingActive{false};
    uint32_t mRecordedDataBytes = 0;

    int mControlSubBlockCounter = 0;

    void updateControlRateCoefficients(FlagshipChannelStrip& ch);
    void processChannelStrip64(FlagshipChannelStrip& ch, bool isBgmDeck, double subSidechainDuck, double& outL, double& outR);
    void runStudioPhraseAutoMixer(int numFrames);
};

} // namespace apexdj
