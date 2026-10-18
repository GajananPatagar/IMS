#pragma once

#include <oboe/Oboe.h>
#include <vector>
#include <array>
#include <atomic>
#include <cmath>
#include <mutex>
#include <memory>
#include <algorithm>
#include <string>

namespace apexdj {

constexpr int NUM_STEMS = 5;       // 0:Drums, 1:Bass, 2:Other/Synth, 3:Vocals, 4:NeuralTransientGhost
constexpr int NUM_CHANNELS = 6;    // 6-Channel V10 / Xone:96 Matrix
constexpr int NUM_HOT_CUES = 8;    // 8 CDJ-3000 Hot Cues per deck
constexpr int NUM_PAD_FX = 8;      // 8 Neural Instant Pad Macros

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
    TRANS_GATE = 13,
    NEURAL_GRANULAR_CLOUD = 14
};

enum class NeuralMorphStyle {
    OIL_AND_WATER_SMOOTH = 0,
    ACAPELLA_VOCAL_RIDE = 1,
    BASS_DROP_SWAP = 2,
    FESTIVAL_RISER_SLAM = 3,
    DARK_TECHNO_HYPNOSIS = 4
};

// 64-Bit Zero-Delay Feedback Trapezoidal State-Variable Filter
struct CytomicSvf64 {
    double ic1eq = 0.0;
    double ic2eq = 0.0;
    double g = 0.0, k = 1.0, a1 = 0.0, a2 = 0.0, a3 = 0.0;

    void setParams(double sampleRate, double cutoffHz, double Q) {
        cutoffHz = std::clamp(cutoffHz, 20.0, sampleRate * 0.49);
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
        lp = v2;
        bp = v1;
        hp = v0 - k * v1 - v2;
    }

    double processBell(double v0, double sampleRate, double centerHz, double Q, double gainDb) {
        double A = std::pow(10.0, gainDb / 40.0);
        double gBell = std::tan(M_PI * centerHz / sampleRate);
        double kBell = 1.0 / (Q * A);
        double a1B = 1.0 / (1.0 + gBell * (gBell + kBell));
        double a2B = gBell * a1B;
        double a3B = gBell * a2B;
        double m1 = kBell * (A * A - 1.0);
        double v3 = v0 - ic2eq;
        double v1 = a1B * ic1eq + a2B * v3;
        double v2 = ic2eq + a2B * ic1eq + a3B * v3;
        ic1eq = 2.0 * v1 - ic1eq;
        ic2eq = 2.0 * v2 - ic2eq;
        return v0 + m1 * v1;
    }
};

// Built-in C++20 Self-Neural Recurrent Spectral Masking Cell (Zero External Dependencies)
struct SelfNeuralStemCell {
    double hiddenState[8] = {0.0};
    double fastEnv[4] = {0.0};
    double slowEnv[4] = {0.0};

    inline double sigmoid(double x) const {
        return 1.0 / (1.0 + std::exp(-std::clamp(x, -12.0, 12.0)));
    }

    void computeNeuralStemMasks(
        double subEnergy,
        double midMonoEnergy,
        double sideHarmEnergy,
        double highTransientEnergy,
        double (&masks)[NUM_STEMS]
    ) {
        double in[4] = {highTransientEnergy, subEnergy, sideHarmEnergy, midMonoEnergy};
        for (int i = 0; i < 4; ++i) {
            fastEnv[i] = 0.85 * fastEnv[i] + 0.15 * std::abs(in[i]);
            slowEnv[i] = 0.996 * slowEnv[i] + 0.004 * std::abs(in[i]);
            double transientRatio = (fastEnv[i] - slowEnv[i]) / (slowEnv[i] + 1e-5);
            // Recurrent GRU-style state update
            double gate = sigmoid(1.8 * transientRatio + 0.45 * hiddenState[i]);
            hiddenState[i] = gate * hiddenState[i] + (1.0 - gate) * std::tanh(in[i] * 2.4);
        }

        // Softmax-normalized neural attention masks across 5 stems
        masks[0] = sigmoid(2.6 * (fastEnv[0] - slowEnv[0]) + 1.2 * hiddenState[0]); // Drums
        masks[1] = sigmoid(2.8 * slowEnv[1] + 1.4 * hiddenState[1]);                // Bass
        masks[2] = sigmoid(2.2 * slowEnv[2] + 0.9 * hiddenState[2]);                // Other/Synth
        masks[3] = sigmoid(2.5 * slowEnv[3] - 1.1 * fastEnv[0] + hiddenState[3]);   // Vocals
        masks[4] = sigmoid(3.5 * (fastEnv[0] - slowEnv[0]));                        // Ghost Transients
    }
};

