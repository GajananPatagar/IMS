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

constexpr int NUM_DECKS = 3;        // 0: Deck A, 1: Deck B, 2: Background Deck (Infinity Mode)
constexpr int WAVEFORM_BINS = 256;
constexpr int MAX_TRACK_SAMPLES = 44100 * 360; // 6 minutes stereo buffer per deck

struct CytomicSvf64 {
    double ic1eq = 0.0, ic2eq = 0.0;
    double g = 0.0, k = 1.0, a1 = 0.0, a2 = 0.0, a3 = 0.0, m1 = 0.0;
    double lastCutoff = -1.0, lastQ = -1.0, lastGainDb = -999.0;

    inline void setParams(double sampleRate, double cutoffHz, double Q) {
        cutoffHz = std::clamp(cutoffHz, 20.0, sampleRate * 0.47);
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

struct FlagshipChannelStrip {
    std::vector<float> pcmLeft;
    std::vector<float> pcmRight;
    std::atomic<int> loadedSamples{0};
    std::atomic<int> totalExpectedSamples{1};

    std::array<float, WAVEFORM_BINS * 3> rgbWaveformFlat{};
    std::atomic<bool> playing{false};
    std::atomic<bool> infinityLoopMode{false};
    std::atomic<double> playheadSample{1.0};
    std::atomic<int> nativeTrackSampleRate{44100};

    // Analyzed Cue Points & Tempo/Pitch Sync
    std::atomic<double> analyzedEntryCueSample{1.0};
    std::atomic<double> analyzedExitCueSample{44100.0 * 120.0};
    std::atomic<float> detectedBpm{124.0f};
    std::atomic<int> camelotKeyIndex{8};
    std::atomic<float> tempoRatio{1.0f};
    std::atomic<float> targetSyncRatio{1.0f};
    std::atomic<float> jogScratchVelocity{0.0f};
    std::atomic<bool> jogTouching{false};

    // Vocal Switch: ON = 100% Bit-Pure Stereo; OFF = Subtractive Mid-Vocal Canceler
    std::atomic<bool> vocalSwitchEnabled{true};

    // Only 3 Essential User Knobs + Internal AI Control Multipliers
    std::atomic<float> eqLow{0.0f};         // BASS Knob (-1.0 to +1.0)
    std::atomic<float> vcfCutoffKnob{0.5f}; // FILTER Knob (0.5 = Neutral)
    std::atomic<float> channelFader{1.0f};  // VOLUME Knob (0.0 to 1.0)

    // Internal Automatic Mixing Envelopes
    std::atomic<float> internalBassGain{1.0f};
    std::atomic<float> internalMidPocketDb{0.0f};
    std::atomic<float> internalFilterSweep{0.5f};
    std::atomic<float> liveSubBassEnv{0.0f};
    std::atomic<float> vuMeterPeak{0.0f};

    // Subtractive Vocal Canceler Filters & Internal EQ
    CytomicSvf64 voxHpFilter, voxLpFilter;
    CytomicSvf64 voxFormantNotch1, voxFormantNotch2;
    CytomicSvf64 bassShelfL, bassShelfR;
    CytomicSvf64 internalMidPocketL, internalMidPocketR;
    CytomicSvf64 vcfFilterL, vcfFilterR;

    FlagshipChannelStrip() {
        pcmLeft.resize(MAX_TRACK_SAMPLES, 0.0f);
        pcmRight.resize(MAX_TRACK_SAMPLES, 0.0f);
    }

    void init(int sampleRate) {
        voxHpFilter.setParams(sampleRate, 210.0, 0.7071);
        voxLpFilter.setParams(sampleRate, 4200.0, 0.7071);
        voxFormantNotch1.setBellParams(sampleRate, 1150.0, 1.4, -18.0);
        voxFormantNotch2.setBellParams(sampleRate, 2650.0, 1.4, -18.0);
        bassShelfL.setBellParams(sampleRate, 85.0, 0.7071, 0.0);
        bassShelfR.setBellParams(sampleRate, 85.0, 0.7071, 0.0);
        internalMidPocketL.setBellParams(sampleRate, 1400.0, 1.0, 0.0);
        internalMidPocketR.setBellParams(sampleRate, 1400.0, 1.0, 0.0);
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

    // 120 Built-In Distortion-Free Infinity Background Beats & Bass Grooves (#1 to #120)
    void loadInfinityBgmBeatPreset(int presetIndex);

    // Bidirectional 1000-Transition Smart Auto-Mixer
    void triggerSmartTransitionNow();

    bool startMasterRecording(const std::string& wavPath);
    void stopMasterRecording();
    bool isMasterRecording() const { return mRecordingActive.load(std::memory_order_acquire); }

    void seekNormalized(int deckIndex, float normalizedPos);
    void setVocalSwitch(int deckIndex, bool enabled);
    bool getVocalSwitch(int deckIndex) const;

    oboe::DataCallbackResult onAudioReady(oboe::AudioStream* audioStream, void* audioData, int32_t numFrames) override;

    std::array<std::unique_ptr<FlagshipChannelStrip>, NUM_DECKS> channels;

    // Crossfader applies ONLY to Deck A (-1.0) and Deck B (+1.0); Background Deck is independent!
    std::atomic<float> crossfader{-1.0f};
    std::atomic<float> bgmDeckLevel{0.42f};
    std::atomic<bool> neuralAutoMixEnabled{true};

    std::string getBrainTelemetry() const;

private:
    std::shared_ptr<oboe::AudioStream> mStream;
    double mSampleRate = 48000.0;

    CytomicSvf64 mSubsonicFilterL, mSubsonicFilterR;
    double mMasterLimiterEnv = 1.0;

    int mObbFd = -1;
    const float* mObbMappedFloats = nullptr;
    size_t mObbMappedBytes = 0;
    std::atomic<bool> mObbMapped{false};

    // Bidirectional Lead & Standby Queue State Machine
    std::atomic<int> mActiveLeadDeck{-1};      // -1: None yet, 0: Deck A is Lead, 1: Deck B is Lead
    std::atomic<int> mQueuedStandbyDeck{-1};   // Opposite deck armed & analyzed for transition
    std::atomic<bool> mTransitionInProgress{false};
    double mTransitionProgress = 0.0;
    double mTransitionDurationSec = 10.0;
    int mSelectedTransitionId = 1;             // Chosen out of 1..1000 transition profiles
    std::string mActiveBrainStateText = "Load Deck A or Deck B to Start";

    std::mutex mRecMutex;
    std::ofstream mRecFile;
    std::atomic<bool> mRecordingActive{false};
    uint32_t mRecordedDataBytes = 0;

    int mControlSubBlockCounter = 0;

    void analyzeBothDecksAndSelectTransition(int leadDeck, int incomingDeck);
    void updateControlRateCoefficients(FlagshipChannelStrip& ch);
    void processChannelStrip64(FlagshipChannelStrip& ch, double subSidechainDuck, double& outL, double& outR);
    void runBidirectionalSmartAutoMixer(int numFrames);
};

} // namespace apexdj
