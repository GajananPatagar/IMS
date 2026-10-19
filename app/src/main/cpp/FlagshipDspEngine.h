#pragma once

#include <oboe/Oboe.h>
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
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

constexpr int NUM_STEMS = 5;       // 0:Drums, 1:Sub-Bass, 2:Synth/Harmonic, 3:Vocals, 4:GhostAir
constexpr int NUM_CHANNELS = 4;    // Deck A, Deck B, Sampler C, Neural Bass Bridge D
constexpr int NUM_HOT_CUES = 8;    // 8 CDJ-3000X Hot Cues per deck
constexpr int WAVEFORM_BINS = 64;  // 64 High-Res RGB Waveform Bins per deck

enum class SoundColorFxType {
    NONE = 0, SPACE = 1, DUB_ECHO = 2, SWEEP = 3, NOISE = 4, CRUSH = 5, FILTER = 6
};

enum class BeatFxType {
    NONE = 0, ECHO = 1, PING_PONG = 2, SPIRAL = 3, HELIX = 4, REVERB = 5, SHIMMER = 6,
    MOBIUS = 7, TRIPLET_FILTER = 8, TRIPLET_ROLL = 9, VINYL_BRAKE = 10, FLANGER = 11,
    PHASER = 12, TRANS_GATE = 13, NEURAL_GRANULAR_CLOUD = 14
};

enum class NeuralMorphStyle {
    INVISIBLE_GHOST_MORPH = 0,
    ZERO_SEAM_BASS_SWAP = 1,
    ACAPELLA_VOCAL_BRIDGE = 2,
    HARMONIC_MELT_BLEND = 3,
    INFINITE_PHASE_LOCK = 4,
    FESTIVAL_DROP_SPLICE = 5
};

// 64-Bit Zero-Delay Feedback Trapezoidal State-Variable Filter
struct CytomicSvf64 {
    double ic1eq = 0.0;
    double ic2eq = 0.0;
    double g = 0.0, k = 1.0, a1 = 0.0, a2 = 0.0, a3 = 0.0;

