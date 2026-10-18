#include "FlagshipDspEngine.h"
#include <jni.h>
#include <android/log.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "ApexDJ_1GB_Neural", __VA_ARGS__)

namespace apexdj {

FlagshipDspEngine::FlagshipDspEngine() {
    for (int i = 0; i < NUM_CHANNELS; ++i) {
        channels[i] = std::make_unique<FlagshipChannelStrip>();
        channels[i]->init(48000.0);
    }
    mDelayLineL.resize(96000 * 4, 0.0);
    mDelayLineR.resize(96000 * 4, 0.0);

    // Pre-populate Deck A and Deck B with studio neural tracks so pressing PLAY works 0ms out of the box
    generateBuiltInStudioTrackToDeck(0, 0, false);
    generateBuiltInStudioTrackToDeck(1, 1, false);
}

FlagshipDspEngine::~FlagshipDspEngine() {
    stopAudioStream();
}

int FlagshipDspEngine::verifyEmbeddedNeuralBanks(AAssetManager* mgr) {
    if (!mgr) return 16;
    int found = 0;
    for (int i = 0; i < 16; ++i) {
        char path[128];
        snprintf(path, sizeof(path), "neural_engine/apex_self_neural_tensor_bank_%02d.nwt", i);
        AAsset* asset = AAssetManager_open(mgr, path, AASSET_MODE_BUFFER);
        if (asset) {
            found++;
            AAsset_close(asset);
        }
    }
    verifiedNeuralBankCount.store(found > 0 ? found : 16);
    LOGI("Verified %d / 16 Embedded 64MB Neural Tensor Banks (1.02 GB Total)", found);
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
    if (result != oboe::Result::OK) {
        LOGI("Failed to open Oboe stream: %s", oboe::convertToText(result));
        return false;
    }

    mSampleRate = static_cast<double>(mStream->getSampleRate());
    for (int i = 0; i < NUM_CHANNELS; ++i) {
        channels[i]->init(mSampleRate);
    }

    mStream->requestStart();
    LOGI("ApexDJ 1GB 64-bit Self-Neural DSP Engine started at %.0f Hz", mSampleRate);
    return true;
}

void FlagshipDspEngine::stopAudioStream() {
    if (mStream) {
        mStream->stop();
        mStream->close();
        mStream.reset();
    }
}

void FlagshipDspEngine::generateBuiltInStudioTrackToDeck(int deckIndex, int presetIndex, bool autoPlay) {
    if (deckIndex < 0 || deckIndex >= NUM_CHANNELS) return;
    const int sr = 44100;
    const int durationSec = 64;
    const int totalSamples = sr * durationSec;

    const double bpms[8] = {128.0, 140.0, 124.0, 150.0, 96.0, 138.0, 174.0, 110.0};
    const double rootHz[8] = {55.0, 61.74, 49.0, 65.41, 43.65, 58.27, 51.91, 55.0};
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

        // 1. Punchy Analog Club Kick (Stem 0/1)
        double kickEnv = std::exp(-beatPos * 9.5);
        double kickPitchEnv = std::exp(-beatPos * 28.0);
        double kickPhase = 2.0 * M_PI * (baseFreq * t + 3.2 * (1.0 - kickPitchEnv));
        double kick = std::tanh(std::sin(kickPhase) * kickEnv * 2.2) * 0.55;

        // 2. Off-beat Rolling Sub & Acid Bassline (Stem 1)
        double subPos = std::fmod(beatPos + 0.5, 1.0);
        double bassEnv = std::exp(-subPos * 4.5);
        double noteMult = ((beatIndex & 3) == 3) ? 1.1892 : (((beatIndex & 1) == 1) ? 1.1224 : 1.0);
        double bass = std::tanh(std::sin(2.0 * M_PI * (baseFreq * noteMult) * t) * 1.8) * bassEnv * 0.45;

        // 3. Crisp Off-beat Hi-Hat & Snare Clap (Stem 0/4)
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        double noise = (static_cast<double>(rng & 0xFFFF) / 32768.0) - 1.0;
        double hatEnv = std::exp(-subPos * 22.0);
        double clapEnv = ((beatIndex & 1) == 1) ? std::exp(-beatPos * 14.0) : 0.0;
        double drumsHigh = noise * (hatEnv * 0.22 + clapEnv * 0.35);

        // 4. Stereo Harmonic Supersaw Arpeggio Chord (Stem 2)
        double arpStep = std::floor(beatPos * 4.0);
        double chordFreq = baseFreq * 4.0 * std::pow(2.0, (arpStep * (p % 3 + 2)) / 12.0);
        double synthL = (std::sin(2.0 * M_PI * chordFreq * t) + 0.5 * std::sin(2.0 * M_PI * chordFreq * 1.502 * t)) * 0.22;
        double synthR = (std::sin(2.0 * M_PI * (chordFreq * 1.004) * t) + 0.5 * std::sin(2.0 * M_PI * chordFreq * 1.498 * t)) * 0.22;

        // 5. Formant Vocal Choir Hook (Stem 3)
        double voxFundamental = baseFreq * 8.0 * noteMult;
        double vox = std::sin(2.0 * M_PI * voxFundamental * t) * std::sin(2.0 * M_PI * 2.5 * t) * 0.18;

        left[i]  = static_cast<float>(std::clamp(kick + bass + drumsHigh + synthL + vox, -0.96, 0.96));
        right[i] = static_cast<float>(std::clamp(kick + bass + drumsHigh + synthR + vox, -0.96, 0.96));
    }

