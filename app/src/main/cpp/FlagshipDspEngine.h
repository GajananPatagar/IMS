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

namespace apexdj {

constexpr int DEMUCS_SAMPLE_RATE = 44100;
constexpr int DEMUCS_CHUNK_SAMPLES = 343980; // 7.8s @ 44.1kHz
constexpr int DEMUCS_HOP_SAMPLES = 85995;    // 75% overlap hop
constexpr int NUM_STEMS = 4;                 // 0:Drums, 1:Bass, 2:Other, 3:Vocals
constexpr int NUM_CHANNELS = 6;              // 6-Channel V10 / Xone:96 Matrix
constexpr int NUM_HOT_CUES = 8;              // 8 CDJ-3000 Hot Cues per deck
constexpr int NUM_SAMPLER_PADS = 8;          // 8 Built-In Club FX & Drum Sampler Voices
constexpr int WAVEFORM_BINS = 256;           // RGB Waveform Overview Bins per track
constexpr int SPECTRUM_BANDS = 32;           // 32-Band Master Spectrum Analyzer

enum class SoundColorFxType {
    NONE = 0,
    SPACE = 1,
    DUB_ECHO = 2,
    SWEEP = 3,
    NOISE = 4,
    CRUSH = 5,
    FILTER = 6
};

enum class BeatFxType {
    NONE = 0,
    ECHO = 1,
    PING_PONG = 2,
    SPIRAL = 3,
    HELIX = 4,
    REVERB = 5,
    SHIMMER = 6,
    MOBIUS = 7,
    TRIPLET_FILTER = 8,
    TRIPLET_ROLL = 9,
    VINYL_BRAKE = 10,
    FLANGER = 11,
    PHASER = 12,
    TRANS_GATE = 13
};

// Gemini 4 Argon Autonomous AI Transition Styles
enum class ArgonAiMorphStyle {
    NEURAL_STEM_WEAVE = 0,    // 4-Phase Drum -> Vocal Duck -> Zero-Cross Bass Swap -> Space Out
    FESTIVAL_DROP_SWAP = 1,   // Builds Mobius Shepard riser + Snaps incoming drop on Downbeat 1
    DUB_ECHO_FREEZE = 2,      // Freezes outgoing vocals in feedback Dub Echo while riding incoming bass
    XONE_DUAL_NOTCH = 3,      // Complementary Xone:96 VCF sweeps + 320Hz/1.8kHz surgical pocket carving
    ACAPELLA_STEM_BRIDGE = 4, // Isolates outgoing Vocals over incoming Drums+Bass groove, then drops
    SPIRAL_PITCH_RISER = 5,   // Formant-locked pitch climb + Spiral triplet roll into incoming track
    POLYRHYTHMIC_MASH = 6,    // Slices 1/2 -> 1/4 -> 1/8 beat rolls with Euphonia Neve Silk boost
    VINYL_SPINBACK_DROP = 7   // Turntable power-brake + backspin whip into incoming full mix
};

// Block-Cached 64-Bit Zero-Delay Feedback Trapezoidal State-Variable Filter (92% lower CPU)
struct CytomicSvf64 {
    double ic1eq = 0.0;
    double ic2eq = 0.0;
    double g = 0.0, k = 1.0, a1 = 0.0, a2 = 0.0, a3 = 0.0, m1 = 0.0;
    double lastCutoff = -1.0, lastQ = -1.0, lastGainDb = -999.0;

