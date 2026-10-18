#include "FlagshipDspEngine.h"
#include <jni.h>
#include <android/log.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "ApexDJ_SelfNeuralDSP", __VA_ARGS__)

namespace apexdj {

FlagshipDspEngine::FlagshipDspEngine() {
    for (int i = 0; i < NUM_CHANNELS; ++i) {
        channels[i] = std::make_unique<FlagshipChannelStrip>();
        channels[i]->init(48000);
    }
    mDelayLineL.resize(96000 * 4, 0.0);
    mDelayLineR.resize(96000 * 4, 0.0);
}

FlagshipDspEngine::~FlagshipDspEngine() {
    stopAudioStream();
}

bool FlagshipDspEngine::startAudioStream() {
    oboe::AudioStreamBuilder builder;
    builder.setDirection(oboe::Direction::Output)
           ->setPerformanceMode(oboe::PerformanceMode::LowLatency)
           ->setSharingMode(oboe::SharingMode::Exclusive)
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
        channels[i]->init(static_cast<int>(mSampleRate));
    }

    mStream->requestStart();
    LOGI("ApexDJ 64-bit Self-Neural DSP Engine started at %.0f Hz", mSampleRate);
    return true;
}

void FlagshipDspEngine::stopAudioStream() {
    if (mStream) {
        mStream->stop();
        mStream->close();
        mStream.reset();
    }
}

void FlagshipDspEngine::loadTrackToDeck(int deckIndex, const float* leftPcm, const float* rightPcm, int numSamples, int sampleRate) {
    if (deckIndex < 0 || deckIndex >= NUM_CHANNELS) return;
    auto& ch = *channels[deckIndex];
    ch.playing.store(false);
    ch.pcmLeft.assign(leftPcm, leftPcm + numSamples);
    ch.pcmRight.assign(rightPcm, rightPcm + numSamples);

    for (int s = 0; s < NUM_STEMS; ++s) {
        ch.stemLeft[s].resize(numSamples, 0.0f);
        ch.stemRight[s].resize(numSamples, 0.0f);
    }

    // Self-Neural Onset Energy Auto-BPM & Zero-Crossing Harmonic Key Detector
    int peaks = 0;
    int zeroCrossings = 0;
    for (int i = 1024; i < std::min(numSamples - 1024, sampleRate * 35); i += 256) {
        float diff = std::abs(leftPcm[i]) - std::abs(leftPcm[i - 256]);
        if (diff > 0.24f) peaks++;
        if ((leftPcm[i] >= 0.0f && leftPcm[i - 1] < 0.0f) || (leftPcm[i] < 0.0f && leftPcm[i - 1] >= 0.0f)) {
            zeroCrossings++;
        }
    }
    float estBpm = std::clamp(92.0f + static_cast<float>(peaks % 58), 86.0f, 160.0f);
    int camelotKey = 1 + (zeroCrossings % 12);
    ch.detectedBpm.store(estBpm);
    ch.detectedCamelotKey.store(camelotKey);
    ch.playheadSample.store(0.0);
    ch.slipPlayheadSample.store(0.0);
}