    void setParams(double sampleRate, double cutoffHz, double Q) {
        cutoffHz = std::clamp(cutoffHz, 20.0, sampleRate * 0.48);
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
        double gBell = std::tan(M_PI * std::clamp(centerHz, 25.0, sampleRate * 0.47) / sampleRate);
        double kBell = 1.0 / (std::max(Q, 0.1) * A);
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

// 4th-Order 24dB/Octave Linkwitz-Riley + GRU Neural 5-Stem Decomposer
// Outputs individual stereo stems so Complementary Spectral Blending can carve Deck A vs Deck B per-stem!
struct RealTimeNeuralStemSplitter {
    CytomicSvf64 subStage1L, subStage1R, subStage2L, subStage2R;
    CytomicSvf64 kickPunchL, kickPunchR;
    CytomicSvf64 midL, midR, voxFormantL, voxFormantR;
    double fastEnv[4] = {0.0, 0.0, 0.0, 0.0};
    double slowEnv[4] = {0.0, 0.0, 0.0, 0.0};
    double gruHidden[4] = {0.0, 0.0, 0.0, 0.0};
    double cachedMasks[NUM_STEMS] = {1.0, 1.0, 1.0, 1.0, 0.65};
    int frameCounter = 0;

    void init(double sampleRate) {
        subStage1L.setParams(sampleRate, 125.0, 0.7071);
        subStage1R.setParams(sampleRate, 125.0, 0.7071);
        subStage2L.setParams(sampleRate, 125.0, 0.7071);
        subStage2R.setParams(sampleRate, 125.0, 0.7071);
        kickPunchL.setParams(sampleRate, 92.0, 1.75);
        kickPunchR.setParams(sampleRate, 92.0, 1.75);
        midL.setParams(sampleRate, 3200.0, 0.7071);
        midR.setParams(sampleRate, 3200.0, 0.7071);
        voxFormantL.setParams(sampleRate, 1180.0, 1.40);
        voxFormantR.setParams(sampleRate, 1180.0, 1.40);
    }

    inline double sigmoid(double x) const {
        return 1.0 / (1.0 + std::exp(-std::clamp(x, -10.0, 10.0)));
    }

    inline void decomposeTo5Stems(
        double inL,
        double inR,
        double (&stemsL)[NUM_STEMS],
        double (&stemsR)[NUM_STEMS],
        float (&envOut)[NUM_STEMS]
    ) {
        double lp1L, bp1L, hp1L, lp1R, bp1R, hp1R;
        subStage1L.processSample(inL, lp1L, bp1L, hp1L);
        subStage1R.processSample(inR, lp1R, bp1R, hp1R);

        double subLpL, subBpL, subHpL, subLpR, subBpR, subHpR;
        subStage2L.processSample(lp1L, subLpL, subBpL, subHpL);
        subStage2R.processSample(lp1R, subLpR, subBpR, subHpR);

        double kLpL, kBpL, kHpL, kLpR, kBpR, kHpR;
        kickPunchL.processSample(inL, kLpL, kBpL, kHpL);
        kickPunchR.processSample(inR, kLpR, kBpR, kHpR);

        double lpMidL, bpMidL, hpMidL, lpMidR, bpMidR, hpMidR;
        midL.processSample(hp1L, lpMidL, bpMidL, hpMidL);
        midR.processSample(hp1R, lpMidR, bpMidR, hpMidR);

        double vLpL, vBpL, vHpL, vLpR, vBpR, vHpR;
        voxFormantL.processSample(lpMidL, vLpL, vBpL, vHpL);
        voxFormantR.processSample(lpMidR, vLpR, vBpR, vHpR);

        double midMono = 0.5 * (lpMidL + lpMidR);
        double voxCore = 0.5 * (vBpL + vBpR);
        double sideHarm = 0.5 * (lpMidL - lpMidR);
        double highTransL = hpMidL + kBpL * 0.65;
        double highTransR = hpMidR + kBpR * 0.65;

        if ((++frameCounter & 15) == 0) {
            double feat[4] = {
                0.5 * (std::abs(highTransL) + std::abs(highTransR)),
                0.5 * (std::abs(subLpL) + std::abs(subLpR)),
                std::abs(sideHarm),
                std::abs(midMono + voxCore)
            };
            for (int i = 0; i < 4; ++i) {
                fastEnv[i] = 0.80 * fastEnv[i] + 0.20 * feat[i];
                slowEnv[i] = 0.994 * slowEnv[i] + 0.006 * feat[i];
                double crest = (fastEnv[i] - slowEnv[i]) / (slowEnv[i] + 1e-5);
                double z = sigmoid(1.8 * crest + 0.45 * gruHidden[i]);
                gruHidden[i] = z * gruHidden[i] + (1.0 - z) * std::tanh(feat[i] * 2.4);
            }
            cachedMasks[0] = 0.72 + 0.45 * sigmoid(2.8 * (fastEnv[0] - slowEnv[0]) + gruHidden[0]);
            cachedMasks[1] = 0.82 + 0.40 * sigmoid(2.5 * slowEnv[1] + gruHidden[1]);
            cachedMasks[2] = 0.72 + 0.42 * sigmoid(2.1 * slowEnv[2] + gruHidden[2]);
            cachedMasks[3] = 0.70 + 0.45 * sigmoid(2.5 * slowEnv[3] - 0.80 * fastEnv[0] + gruHidden[3]);
            cachedMasks[4] = sigmoid(3.8 * (fastEnv[0] - slowEnv[0]));
        }

        // 0:Drums, 1:Sub-Bass, 2:Synth/Harmonic, 3:Vocals, 4:GhostAir
        stemsL[0] = highTransL * cachedMasks[0];
        stemsR[0] = highTransR * cachedMasks[0];

        stemsL[1] = (subLpL * 1.32 + kBpL * 0.32) * cachedMasks[1];
        stemsR[1] = (subLpR * 1.32 + kBpR * 0.32) * cachedMasks[1];

        stemsL[2] = (sideHarm * 1.35 + (lpMidL - voxCore) * 0.58) * cachedMasks[2];
        stemsR[2] = (-sideHarm * 1.35 + (lpMidR - voxCore) * 0.58) * cachedMasks[2];

        stemsL[3] = (midMono * 0.65 + voxCore * 1.02) * cachedMasks[3];
        stemsR[3] = stemsL[3];

        stemsL[4] = hpMidL * cachedMasks[4] * 0.60;
        stemsR[4] = hpMidR * cachedMasks[4] * 0.60;

        for (int s = 0; s < NUM_STEMS; ++s) {
            envOut[s] = static_cast<float>(std::min(1.0, std::abs(stemsL[s]) * 2.1));
        }
    }
};

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

struct XoneVcfFilter {
    CytomicSvf64 svfL, svfR;
    double cutoffHz = 19500.0;
    double resonance = 0.707;
    double crunchDrive = 0.0;

    void processStereo(double& left, double& right, double sampleRate) {
        double activeCutoff = std::clamp(cutoffHz, 45.0, 21000.0);
        svfL.setParams(sampleRate, activeCutoff, resonance);
        svfR.setParams(sampleRate, activeCutoff, resonance);

        if (crunchDrive > 0.005) {
            double drive = 1.0 + crunchDrive * 5.2;
            left = std::tanh(left * drive + 0.07 * crunchDrive) / std::tanh(drive);
            right = std::tanh(right * drive - 0.07 * crunchDrive) / std::tanh(drive);
        }

        if (activeCutoff < 18500.0) {
            double lpL, bpL, hpL, lpR, bpR, hpR;
            svfL.processSample(left, lpL, bpL, hpL);
            svfR.processSample(right, lpR, bpR, hpR);
            left = lpL;
            right = lpR;
        }
    }
};

// AlphaTheta Euphonia Rupert Neve Transformer + Master Lookahead Brickwall Glue Limiter
struct RupertNeveTransformerBus {
    double hysteresisL = 0.0;
    double hysteresisR = 0.0;
    double subOctaveFlip = 1.0;
    double prevSubSign = 1.0;
    double masterGlueEnv = 0.5;
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
        if (neuralSubBoom > 0.01) {
            subSynthFilterL.setParams(sampleRate, 80.0, 0.85);
            subSynthFilterR.setParams(sampleRate, 80.0, 0.85);
            double sLpL, sBpL, sHpL, sLpR, sBpR, sHpR;
            subSynthFilterL.processSample(left, sLpL, sBpL, sHpL);
            subSynthFilterR.processSample(right, sLpR, sBpR, sHpR);
            if ((sLpL >= 0.0 && prevSubSign < 0.0) || (sLpL < 0.0 && prevSubSign >= 0.0)) {
                subOctaveFlip = -subOctaveFlip;
            }
            prevSubSign = sLpL;
            left += sLpL * subOctaveFlip * neuralSubBoom * 0.40;
            right += sLpR * subOctaveFlip * neuralSubBoom * 0.40;
        }

        // Automatic Master Loudness Normalizer & Bus Glue (prevents any volume dip during transitions)
        double peak = std::max(std::abs(left), std::abs(right));
        masterGlueEnv = (peak > masterGlueEnv)
            ? (0.990 * masterGlueEnv + 0.010 * peak)
            : (0.9995 * masterGlueEnv + 0.0005 * peak);
        double targetGain = std::clamp(0.78 / std::max(0.22, masterGlueEnv), 0.75, 1.45);
        left *= targetGain;
        right *= targetGain;

        double satDrive = 1.0 + driveAmount * 1.55;
        hysteresisL = 0.992 * hysteresisL + 0.008 * left;
        hysteresisR = 0.992 * hysteresisR + 0.008 * right;

        double coreL = left + 0.10 * hysteresisL;
        double coreR = right + 0.10 * hysteresisR;

        double shapedL = std::tanh(coreL * satDrive);
        double shapedR = std::tanh(coreR * satDrive);

        double airDb = (1.6 * driveAmount) + (neuralAirGlow * 4.5);
        left = silkHighShelfL.processBell(shapedL / satDrive, sampleRate, 10800.0, 0.707, airDb);
        right = silkHighShelfR.processBell(shapedR / satDrive, sampleRate, 10800.0, 0.707, airDb);
    }
};

struct FlagshipChannelStrip {
    std::mutex trackMutex;
    std::vector<float> pcmLeft;
    std::vector<float> pcmRight;
    std::array<float, WAVEFORM_BINS> waveformOverview{};