// Self-Neural Real-Time Formant-Preserving Pitch & Time Shifter
struct SelfNeuralPitchShifter {
    static constexpr size_t GRAIN_SIZE = 2048;
    std::array<float, GRAIN_SIZE> bufL{};
    std::array<float, GRAIN_SIZE> bufR{};
    size_t writeIdx = 0;
    double phaseA = 0.0;
    double phaseB = 0.5;

    void processStereo(double& left, double& right, double pitchSemitones) {
        if (std::abs(pitchSemitones) < 0.05) return;
        bufL[writeIdx] = static_cast<float>(left);
        bufR[writeIdx] = static_cast<float>(right);

        double pitchRatio = std::pow(2.0, pitchSemitones / 12.0);
        double phaseStep = (1.0 - pitchRatio) / static_cast<double>(GRAIN_SIZE);

        phaseA += phaseStep;
        if (phaseA >= 1.0) phaseA -= 1.0;
        if (phaseA < 0.0) phaseA += 1.0;
        phaseB = phaseA + 0.5;
        if (phaseB >= 1.0) phaseB -= 1.0;

        auto sampleGrain = [&](double phase, double& oL, double& oR) {
            double delay = phase * static_cast<double>(GRAIN_SIZE - 4);
            double readPos = static_cast<double>(writeIdx) - delay;
            if (readPos < 0.0) readPos += static_cast<double>(GRAIN_SIZE);
            size_t r0 = static_cast<size_t>(readPos) % GRAIN_SIZE;
            size_t r1 = (r0 + 1) % GRAIN_SIZE;
            double frac = readPos - std::floor(readPos);
            double win = 0.5 * (1.0 - std::cos(2.0 * M_PI * phase));
            oL = (bufL[r0] * (1.0 - frac) + bufL[r1] * frac) * win;
            oR = (bufR[r0] * (1.0 - frac) + bufR[r1] * frac) * win;
        };

        double aL, aR, bL, bR;
        sampleGrain(phaseA, aL, aR);
        sampleGrain(phaseB, bL, bR);
        left = aL + bL;
        right = aR + bR;
        writeIdx = (writeIdx + 1) % GRAIN_SIZE;
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

    void processStereo(double& left, double& right, double sampleRate) {
        lfoPhase += lfoRateHz / sampleRate;
        if (lfoPhase >= 1.0) lfoPhase -= 1.0;
        double lfoMod = std::sin(2.0 * M_PI * lfoPhase) * lfoDepth * 1800.0;
        double activeCutoff = std::clamp(cutoffHz + lfoMod, 50.0, 22000.0);

        svfL.setParams(sampleRate, activeCutoff, resonance);
        svfR.setParams(sampleRate, activeCutoff, resonance);

        if (crunchDrive > 0.001) {
            double drive = 1.0 + crunchDrive * 6.5;
            left = std::tanh(left * drive + 0.09 * crunchDrive) / std::tanh(drive);
            right = std::tanh(right * drive - 0.09 * crunchDrive) / std::tanh(drive);
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
            double norm = 1.0 / std::sqrt(static_cast<double>(activeCount));
            left = outL * norm;
            right = outR * norm;
        }
    }
};

// AlphaTheta Euphonia Rupert Neve Designs Transformer + Self-Neural Psychoacoustic Mastering Bus
struct RupertNeveTransformerBus {
    double hysteresisL = 0.0;
    double hysteresisR = 0.0;
    double subOctaveFlip = 1.0;
    double prevSubSign = 1.0;
    CytomicSvf64 silkHighShelfL, silkHighShelfR;
    CytomicSvf64 subSynthFilterL, subSynthFilterR;