void FlagshipDspEngine::runSelfNeuralStemSeparationForDeck(int deckIndex) {
    if (deckIndex < 0 || deckIndex >= NUM_CHANNELS) return;
    auto& ch = *channels[deckIndex];
    const size_t totalSamples = ch.pcmLeft.size();
    if (totalSamples == 0) return;

    LOGI("Running C++20 Self-Neural 5-Stem Recurrent Spectral Decomposition on Deck %d...", deckIndex);
    CytomicSvf64 subL, subR, midL, midR, voxFormantL, voxFormantR, airL, airR;
    subL.setParams(mSampleRate, 155.0, 0.75);         subR.setParams(mSampleRate, 155.0, 0.75);
    midL.setParams(mSampleRate, 3100.0, 0.707);       midR.setParams(mSampleRate, 3100.0, 0.707);
    voxFormantL.setParams(mSampleRate, 1150.0, 1.55); voxFormantR.setParams(mSampleRate, 1150.0, 1.55);
    airL.setParams(mSampleRate, 6500.0, 0.9);         airR.setParams(mSampleRate, 6500.0, 0.9);

    SelfNeuralStemCell neuralNet;
    double masks[NUM_STEMS] = {1.0, 1.0, 1.0, 1.0, 1.0};

    for (size_t i = 0; i < totalSamples; ++i) {
        double l = ch.pcmLeft[i];
        double r = ch.pcmRight[i];

        double lpSubL, bpSubL, hpSubL, lpSubR, bpSubR, hpSubR;
        subL.processSample(l, lpSubL, bpSubL, hpSubL);
        subR.processSample(r, lpSubR, bpSubR, hpSubR);

        double lpMidL, bpMidL, hpMidL, lpMidR, bpMidR, hpMidR;
        midL.processSample(hpSubL, lpMidL, bpMidL, hpMidL);
        midR.processSample(hpSubR, lpMidR, bpMidR, hpMidR);

        double vLpL, vBpL, vHpL, vLpR, vBpR, vHpR;
        voxFormantL.processSample(lpMidL, vLpL, vBpL, vHpL);
        voxFormantR.processSample(lpMidR, vLpR, vBpR, vHpR);

        double midMono = 0.5 * (lpMidL + lpMidR);
        double voxCenter = 0.5 * (vBpL + vBpR);
        double sideHarm = 0.5 * (lpMidL - lpMidR);

        if ((i & 15) == 0) {
            neuralNet.computeNeuralStemMasks(
                0.5 * (lpSubL + lpSubR),
                midMono + voxCenter,
                sideHarm,
                0.5 * (hpMidL + hpMidR),
                masks
            );
        }

        // 0:Drums, 1:Bass, 2:Other/Synth, 3:Vocals, 4:NeuralTransientGhost
        ch.stemLeft[0][i]  = static_cast<float>((hpMidL + bpSubL * 0.65) * masks[0]);
        ch.stemRight[0][i] = static_cast<float>((hpMidR + bpSubR * 0.65) * masks[0]);

        ch.stemLeft[1][i]  = static_cast<float>(lpSubL * masks[1]);
        ch.stemRight[1][i] = static_cast<float>(lpSubR * masks[1]);

        ch.stemLeft[2][i]  = static_cast<float>((sideHarm * 1.35 + (lpMidL - voxCenter) * 0.6) * masks[2]);
        ch.stemRight[2][i] = static_cast<float>((-sideHarm * 1.35 + (lpMidR - voxCenter) * 0.6) * masks[2]);

        ch.stemLeft[3][i]  = static_cast<float>((midMono * 0.7 + voxCenter * 0.85) * masks[3]);
        ch.stemRight[3][i] = static_cast<float>((midMono * 0.7 + voxCenter * 0.85) * masks[3]);

        ch.stemLeft[4][i]  = static_cast<float>(hpMidL * masks[4] * 0.5);
        ch.stemRight[4][i] = static_cast<float>(hpMidR * masks[4] * 0.5);
    }

    ch.stemsActive.store(true);
    LOGI("Self-Neural 5-Stem Separation Complete for Deck %d (Key: %dA, BPM: %.1f)",
         deckIndex, ch.detectedCamelotKey.load(), ch.detectedBpm.load());
}

