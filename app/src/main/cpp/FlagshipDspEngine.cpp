#include "FlagshipDspEngine.h"
#include <jni.h>
#include <android/log.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "ApexDJ_V10_DSP", __VA_ARGS__)

namespace apexdj {

FlagshipDspEngine::FlagshipDspEngine() {
    for (int i = 0; i < NUM_CHANNELS; ++i) {
        channels[i] = std::make_unique<FlagshipChannelStrip>();
        channels[i]->init(48000);
    }
    mDelayLineL.resize(96000 * 4, 0.0);
    mDelayLineR.resize(96000 * 4, 0.0);
    synthesizeClubSamplerBank(48000);
}

FlagshipDspEngine::~FlagshipDspEngine() {
    stopAudioStream();
}

bool FlagshipDspEngine::startAudioStream() {
    oboe::AudioStreamBuilder builder;
    builder.setDirection(oboe::Direction::Output)
           ->setPerformanceMode(oboe::PerformanceMode::LowLatency)
           ->setSharingMode(oboe::SharingMode::Shared)
           ->setFormat(oboe::AudioFormat::Float)
           ->setChannelCount(oboe::ChannelCount::Stereo)
           ->setFramesPerDataCallback(256)
           ->setDataCallback(this)
           ->setErrorCallback(this);

    oboe::Result result = builder.openStream(mStream);
    if (result != oboe::Result::OK) return false;

    mSampleRate = static_cast<double>(mStream->getSampleRate());
    for (int i = 0; i < NUM_CHANNELS; ++i) {
        channels[i]->init(static_cast<int>(mSampleRate));
    }
    synthesizeClubSamplerBank(static_cast<int>(mSampleRate));

    mStream->requestStart();
    LOGI("ApexDJ V10 Sovereign 64-Bit Engine active at %.0f Hz", mSampleRate);
    return true;
}

void FlagshipDspEngine::stopAudioStream() {
    if (mStream) {
        mStream->stop();
        mStream->close();
        mStream.reset();
    }
}

void FlagshipDspEngine::onErrorAfterClose(oboe::AudioStream*, oboe::Result) {
    startAudioStream();
}

void FlagshipDspEngine::synthesizeClubSamplerBank(int sr) {
    // Pad 0: Festival Airhorn
    int len0 = static_cast<int>(sr * 1.4);
    samplerPads[0].sampleL.resize(len0); samplerPads[0].sampleR.resize(len0);
    for (int i = 0; i < len0; ++i) {
        double t = static_cast<double>(i) / sr;
        double env = std::min(1.0, t * 25.0) * std::pow(1.0 - (t / 1.4), 0.6);
        double osc = std::sin(2.0 * M_PI * 440.0 * t) + 0.7 * std::sin(2.0 * M_PI * 554.37 * t) + 0.6 * std::sin(2.0 * M_PI * 659.25 * t);
        float val = static_cast<float>(std::tanh(osc * 2.8) * env * 0.75);
        samplerPads[0].sampleL[i] = val; samplerPads[0].sampleR[i] = val;
    }

    // Pad 1: 808 Sub-Bass Boom Drop
    int len1 = static_cast<int>(sr * 1.8);
    samplerPads[1].sampleL.resize(len1); samplerPads[1].sampleR.resize(len1);
    double phase1 = 0.0;
    for (int i = 0; i < len1; ++i) {
        double t = static_cast<double>(i) / sr;
        phase1 += (38.0 + 95.0 * std::exp(-t * 6.5)) / sr;
        float val = static_cast<float>(std::tanh(std::sin(2.0 * M_PI * phase1) * 1.9) * std::pow(1.0 - (t / 1.8), 1.2) * 0.9);
        samplerPads[1].sampleL[i] = val; samplerPads[1].sampleR[i] = val;
    }

    // Pad 2: White Noise Jet Riser
    int len2 = static_cast<int>(sr * 2.0);
    samplerPads[2].sampleL.resize(len2); samplerPads[2].sampleR.resize(len2);
    CytomicSvf64 riserSvf;
    uint32_t rng = 123456789U;
    for (int i = 0; i < len2; ++i) {
        double p = static_cast<double>(i) / len2;
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        double white = (static_cast<double>(rng) / 2147483648.0) - 1.0;
        riserSvf.setParams(sr, 200.0 * std::pow(35.0, p), 3.5);
        double lp, bp, hp;
        riserSvf.processSample(white, lp, bp, hp);
        float val = static_cast<float>(bp * p * 0.85);
        samplerPads[2].sampleL[i] = val; samplerPads[2].sampleR[i] = val;
    }

    // Pad 3: Laser Dub Zap
    int len3 = static_cast<int>(sr * 0.65);
    samplerPads[3].sampleL.resize(len3); samplerPads[3].sampleR.resize(len3);
    double phase3 = 0.0;
    for (int i = 0; i < len3; ++i) {
        double p = static_cast<double>(i) / len3;
        phase3 += (180.0 + 3600.0 * std::pow(1.0 - p, 3.2)) / sr;
        float val = static_cast<float>(std::sin(2.0 * M_PI * phase3) * (1.0 - p) * 0.75);
        samplerPads[3].sampleL[i] = val; samplerPads[3].sampleR[i] = val;
    }

    // Pad 4: Stadium Snare Clap
    int len4 = static_cast<int>(sr * 0.5);
    samplerPads[4].sampleL.resize(len4); samplerPads[4].sampleR.resize(len4);
    for (int i = 0; i < len4; ++i) {
        double p = static_cast<double>(i) / len4;
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        double noise = (static_cast<double>(rng) / 2147483648.0) - 1.0;
        double body = std::sin(2.0 * M_PI * 185.0 * (static_cast<double>(i) / sr)) * std::exp(-p * 18.0);
        float val = static_cast<float>(noise * std::exp(-p * 9.0) * 0.65 + body * 0.7);
        samplerPads[4].sampleL[i] = val; samplerPads[4].sampleR[i] = val;
    }

    // Pad 5: Reverse Sub Vacuum
    int len5 = static_cast<int>(sr * 1.2);
    samplerPads[5].sampleL.resize(len5); samplerPads[5].sampleR.resize(len5);
    double phase5 = 0.0;
    for (int i = 0; i < len5; ++i) {
        double p = static_cast<double>(i) / len5;
        phase5 += (45.0 + 220.0 * (p * p * p)) / sr;
        float val = static_cast<float>(std::sin(2.0 * M_PI * phase5) * (p * p) * 0.85);
        samplerPads[5].sampleL[i] = val; samplerPads[5].sampleR[i] = val;
    }

    // Pad 6: Jamaican Dub Siren
    int len6 = static_cast<int>(sr * 1.5);
    samplerPads[6].sampleL.resize(len6); samplerPads[6].sampleR.resize(len6);
    double phase6 = 0.0;
    for (int i = 0; i < len6; ++i) {
        double t = static_cast<double>(i) / sr;
        phase6 += (680.0 + std::sin(2.0 * M_PI * 6.5 * t) * 260.0) / sr;
        float val = static_cast<float>(std::tanh(std::sin(2.0 * M_PI * phase6) * 2.2) * (1.0 - t / 1.5) * 0.65);
        samplerPads[6].sampleL[i] = val; samplerPads[6].sampleR[i] = val;
    }

    // Pad 7: Turntable Backspin Whip
    int len7 = static_cast<int>(sr * 0.8);
    samplerPads[7].sampleL.resize(len7); samplerPads[7].sampleR.resize(len7);
    double phase7 = 0.0;
    for (int i = 0; i < len7; ++i) {
        double p = static_cast<double>(i) / len7;
        phase7 += (320.0 + 1400.0 * std::sin(M_PI * p)) / sr;
        float val = static_cast<float>(std::sin(2.0 * M_PI * phase7) * (1.0 - p * 0.7) * 0.75);
        samplerPads[7].sampleL[i] = val; samplerPads[7].sampleR[i] = val;
    }
}

void FlagshipDspEngine::triggerSamplerPad(int padIndex) {
    if (padIndex < 0 || padIndex >= NUM_SAMPLER_PADS) return;
    samplerPads[padIndex].readPos.store(0);
    samplerPads[padIndex].active.store(true);
}

void FlagshipDspEngine::loadTrackToDeck(int deckIndex, const float* leftPcm, const float* rightPcm, int numSamples, int sampleRate) {
    if (deckIndex < 0 || deckIndex >= NUM_CHANNELS || numSamples <= 64) return;
    auto& ch = *channels[deckIndex];
    std::lock_guard<std::mutex> guard(ch.loadMutex);

    int targetSlot = 1 - ch.activeSlot.load(std::memory_order_relaxed);
    auto& slot = ch.bufferSlots[targetSlot];
    slot.pcmLeft.assign(leftPcm, leftPcm + numSamples);
    slot.pcmRight.assign(rightPcm, rightPcm + numSamples);
    slot.numSamples = numSamples;

    CytomicSvf64 subF, midF;
    subF.setParams(sampleRate, 165.0, 0.707);
    midF.setParams(sampleRate, 2600.0, 0.707);

    std::array<float, WAVEFORM_BINS * 3> flatWave{};
    int samplesPerBin = std::max(1, numSamples / WAVEFORM_BINS);
    int onsetPeaks = 0;
    int zeroCrossings = 0;

    for (int i = 0; i < numSamples; i += 4) {
        double l = leftPcm[i];
        if (i >= 4 && ((l >= 0.0 && leftPcm[i - 4] < 0.0) || (l < 0.0 && leftPcm[i - 4] >= 0.0))) {
            zeroCrossings++;
        }
        double lpSub, bpSub, hpSub;
        subF.processSample(l, lpSub, bpSub, hpSub);
        double lpMid, bpMid, hpMid;
        midF.processSample(hpSub, lpMid, bpMid, hpMid);

        int bin = std::min(WAVEFORM_BINS - 1, i / samplesPerBin);
        flatWave[bin * 3]     = std::max(flatWave[bin * 3],     static_cast<float>(std::abs(lpSub)));
        flatWave[bin * 3 + 1] = std::max(flatWave[bin * 3 + 1], static_cast<float>(std::abs(lpMid)));
        flatWave[bin * 3 + 2] = std::max(flatWave[bin * 3 + 2], static_cast<float>(std::abs(hpMid)));

        if ((i & 511) == 0 && i >= 512) {
            if (std::abs(l) - std::abs(leftPcm[i - 512]) > 0.24) onsetPeaks++;
        }
    }

    float durationSec = std::max(1.0f, static_cast<float>(numSamples) / sampleRate);
    float rawBpm = (onsetPeaks * 60.0f) / (durationSec * 2.2f);
    while (rawBpm < 86.0f) rawBpm *= 1.5f;
    while (rawBpm > 162.0f) rawBpm *= 0.5f;
    int camelot = 1 + ((zeroCrossings / std::max(1, static_cast<int>(durationSec))) % 12);

    ch.rgbWaveformFlat = flatWave;
    ch.totalTrackSamples.store(numSamples, std::memory_order_release);
    ch.playheadSample.store(0.0, std::memory_order_release);
    ch.slipPlayheadSample.store(0.0, std::memory_order_release);
    ch.detectedBpm.store(std::clamp(rawBpm, 88.0f, 160.0f), std::memory_order_release);
    ch.camelotKeyIndex.store(camelot, std::memory_order_release);
    for (int c = 0; c < NUM_HOT_CUES; ++c) ch.hotCueSamples[c].store(-1.0);

    ch.activeSlot.store(targetSlot, std::memory_order_release);
    ch.waveformVersion.fetch_add(1);
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

void FlagshipDspEngine::clearHotCue(int deckIndex, int cueIndex) {
    if (deckIndex < 0 || deckIndex >= NUM_CHANNELS || cueIndex < 0 || cueIndex >= NUM_HOT_CUES) return;
    channels[deckIndex]->hotCueSamples[cueIndex].store(-1.0);
}

void FlagshipDspEngine::toggleBeatLoop(int deckIndex, double beats) {
    if (deckIndex < 0 || deckIndex >= NUM_CHANNELS) return;
    auto& ch = *channels[deckIndex];
    if (beats <= 0.0 || ch.loopActive.load()) {
        ch.loopActive.store(false);
        if (ch.slipModeEnabled.load()) {
            ch.playheadSample.store(ch.slipPlayheadSample.load());
        }
        return;
    }
    double bpm = std::max(60.0f, ch.detectedBpm.load());
    double samplesPerBeat = (60.0 / bpm) * mSampleRate;
    double start = ch.playheadSample.load();
    double total = static_cast<double>(ch.totalTrackSamples.load());
    ch.loopStartSample.store(start);
    ch.loopEndSample.store(std::min(total - 2.0, start + samplesPerBeat * beats));
    ch.loopActive.store(true);
}

void FlagshipDspEngine::beatJump(int deckIndex, double beats) {
    if (deckIndex < 0 || deckIndex >= NUM_CHANNELS) return;
    auto& ch = *channels[deckIndex];
    double bpm = std::max(60.0f, ch.detectedBpm.load());
    double deltaSamples = (60.0 / bpm) * mSampleRate * beats;
    double total = static_cast<double>(ch.totalTrackSamples.load());
    double nextPos = std::clamp(ch.playheadSample.load() + deltaSamples, 0.0, std::max(1.0, total - 2.0));
    ch.playheadSample.store(nextPos);
}

void FlagshipDspEngine::seekNormalized(int deckIndex, float normalizedPos) {
    if (deckIndex < 0 || deckIndex >= NUM_CHANNELS) return;
    auto& ch = *channels[deckIndex];
    double total = static_cast<double>(ch.totalTrackSamples.load());
    double target = std::clamp(static_cast<double>(normalizedPos) * total, 0.0, std::max(1.0, total - 2.0));
    ch.playheadSample.store(target);
    ch.slipPlayheadSample.store(target);
}

void FlagshipDspEngine::triggerButterMorph(int fromDeck, int toDeck, double durationSeconds, int styleId) {
    if (fromDeck < 0 || fromDeck >= NUM_CHANNELS || toDeck < 0 || toDeck >= NUM_CHANNELS) return;
    morphSourceDeck = fromDeck;
    morphTargetDeck = toDeck;
    morphStyle.store(styleId);
    morphTotalSamples = std::max(mSampleRate * 4.0, durationSeconds * mSampleRate);
    morphElapsedSamples = 0.0;
    morphProgressUI.store(0.0f);

    // Phase-Locked Loop (PLL) Sub-Sample Beatgrid Alignment
    auto& src = *channels[fromDeck];
    auto& dst = *channels[toDeck];
    double srcSamplesPerBeat = (60.0 / std::max(60.0f, src.detectedBpm.load())) * mSampleRate;
    double dstSamplesPerBeat = (60.0 / std::max(60.0f, dst.detectedBpm.load())) * mSampleRate;
    double srcBeatPhase = std::fmod(src.playheadSample.load(), srcSamplesPerBeat) / srcSamplesPerBeat;
    double dstCurrentBeat = std::floor(dst.playheadSample.load() / dstSamplesPerBeat);
    double totalDst = static_cast<double>(dst.totalTrackSamples.load());
    double alignedPos = std::fmod((dstCurrentBeat + srcBeatPhase) * dstSamplesPerBeat, std::max(1.0, totalDst - 4.0));
    dst.playheadSample.store(alignedPos);

    src.playing.store(true);
    dst.playing.store(true);
    morphActive.store(true);
}

// 12 Superhuman Gemini 4 Argon V10 AI Transition Programs
void FlagshipDspEngine::updateArgonAiMorphAutomation(int numFrames) {
    if (!morphActive.load()) return;
    morphElapsedSamples += static_cast<double>(numFrames);
    double p = std::clamp(morphElapsedSamples / morphTotalSamples, 0.0, 1.0);
    morphProgressUI.store(static_cast<float>(p));

    auto& src = *channels[morphSourceDeck];
    auto& dst = *channels[morphTargetDeck];

    // C2-continuous Cosine S-curve (0.0 -> 1.0 with zero velocity at start and end)
    double sCurve = 0.5 * (1.0 - std::cos(M_PI * p));

    // Continuous Fluid Beatgrid Tempo Lock
    float bpmA = src.detectedBpm.load();
    float bpmB = dst.detectedBpm.load();
    float liveBpm = bpmA + static_cast<float>(sCurve) * (bpmB - bpmA);
    src.tempoRatio.store(liveBpm / std::max(bpmA, 60.0f));
    dst.tempoRatio.store(liveBpm / std::max(bpmB, 60.0f));

    // Base overlapping cosine stem curves
    double dstDrums = std::clamp(0.5 * (1.0 - std::cos(M_PI * std::min(1.0, p / 0.45))), 0.0, 1.0);
    double srcDrums = std::clamp(0.5 * (1.0 + std::cos(M_PI * std::max(0.0, (p - 0.55) / 0.45))), 0.0, 1.0);
    double dstHarm  = std::clamp(0.5 * (1.0 - std::cos(M_PI * std::clamp((p - 0.15) / 0.65, 0.0, 1.0))), 0.0, 1.0);
    double srcHarm  = std::clamp(0.5 * (1.0 + std::cos(M_PI * std::clamp((p - 0.35) / 0.60, 0.0, 1.0))), 0.0, 1.0);
    double dstVox   = std::clamp(0.5 * (1.0 - std::cos(M_PI * std::clamp((p - 0.30) / 0.50, 0.0, 1.0))), 0.0, 1.0);
    double srcVox   = std::clamp(0.5 * (1.0 + std::cos(M_PI * std::clamp((p - 0.20) / 0.45, 0.0, 1.0))), 0.0, 1.0);

    double bassSwap = std::clamp((p - 0.42) / 0.16, 0.0, 1.0);
    double dstBass  = std::sin(bassSwap * M_PI * 0.5);
    double srcBass  = std::cos(bassSwap * M_PI * 0.5);

    auto style = static_cast<ArgonAiMorphStyle>(morphStyle.load());

    switch (style) {
        case ArgonAiMorphStyle::MAINSTAGE_DROP_SWAP: {
            if (p < 0.72) {
                double build = p / 0.72;
                beatFxType.store(static_cast<int>(BeatFxType::MOBIUS));
                beatFxWetDry.store(static_cast<float>(build * 0.78));
                srcBass = 1.0 - build * 0.9;
                dstBass = 0.0;
                dstDrums = build * 0.75;
            } else {
                beatFxWetDry.store(0.0f);
                beatFxType.store(static_cast<int>(BeatFxType::NONE));
                srcBass = 0.0; srcDrums = 0.0; srcHarm = 0.0; srcVox = 0.0;
                dstBass = 1.0; dstDrums = 1.0; dstHarm = 1.0; dstVox = 1.0;
            }
            break;
        }
        case ArgonAiMorphStyle::BERLIN_TECHNO_VCF: {
            src.xoneVcf.lpfEnabled = true;
            src.xoneVcf.cutoffHz = 18000.0 * std::pow(0.015, sCurve);
            src.xoneVcf.crunchDrive = std::sin(M_PI * p) * 0.55;
            dst.xoneVcf.hpfEnabled = true;
            dst.xoneVcf.cutoffHz = 1400.0 * std::pow(0.035, sCurve);
            dst.xoneVcf.crunchDrive = std::sin(M_PI * p) * 0.55;
            break;
        }
        case ArgonAiMorphStyle::BOLLYWOOD_808_BASS_FLIP: {
            // Instant Dhol/Transient lock with 808 Sub-Bass Boost Send
            dstDrums = std::min(1.0, p * 2.5);
            euphoBoostSendActive[0].store(p > 0.45 && p < 0.60);
            break;
        }
        case ArgonAiMorphStyle::EUPHONIA_NEVE_SILK_GLIDE: {
            neveTransformerDrive.store(static_cast<float>(0.65 + 0.30 * std::sin(M_PI * p)));
            euphoBoostSendActive[1].store(p > 0.35 && p < 0.65);
            break;
        }
        case ArgonAiMorphStyle::ACAPELLA_MASHUP_BRIDGE: {
            // Keep outgoing Vocals riding over incoming Drums + Bass groove until 80%
            srcVox = (p < 0.78) ? 1.0 : std::max(0.0, 1.0 - (p - 0.78) / 0.22);
            srcDrums = std::max(0.0, 1.0 - p * 2.2);
            srcBass = std::max(0.0, 1.0 - p * 2.2);
            dstDrums = 1.0;
            dstBass = std::min(1.0, p * 2.2);
            break;
        }
        case ArgonAiMorphStyle::TURNTABLE_SPINBACK_WHIP: {
            if (p > 0.68 && p < 0.82) {
                src.reverseCensorActive.store(true);
            } else {
                src.reverseCensorActive.store(false);
            }
            break;
        }
        case ArgonAiMorphStyle::POLYRHYTHMIC_SPIRAL_ROLL: {
            beatFxType.store(static_cast<int>(BeatFxType::SPIRAL));
            beatFxWetDry.store(static_cast<float>(std::sin(M_PI * p) * 0.62));
            break;
        }
        case ArgonAiMorphStyle::DUB_ECHO_VOCAL_FREEZE: {
            src.colorFxType.store(static_cast<int>(SoundColorFxType::DUB_ECHO));
            src.colorFxKnob.store(static_cast<float>(sCurve * 0.80));
            break;
        }
        case ArgonAiMorphStyle::STEM_CALL_AND_RESPONSE: {
            double gate = (std::sin(2.0 * M_PI * p * 8.0) >= 0.0) ? 1.0 : 0.25;
            srcHarm *= gate;
            dstHarm *= (1.25 - gate);
            break;
        }
        default:
            break;
    }

    dst.stemGain[0].store(static_cast<float>(dstDrums));
    dst.stemGain[1].store(static_cast<float>(dstBass));
    dst.stemGain[2].store(static_cast<float>(dstHarm));
    dst.stemGain[3].store(static_cast<float>(dstVox));

    src.stemGain[0].store(static_cast<float>(srcDrums));
    src.stemGain[1].store(static_cast<float>(srcBass));
    src.stemGain[2].store(static_cast<float>(srcHarm));
    src.stemGain[3].store(static_cast<float>(srcVox));

    src.eqLow.store(static_cast<float>(-0.85 * bassSwap));
    dst.eqLow.store(static_cast<float>(-0.85 * (1.0 - bassSwap)));

    crossfader.store(static_cast<float>(morphSourceDeck == 0 ? (-1.0 + 2.0 * sCurve) : (1.0 - 2.0 * sCurve)));

    if (p >= 1.0) {
        src.playing.store(false);
        src.reverseCensorActive.store(false);
        src.tempoRatio.store(1.0f);
        dst.tempoRatio.store(1.0f);
        src.colorFxKnob.store(0.0f);
        dst.colorFxKnob.store(0.0f);
        src.xoneVcf.hpfEnabled = false; src.xoneVcf.lpfEnabled = true; src.xoneVcf.cutoffHz = 18000.0; src.xoneVcf.crunchDrive = 0.0;
        dst.xoneVcf.hpfEnabled = false; dst.xoneVcf.lpfEnabled = true; dst.xoneVcf.cutoffHz = 18000.0; dst.xoneVcf.crunchDrive = 0.0;
        src.eqLow.store(0.0f); src.eqLoMid.store(0.0f); src.eqHiMid.store(0.0f); src.eqHigh.store(0.0f);
        dst.eqLow.store(0.0f); dst.eqLoMid.store(0.0f); dst.eqHiMid.store(0.0f); dst.eqHigh.store(0.0f);
        for (int s = 0; s < NUM_STEMS; ++s) {
            src.stemGain[s].store(1.0f);
            dst.stemGain[s].store(1.0f);
        }
        euphoBoostSendActive[0].store(false);
        euphoBoostSendActive[1].store(false);
        beatFxWetDry.store(0.0f);
        morphActive.store(false);
    }
}

void FlagshipDspEngine::updateControlRateCoefficients(FlagshipChannelStrip& ch) {
    ch.stemSubFilterL.setParams(mSampleRate, 160.0, 0.707);
    ch.stemSubFilterR.setParams(mSampleRate, 160.0, 0.707);
    ch.stemHighFilterL.setParams(mSampleRate, 2800.0, 0.707);
    ch.stemHighFilterR.setParams(mSampleRate, 2800.0, 0.707);

    ch.lowCrossL.setParams(mSampleRate, 220.0, 0.707);
    ch.lowCrossR.setParams(mSampleRate, 220.0, 0.707);
    ch.highCrossL.setParams(mSampleRate, 2400.0, 0.707);
    ch.highCrossR.setParams(mSampleRate, 2400.0, 0.707);

    float lmVal = ch.eqLoMid.load();
    float hmVal = ch.eqHiMid.load();
    double lmDb = (lmVal >= 0.0f) ? lmVal * 6.0 : lmVal * 30.0;
    double hmDb = (hmVal >= 0.0f) ? hmVal * 6.0 : hmVal * 30.0;

    ch.loMidBellL.setBellParams(mSampleRate, 320.0, 1.4, lmDb);
    ch.loMidBellR.setBellParams(mSampleRate, 320.0, 1.4, lmDb);
    ch.hiMidBellL.setBellParams(mSampleRate, 1800.0, 1.4, hmDb);
    ch.hiMidBellR.setBellParams(mSampleRate, 1800.0, 1.4, hmDb);

    ch.xoneVcf.updateCoefficients(mSampleRate, 32);
}

void FlagshipDspEngine::processChannelStrip64(
    FlagshipChannelStrip& ch,
    double subSidechainDuck,
    double vocalSidechainDuck,
    double& outL,
    double& outR
) {
    if (!ch.playing.load(std::memory_order_relaxed)) {
        ch.liveSubBassEnv.store(0.0f, std::memory_order_relaxed);
        ch.liveVocalEnv.store(0.0f, std::memory_order_relaxed);
        outL = 0.0; outR = 0.0;
        return;
    }

    const int slotIdx = ch.activeSlot.load(std::memory_order_acquire);
    const auto& slot = ch.bufferSlots[slotIdx];
    if (slot.numSamples < 8) {
        outL = 0.0; outR = 0.0;
        return;
    }

    if (ch.vinylBrakeActive.load()) {
        ch.vinylBrakeVelocity = std::max(0.0, ch.vinylBrakeVelocity - (1.0 / (mSampleRate * 0.85)));
    } else {
        ch.vinylBrakeVelocity = std::min(1.0, ch.vinylBrakeVelocity + (1.0 / (mSampleRate * 0.12)));
    }

    double pitchFactor = std::pow(2.0, static_cast<double>(ch.pitchSemitones.load()) / 12.0);
    double baseAdvance = static_cast<double>(ch.tempoRatio.load()) * pitchFactor * ch.vinylBrakeVelocity;
    if (ch.reverseCensorActive.load()) baseAdvance = -baseAdvance;
    if (ch.jogTouching.load()) baseAdvance = static_cast<double>(ch.jogScratchVelocity.load());

    const double maxIdx = static_cast<double>(slot.numSamples - 2);
    double slipPos = ch.slipPlayheadSample.load() + std::abs(baseAdvance);
    if (slipPos >= maxIdx) slipPos = 0.0;
    ch.slipPlayheadSample.store(slipPos);

    double pos = ch.playheadSample.load() + baseAdvance;
    if (ch.loopActive.load() && pos >= ch.loopEndSample.load()) {
        pos = ch.loopStartSample.load();
    }
    if (pos < 0.0) pos = maxIdx;
    if (pos >= maxIdx) pos = 0.0;
    ch.playheadSample.store(pos);

    size_t idx = static_cast<size_t>(pos);
    double frac = pos - static_cast<double>(idx);

    double inL = slot.pcmLeft[idx] * (1.0 - frac) + slot.pcmLeft[idx + 1] * frac;
    double inR = slot.pcmRight[idx] * (1.0 - frac) + slot.pcmRight[idx + 1] * frac;

    // Real-Time 64-Bit 4-Stem Decomposition + Envelope Extraction
    double subLpL, subBpL, subHpL, subLpR, subBpR, subHpR;
    ch.stemSubFilterL.processSample(inL, subLpL, subBpL, subHpL);
    ch.stemSubFilterR.processSample(inR, subLpR, subBpR, subHpR);

    double midLpL, midBpL, midHpL, midLpR, midBpR, midHpR;
    ch.stemHighFilterL.processSample(subHpL, midLpL, midBpL, midHpL);
    ch.stemHighFilterR.processSample(subHpR, midLpR, midBpR, midHpR);

    double energy = std::abs(subHpL) + std::abs(subHpR);
    ch.transientFastEnv = 0.96 * ch.transientFastEnv + 0.04 * energy;
    ch.transientSlowEnv = 0.9985 * ch.transientSlowEnv + 0.0015 * energy;
    double transMask = std::clamp((ch.transientFastEnv - ch.transientSlowEnv) * 4.2, 0.0, 1.0);

    double midMono = 0.5 * (midLpL + midLpR) * (1.0 - transMask);
    double sideHarm = 0.5 * (midLpL - midLpR) * (1.0 - transMask);

    double drumsL = midHpL + midLpL * transMask + subBpL * 0.55;
    double drumsR = midHpR + midLpR * transMask + subBpR * 0.55;
    double bassL  = subLpL;
    double bassR  = subLpR;
    double otherL = sideHarm + midBpL * 0.35;
    double otherR = -sideHarm + midBpR * 0.35;
    double voxL   = midMono;
    double voxR   = midMono;

    // Update live envelopes for cross-deck spectral sidechain ducking
    float subPeak = static_cast<float>(std::max(std::abs(bassL), std::abs(bassR)));
    float voxPeak = static_cast<float>(std::abs(voxL));
    ch.liveSubBassEnv.store(0.992f * ch.liveSubBassEnv.load(std::memory_order_relaxed) + 0.008f * subPeak, std::memory_order_relaxed);
    ch.liveVocalEnv.store(0.992f * ch.liveVocalEnv.load(std::memory_order_relaxed) + 0.008f * voxPeak, std::memory_order_relaxed);

    // Apply Automatic Cross-Deck Spectral Ducking so Sub-Bass & Vocals NEVER clash!
    double subDuckGain = 1.0 - std::clamp(subSidechainDuck, 0.0, 0.85);
    double voxDuckGain = 1.0 - std::clamp(vocalSidechainDuck, 0.0, 0.78);

    double gDrums = ch.stemGain[0].load();
    double gBass  = ch.stemGain[1].load() * subDuckGain;
    double gOther = ch.stemGain[2].load();
    double gVox   = ch.stemGain[3].load() * voxDuckGain;

    double rawL = drumsL * gDrums + bassL * gBass + otherL * gOther + voxL * gVox;
    double rawR = drumsR * gDrums + bassR * gBass + otherR * gOther + voxR * gVox;

    // 1. DJM-V10 Per-Channel Real-Time RMS Compressor
    double compAmt = static_cast<double>(ch.v10CompressorKnob.load());
    if (compAmt > 0.01) {
        double absPeak = std::max(std::abs(rawL), std::abs(rawR));
        ch.compEnv = 0.995 * ch.compEnv + 0.005 * absPeak;
        double threshold = 0.30;
        double gainReduction = 1.0;
        if (ch.compEnv > threshold) {
            double ratio = 1.0 + compAmt * 3.5;
            gainReduction = std::pow(ch.compEnv / threshold, (1.0 / ratio) - 1.0);
        }
        double makeup = 1.0 + compAmt * 0.95;
        rawL *= gainReduction * makeup;
        rawR *= gainReduction * makeup;
    }

    // 2. Xone:92 Mk2 / V10 4-Band Surgical Equalizer
    float loVal = ch.eqLow.load();
    float hiVal = ch.eqHigh.load();
    double loGain = (loVal <= -0.98f) ? 0.0 : (1.0 + loVal * (loVal >= 0.0f ? 1.0 : 0.98));
    double hiGain = (hiVal <= -0.98f) ? 0.0 : (1.0 + hiVal * (hiVal >= 0.0f ? 1.0 : 0.98));

    double lpL, bpL, hpL, lpR, bpR, hpR;
    ch.lowCrossL.processSample(rawL, lpL, bpL, hpL);
    ch.lowCrossR.processSample(rawR, lpR, bpR, hpR);

    double hiLpL, hiBpL, hiHpL, hiLpR, hiBpR, hiHpR;
    ch.highCrossL.processSample(hpL, hiLpL, hiBpL, hiHpL);
    ch.highCrossR.processSample(hpR, hiLpR, hiBpR, hiHpR);

    double midL = ch.hiMidBellL.processBellSample(ch.loMidBellL.processBellSample(hiLpL));
    double midR = ch.hiMidBellR.processBellSample(ch.loMidBellR.processBellSample(hiLpR));

    double eqOutL = lpL * loGain + midL + hiHpL * hiGain;
    double eqOutR = lpR * loGain + midR + hiHpR * hiGain;

    // 3. Allen & Heath Xone:VCF Dual Filter with CRUNCH
    ch.xoneVcf.processStereoSample(eqOutL, eqOutR);

    // 4. Pioneer DJM-A9 6 Sound Color FX with Center Lock
    float cKnob = ch.colorFxKnob.load();
    if (ch.centerLockEnabled.load()) {
        if ((ch.prevColorFxKnob < -0.03f && cKnob > 0.0f) || (ch.prevColorFxKnob > 0.03f && cKnob < 0.0f)) {
            cKnob = 0.0f;
            ch.colorFxKnob.store(0.0f);
        }
    }
    ch.prevColorFxKnob = cKnob;

    if (std::abs(cKnob) > 0.015f) {
        auto cType = static_cast<SoundColorFxType>(ch.colorFxType.load());
        if (cType == SoundColorFxType::CRUSH) {
            double crushSteps = std::max(2.0, std::pow(2.0, 11.0 - std::abs(cKnob) * 9.0));
            eqOutL = std::round(eqOutL * crushSteps) / crushSteps;
            eqOutR = std::round(eqOutR * crushSteps) / crushSteps;
        } else if (cType == SoundColorFxType::NOISE) {
            ch.noiseRngState ^= ch.noiseRngState << 13;
            ch.noiseRngState ^= ch.noiseRngState >> 17;
            ch.noiseRngState ^= ch.noiseRngState << 5;
            double white = (static_cast<double>(ch.noiseRngState) / 2147483648.0) - 1.0;
            double nFreq = 200.0 * std::pow(35.0, std::abs(cKnob));
            ch.colorSvfL.setParams(mSampleRate, nFreq, 3.2);
            double nLp, nBp, nHp;
            ch.colorSvfL.processSample(white * std::abs(cKnob) * 0.35, nLp, nBp, nHp);
            eqOutL += nBp; eqOutR += nBp;
        } else {
            double freq = (cKnob > 0.0f) ? (45.0 * std::pow(220.0, cKnob)) : (18000.0 * std::pow(0.008, -cKnob));
            ch.colorSvfL.setParams(mSampleRate, freq, 2.2);
            ch.colorSvfR.setParams(mSampleRate, freq, 2.2);
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
    ch.vuMeterPeak.store(0.92f * ch.vuMeterPeak.load() + 0.08f * peak);
}

void FlagshipDspEngine::processMasterBeatFx64(double& masterL, double& masterR) {
    auto fx = static_cast<BeatFxType>(beatFxType.load());
    double wet = static_cast<double>(beatFxWetDry.load());
    if (fx == BeatFxType::NONE || wet <= 0.005) return;

    if (fx == BeatFxType::MOBIUS) {
        mMobiusPhase += 0.38 / mSampleRate;
        if (mMobiusPhase >= 1.0) mMobiusPhase -= 1.0;
        double shepardFreq = 120.0 * std::pow(30.0, mMobiusPhase);
        mBeatFxSvfL.setParams(mSampleRate, shepardFreq, 4.2);
        mBeatFxSvfR.setParams(mSampleRate, shepardFreq, 4.2);
        double lpL, bpL, hpL, lpR, bpR, hpR;
        mBeatFxSvfL.processSample(masterL, lpL, bpL, hpL);
        mBeatFxSvfR.processSample(masterR, lpR, bpR, hpR);
        double env = std::sin(M_PI * mMobiusPhase);
        masterL = masterL * (1.0 - wet * 0.45) + bpL * env * wet * 2.4;
        masterR = masterR * (1.0 - wet * 0.45) + bpR * env * wet * 2.4;
    } else if (fx == BeatFxType::FLANGER || fx == BeatFxType::PHASER || fx == BeatFxType::TRIPLET_FILTER) {
        mFlangerPhase += (fx == BeatFxType::TRIPLET_FILTER ? 3.0 : 0.65) / mSampleRate;
        if (mFlangerPhase >= 1.0) mFlangerPhase -= 1.0;
        double sweepHz = 280.0 + (std::sin(2.0 * M_PI * mFlangerPhase) * 0.5 + 0.5) * 3400.0;
        mBeatFxSvfL.setParams(mSampleRate, sweepHz, 3.0);
        mBeatFxSvfR.setParams(mSampleRate, sweepHz, 3.0);
        double lpL, bpL, hpL, lpR, bpR, hpR;
        mBeatFxSvfL.processSample(masterL, lpL, bpL, hpL);
        mBeatFxSvfR.processSample(masterR, lpR, bpR, hpR);
        masterL = masterL * (1.0 - wet) + (masterL - bpL) * wet;
        masterR = masterR * (1.0 - wet) + (masterR - bpR) * wet;
    } else if (fx == BeatFxType::TRANS_GATE || fx == BeatFxType::TRIPLET_ROLL) {
        mFlangerPhase += (fx == BeatFxType::TRIPLET_ROLL ? 6.0 : 4.0) / mSampleRate;
        if (mFlangerPhase >= 1.0) mFlangerPhase -= 1.0;
        double gate = (mFlangerPhase < 0.55) ? 1.0 : (1.0 - wet);
        masterL *= gate;
        masterR *= gate;
    } else {
        double delaySamples = std::clamp(static_cast<double>(beatFxTimeMs.load()) * 0.001 * mSampleRate, 128.0, static_cast<double>(mDelayLineL.size() - 4));
        size_t readPos = (mDelayWritePos + mDelayLineL.size() - static_cast<size_t>(delaySamples)) % mDelayLineL.size();
        double dL = mDelayLineL[readPos];
        double dR = mDelayLineR[readPos];
        double fb = (fx == BeatFxType::HELIX || fx == BeatFxType::SPIRAL || fx == BeatFxType::SHIMMER) ? 0.76 : 0.52;
        mDelayLineL[mDelayWritePos] = masterL + (fx == BeatFxType::PING_PONG ? dR : dL) * fb;
        mDelayLineR[mDelayWritePos] = masterR + (fx == BeatFxType::PING_PONG ? dL : dR) * fb;
        masterL += dL * wet;
        masterR += dR * wet;
    }
}

oboe::DataCallbackResult FlagshipDspEngine::onAudioReady(oboe::AudioStream*, void* audioData, int32_t numFrames) {
    float* outputBuffer = static_cast<float*>(audioData);
    updateArgonAiMorphAutomation(numFrames);

    double xf = static_cast<double>(crossfader.load());
    double normXf = (xf + 1.0) * 0.5;
    double gainA = std::cos(normXf * M_PI * 0.5);
    double gainB = std::sin(normXf * M_PI * 0.5);

    mMasterIsoLowL.setParams(mSampleRate, 300.0, 0.707);
    mMasterIsoLowR.setParams(mSampleRate, 300.0, 0.707);
    mMasterIsoHighL.setParams(mSampleRate, 3500.0, 0.707);
    mMasterIsoHighR.setParams(mSampleRate, 3500.0, 0.707);
    mNeveTransformer.updateCoefficients(static_cast<double>(neveTransformerDrive.load()), mSampleRate);

    double boostDb = static_cast<double>(euphoBoostLevelDb.load());
    double isoG[3];
    float isoParams[3] = {masterIsoLow.load(), masterIsoMid.load(), masterIsoHigh.load()};
    for (int b = 0; b < 3; ++b) {
        double db = (isoParams[b] >= 0.0f) ? isoParams[b] * 9.0 : isoParams[b] * 48.0;
        if (euphoBoostSendActive[b].load()) db += boostDb;
        isoG[b] = (isoParams[b] <= -0.98f && !euphoBoostSendActive[b].load()) ? 0.0 : std::pow(10.0, db / 20.0);
    }

    const bool sidechainOn = spectralSidechainEnabled.load(std::memory_order_relaxed);

    for (int32_t frame = 0; frame < numFrames; ++frame) {
        if ((mControlSubBlockCounter++ & 31) == 0) {
            for (int c = 0; c < 2; ++c) updateControlRateCoefficients(*channels[c]);
        }

        // Compute dynamic cross-deck spectral sidechain ducking amounts
        double subDuckA = 0.0, voxDuckA = 0.0;
        double subDuckB = 0.0, voxDuckB = 0.0;
        if (sidechainOn && channels[0]->playing.load(std::memory_order_relaxed) && channels[1]->playing.load(std::memory_order_relaxed)) {
            double subEnvA = channels[0]->liveSubBassEnv.load(std::memory_order_relaxed) * gainA;
            double subEnvB = channels[1]->liveSubBassEnv.load(std::memory_order_relaxed) * gainB;
            double voxEnvA = channels[0]->liveVocalEnv.load(std::memory_order_relaxed) * gainA;
            double voxEnvB = channels[1]->liveVocalEnv.load(std::memory_order_relaxed) * gainB;

            if (gainB >= gainA) {
                subDuckA = std::clamp(subEnvB * 2.2, 0.0, 0.80);
                voxDuckA = std::clamp(voxEnvB * 2.0, 0.0, 0.72);
            } else {
                subDuckB = std::clamp(subEnvA * 2.2, 0.0, 0.80);
                voxDuckB = std::clamp(voxEnvA * 2.0, 0.0, 0.72);
            }
        }

        double chAL = 0.0, chAR = 0.0, chBL = 0.0, chBR = 0.0;
        processChannelStrip64(*channels[0], subDuckA, voxDuckA, chAL, chAR);
        processChannelStrip64(*channels[1], subDuckB, voxDuckB, chBL, chBR);

        double masterSumL = chAL * gainA + chBL * gainB;
        double masterSumR = chAR * gainA + chBR * gainB;

        for (int p = 0; p < NUM_SAMPLER_PADS; ++p) {
            if (samplerPads[p].active.load()) {
                int pos = samplerPads[p].readPos.load();
                if (pos < static_cast<int>(samplerPads[p].sampleL.size())) {
                    float g = samplerPads[p].gain.load();
                    masterSumL += samplerPads[p].sampleL[pos] * g;
                    masterSumR += samplerPads[p].sampleR[pos] * g;
                    samplerPads[p].readPos.store(pos + 1);
                } else {
                    samplerPads[p].active.store(false);
                }
            }
        }

        double mLpL, mBpL, mHpL, mLpR, mBpR, mHpR;
        mMasterIsoLowL.processSample(masterSumL, mLpL, mBpL, mHpL);
        mMasterIsoLowR.processSample(masterSumR, mLpR, mBpR, mHpR);

        double mHiLpL, mHiBpL, mHiHpL, mHiLpR, mHiBpR, mHiHpR;
        mMasterIsoHighL.processSample(mHpL, mHiLpL, mHiBpL, mHiHpL);
        mMasterIsoHighR.processSample(mHpR, mHiLpR, mHiBpR, mHiHpR);

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

        processMasterBeatFx64(masterSumL, masterSumR);
        mNeveTransformer.processMasterSample(masterSumL, masterSumR, static_cast<double>(neveTransformerDrive.load()));

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
    JNIEnv* env, jobject, jint deckIndex, jfloatArray leftPcm, jfloatArray rightPcm, jint sampleRate, jstring) {
    if (!gEngine || !leftPcm || !rightPcm) return;
    jsize len = env->GetArrayLength(leftPcm);
    jfloat* lPtr = env->GetFloatArrayElements(leftPcm, nullptr);
    jfloat* rPtr = env->GetFloatArrayElements(rightPcm, nullptr);

    gEngine->loadTrackToDeck(deckIndex, lPtr, rPtr, len, sampleRate);

    env->ReleaseFloatArrayElements(leftPcm, lPtr, JNI_ABORT);
    env->ReleaseFloatArrayElements(rightPcm, rPtr, JNI_ABORT);
}

JNIEXPORT void JNICALL
Java_com_apexdj_butter_audio_OnlineAndLocalRepository_nativeSetDeckPlayState(JNIEnv*, jobject, jint deck, jboolean playing) {
    if (gEngine && deck >= 0 && deck < apexdj::NUM_CHANNELS) {
        gEngine->channels[deck]->playing.store(playing);
    }
}

JNIEXPORT jboolean JNICALL
Java_com_apexdj_butter_audio_OnlineAndLocalRepository_nativeGetDeckPlayState(JNIEnv*, jobject, jint deck) {
    if (!gEngine || deck < 0 || deck >= apexdj::NUM_CHANNELS) return JNI_FALSE;
    return gEngine->channels[deck]->playing.load() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_apexdj_butter_audio_OnlineAndLocalRepository_nativeTriggerButterMorph(JNIEnv*, jobject, jint fromDeck, jint toDeck, jdouble seconds, jint styleId) {
    if (gEngine) gEngine->triggerButterMorph(fromDeck, toDeck, seconds, styleId);
}

JNIEXPORT void JNICALL
Java_com_apexdj_butter_audio_OnlineAndLocalRepository_nativeTriggerHotCue(JNIEnv*, jobject, jint deck, jint cueIndex) {
    if (gEngine) gEngine->triggerHotCue(deck, cueIndex);
}

JNIEXPORT void JNICALL
Java_com_apexdj_butter_audio_OnlineAndLocalRepository_nativeClearHotCue(JNIEnv*, jobject, jint deck, jint cueIndex) {
    if (gEngine) gEngine->clearHotCue(deck, cueIndex);
}

JNIEXPORT void JNICALL
Java_com_apexdj_butter_audio_OnlineAndLocalRepository_nativeToggleBeatLoop(JNIEnv*, jobject, jint deck, jdouble beats) {
    if (gEngine) gEngine->toggleBeatLoop(deck, beats);
}

JNIEXPORT void JNICALL
Java_com_apexdj_butter_audio_OnlineAndLocalRepository_nativeBeatJump(JNIEnv*, jobject, jint deck, jdouble beats) {
    if (gEngine) gEngine->beatJump(deck, beats);
}

JNIEXPORT void JNICALL
Java_com_apexdj_butter_audio_OnlineAndLocalRepository_nativeSeekNormalized(JNIEnv*, jobject, jint deck, jfloat normPos) {
    if (gEngine) gEngine->seekNormalized(deck, normPos);
}

JNIEXPORT void JNICALL
Java_com_apexdj_butter_audio_OnlineAndLocalRepository_nativeTriggerSamplerPad(JNIEnv*, jobject, jint padIndex) {
    if (gEngine) gEngine->triggerSamplerPad(padIndex);
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
        case 7: ch.xoneVcf.cutoffHz = 50.0 * std::pow(440.0, std::clamp(value, 0.0f, 1.0f)); break;
        case 8: ch.xoneVcf.crunchDrive = std::clamp(value, 0.0f, 1.0f); break;
        case 9: ch.stemGain[0].store(value); break;
        case 10: ch.stemGain[1].store(value); break;
        case 11: ch.stemGain[2].store(value); break;
        case 12: ch.stemGain[3].store(value); break;
        case 13: ch.tempoRatio.store(std::clamp(value, 0.5f, 2.0f)); break;
        case 14: ch.slipModeEnabled.store(value > 0.5f); break;
        case 15: ch.reverseCensorActive.store(value > 0.5f); break;
        case 16: ch.vinylBrakeActive.store(value > 0.5f); break;
        case 17: ch.colorFxType.store(static_cast<int>(value)); break;
        case 18: ch.jogTouching.store(std::abs(value) > 0.005f); ch.jogScratchVelocity.store(value); break;
        case 19: ch.pitchSemitones.store(std::clamp(value, -12.0f, 12.0f)); break;
        case 20: ch.xoneVcf.resonance = 0.5 + std::clamp(value, 0.0f, 1.0f) * 8.5; break;
        case 21: ch.xoneVcf.hpfEnabled = (value > 0.5f); break;
        case 22: ch.xoneVcf.bpfEnabled = (value > 0.5f); break;
        case 23: ch.xoneVcf.lpfEnabled = (value > 0.5f); break;
    }
}

JNIEXPORT void JNICALL
Java_com_apexdj_butter_audio_OnlineAndLocalRepository_nativeSetMasterParam(JNIEnv*, jobject, jint paramId, jfloat value) {
    if (!gEngine) return;
    switch (paramId) {
        case 0: gEngine->crossfader.store(std::clamp(value, -1.0f, 1.0f)); break;
        case 1: gEngine->masterIsoLow.store(value); break;
        case 2: gEngine->masterIsoMid.store(value); break;
        case 3: gEngine->masterIsoHigh.store(value); break;
        case 4: gEngine->neveTransformerDrive.store(value); break;
        case 5: gEngine->euphoBoostSendActive[0].store(value > 0.5f); break;
        case 6: gEngine->euphoBoostSendActive[1].store(value > 0.5f); break;
        case 7: gEngine->euphoBoostSendActive[2].store(value > 0.5f); break;
        case 8: gEngine->beatFxType.store(static_cast<int>(value)); break;
        case 9: gEngine->beatFxWetDry.store(std::clamp(value, 0.0f, 1.0f)); break;
        case 10: gEngine->beatFxTimeMs.store(std::clamp(value, 20.0f, 2000.0f)); break;
        case 11: gEngine->spectralSidechainEnabled.store(value > 0.5f); break;
    }
}

JNIEXPORT jfloat JNICALL
Java_com_apexdj_butter_audio_OnlineAndLocalRepository_nativeGetChannelVu(JNIEnv*, jobject, jint deck) {
    if (!gEngine || deck < 0 || deck >= apexdj::NUM_CHANNELS) return 0.0f;
    return gEngine->channels[deck]->vuMeterPeak.load(std::memory_order_relaxed);
}

JNIEXPORT jfloat JNICALL
Java_com_apexdj_butter_audio_OnlineAndLocalRepository_nativeGetDetectedBpm(JNIEnv*, jobject, jint deck) {
    if (!gEngine || deck < 0 || deck >= apexdj::NUM_CHANNELS) return 128.0f;
    return gEngine->channels[deck]->detectedBpm.load(std::memory_order_relaxed);
}

JNIEXPORT jint JNICALL
Java_com_apexdj_butter_audio_OnlineAndLocalRepository_nativeGetCamelotKey(JNIEnv*, jobject, jint deck) {
    if (!gEngine || deck < 0 || deck >= apexdj::NUM_CHANNELS) return 8;
    return gEngine->channels[deck]->camelotKeyIndex.load(std::memory_order_relaxed);
}

JNIEXPORT jfloat JNICALL
Java_com_apexdj_butter_audio_OnlineAndLocalRepository_nativeGetPlayheadNormalized(JNIEnv*, jobject, jint deck) {
    if (!gEngine || deck < 0 || deck >= apexdj::NUM_CHANNELS) return 0.0f;
    auto& ch = *gEngine->channels[deck];
    double total = std::max(1.0, static_cast<double>(ch.totalTrackSamples.load(std::memory_order_relaxed)));
    return static_cast<float>(ch.playheadSample.load(std::memory_order_relaxed) / total);
}

JNIEXPORT jfloat JNICALL
Java_com_apexdj_butter_audio_OnlineAndLocalRepository_nativeGetMorphProgress(JNIEnv*, jobject) {
    if (!gEngine || !gEngine->morphActive.load(std::memory_order_relaxed)) return -1.0f;
    return gEngine->morphProgressUI.load(std::memory_order_relaxed);
}

JNIEXPORT jfloatArray JNICALL
Java_com_apexdj_butter_audio_OnlineAndLocalRepository_nativeGetRgbWaveform(JNIEnv* env, jobject, jint deck) {
    jfloatArray result = env->NewFloatArray(apexdj::WAVEFORM_BINS * 3);
    if (!gEngine || deck < 0 || deck >= apexdj::NUM_CHANNELS) return result;
    auto& ch = *gEngine->channels[deck];
    env->SetFloatArrayRegion(result, 0, apexdj::WAVEFORM_BINS * 3, ch.rgbWaveformFlat.data());
    return result;
}

} // extern "C"