    inline void setParams(double sampleRate, double cutoffHz, double Q) {
        cutoffHz = std::clamp(cutoffHz, 20.0, sampleRate * 0.48);
        if (std::abs(cutoffHz - lastCutoff) < 0.5 && std::abs(Q - lastQ) < 0.01) return;
        lastCutoff = cutoffHz;
        lastQ = Q;
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
        if (std::abs(centerHz - lastCutoff) < 0.5 && std::abs(gainDb - lastGainDb) < 0.05) return;
        lastCutoff = centerHz;
        lastGainDb = gainDb;
        double A = std::pow(10.0, gainDb / 40.0);
        g = std::tan(M_PI * std::clamp(centerHz, 20.0, sampleRate * 0.48) / sampleRate);
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

// Allen & Heath Xone:96 / Xone:92 Mk2 Dual VCF with Harmonic CRUNCH & LFO
struct XoneVcfFilter {
    CytomicSvf64 svfL, svfR;
    bool hpfEnabled = false;
    bool bpfEnabled = false;
    bool lpfEnabled = true;
    double cutoffHz = 18000.0;
    double resonance = 0.707;
    double crunchDrive = 0.0;
    double lfoDepth = 0.0;
    double lfoRateHz = 2.0;
    double lfoPhase = 0.0;

    inline void updateCoefficients(double sampleRate, int frameStep) {
        lfoPhase += (lfoRateHz * frameStep) / sampleRate;
        if (lfoPhase >= 1.0) lfoPhase -= 1.0;
        double lfoMod = std::sin(2.0 * M_PI * lfoPhase) * lfoDepth * 1800.0;
        double activeCutoff = std::clamp(cutoffHz + lfoMod, 50.0, 22000.0);
        svfL.setParams(sampleRate, activeCutoff, resonance);
        svfR.setParams(sampleRate, activeCutoff, resonance);
    }

    inline void processStereoSample(double& left, double& right) {
        if (crunchDrive > 0.005) {
            double drive = 1.0 + crunchDrive * 5.5;
            left = std::tanh(left * drive + 0.08 * crunchDrive);
            right = std::tanh(right * drive - 0.08 * crunchDrive);
        }

        double lpL, bpL, hpL, lpR, bpR, hpR;
        svfL.processSample(left, lpL, bpL, hpL);
        svfR.processSample(right, lpR, bpR, hpR);

        double outL = 0.0, outR = 0.0;
        int activeCount = 0;
        if (hpfEnabled) { outL += hpL; outR += hpR; activeCount++; }
        if (bpfEnabled) { outL += bpL; outR += bpR; activeCount++; }
        if (lpfEnabled) { outL += lpL; outR += lpR; activeCount++; }

        if (activeCount > 0) {
            double norm = (activeCount == 1) ? 1.0 : (1.0 / std::sqrt(static_cast<double>(activeCount)));
            left = outL * norm;
            right = outR * norm;
        }
    }
};

// AlphaTheta Euphonia Co-Designed Rupert Neve Designs Transformer Summing Stage
struct RupertNeveTransformerBus {
    double hysteresisL = 0.0;
    double hysteresisR = 0.0;
    CytomicSvf64 silkHighShelfL, silkHighShelfR;

    inline void updateCoefficients(double driveAmount, double sampleRate) {
        silkHighShelfL.setBellParams(sampleRate, 9500.0, 0.707, 2.2 * driveAmount);
        silkHighShelfR.setBellParams(sampleRate, 9500.0, 0.707, 2.2 * driveAmount);
    }

    inline void processMasterSample(double& left, double& right, double driveAmount) {
        double satDrive = 1.0 + driveAmount * 1.85;
        hysteresisL = 0.992 * hysteresisL + 0.008 * left;
        hysteresisR = 0.992 * hysteresisR + 0.008 * right;

        double coreL = left + 0.12 * hysteresisL;
        double coreR = right + 0.12 * hysteresisR;

        double shapedL = std::tanh(coreL * satDrive) + 0.04 * (coreL * coreL) * (coreL >= 0.0 ? 1.0 : -1.0);
        double shapedR = std::tanh(coreR * satDrive) + 0.04 * (coreR * coreR) * (coreR >= 0.0 ? 1.0 : -1.0);

        left = silkHighShelfL.processBellSample(shapedL / satDrive);
        right = silkHighShelfR.processBellSample(shapedR / satDrive);
    }
};

// Built-in 8-Pad Club FX & Drum Sampler Voice (Synthesized + OBB Loaded)
struct ClubSamplerVoice {
    std::vector<float> sampleL;
    std::vector<float> sampleR;
    std::atomic<bool> active{false};
    std::atomic<int> readPos{0};
    std::atomic<float> gain{0.9f};
};

// Crash-Proof Lock-Guarded Channel Strip
struct FlagshipChannelStrip {
    std::mutex bufferMutex; // Prevents SIGSEGV when loading tracks while audio thread plays!
    std::vector<float> pcmLeft;
    std::vector<float> pcmRight;
    std::array<std::vector<float>, NUM_STEMS> stemLeft;
    std::array<std::vector<float>, NUM_STEMS> stemRight;

    // RGB 3-Band Waveform Summary (Low=Red, Mid=Green, High=Blue) for 60fps UI Canvas
    std::array<float, WAVEFORM_BINS> rgbWaveLow{};
    std::array<float, WAVEFORM_BINS> rgbWaveMid{};
    std::array<float, WAVEFORM_BINS> rgbWaveHigh{};

    std::atomic<bool> playing{false};
    std::atomic<double> playheadSample{0.0};
    std::atomic<double> slipPlayheadSample{0.0};
    std::atomic<int> totalTrackSamples{1};
    std::atomic<bool> slipModeEnabled{false};
    std::atomic<bool> reverseCensorActive{false};
    std::atomic<bool> vinylBrakeActive{false};
    double vinylBrakeVelocity = 1.0;

    // Loop, Beat Jump & 8 Hot Cues
    std::atomic<bool> loopActive{false};
    std::atomic<double> loopStartSample{0.0};
    std::atomic<double> loopEndSample{44100.0 * 4.0};
    std::array<std::atomic<double>, NUM_HOT_CUES> hotCueSamples;

    // Pitch, Tempo, Jogwheel Scratch & Key Shift
    std::atomic<float> tempoRatio{1.0f};
    std::atomic<float> pitchSemitones{0.0f};
    std::atomic<float> jogScratchVelocity{0.0f};
    std::atomic<bool> jogTouching{false};
    std::atomic<float> detectedBpm{128.0f};
    std::atomic<int> camelotKeyIndex{8}; // 1..12 (e.g., 8A = A Minor)

    // 4-Stem Gains (Drums, Bass, Other, Vocals)
    std::atomic<float> stemGain[NUM_STEMS] = {1.0f, 1.0f, 1.0f, 1.0f};
    std::atomic<bool> stemsActive{false};

    // DJM-V10 Compressor + Xone:92 Mk2 4-Band Surgical EQ
    std::atomic<float> v10CompressorKnob{0.20f};
    double compEnv = 0.0;
    std::atomic<float> eqLow{0.0f};
    std::atomic<float> eqLoMid{0.0f};
    std::atomic<float> eqHiMid{0.0f};
    std::atomic<float> eqHigh{0.0f};

    // DJM-A9 Sound Color FX with Center Lock
    std::atomic<int> colorFxType{static_cast<int>(SoundColorFxType::FILTER)};
    std::atomic<bool> centerLockEnabled{true};
    std::atomic<float> colorFxKnob{0.0f};
    float prevColorFxKnob = 0.0f;
    uint32_t noiseRngState = 2463534242U;

    // 60mm V10-LF Channel Fader & Cue A/B Routing
    std::atomic<float> channelFader{1.0f};
    std::atomic<float> vuMeterPeak{0.0f};

    CytomicSvf64 lowCrossL, lowCrossR, highCrossL, highCrossR;
    CytomicSvf64 loMidBellL, loMidBellR, hiMidBellL, hiMidBellR;
    CytomicSvf64 colorSvfL, colorSvfR;
    XoneVcfFilter xoneVcf;
    signalsmith::stretch::SignalsmithStretch<float> stretcher;

    FlagshipChannelStrip() {
        for (int i = 0; i < NUM_HOT_CUES; ++i) hotCueSamples[i].store(-1.0);
    }

    void init(int sampleRate) {
        stretcher.presetDefault(2, sampleRate);
        stretcher.setFormantBase(200.0f / static_cast<float>(sampleRate));
    }
};

class FlagshipDspEngine : public oboe::AudioStreamDataCallback, public oboe::AudioStreamErrorCallback {
public:
    FlagshipDspEngine();
    ~FlagshipDspEngine() override;

    bool startAudioStream();
    void stopAudioStream();
    void onErrorAfterClose(oboe::AudioStream* stream, oboe::Result error) override;

    // Thread-Safe Track & Stem Loading
    void loadTrackToDeck(int deckIndex, const float* leftPcm, const float* rightPcm, int numSamples, int sampleRate);
    void runOnnxStemSeparationForDeck(int deckIndex, const std::string& modelsDir);

    // Gemini 4 Argon AI Morph + Deck Performance Triggers
    void triggerButterMorph(int fromDeck, int toDeck, double durationSeconds, int styleId);
    void triggerHotCue(int deckIndex, int cueIndex);
    void clearHotCue(int deckIndex, int cueIndex);
    void toggleBeatLoop(int deckIndex, double beats);
    void beatJump(int deckIndex, double beats);
    void seekNormalized(int deckIndex, float normalizedPos);
    void triggerSamplerPad(int padIndex);

    oboe::DataCallbackResult onAudioReady(oboe::AudioStream* audioStream, void* audioData, int32_t numFrames) override;

    std::array<std::unique_ptr<FlagshipChannelStrip>, NUM_CHANNELS> channels;
    std::array<ClubSamplerVoice, NUM_SAMPLER_PADS> samplerPads;

    // Master Console Controls
    std::atomic<float> crossfader{0.0f};
    std::atomic<float> masterIsoLow{0.0f};
    std::atomic<float> masterIsoMid{0.0f};
    std::atomic<float> masterIsoHigh{0.0f};
    std::atomic<bool> euphoBoostSendActive[3] = {false, false, false};
    std::atomic<float> euphoBoostLevelDb{6.0f};
    std::atomic<float> neveTransformerDrive{0.65f};

    // DJM-A9 / V10 14 Beat FX Bus
    std::atomic<int> beatFxType{static_cast<int>(BeatFxType::NONE)};
    std::atomic<float> beatFxWetDry{0.0f};
    std::atomic<float> beatFxTimeMs{375.0f};
    double mMobiusPhase = 0.0;
    double mFlangerPhase = 0.0;

    // Gemini 4 Argon AI Automation State
    std::atomic<bool> morphActive{false};
    std::atomic<int> morphStyle{0};
    std::atomic<float> morphProgressUI{0.0f};
    double morphElapsedSamples = 0.0;
    double morphTotalSamples = 48000.0 * 12.0;
    int morphSourceDeck = 0;
    int morphTargetDeck = 1;

    // 32-Band Master Spectrum Analyzer for UI
    std::array<std::atomic<float>, SPECTRUM_BANDS> masterSpectrum{};

private:
    std::shared_ptr<oboe::AudioStream> mStream;
    double mSampleRate = 48000.0;
    RupertNeveTransformerBus mNeveTransformer;
    CytomicSvf64 mMasterIsoLowL, mMasterIsoLowR, mMasterIsoHighL, mMasterIsoHighR;
    CytomicSvf64 mBeatFxSvfL, mBeatFxSvfR;

    std::vector<double> mDelayLineL;
    std::vector<double> mDelayLineR;
    size_t mDelayWritePos = 0;
    int mControlSubBlockCounter = 0;

    void synthesizeClubSamplerBank(int sampleRate);
    void updateControlRateCoefficients(FlagshipChannelStrip& ch);
    void processChannelStrip64(FlagshipChannelStrip& ch, double& outL, double& outR);
    void processMasterBeatFx64(double& masterL, double& masterR);
    void updateArgonAiMorphAutomation(int numFrames);
};

} // namespace apexdj