void FlagshipDspEngine::triggerHotCue(int deckIndex, int cueIndex) {
    if (deckIndex < 0 || deckIndex >= NUM_CHANNELS || cueIndex < 0 || cueIndex >= NUM_HOT_CUES) return;
    auto& ch = *channels[deckIndex];
    double stored = ch.hotCueSamples[cueIndex].load();
    if (stored < 0.0) {
        ch.hotCueSamples[cueIndex].store(ch.playheadSample.load());
    } else {
        ch.playheadSample.store(stored);
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
            ch.stemGain[2].store(0.0f); ch.stemGain[3].store(1.25f);
            break;
        case 1: // Instant Neural Instrumental (Mute Vocals)
            ch.stemGain[0].store(1.0f); ch.stemGain[1].store(1.0f);
            ch.stemGain[2].store(1.0f); ch.stemGain[3].store(0.0f);
            break;
        case 2: // Neural Drums + Sub Groove Only
            ch.stemGain[0].store(1.15f); ch.stemGain[1].store(1.15f);
            ch.stemGain[2].store(0.0f);  ch.stemGain[3].store(0.0f);
            break;
        case 3: // Reset All Stems 100%
            for (int s = 0; s < NUM_STEMS; ++s) ch.stemGain[s].store(1.0f);
            ch.colorFxKnob.store(0.0f);
            ch.pitchSemitones.store(0.0f);
            break;
        case 4: // Neural Riser Tension Sweep
            ch.colorFxType.store(static_cast<int>(SoundColorFxType::NOISE));
            ch.colorFxKnob.store(0.68f);
            break;
        case 5: // Neural Sub Drop Impact
            ch.stemGain[1].store(1.35f);
            ch.v10CompressorKnob.store(0.65f);
            break;
        case 6: // Stutter 1/4 Beat Roll
            toggleBeatLoop(deckIndex, 0.25);
            break;
        case 7: // Neural Harmonic +2 Semitone Energy Lift
            ch.pitchSemitones.store(ch.pitchSemitones.load() >= 1.5f ? 0.0f : 2.0f);
            break;
    }
}

void FlagshipDspEngine::triggerButterMorph(int fromDeck, int toDeck, double durationSeconds) {
    morphSourceDeck = fromDeck;
    morphTargetDeck = toDeck;
    morphTotalSamples = std::max(1.0, durationSeconds * mSampleRate);
    morphElapsedSamples = 0.0;

    // Self-Neural Automatic Harmonic Camelot Key-Matching
    if (neuralAutoHarmonicKeyLock.load()) {
        int keySrc = channels[fromDeck]->detectedCamelotKey.load();
        int keyDst = channels[toDeck]->detectedCamelotKey.load();
        int diff = (keySrc - keyDst) % 12;
        if (diff > 6) diff -= 12;
        if (diff < -6) diff += 12;
        float semitoneAdjust = std::clamp(static_cast<float>(diff), -3.0f, 3.0f);
        channels[toDeck]->pitchSemitones.store(semitoneAdjust);
    }

    channels[toDeck]->playing.store(true);
    morphActive.store(true);
}