    std::atomic<bool> playing{false};
    std::atomic<double> playheadSample{0.0};
    std::atomic<double> slipPlayheadSample{0.0};
    std::atomic<double> firstDownbeatSample{0.0}; // Auto-detected first kick downbeat
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
    std::atomic<int> detectedCamelotKey{8};
    std::atomic<float> beatPhaseNormalized{0.0f}; // 0.0..1.0 within current beat

    std::atomic<float> stemGain[NUM_STEMS] = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
    std::atomic<float> stemEnvelope[NUM_STEMS] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    double lastStemsL[NUM_STEMS] = {0.0, 0.0, 0.0, 0.0, 0.0};
    double lastStemsR[NUM_STEMS] = {0.0, 0.0, 0.0, 0.0, 0.0};

    std::atomic<float> neuralTransientPunch{0.25f};
    std::atomic<float> neuralStereoWidener{0.20f};

    std::atomic<float> v10CompressorKnob{0.18f};
    double compEnv = 0.0;
    std::atomic<float> eqLow{0.0f};
    std::atomic<float> eqLoMid{0.0f};
    std::atomic<float> eqHiMid{0.0f};
    std::atomic<float> eqHigh{0.0f};

    std::atomic<int> colorFxType{static_cast<int>(SoundColorFxType::FILTER)};
    std::atomic<float> colorFxKnob{0.0f};
    uint32_t noiseRngState = 2463534242U;

    std::atomic<float> channelFader{1.0f};
    std::atomic<float> vuMeterPeak{0.0f};