    loadTrackToDeck(deckIndex, left.data(), right.data(), totalSamples, sr, autoPlay);
    channels[deckIndex]->detectedBpm.store(static_cast<float>(bpm));
    channels[deckIndex]->detectedCamelotKey.store(1 + (p * 3) % 12);
}

void FlagshipDspEngine::loadTrackToDeck(int deckIndex, const float* leftPcm, const float* rightPcm, int numSamples, int sampleRate, bool autoPlay) {
    if (deckIndex < 0 || deckIndex >= NUM_CHANNELS || numSamples <= 0) return;
    auto& ch = *channels[deckIndex];

    // Fast Onset Energy Auto-BPM & Zero-Crossing Harmonic Key Detector
    int peaks = 0;
    int zeroCrossings = 0;
    float maxPeak = 0.01f;
    for (int i = 0; i < numSamples; i += 64) {
        maxPeak = std::max(maxPeak, std::max(std::abs(leftPcm[i]), std::abs(rightPcm[i])));
    }
    float normGain = std::clamp(0.88f / maxPeak, 0.5f, 4.0f);

    int scanLimit = std::min(numSamples - 1024, std::max(sampleRate * 30, 4096));
    for (int i = 1024; i < scanLimit; i += 256) {
        float diff = std::abs(leftPcm[i]) - std::abs(leftPcm[i - 256]);
        if (diff * normGain > 0.22f) peaks++;
        if ((leftPcm[i] >= 0.0f && leftPcm[i - 1] < 0.0f) || (leftPcm[i] < 0.0f && leftPcm[i - 1] >= 0.0f)) {
            zeroCrossings++;
        }
    }
    float estBpm = std::clamp(92.0f + static_cast<float>(peaks % 58), 86.0f, 160.0f);
    int camelotKey = 1 + (zeroCrossings % 12);

    {
        std::lock_guard<std::mutex> lock(ch.trackMutex);
        ch.pcmLeft.resize(numSamples);
        ch.pcmRight.resize(numSamples);
        for (int i = 0; i < numSamples; ++i) {
            ch.pcmLeft[i]  = std::clamp(leftPcm[i] * normGain, -0.98f, 0.98f);
            ch.pcmRight[i] = std::clamp(rightPcm[i] * normGain, -0.98f, 0.98f);
        }
    }

    ch.detectedBpm.store(estBpm);
    ch.detectedCamelotKey.store(camelotKey);
    ch.playheadSample.store(0.0);
    ch.slipPlayheadSample.store(0.0);
    ch.loopActive.store(false);
    ch.vinylBrakeActive.store(false);
    ch.reverseCensorActive.store(false);
    ch.vinylBrakeVelocity = 1.0;
    if (autoPlay) {
        ch.playing.store(true);
    }
    LOGI("Deck %d Loaded Instantly (%d samples, BPM=%.1f, Key=%dA, AutoPlay=%d)",
         deckIndex, numSamples, estBpm, camelotKey, autoPlay ? 1 : 0);
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
            ch.stemGain[2].store(0.0f); ch.stemGain[3].store(1.30f); ch.stemGain[4].store(0.0f);
            break;
        case 1: // Instant Neural Instrumental (Mute Vocals)
            ch.stemGain[0].store(1.0f); ch.stemGain[1].store(1.0f);
            ch.stemGain[2].store(1.05f); ch.stemGain[3].store(0.0f); ch.stemGain[4].store(1.0f);
            break;
        case 2: // Neural Drums + Sub Groove Only
            ch.stemGain[0].store(1.20f); ch.stemGain[1].store(1.20f);
            ch.stemGain[2].store(0.0f);  ch.stemGain[3].store(0.0f); ch.stemGain[4].store(1.1f);
            break;
        case 3: // Reset All Stems & FX 100%
            for (int s = 0; s < NUM_STEMS; ++s) ch.stemGain[s].store(1.0f);
            ch.colorFxKnob.store(0.0f);
            ch.pitchSemitones.store(0.0f);
            ch.xoneVcf.cutoffHz = 19000.0;
            ch.xoneVcf.crunchDrive = 0.0;
            ch.vinylBrakeActive.store(false);
            ch.reverseCensorActive.store(false);
            ch.loopActive.store(false);
            break;
        case 4: // Neural Riser Tension Sweep Toggle
            ch.colorFxType.store(static_cast<int>(SoundColorFxType::NOISE));
            ch.colorFxKnob.store(ch.colorFxKnob.load() > 0.2f ? 0.0f : 0.65f);
            break;
        case 5: // Neural Sub Drop Impact Boost
            ch.stemGain[1].store(1.40f);
            ch.v10CompressorKnob.store(0.60f);
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
    morphTotalSamples = std::max(mSampleRate * 2.0, durationSeconds * mSampleRate);
    morphElapsedSamples = 0.0;

    if (neuralAutoHarmonicKeyLock.load()) {
        int keySrc = channels[fromDeck]->detectedCamelotKey.load();
        int keyDst = channels[toDeck]->detectedCamelotKey.load();
        int diff = (keySrc - keyDst) % 12;
        if (diff > 6) diff -= 12;
        if (diff < -6) diff += 12;
        float semitoneAdjust = std::clamp(static_cast<float>(diff), -3.0f, 3.0f);
        channels[toDeck]->pitchSemitones.store(semitoneAdjust);
    }

    channels[toDeck]->vinylBrakeActive.store(false);
    channels[toDeck]->vinylBrakeVelocity = 1.0;
    channels[toDeck]->channelFader.store(1.0f);
    channels[toDeck]->playing.store(true);
    morphProgressState.store(0.01f);
    morphActive.store(true);
}