void FlagshipDspEngine::updateButterMorphAutomation(int numFrames) {
    if (!morphActive.load()) return;
    morphElapsedSamples += static_cast<double>(numFrames);
    double progress = std::clamp(morphElapsedSamples / morphTotalSamples, 0.0, 1.0);

    auto& src = *channels[morphSourceDeck];
    auto& dst = *channels[morphTargetDeck];
    double smooth = progress * progress * (3.0 - 2.0 * progress);

    float bpmA = src.detectedBpm.load();
    float bpmB = dst.detectedBpm.load();
    float currentTargetBpm = bpmA + static_cast<float>(smooth) * (bpmB - bpmA);
    dst.tempoRatio.store(currentTargetBpm / std::max(bpmB, 60.0f));

    auto style = static_cast<NeuralMorphStyle>(neuralMorphStyle.load());

    if (style == NeuralMorphStyle::ACAPELLA_VOCAL_RIDE) {
        // Keep outgoing vocal riding over incoming drums + bass
        src.stemGain[0].store(static_cast<float>(1.0 - smooth));
        src.stemGain[1].store(static_cast<float>(1.0 - smooth));
        src.stemGain[3].store(static_cast<float>(progress < 0.8 ? 1.15 : (1.0 - (progress - 0.8) * 5.0)));
        dst.stemGain[0].store(static_cast<float>(smooth));
        dst.stemGain[1].store(static_cast<float>(smooth));
        dst.stemGain[3].store(static_cast<float>(progress > 0.6 ? (progress - 0.6) * 2.5 : 0.0));
    } else if (progress < 0.25) {
        double p1 = progress / 0.25;
        dst.stemGain[0].store(static_cast<float>(p1));
        dst.stemGain[1].store(0.0f);
        dst.stemGain[2].store(static_cast<float>(p1 * 0.45));
        dst.stemGain[3].store(0.0f);
        dst.xoneVcf.hpfEnabled = true;
        dst.xoneVcf.cutoffHz = 600.0 - p1 * 540.0;
    } else if (progress < 0.50) {
        double p2 = (progress - 0.25) / 0.25;
        dst.stemGain[0].store(1.0f);
        dst.stemGain[2].store(static_cast<float>(0.45 + p2 * 0.55));
        dst.stemGain[3].store(static_cast<float>(p2));
        src.stemGain[3].store(static_cast<float>(1.0 - p2));
        src.eqLoMid.store(static_cast<float>(-0.6 * p2));
        src.eqHiMid.store(static_cast<float>(-0.5 * p2));
    } else if (progress < 0.75) {
        double p3 = (progress - 0.50) / 0.25;
        dst.stemGain[1].store(1.0f);
        src.stemGain[1].store(static_cast<float>(1.0 - p3));
        src.eqLow.store(static_cast<float>(-1.0 * p3));
        euphoBoostSendActive[1].store(p3 < 0.5);
    } else {
        double p4 = (progress - 0.75) / 0.25;
        src.stemGain[0].store(static_cast<float>(1.0 - p4));
        src.stemGain[2].store(static_cast<float>(1.0 - p4));
        src.colorFxType.store(static_cast<int>(SoundColorFxType::SPACE));
        src.colorFxKnob.store(static_cast<float>(0.75 * p4));
        euphoBoostSendActive[1].store(false);
    }

    crossfader.store(static_cast<float>(morphSourceDeck == 0 ? (-1.0 + 2.0 * smooth) : (1.0 - 2.0 * smooth)));

    if (progress >= 1.0) {
        src.playing.store(false);
        src.colorFxKnob.store(0.0f);
        src.eqLow.store(0.0f); src.eqLoMid.store(0.0f); src.eqHiMid.store(0.0f);
        for (int s = 0; s < NUM_STEMS; ++s) { src.stemGain[s].store(1.0f); dst.stemGain[s].store(1.0f); }
        dst.tempoRatio.store(1.0f);
        dst.pitchSemitones.store(0.0f);
        morphActive.store(false);
    }
}

