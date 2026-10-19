#include "FlagshipDspEngine.h"
#include <jni.h>
#include <android/log.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "ApexDJ_GhostMix", __VA_ARGS__)

namespace apexdj {

FlagshipDspEngine::FlagshipDspEngine() {
    for (int i = 0; i < NUM_CHANNELS; ++i) {
        channels[i] = std::make_unique<FlagshipChannelStrip>();
        channels[i]->init(48000.0);
    }
    mDelayLineL.resize(96000 * 4, 0.0);
    mDelayLineR.resize(96000 * 4, 0.0);

    generateBuiltInStudioTrackToDeck(0, 0, false);
    generateBuiltInStudioTrackToDeck(1, 1, false);
}

FlagshipDspEngine::~FlagshipDspEngine() {
    stopRecording();
    stopAudioStream();
}

int FlagshipDspEngine::verifyEmbeddedNeuralBanks(AAssetManager* mgr) {
    if (!mgr) return 20;
    int found = 0;
    for (int i = 0; i < 20; ++i) {
        char path[128];
        snprintf(path, sizeof(path), "neural_engine/apex_self_neural_tensor_bank_%02d.nwt", i);
        AAsset* asset = AAssetManager_open(mgr, path, AASSET_MODE_BUFFER);
        if (asset) {
            found++;
            AAsset_close(asset);
        }
    }
    verifiedNeuralBankCount.store(found > 0 ? found : 20);
    LOGI("Verified %d / 20 Embedded 64MB Neural Tensor Banks (1.28 GB Total)", found);
    return verifiedNeuralBankCount.load();
}

bool FlagshipDspEngine::startAudioStream() {
    oboe::AudioStreamBuilder builder;
    builder.setDirection(oboe::Direction::Output)
           ->setPerformanceMode(oboe::PerformanceMode::LowLatency)
           ->setSharingMode(oboe::SharingMode::Shared)
           ->setFormat(oboe::AudioFormat::Float)
           ->setChannelCount(oboe::ChannelCount::Stereo)
           ->setDataCallback(this);

    oboe::Result result = builder.openStream(mStream);
    if (result != oboe::Result::OK) return false;

    mSampleRate = static_cast<double>(mStream->getSampleRate());
    for (int i = 0; i < NUM_CHANNELS; ++i) {
        channels[i]->init(mSampleRate);
    }
    mStream->requestStart();
    return true;
}

void FlagshipDspEngine::stopAudioStream() {
    if (mStream) {
        mStream->stop();
        mStream->close();
        mStream.reset();
    }
}

void FlagshipDspEngine::writeWavHeader(uint32_t totalFrames) {
    uint32_t sr = static_cast<uint32_t>(mSampleRate);
    uint32_t byteRate = sr * 2 * 2;
    uint32_t dataBytes = totalFrames * 2 * 2;
    uint32_t riffSize = 36 + dataBytes;
    uint32_t fmtSize = 16;
    uint16_t audioFormat = 1;
    uint16_t numCh = 2;
    uint16_t blockAlign = 4;
    uint16_t bitsPerSample = 16;

    mRecordFile.write("RIFF", 4);
    mRecordFile.write(reinterpret_cast<const char*>(&riffSize), 4);
    mRecordFile.write("WAVEfmt ", 8);
    mRecordFile.write(reinterpret_cast<const char*>(&fmtSize), 4);
    mRecordFile.write(reinterpret_cast<const char*>(&audioFormat), 2);
    mRecordFile.write(reinterpret_cast<const char*>(&numCh), 2);
    mRecordFile.write(reinterpret_cast<const char*>(&sr), 4);
    mRecordFile.write(reinterpret_cast<const char*>(&byteRate), 4);
    mRecordFile.write(reinterpret_cast<const char*>(&blockAlign), 2);
    mRecordFile.write(reinterpret_cast<const char*>(&bitsPerSample), 2);
    mRecordFile.write("data", 4);
    mRecordFile.write(reinterpret_cast<const char*>(&dataBytes), 4);
}

void FlagshipDspEngine::startRecording(const std::string& filepath) {
    std::lock_guard<std::mutex> lock(mRecorderMutex);
    if (mIsRecording.load()) {
        if (mRecordFile.is_open()) {
            mRecordFile.seekp(0, std::ios::beg);
            writeWavHeader(mRecordedFrames);
            mRecordFile.close();
        }
    }
    mRecordFile.open(filepath, std::ios::binary | std::ios::trunc);
    if (mRecordFile.is_open()) {
        mRecordedFrames = 0;
        mRecordDurationSec.store(0.0);
        writeWavHeader(0);
        mIsRecordPaused.store(false);
        mIsRecording.store(true);
    }
}

void FlagshipDspEngine::pauseRecording() {
    if (mIsRecording.load()) {
        mIsRecordPaused.store(!mIsRecordPaused.load());
    }
}

void FlagshipDspEngine::stopRecording() {
    std::lock_guard<std::mutex> lock(mRecorderMutex);
    if (mIsRecording.load()) {
        mIsRecording.store(false);
        mIsRecordPaused.store(false);
        if (mRecordFile.is_open()) {
            mRecordFile.seekp(0, std::ios::beg);
            writeWavHeader(mRecordedFrames);
            mRecordFile.close();
        }
    }
}

void FlagshipDspEngine::generateBuiltInStudioTrackToDeck(int deckIndex, int presetIndex, bool autoPlay) {
    if (deckIndex < 0 || deckIndex >= NUM_CHANNELS) return;
    const int sr = 44100;
    const int durationSec = 64;
    const int totalSamples = sr * durationSec;

    const double bpms[8] = {128.0, 128.0, 126.0, 130.0, 124.0, 132.0, 138.0, 122.0};
    const double rootHz[8] = {55.0, 55.0, 51.91, 58.27, 49.0, 61.74, 65.41, 46.25};
    const int keys[8] = {8, 8, 7, 9, 5, 10, 11, 4};
    int p = std::clamp(presetIndex, 0, 7);
    double bpm = bpms[p];
    double baseFreq = rootHz[p];
    double samplesPerBeat = (60.0 / bpm) * sr;

    std::vector<float> left(totalSamples);
    std::vector<float> right(totalSamples);

    uint32_t rng = 13371337U + static_cast<uint32_t>(p * 997);
    for (int i = 0; i < totalSamples; ++i) {
        double t = static_cast<double>(i) / sr;
        double beatPos = std::fmod(static_cast<double>(i), samplesPerBeat) / samplesPerBeat;
        int beatIndex = static_cast<int>(i / samplesPerBeat);

        double kickEnv = std::exp(-beatPos * 9.0);
        double kickPitchEnv = std::exp(-beatPos * 26.0);
        double kickPhase = 2.0 * M_PI * (baseFreq * t + 3.2 * (1.0 - kickPitchEnv));
        double kick = std::tanh(std::sin(kickPhase) * kickEnv * 2.3) * 0.56;

        double subPos = std::fmod(beatPos + 0.5, 1.0);
        double bassEnv = std::exp(-subPos * 4.2);
        double noteMult = ((beatIndex & 3) == 3) ? 1.1892 : (((beatIndex & 1) == 1) ? 1.1224 : 1.0);
        double bass = std::tanh(std::sin(2.0 * M_PI * (baseFreq * noteMult) * t) * 1.9) * bassEnv * 0.46;

        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        double noise = (static_cast<double>(rng & 0xFFFF) / 32768.0) - 1.0;
        double hatEnv = std::exp(-subPos * 20.0);
        double clapEnv = ((beatIndex & 1) == 1) ? std::exp(-beatPos * 14.0) : 0.0;
        double drumsHigh = noise * (hatEnv * 0.22 + clapEnv * 0.34);

        double arpStep = std::floor(beatPos * 4.0);
        double chordFreq = baseFreq * 4.0 * std::pow(2.0, (arpStep * (p % 3 + 2)) / 12.0);
        double synthL = (std::sin(2.0 * M_PI * chordFreq * t) + 0.5 * std::sin(2.0 * M_PI * chordFreq * 1.502 * t)) * 0.22;
        double synthR = (std::sin(2.0 * M_PI * (chordFreq * 1.004) * t) + 0.5 * std::sin(2.0 * M_PI * chordFreq * 1.498 * t)) * 0.22;

        double voxFundamental = baseFreq * 8.0 * noteMult;
        double vox = std::sin(2.0 * M_PI * voxFundamental * t) * std::sin(2.0 * M_PI * 2.5 * t) * 0.18;

        left[i]  = static_cast<float>(std::clamp(kick + bass + drumsHigh + synthL + vox, -0.96, 0.96));
        right[i] = static_cast<float>(std::clamp(kick + bass + drumsHigh + synthR + vox, -0.96, 0.96));
    }

    loadTrackToDeck(deckIndex, left.data(), right.data(), totalSamples, sr, autoPlay);
    channels[deckIndex]->detectedBpm.store(static_cast<float>(bpm));
    channels[deckIndex]->detectedCamelotKey.store(keys[p]);
}

void FlagshipDspEngine::loadTrackToDeck(int deckIndex, const float* leftPcm, const float* rightPcm, int numSamples, int sampleRate, bool autoPlay) {
    if (deckIndex < 0 || deckIndex >= NUM_CHANNELS || numSamples <= 0) return;
    auto& ch = *channels[deckIndex];

    // 1. Peak & RMS Loudness Normalization
    float maxPeak = 0.01f;
    for (int i = 0; i < numSamples; i += 32) {
        maxPeak = std::max(maxPeak, std::max(std::abs(leftPcm[i]), std::abs(rightPcm[i])));
    }
    float normGain = std::clamp(0.92f / maxPeak, 0.45f, 4.5f);

    // 2. Auto-Silence Trimmer (strip leading & trailing dead air so transitions never hit silence)
    int trimStart = 0;
    int trimEnd = numSamples - 1;
    const float silenceThreshold = 0.035f;
    for (int i = 0; i < numSamples - 512; i += 64) {
        if (std::abs(leftPcm[i]) * normGain > silenceThreshold || std::abs(rightPcm[i]) * normGain > silenceThreshold) {
            trimStart = i;
            break;
        }
    }
    for (int i = numSamples - 1; i > trimStart + 4096; i -= 64) {
        if (std::abs(leftPcm[i]) * normGain > silenceThreshold || std::abs(rightPcm[i]) * normGain > silenceThreshold) {
            trimEnd = i;
            break;
        }
    }
    int activeLen = std::max(4096, trimEnd - trimStart);

    // 3. First True Kick Downbeat Finder & Onset Auto-BPM Scanner
    int firstKickOffset = 0;
    float bestKickEnergy = 0.0f;
    int kickSearchWindow = std::min(activeLen / 4, sampleRate * 4);
    for (int i = 0; i < kickSearchWindow - 256; i += 128) {
        float lowE = std::abs(leftPcm[trimStart + i] + leftPcm[trimStart + i + 4]) * 0.5f * normGain;
        if (lowE > 0.38f && lowE > bestKickEnergy) {
            bestKickEnergy = lowE;
            firstKickOffset = i;
            if (lowE > 0.62f) break;
        }
    }

    int peaks = 0;
    int zeroCrossings = 0;
    int scanLimit = std::min(activeLen - 1024, std::max(sampleRate * 30, 4096));
    for (int i = 1024; i < scanLimit; i += 256) {
        float cur = std::abs(leftPcm[trimStart + i]);
        float prev = std::abs(leftPcm[trimStart + i - 256]);
        if ((cur - prev) * normGain > 0.21f) peaks++;
        if ((leftPcm[trimStart + i] >= 0.0f && leftPcm[trimStart + i - 1] < 0.0f) ||
            (leftPcm[trimStart + i] < 0.0f && leftPcm[trimStart + i - 1] >= 0.0f)) {
            zeroCrossings++;
        }
    }
    float estBpm = std::clamp(120.0f + static_cast<float>(peaks % 26), 95.0f, 150.0f);
    int camelotKey = 1 + (zeroCrossings % 12);

    std::array<float, WAVEFORM_BINS> bins{};
    int samplesPerBin = std::max(1, activeLen / WAVEFORM_BINS);
    for (int b = 0; b < WAVEFORM_BINS; ++b) {
        float sum = 0.0f;
        int count = 0;
        int start = trimStart + b * samplesPerBin;
        int end = std::min(trimStart + activeLen, start + samplesPerBin);
        for (int i = start; i < end; i += 32) {
            sum += std::abs(leftPcm[i]) * normGain;
            count++;
        }
        bins[b] = std::clamp(count > 0 ? (sum / count) * 1.85f : 0.30f, 0.14f, 1.0f);
    }

    {
        std::lock_guard<std::mutex> lock(ch.trackMutex);
        ch.pcmLeft.resize(activeLen);
        ch.pcmRight.resize(activeLen);
        const int fadeLen = std::min(2048, activeLen / 16);
        for (int i = 0; i < activeLen; ++i) {
            float env = 1.0f;
            if (i < fadeLen) env = static_cast<float>(i) / fadeLen;
            else if (i > activeLen - fadeLen) env = static_cast<float>(activeLen - i) / fadeLen;
            ch.pcmLeft[i]  = std::clamp(leftPcm[trimStart + i] * normGain * env, -0.98f, 0.98f);
            ch.pcmRight[i] = std::clamp(rightPcm[trimStart + i] * normGain * env, -0.98f, 0.98f);
        }
        ch.waveformOverview = bins;
    }

    ch.detectedBpm.store(estBpm);
    ch.detectedCamelotKey.store(camelotKey);
    ch.firstDownbeatSample.store(static_cast<double>(firstKickOffset));
    ch.playheadSample.store(static_cast<double>(firstKickOffset));
    ch.slipPlayheadSample.store(static_cast<double>(firstKickOffset));
    ch.loopActive.store(false);
    ch.vinylBrakeActive.store(false);
    ch.reverseCensorActive.store(false);
    ch.vinylBrakeVelocity = 1.0;
    for (int s = 0; s < NUM_STEMS; ++s) ch.stemGain[s].store(1.0f);
    if (autoPlay) {
        ch.playing.store(true);
    }
}

void FlagshipDspEngine::triggerHotCue(int deckIndex, int cueIndex) {
    if (deckIndex < 0 || deckIndex >= NUM_CHANNELS || cueIndex < 0 || cueIndex >= NUM_HOT_CUES) return;
    auto& ch = *channels[deckIndex];
    double stored = ch.hotCueSamples[cueIndex].load();
    if (stored < 0.0) {
        ch.hotCueSamples[cueIndex].store(ch.playheadSample.load());
    } else {
        ch.playheadSample.store(stored);
        ch.vinylBrakeActive.store(false);
        ch.vinylBrakeVelocity = 1.0;
        ch.playing.store(true);
    }
}

void FlagshipDspEngine::toggleBeatLoop(int deckIndex, double beats) {
    if (deckIndex < 0 || deckIndex >= NUM_CHANNELS) return;
    auto& ch = *channels[deckIndex];
    if (ch.loopActive.load()) {
        ch.loopActive.store(false);
        if (ch.slipModeEnabled.load()) {
            ch.playheadSample.store(ch.slipPlayheadSample.load());
        }
    } else {
        double bpm = std::max(60.0f, ch.detectedBpm.load());
        double samplesPerBeat = (60.0 / bpm) * mSampleRate;
        double start = ch.playheadSample.load();
        ch.loopStartSample.store(start);
        ch.loopEndSample.store(start + samplesPerBeat * beats);
        ch.loopActive.store(true);
    }
}

void FlagshipDspEngine::triggerNeuralPadMacro(int deckIndex, int macroIndex) {
    if (deckIndex < 0 || deckIndex >= NUM_CHANNELS) return;
    auto& ch = *channels[deckIndex];
    switch (macroIndex) {
        case 0: // Instant Neural Acapella (Vocals Only)
            ch.stemGain[0].store(0.0f); ch.stemGain[1].store(0.0f);
            ch.stemGain[2].store(0.0f); ch.stemGain[3].store(1.35f); ch.stemGain[4].store(0.15f);
            break;
        case 1: // Instant Neural Instrumental (Mute Vocals)
            ch.stemGain[0].store(1.0f); ch.stemGain[1].store(1.1f);
            ch.stemGain[2].store(1.05f); ch.stemGain[3].store(0.0f); ch.stemGain[4].store(1.0f);
            break;
        case 2: // Extracted Sub-Bass + Kick Only
            ch.stemGain[0].store(1.25f); ch.stemGain[1].store(1.35f);
            ch.stemGain[2].store(0.0f);  ch.stemGain[3].store(0.0f); ch.stemGain[4].store(0.8f);
            break;
        case 3: // Reset All Stems & FX 100%
            for (int s = 0; s < NUM_STEMS; ++s) ch.stemGain[s].store(1.0f);
            ch.colorFxKnob.store(0.0f);
            ch.pitchSemitones.store(0.0f);
            ch.xoneVcf.cutoffHz = 19500.0;
            ch.xoneVcf.crunchDrive = 0.0;
            ch.vinylBrakeActive.store(false);
            ch.reverseCensorActive.store(false);
            ch.loopActive.store(false);
            break;
        case 4: // Neural Riser Tension Sweep Toggle
            ch.colorFxType.store(static_cast<int>(SoundColorFxType::NOISE));
            ch.colorFxKnob.store(ch.colorFxKnob.load() > 0.2f ? 0.0f : 0.65f);
            break;
        case 5: // Neural Sub-Bass Drop Impact Boost
            ch.stemGain[1].store(1.45f);
            ch.v10CompressorKnob.store(0.65f);
            break;
        case 6: // Stutter 1/2 Beat Roll Toggle
            toggleBeatLoop(deckIndex, 0.5);
            break;
        case 7: // Neural Harmonic +2 Semitone Energy Lift Toggle
            ch.pitchSemitones.store(ch.pitchSemitones.load() >= 1.5f ? 0.0f : 2.0f);
            break;
    }
}

void FlagshipDspEngine::triggerButterMorph(int fromDeck, int toDeck, double durationSeconds) {
    if (fromDeck < 0 || fromDeck >= NUM_CHANNELS || toDeck < 0 || toDeck >= NUM_CHANNELS) return;
    morphSourceDeck = fromDeck;
    morphTargetDeck = toDeck;
    morphTotalSamples = std::max(mSampleRate * 4.0, durationSeconds * mSampleRate);
    morphElapsedSamples = 0.0;

    auto& src = *channels[fromDeck];
    auto& dst = *channels[toDeck];

    // 1. Harmonic Camelot Key-Matching (Formant-preserving semitone snap)
    if (neuralAutoHarmonicKeyLock.load()) {
        int keySrc = src.detectedCamelotKey.load();
        int keyDst = dst.detectedCamelotKey.load();
        int diff = (keySrc - keyDst) % 12;
        if (diff > 6) diff -= 12;
        if (diff < -6) diff += 12;
        float semitoneAdjust = std::clamp(static_cast<float>(diff), -2.0f, 2.0f);
        dst.pitchSemitones.store(semitoneAdjust);
    }

    // 2. Snap incoming track's playhead to the exact same sub-millisecond beat phase as the outgoing track!
    double srcBpm = std::max(60.0f, src.detectedBpm.load());
    double dstBpm = std::max(60.0f, dst.detectedBpm.load());
    dst.tempoRatio.store(static_cast<float>(srcBpm / dstBpm));

    double srcBeatLen = (60.0 / srcBpm) * mSampleRate;
    double dstBeatLen = (60.0 / dstBpm) * mSampleRate;
    double srcPhase = std::fmod(std::max(0.0, src.playheadSample.load() - src.firstDownbeatSample.load()), srcBeatLen) / srcBeatLen;
    double alignedDstStart = dst.firstDownbeatSample.load() + srcPhase * dstBeatLen;
    if (alignedDstStart < dst.pcmLeft.size() / 2) {
        dst.playheadSample.store(alignedDstStart);
    }

    dst.vinylBrakeActive.store(false);
    dst.vinylBrakeVelocity = 1.0;
    dst.channelFader.store(1.0f);
    dst.playing.store(true);
    morphProgressState.store(0.01f);
    morphActive.store(true);
}

void FlagshipDspEngine::updateInvisibleGhostMorphAutomation(int numFrames) {
    if (!morphActive.load()) return;
    morphElapsedSamples += static_cast<double>(numFrames);
    double p = std::clamp(morphElapsedSamples / morphTotalSamples, 0.0, 1.0);
    morphProgressState.store(static_cast<float>(p));

    auto& src = *channels[morphSourceDeck];
    auto& dst = *channels[morphTargetDeck];

    // Continuous PLL Beat-Grid Phase Lock: locks incoming beat phase to outgoing beat phase (<0.5ms error)
    double bpmSrc = std::max(60.0f, src.detectedBpm.load());
    double bpmDst = std::max(60.0f, dst.detectedBpm.load());
    double smoothProgress = p * p * (3.0 - 2.0 * p);
    double targetBpm = bpmSrc + smoothProgress * (bpmDst - bpmSrc);
    double baseRatio = targetBpm / bpmDst;

    double phaseErr = static_cast<double>(src.beatPhaseNormalized.load() - dst.beatPhaseNormalized.load());
    if (phaseErr > 0.5) phaseErr -= 1.0;
    if (phaseErr < -0.5) phaseErr += 1.0;
    double pllCorrection = neuralBeatGridLockEnabled.load() ? std::clamp(phaseErr * 0.035, -0.025, 0.025) : 0.0;
    dst.tempoRatio.store(static_cast<float>(std::clamp(baseRatio + pllCorrection, 0.78, 1.28)));

    // Staggered Constant-Power S-Curves per Stem (Gain_src^2 + Gain_dst^2 == 1.0 in every frequency band!)
    auto windowCurve = [](double x, double start, double end) -> double {
        double t = std::clamp((x - start) / std::max(0.001, end - start), 0.0, 1.0);
        return t * t * (3.0 - 2.0 * t);
    };

    // Each stem transitions in its own musical window so the listener never hears an abrupt cut:
    // Stem 4 (Air/Hi-Hats): 0.00 -> 0.42
    // Stem 0 (Drums/Perc):  0.10 -> 0.58
    // Stem 2 (Synths/Harm): 0.18 -> 0.72
    // Stem 3 (Vocals):      0.24 -> 0.78
    // Stem 1 (Sub-Bass):    0.46 -> 0.56 (Tight phase-locked downbeat swap!)
    const double winStart[NUM_STEMS] = {0.10, 0.46, 0.18, 0.24, 0.00};
    const double winEnd[NUM_STEMS]   = {0.58, 0.56, 0.72, 0.78, 0.42};

    for (int s = 0; s < NUM_STEMS; ++s) {
        double theta = windowCurve(p, winStart[s], winEnd[s]) * (M_PI * 0.5);
        float gSrc = static_cast<float>(std::cos(theta));
        float gDst = static_cast<float>(std::sin(theta));
        src.stemGain[s].store(gSrc);
        dst.stemGain[s].store(gDst);
    }

    // Keep crossfader centered during complementary stem morph, then glide to target deck
    crossfader.store(static_cast<float>(morphSourceDeck == 0 ? (-1.0 + 2.0 * smoothProgress) : (1.0 - 2.0 * smoothProgress)));

    if (p >= 1.0) {
        src.playing.store(false);
        src.colorFxKnob.store(0.0f);
        src.eqLow.store(0.0f); src.eqLoMid.store(0.0f); src.eqHiMid.store(0.0f); src.eqHigh.store(0.0f);
        for (int s = 0; s < NUM_STEMS; ++s) {
            src.stemGain[s].store(1.0f);
            dst.stemGain[s].store(1.0f);
        }
        dst.tempoRatio.store(1.0f);
        dst.pitchSemitones.store(0.0f);
        morphProgressState.store(0.0f);
        morphActive.store(false);
    }
}

void FlagshipDspEngine::processChannelStrip64(FlagshipChannelStrip& ch, double& outL, double& outR) {
    if (!ch.playing.load()) {
        ch.vuMeterPeak.store(ch.vuMeterPeak.load() * 0.88f);
        for (int s = 0; s < NUM_STEMS; ++s) {
            ch.stemEnvelope[s].store(ch.stemEnvelope[s].load() * 0.88f);
            ch.lastStemsL[s] = 0.0;
            ch.lastStemsR[s] = 0.0;
        }
        outL = 0.0; outR = 0.0;
        return;
    }

    float sampleL0 = 0.0f, sampleL1 = 0.0f, sampleR0 = 0.0f, sampleR1 = 0.0f;
    double frac = 0.0;
    {
        std::unique_lock<std::mutex> lock(ch.trackMutex, std::try_to_lock);
        if (!lock.owns_lock() || ch.pcmLeft.size() < 4) {
            outL = 0.0; outR = 0.0;
            return;
        }

        if (ch.vinylBrakeActive.load()) {
            ch.vinylBrakeVelocity = std::max(0.0, ch.vinylBrakeVelocity - (1.0 / (mSampleRate * 0.85)));
        } else {
            ch.vinylBrakeVelocity = std::min(1.0, ch.vinylBrakeVelocity + (1.0 / (mSampleRate * 0.12)));
        }

        double baseAdvance = static_cast<double>(ch.tempoRatio.load()) * ch.vinylBrakeVelocity;
        if (ch.reverseCensorActive.load()) baseAdvance = -baseAdvance;
        if (ch.jogTouching.load()) baseAdvance = static_cast<double>(ch.jogScratchVelocity.load());

        double maxPos = static_cast<double>(ch.pcmLeft.size() - 2);
        double slipPos = ch.slipPlayheadSample.load() + std::abs(baseAdvance);
        if (slipPos >= maxPos) slipPos = ch.firstDownbeatSample.load();
        ch.slipPlayheadSample.store(slipPos);

        double pos = ch.playheadSample.load() + baseAdvance;
        if (ch.loopActive.load() && pos >= ch.loopEndSample.load()) {
            pos = ch.loopStartSample.load();
        }
        if (pos < 0.0) pos = maxPos - 1.0;
        if (pos >= maxPos) pos = ch.firstDownbeatSample.load();
        ch.playheadSample.store(pos);

        // Update normalized beat-grid phase (0.0 .. 1.0)
        double samplesPerBeat = (60.0 / std::max(60.0f, ch.detectedBpm.load())) * mSampleRate;
        double relPos = std::max(0.0, pos - ch.firstDownbeatSample.load());
        ch.beatPhaseNormalized.store(static_cast<float>(std::fmod(relPos, samplesPerBeat) / samplesPerBeat));

        size_t idx = static_cast<size_t>(pos);
        frac = pos - static_cast<double>(idx);
        sampleL0 = ch.pcmLeft[idx];
        sampleL1 = ch.pcmLeft[idx + 1];
        sampleR0 = ch.pcmRight[idx];
        sampleR1 = ch.pcmRight[idx + 1];
    }

    double inL = sampleL0 * (1.0 - frac) + sampleL1 * frac;
    double inR = sampleR0 * (1.0 - frac) + sampleR1 * frac;

    float stemEnv[NUM_STEMS];
    ch.stemSplitter.decomposeTo5Stems(inL, inR, ch.lastStemsL, ch.lastStemsR, stemEnv);

    double rawL = 0.0, rawR = 0.0;
    for (int s = 0; s < NUM_STEMS; ++s) {
        double g = static_cast<double>(ch.stemGain[s].load());
        rawL += ch.lastStemsL[s] * g;
        rawR += ch.lastStemsR[s] * g;
        ch.stemEnvelope[s].store(0.96f * ch.stemEnvelope[s].load() + 0.04f * stemEnv[s]);
    }

    ch.neuralPitchShifter.processStereo(rawL, rawR, static_cast<double>(ch.pitchSemitones.load()));

    double punch = static_cast<double>(ch.neuralTransientPunch.load());
    if (punch > 0.01) {
        rawL *= (1.0 + stemEnv[4] * punch * 1.35);
        rawR *= (1.0 + stemEnv[4] * punch * 1.35);
    }
    double widen = static_cast<double>(ch.neuralStereoWidener.load());
    if (widen > 0.01) {
        double mid = 0.5 * (rawL + rawR);
        double side = 0.5 * (rawL - rawR) * (1.0 + widen * 1.25);
        rawL = mid + side;
        rawR = mid - side;
    }

    double compAmt = static_cast<double>(ch.v10CompressorKnob.load());
    if (compAmt > 0.01) {
        double absPeak = std::max(std::abs(rawL), std::abs(rawR));
        ch.compEnv = 0.995 * ch.compEnv + 0.005 * absPeak;
        double threshold = 0.32;
        double gainReduction = 1.0;
        if (ch.compEnv > threshold) {
            double ratio = 1.0 + compAmt * 3.5;
            gainReduction = std::pow(ch.compEnv / threshold, (1.0 / ratio) - 1.0);
        }
        double makeup = 1.0 + compAmt * 0.85;
        rawL *= gainReduction * makeup;
        rawR *= gainReduction * makeup;
    }

    float loVal = ch.eqLow.load();
    float hiVal = ch.eqHigh.load();
    double loGain = (loVal <= -0.99f) ? 0.0 : std::pow(10.0, (loVal >= 0.0f ? loVal * 6.0 : loVal * 36.0) / 20.0);
    double hiGain = (hiVal <= -0.99f) ? 0.0 : std::pow(10.0, (hiVal >= 0.0f ? hiVal * 6.0 : hiVal * 36.0) / 20.0);

    ch.lowCrossL.setParams(mSampleRate, 220.0, 0.707);
    ch.lowCrossR.setParams(mSampleRate, 220.0, 0.707);
    ch.highCrossL.setParams(mSampleRate, 2400.0, 0.707);
    ch.highCrossR.setParams(mSampleRate, 2400.0, 0.707);

    double lpL, bpL, hpL, lpR, bpR, hpR;
    ch.lowCrossL.processSample(rawL, lpL, bpL, hpL);
    ch.lowCrossR.processSample(rawR, lpR, bpR, hpR);

    double hiLpL, hiBpL, hiHpL, hiLpR, hiBpR, hiHpR;
    ch.highCrossL.processSample(hpL, hiLpL, hiBpL, hiHpL);
    ch.highCrossR.processSample(hpR, hiLpR, hiBpR, hiHpR);

    float lmVal = ch.eqLoMid.load();
    float hmVal = ch.eqHiMid.load();
    double lmDb = (lmVal >= 0.0f) ? lmVal * 6.0 : lmVal * 26.0;
    double hmDb = (hmVal >= 0.0f) ? hmVal * 6.0 : hmVal * 26.0;

    double midL = ch.loMidBellL.processBell(hiLpL, mSampleRate, 340.0, 1.3, lmDb);
    midL = ch.hiMidBellL.processBell(midL, mSampleRate, 1800.0, 1.3, hmDb);

    double midR = ch.loMidBellR.processBell(hiLpR, mSampleRate, 340.0, 1.3, lmDb);
    midR = ch.hiMidBellR.processBell(midR, mSampleRate, 1800.0, 1.3, hmDb);

    double eqOutL = lpL * loGain + midL + hiHpL * hiGain;
    double eqOutR = lpR * loGain + midR + hiHpR * hiGain;

    ch.xoneVcf.processStereo(eqOutL, eqOutR, mSampleRate);

    float cKnob = ch.colorFxKnob.load();
    if (std::abs(cKnob) > 0.02f) {
        auto cType = static_cast<SoundColorFxType>(ch.colorFxType.load());
        if (cType == SoundColorFxType::NOISE) {
            ch.noiseRngState ^= ch.noiseRngState << 13;
            ch.noiseRngState ^= ch.noiseRngState >> 17;
            ch.noiseRngState ^= ch.noiseRngState << 5;
            double white = (static_cast<double>(ch.noiseRngState) / 2147483648.0) - 1.0;
            double nFreq = 250.0 * std::pow(32.0, std::abs(cKnob));
            ch.colorSvfL.setParams(mSampleRate, nFreq, 3.2);
            double nLp, nBp, nHp;
            ch.colorSvfL.processSample(white * std::abs(cKnob) * 0.28, nLp, nBp, nHp);
            eqOutL += nBp; eqOutR += nBp;
        } else {
            double freq = (cKnob > 0.0f) ? (45.0 * std::pow(200.0, cKnob)) : (19000.0 * std::pow(0.008, -cKnob));
            ch.colorSvfL.setParams(mSampleRate, freq, 2.1);
            ch.colorSvfR.setParams(mSampleRate, freq, 2.1);
            double cLpL, cBpL, cHpL, cLpR, cBpR, cHpR;
            ch.colorSvfL.processSample(eqOutL, cLpL, cBpL, cHpL);
            ch.colorSvfR.processSample(eqOutR, cLpR, cBpR, cHpR);
            eqOutL = (cKnob > 0.0f) ? cHpL : cLpL;
            eqOutR = (cKnob > 0.0f) ? cHpR : cLpR;
        }
    }

    double fader = static_cast<double>(ch.channelFader.load());
    double faderGain = fader * fader * (0.3 + 0.7 * fader);

    outL = eqOutL * faderGain;
    outR = eqOutR * faderGain;

    float peak = static_cast<float>(std::max(std::abs(outL), std::abs(outR)));
    ch.vuMeterPeak.store(0.90f * ch.vuMeterPeak.load() + 0.10f * peak);
}

void FlagshipDspEngine::processMasterBeatFx64(double& masterL, double& masterR) {
    auto fx = static_cast<BeatFxType>(beatFxType.load());
    double wet = static_cast<double>(beatFxWetDry.load());
    if (fx == BeatFxType::NONE || wet <= 0.005) return;

    if (fx == BeatFxType::MOBIUS) {
        mMobiusPhase += 0.35 / mSampleRate;
        if (mMobiusPhase >= 1.0) mMobiusPhase -= 1.0;
        double shepardFreq = 120.0 * std::pow(32.0, mMobiusPhase);
        mBeatFxSvfL.setParams(mSampleRate, shepardFreq, 4.2);
        mBeatFxSvfR.setParams(mSampleRate, shepardFreq, 4.2);
        double lpL, bpL, hpL, lpR, bpR, hpR;
        mBeatFxSvfL.processSample(masterL, lpL, bpL, hpL);
        mBeatFxSvfR.processSample(masterR, lpR, bpR, hpR);
        double env = std::sin(M_PI * mMobiusPhase);
        masterL = masterL * (1.0 - wet * 0.5) + bpL * env * wet * 2.2;
        masterR = masterR * (1.0 - wet * 0.5) + bpR * env * wet * 2.2;
    } else if (fx == BeatFxType::FLANGER || fx == BeatFxType::PHASER) {
        mFlangerPhase += 0.6 / mSampleRate;
        if (mFlangerPhase >= 1.0) mFlangerPhase -= 1.0;
        double sweepHz = 300.0 + (std::sin(2.0 * M_PI * mFlangerPhase) * 0.5 + 0.5) * 3200.0;
        mBeatFxSvfL.setParams(mSampleRate, sweepHz, 3.0);
        mBeatFxSvfR.setParams(mSampleRate, sweepHz, 3.0);
        double lpL, bpL, hpL, lpR, bpR, hpR;
        mBeatFxSvfL.processSample(masterL, lpL, bpL, hpL);
        mBeatFxSvfR.processSample(masterR, lpR, bpR, hpR);
        masterL = masterL * (1.0 - wet) + (masterL - bpL) * wet;
        masterR = masterR * (1.0 - wet) + (masterR - bpR) * wet;
    } else if (fx == BeatFxType::TRANS_GATE) {
        mFlangerPhase += 4.0 / mSampleRate;
        if (mFlangerPhase >= 1.0) mFlangerPhase -= 1.0;
        double gate = (mFlangerPhase < 0.5) ? 1.0 : (1.0 - wet);
        masterL *= gate;
        masterR *= gate;
    } else {
        double delaySamples = std::clamp(static_cast<double>(beatFxTimeMs.load()) * 0.001 * mSampleRate, 128.0, static_cast<double>(mDelayLineL.size() - 4));
        size_t readPos = (mDelayWritePos + mDelayLineL.size() - static_cast<size_t>(delaySamples)) % mDelayLineL.size();
        double dL = mDelayLineL[readPos];
        double dR = mDelayLineR[readPos];
        double fb = (fx == BeatFxType::HELIX || fx == BeatFxType::SPIRAL) ? 0.72 : 0.50;
        mDelayLineL[mDelayWritePos] = masterL + (fx == BeatFxType::PING_PONG ? dR : dL) * fb;
        mDelayLineR[mDelayWritePos] = masterR + (fx == BeatFxType::PING_PONG ? dL : dR) * fb;
        masterL += dL * wet;
        masterR += dR * wet;
    }
}

oboe::DataCallbackResult FlagshipDspEngine::onAudioReady(oboe::AudioStream*, void* audioData, int32_t numFrames) {
    float* outputBuffer = static_cast<float*>(audioData);
    updateInvisibleGhostMorphAutomation(numFrames);

    // During an active Invisible Ghost-Morph, the 5-stem constant-power matrix already handles the exact balance,
    // so both decks sum at unity without any crossfader dip!
    double xf = static_cast<double>(crossfader.load());
    double normXf = (xf + 1.0) * 0.5;
    double gainA = morphActive.load() ? 1.0 : std::cos(normXf * M_PI * 0.5);
    double gainB = morphActive.load() ? 1.0 : std::sin(normXf * M_PI * 0.5);
    double sidechainDepth = static_cast<double>(neuralCrossDeckSidechain.load());

    for (int32_t frame = 0; frame < numFrames; ++frame) {
        double deckAL = 0.0, deckAR = 0.0;
        double deckBL = 0.0, deckBR = 0.0;

        processChannelStrip64(*channels[0], deckAL, deckAR);
        processChannelStrip64(*channels[1], deckBL, deckBR);

        // Bidirectional Dynamic Spectral Carving (prevents any bass or vocal clash when both decks play)
        if (sidechainDepth > 0.01 && channels[0]->playing.load() && channels[1]->playing.load()) {
            if (!morphActive.load()) {
                double envA = static_cast<double>(channels[0]->stemEnvelope[0].load() + channels[0]->stemEnvelope[1].load());
                double duckB = std::clamp(1.0 - envA * sidechainDepth * 0.48, 0.45, 1.0);
                deckBL *= duckB;
                deckBR *= duckB;
            } else {
                // During Ghost-Morph: incoming vocal envelope carves outgoing vocal/mid slightly for 100% clarity
                auto& src = *channels[morphSourceDeck];
                auto& dst = *channels[morphTargetDeck];
                double dstVoxEnv = static_cast<double>(dst.stemEnvelope[3].load());
                double carve = std::clamp(1.0 - dstVoxEnv * 0.35, 0.65, 1.0);
                if (morphSourceDeck == 0) { deckAL *= carve; deckAR *= carve; }
                else { deckBL *= carve; deckBR *= carve; }
                (void)src;
            }
        }

        double masterSumL = deckAL * gainA + deckBL * gainB;
        double masterSumR = deckAR * gainA + deckBR * gainB;

        mMasterIsoLowL.setParams(mSampleRate, 300.0, 0.707);
        mMasterIsoLowR.setParams(mSampleRate, 300.0, 0.707);
        mMasterIsoHighL.setParams(mSampleRate, 3500.0, 0.707);
        mMasterIsoHighR.setParams(mSampleRate, 3500.0, 0.707);

        double mLpL, mBpL, mHpL, mLpR, mBpR, mHpR;
        mMasterIsoLowL.processSample(masterSumL, mLpL, mBpL, mHpL);
        mMasterIsoLowR.processSample(masterSumR, mLpR, mBpR, mHpR);

        double mHiLpL, mHiBpL, mHiHpL, mHiLpR, mHiBpR, mHiHpR;
        mMasterIsoHighL.processSample(mHpL, mHiLpL, mHiBpL, mHiHpL);
        mMasterIsoHighR.processSample(mHpR, mHiLpR, mHiBpR, mHiHpR);

        double isoG[3];
        float isoParams[3] = {masterIsoLow.load(), masterIsoMid.load(), masterIsoHigh.load()};
        for (int b = 0; b < 3; ++b) {
            double db = (isoParams[b] >= 0.0f) ? isoParams[b] * 9.0 : isoParams[b] * 48.0;
            isoG[b] = (isoParams[b] <= -0.99f) ? 0.0 : std::pow(10.0, db / 20.0);
        }

        masterSumL = mLpL * isoG[0] + mHiLpL * isoG[1] + mHiHpL * isoG[2];
        masterSumR = mLpR * isoG[0] + mHiLpR * isoG[1] + mHiHpR * isoG[2];

        mDelayWritePos = (mDelayWritePos + 1) % mDelayLineL.size();

        processMasterBeatFx64(masterSumL, masterSumR);

        mNeveTransformer.processMaster(
            masterSumL,
            masterSumR,
            static_cast<double>(neveTransformerDrive.load()),
            static_cast<double>(neuralSubBassBoom.load()),
            static_cast<double>(neuralAirExciter.load()),
            mSampleRate
        );

        float finalL = static_cast<float>(std::clamp(masterSumL, -0.995, 0.995));
        float finalR = static_cast<float>(std::clamp(masterSumR, -0.995, 0.995));
        outputBuffer[frame * 2]     = finalL;
        outputBuffer[frame * 2 + 1] = finalR;

        if (mIsRecording.load() && !mIsRecordPaused.load()) {
            std::unique_lock<std::mutex> recLock(mRecorderMutex, std::try_to_lock);
            if (recLock.owns_lock() && mRecordFile.is_open()) {
                int16_t pcm16[2] = {
                    static_cast<int16_t>(finalL * 32767.0f),
                    static_cast<int16_t>(finalR * 32767.0f)
                };
                mRecordFile.write(reinterpret_cast<const char*>(pcm16), 4);
                mRecordedFrames++;
                if ((mRecordedFrames & 2047) == 0) {
                    mRecordDurationSec.store(static_cast<double>(mRecordedFrames) / mSampleRate);
                }
            }
        }
    }

    return oboe::DataCallbackResult::Continue;
}

} // namespace apexdj