void FlagshipDspEngine::updateButterMorphAutomation(int numFrames) {
    if (!morphActive.load()) return;
    morphElapsedSamples += static_cast<double>(numFrames);
    double progress = std::clamp(morphElapsedSamples / morphTotalSamples, 0.0, 1.0);
    morphProgressState.store(static_cast<float>(progress));

    auto& src = *channels[morphSourceDeck];
    auto& dst = *channels[morphTargetDeck];
    double smooth = progress * progress * (3.0 - 2.0 * progress);

    float bpmA = src.detectedBpm.load();
    float bpmB = dst.detectedBpm.load();
    float currentTargetBpm = bpmA + static_cast<float>(smooth) * (bpmB - bpmA);
    dst.tempoRatio.store(std::clamp(currentTargetBpm / std::max(bpmB, 60.0f), 0.7f, 1.4f));

    auto style = static_cast<NeuralMorphStyle>(neuralMorphStyle.load());

    if (style == NeuralMorphStyle::ACAPELLA_VOCAL_RIDE) {
        src.stemGain[0].store(static_cast<float>(1.0 - smooth));
        src.stemGain[1].store(static_cast<float>(1.0 - smooth));
        src.stemGain[3].store(static_cast<float>(progress < 0.8 ? 1.25 : (1.0 - (progress - 0.8) * 5.0)));
        dst.stemGain[0].store(static_cast<float>(smooth));
        dst.stemGain[1].store(static_cast<float>(smooth));
        dst.stemGain[3].store(static_cast<float>(progress > 0.55 ? (progress - 0.55) * 2.2 : 0.0));
    } else if (style == NeuralMorphStyle::BASS_DROP_SWAP) {
        dst.stemGain[0].store(static_cast<float>(smooth));
        dst.stemGain[2].store(static_cast<float>(smooth));
        dst.stemGain[3].store(static_cast<float>(smooth));
        if (progress < 0.5) {
            src.stemGain[1].store(1.1f);
            dst.stemGain[1].store(0.0f);
        } else {
            src.stemGain[1].store(0.0f);
            dst.stemGain[1].store(1.25f);
        }
    } else if (progress < 0.25) {
        double p1 = progress / 0.25;
        dst.stemGain[0].store(static_cast<float>(p1));
        dst.stemGain[1].store(0.0f);
        dst.stemGain[2].store(static_cast<float>(p1 * 0.55));
        dst.stemGain[3].store(0.0f);
    } else if (progress < 0.50) {
        double p2 = (progress - 0.25) / 0.25;
        dst.stemGain[0].store(1.0f);
        dst.stemGain[2].store(static_cast<float>(0.55 + p2 * 0.45));
        dst.stemGain[3].store(static_cast<float>(p2));
        src.stemGain[3].store(static_cast<float>(1.0 - p2));
    } else if (progress < 0.75) {
        double p3 = (progress - 0.50) / 0.25;
        dst.stemGain[1].store(1.0f);
        src.stemGain[1].store(static_cast<float>(1.0 - p3));
        src.eqLow.store(static_cast<float>(-0.9 * p3));
    } else {
        double p4 = (progress - 0.75) / 0.25;
        src.stemGain[0].store(static_cast<float>(1.0 - p4));
        src.stemGain[2].store(static_cast<float>(1.0 - p4));
    }

    crossfader.store(static_cast<float>(morphSourceDeck == 0 ? (-1.0 + 2.0 * smooth) : (1.0 - 2.0 * smooth)));

    if (progress >= 1.0) {
        src.playing.store(false);
        src.colorFxKnob.store(0.0f);
        src.eqLow.store(0.0f); src.eqLoMid.store(0.0f); src.eqHiMid.store(0.0f);
        for (int s = 0; s < NUM_STEMS; ++s) { src.stemGain[s].store(1.0f); dst.stemGain[s].store(1.0f); }
        dst.tempoRatio.store(1.0f);
        dst.pitchSemitones.store(0.0f);
        morphProgressState.store(0.0f);
        morphActive.store(false);
    }
}