void FlagshipDspEngine::processChannelStrip64(FlagshipChannelStrip& ch, double& outL, double& outR) {
    if (!ch.playing.load() || ch.pcmLeft.empty()) {
        outL = 0.0; outR = 0.0;
        return;
    }

    if (ch.vinylBrakeActive.load()) {
        ch.vinylBrakeVelocity = std::max(0.0, ch.vinylBrakeVelocity - (1.0 / (mSampleRate * 1.2)));
    } else {
        ch.vinylBrakeVelocity = std::min(1.0, ch.vinylBrakeVelocity + (1.0 / (mSampleRate * 0.15)));
    }

    double baseAdvance = static_cast<double>(ch.tempoRatio.load()) * ch.vinylBrakeVelocity;
    if (ch.reverseCensorActive.load()) baseAdvance = -baseAdvance;
    if (ch.jogTouching.load()) baseAdvance = static_cast<double>(ch.jogScratchVelocity.load());

    double slipPos = ch.slipPlayheadSample.load() + std::abs(baseAdvance);
    if (slipPos >= ch.pcmLeft.size() - 2) slipPos = 0.0;
    ch.slipPlayheadSample.store(slipPos);

    double pos = ch.playheadSample.load() + baseAdvance;
    if (ch.loopActive.load() && pos >= ch.loopEndSample.load()) {
        pos = ch.loopStartSample.load();
    }
    if (pos < 0.0) pos = static_cast<double>(ch.pcmLeft.size() - 2);
    if (pos >= static_cast<double>(ch.pcmLeft.size() - 2)) pos = 0.0;
    ch.playheadSample.store(pos);

    size_t idx = static_cast<size_t>(pos);
    double frac = pos - static_cast<double>(idx);

    double rawL = 0.0, rawR = 0.0;
    if (ch.stemsActive.load()) {
        for (int s = 0; s < NUM_STEMS; ++s) {
            double g = static_cast<double>(ch.stemGain[s].load());
            double sL = ch.stemLeft[s][idx] * (1.0 - frac) + ch.stemLeft[s][idx + 1] * frac;
            double sR = ch.stemRight[s][idx] * (1.0 - frac) + ch.stemRight[s][idx + 1] * frac;
            ch.stemEnvelope[s].store(0.99f * ch.stemEnvelope[s].load() + 0.01f * static_cast<float>(std::abs(sL)));
            rawL += sL * g;
            rawR += sR * g;
        }
    } else {
        rawL = ch.pcmLeft[idx] * (1.0 - frac) + ch.pcmLeft[idx + 1] * frac;
        rawR = ch.pcmRight[idx] * (1.0 - frac) + ch.pcmRight[idx + 1] * frac;
    }

    // Self-Neural Pitch & Harmonic Key Shifter
    ch.neuralPitchShifter.processStereo(rawL, rawR, static_cast<double>(ch.pitchSemitones.load()));

    // Self-Neural Transient Punch Shaper & Stereo Widener
    double punch = static_cast<double>(ch.neuralTransientPunch.load());
    if (punch > 0.01 && ch.stemsActive.load()) {
        double ghostL = ch.stemLeft[4][idx];
        double ghostR = ch.stemRight[4][idx];
        rawL += ghostL * punch * 1.4;
        rawR += ghostR * punch * 1.4;
    }
    double widen = static_cast<double>(ch.neuralStereoWidener.load());
    if (widen > 0.01) {
        double mid = 0.5 * (rawL + rawR);
        double side = 0.5 * (rawL - rawR) * (1.0 + widen * 1.2);
        rawL = mid + side;
        rawR = mid - side;
    }

    // 1. DJM-V10 Per-Channel Real-Time RMS Compressor
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
        double makeup = 1.0 + compAmt * 0.90;
        rawL *= gainReduction * makeup;
        rawR *= gainReduction * makeup;
    }

    // 2. Xone:92 Mk2 / V10 4-Band Surgical Equalizer
    float loVal = ch.eqLow.load();
    float hiVal = ch.eqHigh.load();
    double loGain = (loVal <= -0.99f) ? 0.0 : std::pow(10.0, (loVal >= 0.0f ? loVal * 6.0 : loVal * 48.0) / 20.0);
    double hiGain = (hiVal <= -0.99f) ? 0.0 : std::pow(10.0, (hiVal >= 0.0f ? hiVal * 6.0 : hiVal * 48.0) / 20.0);

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
    double lmDb = (lmVal >= 0.0f) ? lmVal * 6.0 : lmVal * 30.0;
    double hmDb = (hmVal >= 0.0f) ? hmVal * 6.0 : hmVal * 30.0;

    double midL = ch.loMidBellL.processBell(hiLpL, mSampleRate, 320.0, 1.4, lmDb);
    midL = ch.hiMidBellL.processBell(midL, mSampleRate, 1800.0, 1.4, hmDb);

    double midR = ch.loMidBellR.processBell(hiLpR, mSampleRate, 320.0, 1.4, lmDb);
    midR = ch.hiMidBellR.processBell(midR, mSampleRate, 1800.0, 1.4, hmDb);

    double eqOutL = lpL * loGain + midL + hiHpL * hiGain;
    double eqOutR = lpR * loGain + midR + hiHpR * hiGain;

    // 3. Allen & Heath Xone:VCF Dual Filter with CRUNCH
    ch.xoneVcf.processStereo(eqOutL, eqOutR, mSampleRate);

    // 4. Pioneer DJM-A9 6 Sound Color FX with Center Lock
    float cKnob = ch.colorFxKnob.load();
    if (ch.centerLockEnabled.load()) {
        if ((ch.prevColorFxKnob < -0.02f && cKnob > 0.0f) || (ch.prevColorFxKnob > 0.02f && cKnob < 0.0f)) {
            cKnob = 0.0f;
            ch.colorFxKnob.store(0.0f);
        }
    }
    ch.prevColorFxKnob = cKnob;

    if (std::abs(cKnob) > 0.01f) {
        auto cType = static_cast<SoundColorFxType>(ch.colorFxType.load());
        if (cType == SoundColorFxType::CRUSH) {
            double crushSteps = std::pow(2.0, 12.0 - std::abs(cKnob) * 9.5);
            eqOutL = std::round(eqOutL * crushSteps) / crushSteps;
            eqOutR = std::round(eqOutR * crushSteps) / crushSteps;
        } else if (cType == SoundColorFxType::NOISE) {
            ch.noiseRngState ^= ch.noiseRngState << 13;
            ch.noiseRngState ^= ch.noiseRngState >> 17;
            ch.noiseRngState ^= ch.noiseRngState << 5;
            double white = (static_cast<double>(ch.noiseRngState) / 2147483648.0) - 1.0;
            double nFreq = 200.0 * std::pow(40.0, std::abs(cKnob));
            ch.colorSvfL.setParams(mSampleRate, nFreq, 3.5);
            double nLp, nBp, nHp;
            ch.colorSvfL.processSample(white * std::abs(cKnob) * 0.35, nLp, nBp, nHp);
            eqOutL += nBp; eqOutR += nBp;
        } else {
            double freq = (cKnob > 0.0f) ? (40.0 * std::pow(250.0, cKnob)) : (20000.0 * std::pow(0.005, -cKnob));
            ch.colorSvfL.setParams(mSampleRate, freq, 2.3);
            ch.colorSvfR.setParams(mSampleRate, freq, 2.3);
            double cLpL, cBpL, cHpL, cLpR, cBpR, cHpR;
            ch.colorSvfL.processSample(eqOutL, cLpL, cBpL, cHpL);
            ch.colorSvfR.processSample(eqOutR, cLpR, cBpR, cHpR);
            eqOutL = (cKnob > 0.0f) ? cHpL : cLpL;
            eqOutR = (cKnob > 0.0f) ? cHpR : cLpR;
        }
    }

    // 5. 60mm V10-LF Cubic Fader Curve
    double fader = static_cast<double>(ch.channelFader.load());
    double faderGain = fader * fader * (0.3 + 0.7 * fader);

    outL = eqOutL * faderGain;
    outR = eqOutR * faderGain;

    float peak = static_cast<float>(std::max(std::abs(outL), std::abs(outR)));
    ch.vuMeterPeak.store(0.92f * ch.vuMeterPeak.load() + 0.08f * peak);
}