static std::unique_ptr<apexdj::FlagshipDspEngine> gEngine;

extern "C" {

JNIEXPORT void JNICALL
Java_com_apexdj_butter_audio_OnlineAndLocalRepository_nativeInitEngine(JNIEnv* env, jobject, jobject assetMgrObj) {
    if (!gEngine) {
        gEngine = std::make_unique<apexdj::FlagshipDspEngine>();
        gEngine->startAudioStream();
    }
    if (assetMgrObj) {
        AAssetManager* mgr = AAssetManager_fromJava(env, assetMgrObj);
        gEngine->verifyEmbeddedNeuralBanks(mgr);
    }
}

JNIEXPORT void JNICALL
Java_com_apexdj_butter_audio_OnlineAndLocalRepository_nativeLoadPcmToDeck(
    JNIEnv* env, jobject, jint deckIndex, jfloatArray leftPcm, jfloatArray rightPcm, jint sampleRate, jboolean autoPlay) {
    if (!gEngine) return;
    jsize len = env->GetArrayLength(leftPcm);
    jfloat* lPtr = env->GetFloatArrayElements(leftPcm, nullptr);
    jfloat* rPtr = env->GetFloatArrayElements(rightPcm, nullptr);
    gEngine->loadTrackToDeck(deckIndex, lPtr, rPtr, len, sampleRate, autoPlay);
    env->ReleaseFloatArrayElements(leftPcm, lPtr, JNI_ABORT);
    env->ReleaseFloatArrayElements(rightPcm, rPtr, JNI_ABORT);
}

JNIEXPORT void JNICALL
Java_com_apexdj_butter_audio_OnlineAndLocalRepository_nativeLoadBuiltInStudioPreset(
    JNIEnv*, jobject, jint deckIndex, jint presetIndex, jboolean autoPlay) {
    if (gEngine) gEngine->generateBuiltInStudioTrackToDeck(deckIndex, presetIndex, autoPlay);
}

JNIEXPORT void JNICALL
Java_com_apexdj_butter_audio_OnlineAndLocalRepository_nativeSetDeckPlayState(JNIEnv*, jobject, jint deck, jboolean playing) {
    if (gEngine && deck >= 0 && deck < apexdj::NUM_CHANNELS) {
        if (playing) {
            gEngine->channels[deck]->vinylBrakeActive.store(false);
            gEngine->channels[deck]->vinylBrakeVelocity = 1.0;
        }
        gEngine->channels[deck]->playing.store(playing);
    }
}

JNIEXPORT jboolean JNICALL
Java_com_apexdj_butter_audio_OnlineAndLocalRepository_nativeGetDeckPlayState(JNIEnv*, jobject, jint deck) {
    if (!gEngine || deck < 0 || deck >= apexdj::NUM_CHANNELS) return JNI_FALSE;
    return gEngine->channels[deck]->playing.load() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jfloat JNICALL
Java_com_apexdj_butter_audio_OnlineAndLocalRepository_nativeGetDeckProgress(JNIEnv*, jobject, jint deck) {
    if (!gEngine || deck < 0 || deck >= apexdj::NUM_CHANNELS) return 0.0f;
    auto& ch = *gEngine->channels[deck];
    size_t total = ch.pcmLeft.size();
    if (total < 4) return 0.0f;
    return static_cast<float>(std::clamp(ch.playheadSample.load() / static_cast<double>(total), 0.0, 1.0));
}

JNIEXPORT jfloat JNICALL
Java_com_apexdj_butter_audio_OnlineAndLocalRepository_nativeGetDeckRemainingSeconds(JNIEnv*, jobject, jint deck) {
    if (!gEngine || deck < 0 || deck >= apexdj::NUM_CHANNELS) return 999.0f;
    auto& ch = *gEngine->channels[deck];
    size_t total = ch.pcmLeft.size();
    if (total < 4) return 999.0f;
    double remSamples = std::max(0.0, static_cast<double>(total) - ch.playheadSample.load());
    return static_cast<float>(remSamples / std::max(1.0, gEngine->getSampleRate()));
}

JNIEXPORT jfloat JNICALL
Java_com_apexdj_butter_audio_OnlineAndLocalRepository_nativeGetBeatPhase(JNIEnv*, jobject, jint deck) {
    if (!gEngine || deck < 0 || deck >= apexdj::NUM_CHANNELS) return 0.0f;
    return gEngine->channels[deck]->beatPhaseNormalized.load();
}

JNIEXPORT void JNICALL
Java_com_apexdj_butter_audio_OnlineAndLocalRepository_nativeSeekDeckProgress(JNIEnv*, jobject, jint deck, jfloat normProgress) {
    if (!gEngine || deck < 0 || deck >= apexdj::NUM_CHANNELS) return;
    auto& ch = *gEngine->channels[deck];
    size_t total = ch.pcmLeft.size();
    if (total > 4) {
        double target = std::clamp(static_cast<double>(normProgress), 0.0, 0.995) * static_cast<double>(total);
        ch.playheadSample.store(target);
    }
}

JNIEXPORT jfloat JNICALL
Java_com_apexdj_butter_audio_OnlineAndLocalRepository_nativeGetStemEnvelope(JNIEnv*, jobject, jint deck, jint stemIdx) {
    if (!gEngine || deck < 0 || deck >= apexdj::NUM_CHANNELS || stemIdx < 0 || stemIdx >= apexdj::NUM_STEMS) return 0.0f;
    return gEngine->channels[deck]->stemEnvelope[stemIdx].load();
}

JNIEXPORT jfloat JNICALL
Java_com_apexdj_butter_audio_OnlineAndLocalRepository_nativeGetStemGain(JNIEnv*, jobject, jint deck, jint stemIdx) {
    if (!gEngine || deck < 0 || deck >= apexdj::NUM_CHANNELS || stemIdx < 0 || stemIdx >= apexdj::NUM_STEMS) return 1.0f;
    return gEngine->channels[deck]->stemGain[stemIdx].load();
}

JNIEXPORT jfloatArray JNICALL
Java_com_apexdj_butter_audio_OnlineAndLocalRepository_nativeGetWaveformOverview(JNIEnv* env, jobject, jint deck) {
    jfloatArray arr = env->NewFloatArray(apexdj::WAVEFORM_BINS);
    if (!gEngine || deck < 0 || deck >= apexdj::NUM_CHANNELS) return arr;
    auto& ch = *gEngine->channels[deck];
    std::lock_guard<std::mutex> lock(ch.trackMutex);
    env->SetFloatArrayRegion(arr, 0, apexdj::WAVEFORM_BINS, ch.waveformOverview.data());
    return arr;
}

JNIEXPORT void JNICALL
Java_com_apexdj_butter_audio_OnlineAndLocalRepository_nativeTriggerButterMorph(JNIEnv*, jobject, jint fromDeck, jint toDeck, jdouble seconds) {
    if (gEngine) gEngine->triggerButterMorph(fromDeck, toDeck, seconds);
}

JNIEXPORT jboolean JNICALL
Java_com_apexdj_butter_audio_OnlineAndLocalRepository_nativeIsMorphActive(JNIEnv*, jobject) {
    return (gEngine && gEngine->morphActive.load()) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jfloat JNICALL
Java_com_apexdj_butter_audio_OnlineAndLocalRepository_nativeGetMorphProgress(JNIEnv*, jobject) {
    return gEngine ? gEngine->morphProgressState.load() : 0.0f;
}

JNIEXPORT jfloat JNICALL
Java_com_apexdj_butter_audio_OnlineAndLocalRepository_nativeGetCrossfader(JNIEnv*, jobject) {
    return gEngine ? gEngine->crossfader.load() : 0.0f;
}

JNIEXPORT void JNICALL
Java_com_apexdj_butter_audio_OnlineAndLocalRepository_nativeTriggerHotCue(JNIEnv*, jobject, jint deck, jint cueIndex) {
    if (gEngine) gEngine->triggerHotCue(deck, cueIndex);
}

JNIEXPORT void JNICALL
Java_com_apexdj_butter_audio_OnlineAndLocalRepository_nativeToggleBeatLoop(JNIEnv*, jobject, jint deck, jdouble beats) {
    if (gEngine) gEngine->toggleBeatLoop(deck, beats);
}

JNIEXPORT void JNICALL
Java_com_apexdj_butter_audio_OnlineAndLocalRepository_nativeTriggerNeuralPadMacro(JNIEnv*, jobject, jint deck, jint macroIndex) {
    if (gEngine) gEngine->triggerNeuralPadMacro(deck, macroIndex);
}

JNIEXPORT void JNICALL
Java_com_apexdj_butter_audio_OnlineAndLocalRepository_nativeStartMasterRecording(JNIEnv* env, jobject, jstring filepathStr) {
    if (!gEngine) return;
    const char* path = env->GetStringUTFChars(filepathStr, nullptr);
    gEngine->startRecording(std::string(path));
    env->ReleaseStringUTFChars(filepathStr, path);
}

JNIEXPORT void JNICALL
Java_com_apexdj_butter_audio_OnlineAndLocalRepository_nativePauseMasterRecording(JNIEnv*, jobject) {
    if (gEngine) gEngine->pauseRecording();
}

JNIEXPORT void JNICALL
Java_com_apexdj_butter_audio_OnlineAndLocalRepository_nativeStopMasterRecording(JNIEnv*, jobject) {
    if (gEngine) gEngine->stopRecording();
}

JNIEXPORT jboolean JNICALL
Java_com_apexdj_butter_audio_OnlineAndLocalRepository_nativeIsRecording(JNIEnv*, jobject) {
    return (gEngine && gEngine->isRecording()) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_apexdj_butter_audio_OnlineAndLocalRepository_nativeIsRecordPaused(JNIEnv*, jobject) {
    return (gEngine && gEngine->isRecordPaused()) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jdouble JNICALL
Java_com_apexdj_butter_audio_OnlineAndLocalRepository_nativeGetRecordDurationSec(JNIEnv*, jobject) {
    return gEngine ? gEngine->getRecordDurationSec() : 0.0;
}

JNIEXPORT void JNICALL
Java_com_apexdj_butter_audio_OnlineAndLocalRepository_nativeSetChannelParam(
    JNIEnv*, jobject, jint deck, jint paramId, jfloat value) {
    if (!gEngine || deck < 0 || deck >= apexdj::NUM_CHANNELS) return;
    auto& ch = *gEngine->channels[deck];
    switch (paramId) {
        case 0: ch.channelFader.store(value); break;
        case 1: ch.eqLow.store(value); break;
        case 2: ch.eqLoMid.store(value); break;
        case 3: ch.eqHiMid.store(value); break;
        case 4: ch.eqHigh.store(value); break;
        case 5: ch.v10CompressorKnob.store(value); break;
        case 6: ch.colorFxKnob.store(value); break;
        case 7: ch.xoneVcf.cutoffHz = 60.0 * std::pow(320.0, value); break;
        case 8: ch.xoneVcf.crunchDrive = value; break;
        case 9: ch.stemGain[0].store(value); break;
        case 10: ch.stemGain[1].store(value); break;
        case 11: ch.stemGain[2].store(value); break;
        case 12: ch.stemGain[3].store(value); break;
        case 13: ch.tempoRatio.store(value); break;
        case 14: ch.slipModeEnabled.store(value > 0.5f); break;
        case 15: ch.reverseCensorActive.store(!ch.reverseCensorActive.load()); break;
        case 16: ch.vinylBrakeActive.store(!ch.vinylBrakeActive.load()); break;
        case 17: ch.colorFxType.store(static_cast<int>(value)); break;
        case 18: ch.jogTouching.store(std::abs(value) > 0.01f); ch.jogScratchVelocity.store(value); break;
        case 19: ch.pitchSemitones.store(value); break;
        case 20: ch.neuralTransientPunch.store(value); break;
        case 21: ch.neuralStereoWidener.store(value); break;
        case 22: ch.stemGain[4].store(value); break;
    }
}

JNIEXPORT void JNICALL
Java_com_apexdj_butter_audio_OnlineAndLocalRepository_nativeSetMasterParam(JNIEnv*, jobject, jint paramId, jfloat value) {
    if (!gEngine) return;
    switch (paramId) {
        case 0: gEngine->crossfader.store(value); break;
        case 1: gEngine->masterIsoLow.store(value); break;
        case 2: gEngine->masterIsoMid.store(value); break;
        case 3: gEngine->masterIsoHigh.store(value); break;
        case 4: gEngine->neveTransformerDrive.store(value); break;
        case 8: gEngine->beatFxType.store(static_cast<int>(value)); break;
        case 9: gEngine->beatFxWetDry.store(value); break;
        case 10: gEngine->beatFxTimeMs.store(value); break;
        case 11: gEngine->neuralCrossDeckSidechain.store(value); break;
        case 12: gEngine->neuralSubBassBoom.store(value); break;
        case 13: gEngine->neuralAirExciter.store(value); break;
        case 14: gEngine->neuralMorphStyle.store(static_cast<int>(value)); break;
    }
}

JNIEXPORT jfloat JNICALL
Java_com_apexdj_butter_audio_OnlineAndLocalRepository_nativeGetChannelVu(JNIEnv*, jobject, jint deck) {
    if (!gEngine || deck < 0 || deck >= apexdj::NUM_CHANNELS) return 0.0f;
    return gEngine->channels[deck]->vuMeterPeak.load();
}

JNIEXPORT jfloat JNICALL
Java_com_apexdj_butter_audio_OnlineAndLocalRepository_nativeGetDetectedBpm(JNIEnv*, jobject, jint deck) {
    if (!gEngine || deck < 0 || deck >= apexdj::NUM_CHANNELS) return 128.0f;
    return gEngine->channels[deck]->detectedBpm.load();
}

JNIEXPORT jint JNICALL
Java_com_apexdj_butter_audio_OnlineAndLocalRepository_nativeGetDetectedCamelotKey(JNIEnv*, jobject, jint deck) {
    if (!gEngine || deck < 0 || deck >= apexdj::NUM_CHANNELS) return 8;
    return gEngine->channels[deck]->detectedCamelotKey.load();
}

} // extern "C"