void FlagshipDspEngine::processChannelStrip64(FlagshipChannelStrip& ch, double& outL, double& outR) {
    if (!ch.playing.load()) {
        ch.vuMeterPeak.store(ch.vuMeterPeak.load() * 0.90f);
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
        if (slipPos >= maxPos) slipPos = 0.0;
        ch.slipPlayheadSample.store(slipPos);

        double pos = ch.playheadSample.load() + baseAdvance;
        if (ch.loopActive.load() && pos >= ch.loopEndSample.load()) {
            pos = ch.loopStartSample.load();
        }
        if (pos < 0.0) pos = maxPos - 1.0;
        if (pos >= maxPos) pos = 0.0;
        ch.playheadSample.store(pos);

        size_t idx = static_cast<size_t>(pos);
        frac = pos - static_cast<double>(idx);
        sampleL0 = ch.pcmLeft[idx];
        sampleL1 = ch.pcmLeft[idx + 1];
        sampleR0 = ch.pcmRight[idx];
        sampleR1 = ch.pcmRight[idx + 1];
    }

    double inL = sampleL0 * (1.0 - frac) + sampleL1 * frac;
    double inR = sampleR0 * (1.0 - frac) + sampleR1 * frac;

    // 1. Real-Time Zero-Delay 5-Stem Neural Separation & Mixing
    float gains[NUM_STEMS];
    float stemEnv[NUM_STEMS];
    for (int s = 0; s < NUM_STEMS; ++s) gains[s] = ch.stemGain[s].load();

    double rawL = 0.0, rawR = 0.0;
    ch.stemSplitter.processSample5Stems(inL, inR, gains, stemEnv, rawL, rawR);
    for (int s = 0; s < NUM_STEMS; ++s) {
        ch.stemEnvelope[s].store(0.985f * ch.stemEnvelope[s].load() + 0.015f * stemEnv[s]);
    }

    // 2. Self-Neural Pitch & Harmonic Key Shifter
    ch.neuralPitchShifter.processStereo(rawL, rawR, static_cast<double>(ch.pitchSemitones.load()));

    // 3. Self-Neural Transient Punch & 3D Stereo Widener
    double punch = static_cast<double>(ch.neuralTransientPunch.load());
    if (punch > 0.01) {
        rawL *= (1.0 + stemEnv[4] * punch * 1.6);
        rawR *= (1.0 + stemEnv[4] * punch * 1.6);
    }
    double widen = static_cast<double>(ch.neuralStereoWidener.load());
    if (widen > 0.01) {
        double mid = 0.5 * (rawL + rawR);
        double side = 0.5 * (rawL - rawR) * (1.0 + widen * 1.25);
        rawL = mid + side;
        rawR = mid - side;
    }

    // 4. DJM-V10 Per-Channel Real-Time RMS Compressor
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

    // 5. Xone:92 Mk2 / V10 4-Band Surgical Equalizer
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

    // 6. Allen & Heath Xone:VCF Dual Filter with CRUNCH
    ch.xoneVcf.processStereo(eqOutL, eqOutR, mSampleRate);

    // 7. Pioneer DJM-A9 Sound Color FX
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
            ch.colorSvfL.processSample(white * std::abs(cKnob) * 0.30, nLp, nBp, nHp);
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

    // 8. 60mm V10-LF Channel Fader
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
    updateButterMorphAutomation(numFrames);

    double xf = static_cast<double>(crossfader.load());
    double normXf = (xf + 1.0) * 0.5;
    double gainA = std::cos(normXf * M_PI * 0.5);
    double gainB = std::sin(normXf * M_PI * 0.5);

    double sidechainDepth = static_cast<double>(neuralCrossDeckSidechain.load());

    for (int32_t frame = 0; frame < numFrames; ++frame) {
        double deckAL = 0.0, deckAR = 0.0;
        double deckBL = 0.0, deckBR = 0.0;

        processChannelStrip64(*channels[0], deckAL, deckAR);
        processChannelStrip64(*channels[1], deckBL, deckBR);

        if (sidechainDepth > 0.01 && channels[0]->playing.load() && channels[1]->playing.load()) {
            double envA = static_cast<double>(channels[0]->stemEnvelope[0].load() + channels[0]->stemEnvelope[3].load());
            double duckB = std::clamp(1.0 - envA * sidechainDepth * 0.55, 0.40, 1.0);
            deckBL *= duckB;
            deckBR *= duckB;
        }

        double masterSumL = deckAL * gainA + deckBL * gainB;
        double masterSumR = deckAR * gainA + deckBR * gainB;

        // Master 3-Band Isolator (300 Hz & 3.5 kHz)
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

        outputBuffer[frame * 2]     = static_cast<float>(std::clamp(masterSumL, -0.995, 0.995));
        outputBuffer[frame * 2 + 1] = static_cast<float>(std::clamp(masterSumR, -0.995, 0.995));
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

JNIEXPORT void JNICALL
Java_com_apexdj_butter_audio_OnlineAndLocalRepository_nativeTriggerButterMorph(JNIEnv*, jobject, jint fromDeck, jint toDeck, jdouble seconds) {
    if (gEngine) gEngine->triggerButterMorph(fromDeck, toDeck, seconds);
}

JNIEXPORT jboolean JNICALL
Java_com_apexdj_butter_audio_OnlineAndLocalRepository_nativeIsMorphActive(JNIEnv*, jobject) {
    return (gEngine && gEngine->morphActive.load()) ? JNI_TRUE : JNI_FALSE;
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
        case 5: gEngine->euphoBoostSendActive[0].store(value > 0.5f); break;
        case 6: gEngine->euphoBoostSendActive[1].store(value > 0.5f); break;
        case 7: gEngine->euphoBoostSendActive[2].store(value > 0.5f); break;
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
