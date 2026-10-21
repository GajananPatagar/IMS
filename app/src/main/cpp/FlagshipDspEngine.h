#pragma once

#include <oboe/Oboe.h>
#include <signalsmith-stretch.h>
#include <onnxruntime_cxx_api.h>
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

constexpr int NUM_STEMS = 4;        // 0:Drums, 1:Bass, 2:Other/Harmonics, 3:Vocals
constexpr int NUM_DECKS = 3;        // 0:Deck A, 1:Deck B, 2:Background Deck (Continuous Neural Groove/BGM)
constexpr int NUM_HOT_CUES = 8;
constexpr int WAVEFORM_BINS = 256;
constexpr int MAX_TRACK_SAMPLES = 44100 * 300; // 5 minutes pre-allocated 0ms ring buffer per deck

struct CytomicSvf64 {
    double ic1eq = 0.0, ic2eq = 0.0;
    double g = 0.0, k = 1.0, a1 = 0.0, a2 = 0.0, a3 = 0.0, m1 = 0.0;
    double lastCutoff = -1.0, lastQ = -1.0, lastGainDb = -999.0;

    inline void setParams(double sampleRate, double cutoffHz, double Q) {
        cutoffHz = std::clamp(cutoffHz, 20.0, sampleRate * 0.48);
        if (std::abs(cutoffHz - lastCutoff) < 0.5 && std::abs(Q - lastQ) < 0.01) return;
        lastCutoff = cutoffHz; lastQ = Q;
        g = std::tan(M_PI * cutoffHz / sampleRate);
        k = 1.0 / std::max(Q, 0.1);
        a1 = 1.0 / (1.0 + g * (g + k));
        a2 = g * a1; a3 = g * a2;
    }

    inline void processSample(double v0, double& lp, double& bp, double& hp) {
        double v3 = v0 - ic2eq;
        double v1 = a1 * ic1eq + a2 * v3;
        double v2 = ic2eq + a2 * ic1eq + a3 * v3;
        ic1eq = 2.0 * v1 - ic1eq;
        ic2eq = 2.0 * v2 - ic2eq;
        if (!std::isfinite(ic1eq)) ic1eq = 0.0;
        if (!std::isfinite(ic2eq)) ic2eq = 0.0;
        lp = v2; bp = v1; hp = v0 - k * v1 - v2;
    }