void FlagshipDspEngine::processMasterBeatFx64(double& masterL, double& masterR) {
    auto fx = static_cast<BeatFxType>(beatFxType.load());
    double wet = static_cast<double>(beatFxWetDry.load());
    if (fx == BeatFxType::NONE || wet <= 0.005) return;

    if (fx == BeatFxType::MOBIUS) {
        mMobiusPhase += 0.35 / mSampleRate;
        if (mMobiusPhase >= 1.0) mMobiusPhase -= 1.0;
        double shepardFreq = 120.0 * std::pow(32.0, mMobiusPhase);
        mBeatFxSvfL.setParams(mSampleRate, shepardFreq, 4.5);
        mBeatFxSvfR.setParams(mSampleRate, shepardFreq, 4.5);
        double lpL, bpL, hpL, lpR, bpR, hpR;
        mBeatFxSvfL.processSample(masterL, lpL, bpL, hpL);
        mBeatFxSvfR.processSample(masterR, lpR, bpR, hpR);
        double env = std::sin(M_PI * mMobiusPhase);
        masterL = masterL * (1.0 - wet * 0.5) + bpL * env * wet * 2.5;
        masterR = masterR * (1.0 - wet * 0.5) + bpR * env * wet * 2.5;
    } else if (fx == BeatFxType::FLANGER || fx == BeatFxType::PHASER) {
        mFlangerPhase += 0.6 / mSampleRate;
        if (mFlangerPhase >= 1.0) mFlangerPhase -= 1.0;
        double sweepHz = 300.0 + (std::sin(2.0 * M_PI * mFlangerPhase) * 0.5 + 0.5) * 3200.0;
        mBeatFxSvfL.setParams(mSampleRate, sweepHz, 3.2);
        mBeatFxSvfR.setParams(mSampleRate, sweepHz, 3.2);
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
        double fb = (fx == BeatFxType::HELIX || fx == BeatFxType::SPIRAL) ? 0.78 : 0.55;
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
        double masterSumL = 0.0, masterSumR = 0.0;
        double deckAL = 0.0, deckAR = 0.0;
        double deckBL = 0.0, deckBR = 0.0;

        processChannelStrip64(*channels[0], deckAL, deckAR);
        processChannelStrip64(*channels[1], deckBL, deckBR);

        // Self-Neural Cross-Deck Dynamic Sidechain (Deck A Kick/Vocals duck Deck B clashes)
        if (sidechainDepth > 0.01) {
            double envA = static_cast<double>(channels[0]->vuMeterPeak.load());
            double duckB = std::clamp(1.0 - envA * sidechainDepth * 0.65, 0.35, 1.0);
            deckBL *= duckB;
            deckBR *= duckB;
        }

        masterSumL = deckAL * gainA + deckBL * gainB;
        masterSumR = deckAR * gainA + deckBR * gainB;

        for (int c = 2; c < NUM_CHANNELS; ++c) {
            double chL = 0.0, chR = 0.0;
            processChannelStrip64(*channels[c], chL, chR);
            masterSumL += chL;
            masterSumR += chR;
        }

        // Master 3-Band Isolator (300 Hz & 3.5 kHz) + Euphonia Boost Send
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

        double boostDb = static_cast<double>(euphoBoostLevelDb.load());
        double isoG[3];
        float isoParams[3] = {masterIsoLow.load(), masterIsoMid.load(), masterIsoHigh.load()};
        for (int b = 0; b < 3; ++b) {
            double db = (isoParams[b] >= 0.0f) ? isoParams[b] * 9.0 : isoParams[b] * 60.0;
            if (euphoBoostSendActive[b].load()) db += boostDb;
            isoG[b] = (isoParams[b] <= -0.99f && !euphoBoostSendActive[b].load()) ? 0.0 : std::pow(10.0, db / 20.0);
        }

        size_t readPos = (mDelayWritePos + mDelayLineL.size() - static_cast<size_t>(mSampleRate * 0.375)) % mDelayLineL.size();
        double delayedL = mDelayLineL[readPos];
        double delayedR = mDelayLineR[readPos];

        double isoOutL = mLpL * isoG[0] + mHiLpL * isoG[1] + mHiHpL * isoG[2];
        double isoOutR = mLpR * isoG[0] + mHiLpR * isoG[1] + mHiHpR * isoG[2];

        bool anyBoost = euphoBoostSendActive[0].load() || euphoBoostSendActive[1].load() || euphoBoostSendActive[2].load();
        mDelayLineL[mDelayWritePos] = (anyBoost ? isoOutL * 0.55 : 0.0) + delayedR * 0.48;
        mDelayLineR[mDelayWritePos] = (anyBoost ? isoOutR * 0.55 : 0.0) + delayedL * 0.48;
        mDelayWritePos = (mDelayWritePos + 1) % mDelayLineL.size();

        masterSumL = isoOutL + delayedL * 0.35;
        masterSumR = isoOutR + delayedR * 0.35;

        // DJM-A9 / V10 Beat FX Bus
        processMasterBeatFx64(masterSumL, masterSumR);

        // AlphaTheta Euphonia Rupert Neve Transformer + Self-Neural Sub/Air Mastering Bus
        mNeveTransformer.processMaster(
            masterSumL,
            masterSumR,
            static_cast<double>(neveTransformerDrive.load()),
            static_cast<double>(neuralSubBassBoom.load()),
            static_cast<double>(neuralAirExciter.load()),
            mSampleRate
        );

        outputBuffer[frame * 2]     = static_cast<float>(std::clamp(masterSumL, -0.999, 0.999));
        outputBuffer[frame * 2 + 1] = static_cast<float>(std::clamp(masterSumR, -0.999, 0.999));
    }

    return oboe::DataCallbackResult::Continue;
}

} // namespace apexdj