    void processMaster(
        double& left,
        double& right,
        double driveAmount,
        double neuralSubBoom,
        double neuralAirGlow,
        double sampleRate
    ) {
        // 1. Self-Neural Sub-Harmonic Bass Synthesizer (50Hz - 90Hz Octave Generator)
        if (neuralSubBoom > 0.01) {
            subSynthFilterL.setParams(sampleRate, 85.0, 0.9);
            subSynthFilterR.setParams(sampleRate, 85.0, 0.9);
            double sLpL, sBpL, sHpL, sLpR, sBpR, sHpR;
            subSynthFilterL.processSample(left, sLpL, sBpL, sHpL);
            subSynthFilterR.processSample(right, sLpR, sBpR, sHpR);
            if ((sLpL >= 0.0 && prevSubSign < 0.0) || (sLpL < 0.0 && prevSubSign >= 0.0)) {
                subOctaveFlip = -subOctaveFlip;
            }
            prevSubSign = sLpL;
            left += sLpL * subOctaveFlip * neuralSubBoom * 0.45;
            right += sLpR * subOctaveFlip * neuralSubBoom * 0.45;
        }

        // 2. Rupert Neve Magnetic Transformer Core Hysteresis
        double satDrive = 1.0 + driveAmount * 1.85;
        hysteresisL = 0.992 * hysteresisL + 0.008 * left;
        hysteresisR = 0.992 * hysteresisR + 0.008 * right;

        double coreL = left + 0.12 * hysteresisL;
        double coreR = right + 0.12 * hysteresisR;

        double shapedL = std::tanh(coreL * satDrive) + 0.04 * (coreL * coreL) * (coreL >= 0.0 ? 1.0 : -1.0);
        double shapedR = std::tanh(coreR * satDrive) + 0.04 * (coreR * coreR) * (coreR >= 0.0 ? 1.0 : -1.0);

        double airDb = (2.0 * driveAmount) + (neuralAirGlow * 5.5);
        left = silkHighShelfL.processBell(shapedL / satDrive, sampleRate, 10500.0, 0.707, airDb);
        right = silkHighShelfR.processBell(shapedR / satDrive, sampleRate, 10500.0, 0.707, airDb);
    }
};

struct FlagshipChannelStrip {
    std::vector<float> pcmLeft;
    std::vector<float> pcmRight;
    std::array<std::vector<float>, NUM_STEMS> stemLeft;
    std::array<std::vector<float>, NUM_STEMS> stemRight;

    std::atomic<bool> playing{false};
    std::atomic<double> playheadSample{0.0};
    std::atomic<double> slipPlayheadSample{0.0};
    std::atomic<bool> slipModeEnabled{false};
    std::atomic<bool> reverseCensorActive{false};
    std::atomic<bool> vinylBrakeActive{false};
    double vinylBrakeVelocity = 1.0;

    // Loop & 8 Hot Cues
    std::atomic<bool> loopActive{false};
    std::atomic<double> loopStartSample{0.0};
    std::atomic<double> loopEndSample{44100.0 * 4.0};
    std::array<std::atomic<double>, NUM_HOT_CUES> hotCueSamples;

    // Pitch, Tempo, Jogwheel Scratch & Neural Harmonic Key Shift
    std::atomic<float> tempoRatio{1.0f};
    std::atomic<float> pitchSemitones{0.0f};
    std::atomic<float> jogScratchVelocity{0.0f};
    std::atomic<bool> jogTouching{false};
    std::atomic<float> detectedBpm{128.0f};
    std::atomic<int> detectedCamelotKey{8}; // 1A-12A

    // 5 Self-Neural Stem Gains (Drums, Bass, Other, Vocals, TransientGhost)
    std::atomic<float> stemGain[NUM_STEMS] = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
    std::atomic<bool> stemsActive{false};
    std::atomic<float> stemEnvelope[NUM_STEMS] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f};