    inline void setBellParams(double sampleRate, double centerHz, double Q, double gainDb) {
        if (std::abs(centerHz - lastCutoff) < 0.5 && std::abs(gainDb - lastGainDb) < 0.05) return;
        lastCutoff = centerHz; lastGainDb = gainDb;
        double A = std::pow(10.0, gainDb / 40.0);
        g = std::tan(M_PI * std::clamp(centerHz, 20.0, sampleRate * 0.48) / sampleRate);
        k = 1.0 / std::max(Q * A, 0.1);
        a1 = 1.0 / (1.0 + g * (g + k));
        a2 = g * a1; a3 = g * a2;
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

struct XoneVcfFilter {
    CytomicSvf64 svfL, svfR;
    bool hpfEnabled = false, bpfEnabled = false, lpfEnabled = true;
    double cutoffHz = 18000.0, resonance = 0.707, crunchDrive = 0.0;

    inline void updateCoefficients(double sampleRate) {
        double activeCutoff = std::clamp(cutoffHz, 50.0, 22000.0);
        svfL.setParams(sampleRate, activeCutoff, resonance);
        svfR.setParams(sampleRate, activeCutoff, resonance);
    }

    inline void processStereoSample(double& left, double& right) {
        if (crunchDrive > 0.005) {
            double drive = 1.0 + crunchDrive * 5.0;
            left = std::tanh(left * drive + 0.08 * crunchDrive);
            right = std::tanh(right * drive - 0.08 * crunchDrive);
        }
        double lpL, bpL, hpL, lpR, bpR, hpR;
        svfL.processSample(left, lpL, bpL, hpL);
        svfR.processSample(right, lpR, bpR, hpR);
        double outL = 0.0, outR = 0.0;
        int cnt = 0;
        if (hpfEnabled) { outL += hpL; outR += hpR; cnt++; }
        if (bpfEnabled) { outL += bpL; outR += bpR; cnt++; }
        if (lpfEnabled) { outL += lpL; outR += lpR; cnt++; }
        if (cnt > 0) {
            double norm = (cnt == 1) ? 1.0 : (1.0 / std::sqrt(static_cast<double>(cnt)));
            left = outL * norm; right = outR * norm;
        }
    }
};

struct RupertNeveTransformerBus {
    double hysteresisL = 0.0, hysteresisR = 0.0;
    CytomicSvf64 silkHighShelfL, silkHighShelfR;

    inline void updateCoefficients(double driveAmount, double sampleRate) {
        silkHighShelfL.setBellParams(sampleRate, 9500.0, 0.707, 2.2 * driveAmount);
        silkHighShelfR.setBellParams(sampleRate, 9500.0, 0.707, 2.2 * driveAmount);
    }

    inline void processMasterSample(double& left, double& right, double driveAmount) {
        double satDrive = 1.0 + driveAmount * 1.65;
        hysteresisL = 0.992 * hysteresisL + 0.008 * left;
        hysteresisR = 0.992 * hysteresisR + 0.008 * right;
        double coreL = left + 0.12 * hysteresisL;
        double coreR = right + 0.12 * hysteresisR;
        double shapedL = std::tanh(coreL * satDrive) + 0.035 * (coreL * coreL) * (coreL >= 0.0 ? 1.0 : -1.0);
        double shapedR = std::tanh(coreR * satDrive) + 0.035 * (coreR * coreR) * (coreR >= 0.0 ? 1.0 : -1.0);
        left = silkHighShelfL.processBellSample(shapedL / satDrive);
        right = silkHighShelfR.processBellSample(shapedR / satDrive);
    }
};

// 0ms Instant Progressive Streaming Deck Strip
struct FlagshipChannelStrip {
    std::vector<float> pcmLeft;
    std::vector<float> pcmRight;
    std::atomic<int> loadedSamples{0};
    std::atomic<int> totalExpectedSamples{1};

    std::array<float, WAVEFORM_BINS * 3> rgbWaveformFlat{};
    std::atomic<bool> playing{false};
    std::atomic<double> playheadSample{0.0};
    std::atomic<double> slipPlayheadSample{0.0};
    std::atomic<bool> slipModeEnabled{false};
    std::atomic<bool> reverseCensorActive{false};
    std::atomic<bool> vinylBrakeActive{false};
    double vinylBrakeVelocity = 1.0;

    std::atomic<bool> loopActive{false};
    std::atomic<double> loopStartSample{0.0};
    std::atomic<double> loopEndSample{44100.0 * 4.0};
    std::array<std::atomic<double>, NUM_HOT_CUES> hotCueSamples;

    std::atomic<float> tempoRatio{1.0f};
    std::atomic<float> pitchSemitones{0.0f};
    std::atomic<float> jogScratchVelocity{0.0f};
    std::atomic<bool> jogTouching{false};
    std::atomic<float> detectedBpm{128.0f};
    std::atomic<int> camelotKeyIndex{8};

    // 4-Stem Gains (0:Drums, 1:Bass, 2:Other, 3:Vocals) + AI Neural Brain Multipliers
    std::atomic<float> stemGain[NUM_STEMS] = {1.0f, 1.0f, 1.0f, 1.0f};
    std::atomic<float> aiStemMod[NUM_STEMS] = {1.0f, 1.0f, 1.0f, 1.0f};

    CytomicSvf64 stemSubFilterL, stemSubFilterR;
    CytomicSvf64 stemHighFilterL, stemHighFilterR;
    double transientFastEnv = 0.0, transientSlowEnv = 0.0;
    std::atomic<float> liveSubBassEnv{0.0f};
    std::atomic<float> liveVocalEnv{0.0f};
    std::atomic<float> liveTransientEnv{0.0f};

    std::atomic<float> v10CompressorKnob{0.25f};
    double compEnv = 0.0;
    std::atomic<float> eqLow{0.0f}, eqLoMid{0.0f}, eqHiMid{0.0f}, eqHigh{0.0f};
    std::atomic<float> colorFxKnob{0.0f};
    float prevColorFxKnob = 0.0f;

    std::atomic<float> channelFader{1.0f};
    std::atomic<float> vuMeterPeak{0.0f};

    CytomicSvf64 lowCrossL, lowCrossR, highCrossL, highCrossR;
    CytomicSvf64 loMidBellL, loMidBellR, hiMidBellL, hiMidBellR;
    CytomicSvf64 colorSvfL, colorSvfR;
    XoneVcfFilter xoneVcf;

    FlagshipChannelStrip() {
        pcmLeft.resize(MAX_TRACK_SAMPLES, 0.0f);
        pcmRight.resize(MAX_TRACK_SAMPLES, 0.0f);
        for (int i = 0; i < NUM_HOT_CUES; ++i) hotCueSamples[i].store(-1.0);
    }

    void init(int sampleRate) {
        stemSubFilterL.setParams(sampleRate, 160.0, 0.707);
        stemSubFilterR.setParams(sampleRate, 160.0, 0.707);
        stemHighFilterL.setParams(sampleRate, 2800.0, 0.707);
        stemHighFilterR.setParams(sampleRate, 2800.0, 0.707);
    }
};

class FlagshipDspEngine : public oboe::AudioStreamDataCallback, public oboe::AudioStreamErrorCallback {
public:
    FlagshipDspEngine();
    ~FlagshipDspEngine() override;

    bool startAudioStream();
    void stopAudioStream();
    void onErrorAfterClose(oboe::AudioStream* stream, oboe::Result error) override;

    // Mandatory 1.5GB OBB Neural Brain Mounter (mmap64)
    bool mountObbBrain(const std::string& obbPath);
    bool isObbBrainMounted() const { return mObbMapped.load(std::memory_order_acquire); }

    // 0ms Instant Progressive Track Loading
    void beginTrackLoad(int deckIndex, int totalExpectedSamples, int sampleRate);
    void appendPcmChunk(int deckIndex, const float* leftChunk, const float* rightChunk, int chunkSamples, int sampleRate);

    // Live Master WAV Recorder
    bool startMasterRecording(const std::string& wavPath);
    void stopMasterRecording();
    bool isMasterRecording() const { return mRecordingActive.load(std::memory_order_acquire); }

    void triggerHotCue(int deckIndex, int cueIndex);
    void toggleBeatLoop(int deckIndex, double beats);
    void seekNormalized(int deckIndex, float normalizedPos);

    oboe::DataCallbackResult onAudioReady(oboe::AudioStream* audioStream, void* audioData, int32_t numFrames) override;

    std::array<std::unique_ptr<FlagshipChannelStrip>, NUM_DECKS> channels;

    std::atomic<float> crossfader{0.0f};
    std::atomic<float> bgmDeckLevel{0.45f}; // Deck 2 (Background Deck) continuous groove level
    std::atomic<float> masterIsoLow{0.0f}, masterIsoMid{0.0f}, masterIsoHigh{0.0f};
    std::atomic<float> neveTransformerDrive{0.65f};
    std::atomic<bool> neuralAutoMixEnabled{false};

    std::string getBrainTelemetry() const;

private:
    std::shared_ptr<oboe::AudioStream> mStream;
    double mSampleRate = 48000.0;
    RupertNeveTransformerBus mNeveTransformer;
    CytomicSvf64 mMasterIsoLowL, mMasterIsoLowR, mMasterIsoHighL, mMasterIsoHighR;

    // 1.5GB OBB Memory-Mapped Neural Brain State
    int mObbFd = -1;
    const float* mObbMappedFloats = nullptr;
    size_t mObbMappedBytes = 0;
    size_t mObbBrainCursor = 0;
    std::atomic<bool> mObbMapped{false};

    // Dynamic Non-Fixed Neural Brain Auto-Mix State
    double mBrainPhaseAccumulator = 0.0;
    int mDominantLeadDeck = 0; // 0 = Deck A, 1 = Deck B
    double mDynamicCrossfadeTarget = -1.0;
    double mCurrentNeuralXf = -1.0;
    std::string mActiveBrainStateText = "OBB Neural Brain Standby";

    // Master WAV File Recorder State
    std::mutex mRecMutex;
    std::ofstream mRecFile;
    std::atomic<bool> mRecordingActive{false};
    uint32_t mRecordedDataBytes = 0;

    int mControlSubBlockCounter = 0;

    void updateControlRateCoefficients(FlagshipChannelStrip& ch);
    void processChannelStrip64(FlagshipChannelStrip& ch, double subSidechainDuck, double vocalSidechainDuck, double& outL, double& outR);
    void runDynamicObbBrainDirector(int numFrames);
};

} // namespace apexdj