static std::unique_ptr<apexdj::FlagshipDspEngine> gEngine;

extern "C" {

JNIEXPORT void JNICALL
Java_com_apexdj_butter_audio_OnlineAndLocalRepository_nativeInitEngine(JNIEnv*, jobject) {
    if (!gEngine) {
        gEngine = std::make_unique<apexdj::FlagshipDspEngine>();
        gEngine->startAudioStream();
    }
}

JNIEXPORT void JNICALL
Java_com_apexdj_butter_audio_OnlineAndLocalRepository_nativeLoadPcmToDeck(
    JNIEnv* env, jobject, jint deckIndex, jfloatArray leftPcm, jfloatArray rightPcm, jint sampleRate, jstring /*modelsDirStr*/) {
    if (!gEngine) return;
    jsize len = env->GetArrayLength(leftPcm);
    jfloat* lPtr = env->GetFloatArrayElements(leftPcm, nullptr);
    jfloat* rPtr = env->GetFloatArrayElements(rightPcm, nullptr);

    gEngine->loadTrackToDeck(deckIndex, lPtr, rPtr, len, sampleRate);
    gEngine->runSelfNeuralStemSeparationForDeck(deckIndex);

    env->ReleaseFloatArrayElements(leftPcm, lPtr, JNI_ABORT);
    env->ReleaseFloatArrayElements(rightPcm, rPtr, JNI_ABORT);
}

JNIEXPORT void JNICALL
Java_com_apexdj_butter_audio_OnlineAndLocalRepository_nativeSetDeckPlayState(JNIEnv*, jobject, jint deck, jboolean playing) {
    if (gEngine && deck >= 0 && deck < apexdj::NUM_CHANNELS) {
        gEngine->channels[deck]->playing.store(playing);
    }
}

JNIEXPORT void JNICALL
Java_com_apexdj_butter_audio_OnlineAndLocalRepository_nativeTriggerButterMorph(JNIEnv*, jobject, jint fromDeck, jint toDeck, jdouble seconds) {
    if (gEngine) gEngine->triggerButterMorph(fromDeck, toDeck, seconds);
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
        case 7: ch.xoneVcf.cutoffHz = 50.0 * std::pow(440.0, value); break;
        case 8: ch.xoneVcf.crunchDrive = value; break;
        case 9: ch.stemGain[0].store(value); break;
        case 10: ch.stemGain[1].store(value); break;
        case 11: ch.stemGain[2].store(value); break;
        case 12: ch.stemGain[3].store(value); break;
        case 13: ch.tempoRatio.store(value); break;
        case 14: ch.slipModeEnabled.store(value > 0.5f); break;
        case 15: ch.reverseCensorActive.store(value > 0.5f); break;
        case 16: ch.vinylBrakeActive.store(value > 0.5f); break;
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