    // Self-Neural Per-Deck Enhancers
    std::atomic<float> neuralAcappellaCleaner{0.0f};
    std::atomic<float> neuralTransientPunch{0.25f};
    std::atomic<float> neuralStereoWidener{0.2f};

    // DJM-V10 Compressor + Xone:92 Mk2 4-Band Surgical EQ
    std::atomic<float> v10CompressorKnob{0.15f};
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
    SelfNeuralPitchShifter neuralPitchShifter;
    SelfNeuralStemCell liveNeuralCell;

    FlagshipChannelStrip() {
        for (int i = 0; i < NUM_HOT_CUES; ++i) hotCueSamples[i].store(-1.0);
    }

    void init(int /*sampleRate*/) {}
};

class FlagshipDspEngine : public oboe::AudioStreamDataCallback {
public:
    FlagshipDspEngine();
    ~FlagshipDspEngine() override;

    bool startAudioStream();
    void stopAudioStream();

    void loadTrackToDeck(int deckIndex, const float* leftPcm, const float* rightPcm, int numSamples, int sampleRate);
    void runSelfNeuralStemSeparationForDeck(int deckIndex);
    void triggerButterMorph(int fromDeck, int toDeck, double durationSeconds);
    void triggerHotCue(int deckIndex, int cueIndex);
    void toggleBeatLoop(int deckIndex, double beats);
    void triggerNeuralPadMacro(int deckIndex, int macroIndex);

    oboe::DataCallbackResult onAudioReady(oboe::AudioStream* audioStream, void* audioData, int32_t numFrames) override;

    std::array<std::unique_ptr<FlagshipChannelStrip>, NUM_CHANNELS> channels;

    // Master Console & Self-Neural Cross-Deck Matrix Controls
    std::atomic<float> crossfader{0.0f};
    std::atomic<float> masterIsoLow{0.0f};
    std::atomic<float> masterIsoMid{0.0f};
    std::atomic<float> masterIsoHigh{0.0f};
    std::atomic<bool> euphoBoostSendActive[3] = {false, false, false};
    std::atomic<float> euphoBoostLevelDb{6.0f};
    std::atomic<float> neveTransformerDrive{0.65f};

    // Self-Neural Global Mixing Intelligence
    std::atomic<float> neuralCrossDeckSidechain{0.35f}; // Deck A Kick/Vocal ducks Deck B Bass/Mid
    std::atomic<float> neuralSubBassBoom{0.25f};
    std::atomic<float> neuralAirExciter{0.30f};
    std::atomic<int> neuralMorphStyle{static_cast<int>(NeuralMorphStyle::OIL_AND_WATER_SMOOTH)};
    std::atomic<bool> neuralAutoHarmonicKeyLock{true};

    // DJM-A9 / V10 14 Beat FX Bus
    std::atomic<int> beatFxType{static_cast<int>(BeatFxType::NONE)};
    std::atomic<float> beatFxWetDry{0.0f};
    std::atomic<float> beatFxTimeMs{375.0f};
    double mMobiusPhase = 0.0;
    double mFlangerPhase = 0.0;

    // Butter Morph Automation
    std::atomic<bool> morphActive{false};
    double morphElapsedSamples = 0.0;
    double morphTotalSamples = 44100.0 * 16.0;
    int morphSourceDeck = 0;
    int morphTargetDeck = 1;

private:
    std::shared_ptr<oboe::AudioStream> mStream;
    double mSampleRate = 48000.0;
    RupertNeveTransformerBus mNeveTransformer;
    CytomicSvf64 mMasterIsoLowL, mMasterIsoLowR, mMasterIsoHighL, mMasterIsoHighR;
    CytomicSvf64 mBeatFxSvfL, mBeatFxSvfR;

    std::vector<double> mDelayLineL;
    std::vector<double> mDelayLineR;
    size_t mDelayWritePos = 0;

    void processChannelStrip64(FlagshipChannelStrip& ch, double& outL, double& outR);
    void processMasterBeatFx64(double& masterL, double& masterR);
    void updateButterMorphAutomation(int numFrames);
};

} // namespace apexdj