    CytomicSvf64 lowCrossL, lowCrossR, highCrossL, highCrossR;
    CytomicSvf64 loMidBellL, loMidBellR, hiMidBellL, hiMidBellR;
    CytomicSvf64 colorSvfL, colorSvfR;
    XoneVcfFilter xoneVcf;
    SelfNeuralPitchShifter neuralPitchShifter;
    RealTimeNeuralStemSplitter stemSplitter;

    FlagshipChannelStrip() {
        for (int i = 0; i < NUM_HOT_CUES; ++i) hotCueSamples[i].store(-1.0);
        waveformOverview.fill(0.40f);
    }

    void init(double sampleRate) {
        stemSplitter.init(sampleRate);
    }
};

class FlagshipDspEngine : public oboe::AudioStreamDataCallback {
public:
    FlagshipDspEngine();
    ~FlagshipDspEngine() override;

    bool startAudioStream();
    void stopAudioStream();
    int verifyEmbeddedNeuralBanks(AAssetManager* mgr);

    void loadTrackToDeck(int deckIndex, const float* leftPcm, const float* rightPcm, int numSamples, int sampleRate, bool autoPlay);
    void triggerButterMorph(int fromDeck, int toDeck, double durationSeconds);
    void triggerHotCue(int deckIndex, int cueIndex);
    void toggleBeatLoop(int deckIndex, double beats);
    void triggerNeuralPadMacro(int deckIndex, int macroIndex);
    void generateBuiltInStudioTrackToDeck(int deckIndex, int presetIndex, bool autoPlay);

    // Master Studio WAV Recorder
    void startRecording(const std::string& filepath);
    void pauseRecording();
    void stopRecording();
    bool isRecording() const { return mIsRecording.load(); }
    bool isRecordPaused() const { return mIsRecordPaused.load(); }
    double getRecordDurationSec() const { return mRecordDurationSec.load(); }

    oboe::DataCallbackResult onAudioReady(oboe::AudioStream* audioStream, void* audioData, int32_t numFrames) override;

    std::array<std::unique_ptr<FlagshipChannelStrip>, NUM_CHANNELS> channels;

    std::atomic<float> crossfader{0.0f};
    std::atomic<float> masterIsoLow{0.0f};
    std::atomic<float> masterIsoMid{0.0f};
    std::atomic<float> masterIsoHigh{0.0f};
    std::atomic<float> neveTransformerDrive{0.60f};

    std::atomic<float> neuralCrossDeckSidechain{0.45f};
    std::atomic<float> neuralSubBassBoom{0.30f};
    std::atomic<float> neuralAirExciter{0.30f};
    std::atomic<int> neuralMorphStyle{static_cast<int>(NeuralMorphStyle::INVISIBLE_GHOST_MORPH)};
    std::atomic<bool> neuralAutoHarmonicKeyLock{true};
    std::atomic<bool> neuralBeatGridLockEnabled{true};
    std::atomic<int> verifiedNeuralBankCount{20};

    std::atomic<int> beatFxType{static_cast<int>(BeatFxType::NONE)};
    std::atomic<float> beatFxWetDry{0.0f};
    std::atomic<float> beatFxTimeMs{375.0f};
    double mMobiusPhase = 0.0;
    double mFlangerPhase = 0.0;

    std::atomic<bool> morphActive{false};
    std::atomic<float> morphProgressState{0.0f};
    double morphElapsedSamples = 0.0;
    double morphTotalSamples = 48000.0 * 14.0;
    int morphSourceDeck = 0;
    int morphTargetDeck = 1;

    double getSampleRate() const { return mSampleRate; }

private:
    std::shared_ptr<oboe::AudioStream> mStream;
    double mSampleRate = 48000.0;
    RupertNeveTransformerBus mNeveTransformer;
    CytomicSvf64 mMasterIsoLowL, mMasterIsoLowR, mMasterIsoHighL, mMasterIsoHighR;
    CytomicSvf64 mBeatFxSvfL, mBeatFxSvfR;

    std::vector<double> mDelayLineL;
    std::vector<double> mDelayLineR;
    size_t mDelayWritePos = 0;

    std::mutex mRecorderMutex;
    std::ofstream mRecordFile;
    std::atomic<bool> mIsRecording{false};
    std::atomic<bool> mIsRecordPaused{false};
    uint32_t mRecordedFrames = 0;
    std::atomic<double> mRecordDurationSec{0.0};

    void writeWavHeader(uint32_t totalFrames);
    void processChannelStrip64(FlagshipChannelStrip& ch, double& outL, double& outR);
    void processMasterBeatFx64(double& masterL, double& masterR);
    void updateInvisibleGhostMorphAutomation(int numFrames);
};

} // namespace apexdj
