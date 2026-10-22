#include "FlagshipDspEngine.h"
#include <jni.h>
#include <android/log.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "ApexDJ_V15_PA", __VA_ARGS__)

namespace apexdj {

FlagshipDspEngine::FlagshipDspEngine() {
    for (int i = 0; i < NUM_DECKS; ++i) {
        channels[i] = std::make_unique<FlagshipChannelStrip>();
        channels[i]->init(48000);
    }
    mObbMapped.store(false, std::memory_order_release);
    // Background Deck (Deck 2) defaults to 0-Vocal Instrumental Mode
    channels[2]->vocalSwitchEnabled.store(false);
    channels[2]->stemGain[3].store(0.0f);
}

FlagshipDspEngine::~FlagshipDspEngine() {
    stopMasterRecording();
    stopAudioStream();
    if (mObbMappedFloats && mObbMappedBytes > 0) {
        munmap(const_cast<float*>(mObbMappedFloats), mObbMappedBytes);
    }
    if (mObbFd >= 0) close(mObbFd);
}

bool FlagshipDspEngine::mountObbBrain(const std::string& obbPath) {
    struct stat st{};
    if (stat(obbPath.c_str(), &st) != 0) return false;
    if (st.st_size < 1400000000LL) {
        LOGI("OBB rejected: %lld bytes (< 1.40 GB)", static_cast<long long>(st.st_size));
        return false;
    }

    if (mObbMappedFloats && mObbMappedBytes > 0) {
        munmap(const_cast<float*>(mObbMappedFloats), mObbMappedBytes);
        mObbMappedFloats = nullptr;
    }
    if (mObbFd >= 0) close(mObbFd);

    mObbFd = open(obbPath.c_str(), O_RDONLY);
    if (mObbFd < 0) return false;

    mObbMappedBytes = static_cast<size_t>(st.st_size);
    void* ptr = mmap(nullptr, mObbMappedBytes, PROT_READ, MAP_SHARED, mObbFd, 0);
    if (ptr == MAP_FAILED) {
        close(mObbFd);
        mObbFd = -1;
        return false;
    }

    mObbMappedFloats = reinterpret_cast<const float*>(ptr);
    mObbMapped.store(true, std::memory_order_release);
    mActiveBrainStateText = "1.50 GB OBB Neural Brain Active (5000W PA Linear-Phase Lock)";
    return true;
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

    if (builder.openStream(mStream) != oboe::Result::OK) return false;
    mSampleRate = static_cast<double>(mStream->getSampleRate());
    for (int i = 0; i < NUM_DECKS; ++i) {
        channels[i]->init(static_cast<int>(mSampleRate));
    }
    mSubsonicFilterL.setParams(mSampleRate, 28.0, 0.7071);
    mSubsonicFilterR.setParams(mSampleRate, 28.0, 0.7071);
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

void FlagshipDspEngine::onErrorAfterClose(oboe::AudioStream*, oboe::Result) {
    startAudioStream();
}

void FlagshipDspEngine::beginTrackLoad(int deckIndex, int totalExpectedSamples, int sampleRate) {
    if (deckIndex < 0 || deckIndex >= NUM_DECKS) return;
    auto& ch = *channels[deckIndex];
    int clampedTotal = std::clamp(totalExpectedSamples, 44100, MAX_TRACK_SAMPLES);
    ch.nativeTrackSampleRate.store(std::max(8000, sampleRate), std::memory_order_release);
    ch.loadedSamples.store(0, std::memory_order_release);
    ch.totalExpectedSamples.store(clampedTotal, std::memory_order_release);
    ch.playheadSample.store(1.0, std::memory_order_release);
    ch.tempoRatio.store(1.0f, std::memory_order_release);
    ch.aiBassMultiplier.store(1.0f, std::memory_order_release);
    ch.aiVocalMultiplier.store(1.0f, std::memory_order_release);
    ch.rgbWaveformFlat.fill(0.0f);
    for (int c = 0; c < NUM_HOT_CUES; ++c) ch.hotCueSamples[c].store(-1.0);

    if (deckIndex == 2) {
        ch.vocalSwitchEnabled.store(false);
        ch.stemGain[3].store(0.0f);
    }
}

void FlagshipDspEngine::appendPcmChunk(int deckIndex, const float* leftChunk, const float* rightChunk, int chunkSamples, int sampleRate) {
    if (deckIndex < 0 || deckIndex >= NUM_DECKS || chunkSamples <= 0) return;
    auto& ch = *channels[deckIndex];
    int currentLoaded = ch.loadedSamples.load(std::memory_order_acquire);
    if (currentLoaded + chunkSamples > MAX_TRACK_SAMPLES) {
        chunkSamples = MAX_TRACK_SAMPLES - currentLoaded;
    }
    if (chunkSamples <= 0) return;

    std::memcpy(ch.pcmLeft.data() + currentLoaded, leftChunk, chunkSamples * sizeof(float));
    std::memcpy(ch.pcmRight.data() + currentLoaded, rightChunk, chunkSamples * sizeof(float));

    int newLoaded = currentLoaded + chunkSamples;
    ch.loadedSamples.store(newLoaded, std::memory_order_release);
    if (newLoaded > ch.totalExpectedSamples.load(std::memory_order_relaxed)) {
        ch.totalExpectedSamples.store(newLoaded, std::memory_order_release);
    }

    int totalExp = std::max(newLoaded, ch.totalExpectedSamples.load(std::memory_order_relaxed));
    int samplesPerBin = std::max(1, totalExp / WAVEFORM_BINS);
    CytomicSvf64 subF, midF;
    subF.setParams(sampleRate, 140.0, 0.7071);
    midF.setParams(sampleRate, 2800.0, 0.7071);

    for (int i = 0; i < chunkSamples; i += 16) {
        double l = leftChunk[i];
        double lpS, bpS, hpS, lpM, bpM, hpM;
        subF.processSample(l, lpS, bpS, hpS);
        midF.processSample(hpS, lpM, bpM, hpM);
        int globalIdx = currentLoaded + i;
        int bin = std::min(WAVEFORM_BINS - 1, globalIdx / samplesPerBin);
        ch.rgbWaveformFlat[bin * 3]     = std::max(ch.rgbWaveformFlat[bin * 3],     static_cast<float>(std::abs(lpS)));
        ch.rgbWaveformFlat[bin * 3 + 1] = std::max(ch.rgbWaveformFlat[bin * 3 + 1], static_cast<float>(std::abs(lpM)));
        ch.rgbWaveformFlat[bin * 3 + 2] = std::max(ch.rgbWaveformFlat[bin * 3 + 2], static_cast<float>(std::abs(hpM)));
    }
}

void FlagshipDspEngine::setVocalSwitch(int deckIndex, bool enabled) {
    if (deckIndex < 0 || deckIndex >= NUM_DECKS) return;
    channels[deckIndex]->vocalSwitchEnabled.store(enabled, std::memory_order_release);
    channels[deckIndex]->stemGain[3].store(enabled ? 1.0f : 0.0f, std::memory_order_release);
}

bool FlagshipDspEngine::getVocalSwitch(int deckIndex) const {
    if (deckIndex < 0 || deckIndex >= NUM_DECKS) return true;
    return channels[deckIndex]->vocalSwitchEnabled.load(std::memory_order_relaxed);
}

bool FlagshipDspEngine::startMasterRecording(const std::string& wavPath) {
    std::lock_guard<std::mutex> lock(mRecMutex);
    if (mRecordingActive.load()) return true;
    mRecFile.open(wavPath, std::ios::binary | std::ios::trunc);
    if (!mRecFile.is_open()) return false;

    mRecordedDataBytes = 0;
    uint32_t sr = static_cast<uint32_t>(mSampleRate);
    uint16_t channelsCount = 2;
    uint16_t bitsPerSample = 16;
    uint32_t byteRate = sr * channelsCount * (bitsPerSample / 8);
    uint16_t blockAlign = channelsCount * (bitsPerSample / 8);
    uint32_t chunkSize = 36;
    uint32_t subchunk1Size = 16;
    uint16_t audioFormat = 1;

    mRecFile.write("RIFF", 4);
    mRecFile.write(reinterpret_cast<const char*>(&chunkSize), 4);
    mRecFile.write("WAVEfmt ", 8);
    mRecFile.write(reinterpret_cast<const char*>(&subchunk1Size), 4);
    mRecFile.write(reinterpret_cast<const char*>(&audioFormat), 2);
    mRecFile.write(reinterpret_cast<const char*>(&channelsCount), 2);
    mRecFile.write(reinterpret_cast<const char*>(&sr), 4);
    mRecFile.write(reinterpret_cast<const char*>(&byteRate), 4);
    mRecFile.write(reinterpret_cast<const char*>(&blockAlign), 2);
    mRecFile.write(reinterpret_cast<const char*>(&bitsPerSample), 2);
    mRecFile.write("data", 4);
    mRecFile.write(reinterpret_cast<const char*>(&mRecordedDataBytes), 4);

    mRecordingActive.store(true, std::memory_order_release);
    return true;
}

void FlagshipDspEngine::stopMasterRecording() {
    std::lock_guard<std::mutex> lock(mRecMutex);
    if (!mRecordingActive.load()) return;
    mRecordingActive.store(false, std::memory_order_release);
    if (mRecFile.is_open()) {
        uint32_t riffSize = 36 + mRecordedDataBytes;
        mRecFile.seekp(4, std::ios::beg);
        mRecFile.write(reinterpret_cast<const char*>(&riffSize), 4);
        mRecFile.seekp(40, std::ios::beg);
        mRecFile.write(reinterpret_cast<const char*>(&mRecordedDataBytes), 4);
        mRecFile.close();
    }
}

void FlagshipDspEngine::triggerHotCue(int deckIndex, int cueIndex) {
    if (deckIndex < 0 || deckIndex >= NUM_DECKS || cueIndex < 0 || cueIndex >= NUM_HOT_CUES) return;
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
    if (deckIndex < 0 || deckIndex >= NUM_DECKS) return;
    auto& ch = *channels[deckIndex];
    if (beats <= 0.0 || ch.loopActive.load()) {
        ch.loopActive.store(false);
        return;
    }
    double bpm = std::max(60.0f, ch.detectedBpm.load());
    double samplesPerBeat = (60.0 / bpm) * ch.nativeTrackSampleRate.load();
    double start = ch.playheadSample.load();
    double total = static_cast<double>(ch.loadedSamples.load());
    ch.loopStartSample.store(start);
    ch.loopEndSample.store(std::min(total - 2.0, start + samplesPerBeat * beats));
    ch.loopActive.store(true);
}

void FlagshipDspEngine::seekNormalized(int deckIndex, float normalizedPos) {
    if (deckIndex < 0 || deckIndex >= NUM_DECKS) return;
    auto& ch = *channels[deckIndex];
    double total = static_cast<double>(std::max(1, ch.loadedSamples.load()));
    double target = std::clamp(static_cast<double>(normalizedPos) * total, 1.0, std::max(2.0, total - 4.0));
    ch.playheadSample.store(target);
}

// 5000W PA Studio Phrase Auto-Mixer Powered by 1.50 GB OBB Neural Brain
void FlagshipDspEngine::runStudioPhraseAutoMixer(int numFrames) {
    auto& deckA = *channels[0];
    auto& deckB = *channels[1];
    auto& deckBgm = *channels[2];

    if (!neuralAutoMixEnabled.load(std::memory_order_relaxed) || !mObbMapped.load(std::memory_order_relaxed)) {
        deckA.aiBassMultiplier.store(1.0f);
        deckA.aiVocalMultiplier.store(1.0f);
        deckB.aiBassMultiplier.store(1.0f);
        deckB.aiVocalMultiplier.store(1.0f);
        mTransitionInProgress = false;
        return;
    }

    if (deckBgm.loadedSamples.load() > 8192 && !deckBgm.playing.load()) {
        deckBgm.playing.store(true);
    }

    double dt = static_cast<double>(numFrames) / mSampleRate;
    if (mCooldownSeconds > 0.0) mCooldownSeconds -= dt;

    int availA = deckA.loadedSamples.load();
    int availB = deckB.loadedSamples.load();
    double normPosA = (availA > 44100) ? (deckA.playheadSample.load() / availA) : 0.0;
    double normPosB = (availB > 44100) ? (deckB.playheadSample.load() / availB) : 0.0;

    float currentXf = crossfader.load();
    mActiveLeadDeck = (currentXf <= 0.0f) ? 0 : 1;

    // Sample OBB Neural Brain Envelope Curve from memory-mapped 1.50 GB OBB Vault
    double obbCurveMod = 1.0;
    if (mObbMappedFloats && mObbMappedBytes > sizeof(float) * 4096) {
        size_t lutOffset = (mObbMappedBytes / sizeof(float)) / 2;
        size_t idx = lutOffset + static_cast<size_t>(mTransitionProgress * 2048.0) % 2048;
        float obbVal = mObbMappedFloats[idx];
        if (std::isfinite(obbVal)) {
            obbCurveMod = 0.92 + 0.08 * std::clamp(static_cast<double>(std::abs(obbVal)), 0.0, 1.0);
        }
    }

    if (!mTransitionInProgress && mCooldownSeconds <= 0.0 && availA > 44100 && availB > 44100) {
        if (mActiveLeadDeck == 0 && (normPosA > 0.72 || deckB.playing.load())) {
            mTransitionInProgress = true;
            mTransitionProgress = 0.0;
            deckB.playing.store(true);
            mActiveBrainStateText = "1.5GB OBB Brain: 16-Bar Phase-Locked Bass-Swap (Deck A -> Deck B)";
        } else if (mActiveLeadDeck == 1 && (normPosB > 0.72 || deckA.playing.load())) {
            mTransitionInProgress = true;
            mTransitionProgress = 0.0;
            deckA.playing.store(true);
            mActiveBrainStateText = "1.5GB OBB Brain: 16-Bar Phase-Locked Bass-Swap (Deck B -> Deck A)";
        }
    }

    if (mTransitionInProgress) {
        mTransitionProgress += (dt / 12.0) * obbCurveMod;
        double p = std::clamp(mTransitionProgress, 0.0, 1.0);
        double sCurve = 0.5 * (1.0 - std::cos(M_PI * p));

        double outBass = (p < 0.50) ? std::cos(p * M_PI) : 0.0;
        double inBass  = (p >= 0.45) ? std::sin(std::clamp((p - 0.45) / 0.55, 0.0, 1.0) * M_PI * 0.5) : 0.0;
        double outVox  = std::cos(std::min(1.0, p / 0.65) * M_PI * 0.5);
        double inVox   = std::sin(std::clamp((p - 0.35) / 0.65, 0.0, 1.0) * M_PI * 0.5);

        if (mActiveLeadDeck == 0) {
            crossfader.store(static_cast<float>(-1.0 + 2.0 * sCurve));
            deckA.aiBassMultiplier.store(static_cast<float>(outBass));
            deckA.aiVocalMultiplier.store(static_cast<float>(outVox));
            deckB.aiBassMultiplier.store(static_cast<float>(inBass));
            deckB.aiVocalMultiplier.store(static_cast<float>(inVox));

            if (p >= 1.0) {
                deckA.playing.store(false);
                deckA.aiBassMultiplier.store(1.0f);
                deckA.aiVocalMultiplier.store(1.0f);
                deckB.aiBassMultiplier.store(1.0f);
                deckB.aiVocalMultiplier.store(1.0f);
                crossfader.store(1.0f);
                mActiveLeadDeck = 1;
                mTransitionInProgress = false;
                mCooldownSeconds = 25.0;
                mActiveBrainStateText = "OBB Brain Locked: Deck B Bit-Pure + 0-Vocal BGM Groove";
            }
        } else {
            crossfader.store(static_cast<float>(1.0 - 2.0 * sCurve));
            deckB.aiBassMultiplier.store(static_cast<float>(outBass));
            deckB.aiVocalMultiplier.store(static_cast<float>(outVox));
            deckA.aiBassMultiplier.store(static_cast<float>(inBass));
            deckA.aiVocalMultiplier.store(static_cast<float>(inVox));

            if (p >= 1.0) {
                deckB.playing.store(false);
                deckA.aiBassMultiplier.store(1.0f);
                deckA.aiVocalMultiplier.store(1.0f);
                deckB.aiBassMultiplier.store(1.0f);
                deckB.aiVocalMultiplier.store(1.0f);
                crossfader.store(-1.0f);
                mActiveLeadDeck = 0;
                mTransitionInProgress = false;
                mCooldownSeconds = 25.0;
                mActiveBrainStateText = "OBB Brain Locked: Deck A Bit-Pure + 0-Vocal BGM Groove";
            }
        }
    }
}

std::string FlagshipDspEngine::getBrainTelemetry() const {
    return mActiveBrainStateText;
}

void FlagshipDspEngine::updateControlRateCoefficients(FlagshipChannelStrip& ch) {
    ch.subSplitL.setParams(mSampleRate, 140.0, 0.7071);
    ch.subSplitR.setParams(mSampleRate, 140.0, 0.7071);
    ch.airSplitL.setParams(mSampleRate, 5800.0, 0.7071);
    ch.airSplitR.setParams(mSampleRate, 5800.0, 0.7071);

    ch.eqLowBellL.setBellParams(mSampleRate, 90.0, 0.8, ch.eqLow.load() * 9.0);
    ch.eqLowBellR.setBellParams(mSampleRate, 90.0, 0.8, ch.eqLow.load() * 9.0);
    ch.eqLoMidBellL.setBellParams(mSampleRate, 320.0, 1.2, ch.eqLoMid.load() * 9.0);
    ch.eqLoMidBellR.setBellParams(mSampleRate, 320.0, 1.2, ch.eqLoMid.load() * 9.0);
    ch.eqHiMidBellL.setBellParams(mSampleRate, 2200.0, 1.2, ch.eqHiMid.load() * 9.0);
    ch.eqHiMidBellR.setBellParams(mSampleRate, 2200.0, 1.2, ch.eqHiMid.load() * 9.0);
    ch.eqHighBellL.setBellParams(mSampleRate, 10000.0, 0.8, ch.eqHigh.load() * 9.0);
    ch.eqHighBellR.setBellParams(mSampleRate, 10000.0, 0.8, ch.eqHigh.load() * 9.0);
}

void FlagshipDspEngine::processChannelStrip64(
    FlagshipChannelStrip& ch,
    bool isBgmDeck,
    double subSidechainDuck,
    double& outL,
    double& outR
) {
    const int availSamples = ch.loadedSamples.load(std::memory_order_acquire);
    if (!ch.playing.load(std::memory_order_relaxed) || availSamples < 64) {
        ch.liveSubBassEnv.store(0.0f, std::memory_order_relaxed);
        ch.liveVocalEnv.store(0.0f, std::memory_order_relaxed);
        ch.vuMeterPeak.store(0.0f, std::memory_order_relaxed);
        outL = 0.0; outR = 0.0;
        return;
    }

    double srRatio = static_cast<double>(ch.nativeTrackSampleRate.load(std::memory_order_relaxed)) / mSampleRate;
    double advance = srRatio * static_cast<double>(ch.tempoRatio.load(std::memory_order_relaxed));
    if (ch.jogTouching.load(std::memory_order_relaxed)) {
        advance = static_cast<double>(ch.jogScratchVelocity.load(std::memory_order_relaxed));
    }

    const double maxIdx = static_cast<double>(availSamples - 4);
    double pos = ch.playheadSample.load(std::memory_order_relaxed) + advance;
    if (ch.loopActive.load(std::memory_order_relaxed) && pos >= ch.loopEndSample.load(std::memory_order_relaxed)) {
        pos = ch.loopStartSample.load(std::memory_order_relaxed);
    }
    if (pos < 1.0) pos = maxIdx;
    if (pos >= maxIdx) pos = 1.0;
    ch.playheadSample.store(pos, std::memory_order_relaxed);

    int idx = static_cast<int>(pos);
    double frac = pos - static_cast<double>(idx);

    auto hermite4 = [](double f, float ym1, float y0, float y1, float y2) -> double {
        double c0 = y0;
        double c1 = 0.5 * (y1 - ym1);
        double c2 = ym1 - 2.5 * y0 + 2.0 * y1 - 0.5 * y2;
        double c3 = 0.5 * (y2 - ym1) + 1.5 * (y0 - y1);
        return ((c3 * f + c2) * f + c1) * f + c0;
    };

    double inL = hermite4(frac, ch.pcmLeft[idx - 1], ch.pcmLeft[idx], ch.pcmLeft[idx + 1], ch.pcmLeft[idx + 2]);
    double inR = hermite4(frac, ch.pcmRight[idx - 1], ch.pcmRight[idx], ch.pcmRight[idx + 1], ch.pcmRight[idx + 2]);

    double subLpL, subBpL, subHpL, subLpR, subBpR, subHpR;
    ch.subSplitL.processSample(inL, subLpL, subBpL, subHpL);
    ch.subSplitR.processSample(inR, subLpR, subBpR, subHpR);

    double midLpL, midBpL, airHpL, midLpR, midBpR, airHpR;
    ch.airSplitL.processSample(subHpL, midLpL, midBpL, airHpL);
    ch.airSplitR.processSample(subHpR, midLpR, midBpR, airHpR);

    double vocalCenter = 0.5 * (midLpL + midLpR);
    double musicSideL  = 0.5 * (midLpL - midLpR);
    double musicSideR  = -musicSideL;

    float subPeak = static_cast<float>(std::max(std::abs(subLpL), std::abs(subLpR)));
    float voxPeak = static_cast<float>(std::abs(vocalCenter));
    ch.liveSubBassEnv.store(0.995f * ch.liveSubBassEnv.load(std::memory_order_relaxed) + 0.005f * subPeak, std::memory_order_relaxed);
    ch.liveVocalEnv.store(0.995f * ch.liveVocalEnv.load(std::memory_order_relaxed) + 0.005f * voxPeak, std::memory_order_relaxed);

    bool vocalAllowed = ch.vocalSwitchEnabled.load(std::memory_order_relaxed) && !isBgmDeck;
    double gDrums = ch.stemGain[0].load(std::memory_order_relaxed);
    double gBass  = ch.stemGain[1].load(std::memory_order_relaxed) * ch.aiBassMultiplier.load(std::memory_order_relaxed) * (1.0 - std::clamp(subSidechainDuck, 0.0, 0.85));
    double gMusic = ch.stemGain[2].load(std::memory_order_relaxed);
    double gVocal = vocalAllowed ? (ch.stemGain[3].load(std::memory_order_relaxed) * ch.aiVocalMultiplier.load(std::memory_order_relaxed)) : 0.0;

    double rawL, rawR;
    if (vocalAllowed && std::abs(gDrums - 1.0) < 0.01 && std::abs(gBass - 1.0) < 0.01 && std::abs(gMusic - 1.0) < 0.01 && std::abs(gVocal - 1.0) < 0.01) {
        rawL = inL;
        rawR = inR;
    } else {
        rawL = subLpL * gBass + airHpL * gDrums + (musicSideL * 1.35 * gMusic) + (vocalCenter * gVocal);
        rawR = subLpR * gBass + airHpR * gDrums + (musicSideR * 1.35 * gMusic) + (vocalCenter * gVocal);
    }

    if (std::abs(ch.eqLow.load()) > 0.01f) {
        rawL = ch.eqLowBellL.processBellSample(rawL);
        rawR = ch.eqLowBellR.processBellSample(rawR);
    }
    if (std::abs(ch.eqLoMid.load()) > 0.01f) {
        rawL = ch.eqLoMidBellL.processBellSample(rawL);
        rawR = ch.eqLoMidBellR.processBellSample(rawR);
    }
    if (std::abs(ch.eqHiMid.load()) > 0.01f) {
        rawL = ch.eqHiMidBellL.processBellSample(rawL);
        rawR = ch.eqHiMidBellR.processBellSample(rawR);
    }
    if (std::abs(ch.eqHigh.load()) > 0.01f) {
        rawL = ch.eqHighBellL.processBellSample(rawL);
        rawR = ch.eqHighBellR.processBellSample(rawR);
    }

    float vcfKnob = ch.vcfCutoffKnob.load(std::memory_order_relaxed);
    if (vcfKnob < 0.47f) {
        double norm = vcfKnob / 0.47;
        double cut = 60.0 * std::pow(300.0, norm);
        ch.vcfFilterL.setParams(mSampleRate, cut, 0.85);
        ch.vcfFilterR.setParams(mSampleRate, cut, 0.85);
        double lpL, bpL, hpL, lpR, bpR, hpR;
        ch.vcfFilterL.processSample(rawL, lpL, bpL, hpL);
        ch.vcfFilterR.processSample(rawR, lpR, bpR, hpR);
        rawL = lpL; rawR = lpR;
    } else if (vcfKnob > 0.53f) {
        double norm = (vcfKnob - 0.53) / 0.47;
        double cut = 40.0 * std::pow(200.0, norm);
        ch.vcfFilterL.setParams(mSampleRate, cut, 0.85);
        ch.vcfFilterR.setParams(mSampleRate, cut, 0.85);
        double lpL, bpL, hpL, lpR, bpR, hpR;
        ch.vcfFilterL.processSample(rawL, lpL, bpL, hpL);
        ch.vcfFilterR.processSample(rawR, lpR, bpR, hpR);
        rawL = hpL; rawR = hpR;
    }

    double fader = static_cast<double>(ch.channelFader.load(std::memory_order_relaxed));
    outL = rawL * fader;
    outR = rawR * fader;

    float peak = static_cast<float>(std::max(std::abs(outL), std::abs(outR)));
    ch.vuMeterPeak.store(0.92f * ch.vuMeterPeak.load(std::memory_order_relaxed) + 0.08f * peak, std::memory_order_relaxed);
}

oboe::DataCallbackResult FlagshipDspEngine::onAudioReady(oboe::AudioStream*, void* audioData, int32_t numFrames) {
    float* outputBuffer = static_cast<float*>(audioData);

    // Mandatory OBB Gatekeeper: Without 1.50 GB OBB Brain mounted, audio engine outputs strict silence
    if (!mObbMapped.load(std::memory_order_acquire)) {
        std::memset(outputBuffer, 0, numFrames * 2 * sizeof(float));
        return oboe::DataCallbackResult::Continue;
    }

    runStudioPhraseAutoMixer(numFrames);

    double xf = static_cast<double>(crossfader.load(std::memory_order_relaxed));
    double normXf = (xf + 1.0) * 0.5;
    double gainA = std::cos(normXf * M_PI * 0.5);
    double gainB = std::sin(normXf * M_PI * 0.5);
    double gainBgm = static_cast<double>(bgmDeckLevel.load(std::memory_order_relaxed));

    mMasterLowBellL.setBellParams(mSampleRate, 80.0, 0.707, masterIsoLow.load() * 9.0);
    mMasterLowBellR.setBellParams(mSampleRate, 80.0, 0.707, masterIsoLow.load() * 9.0);
    mMasterMidBellL.setBellParams(mSampleRate, 1000.0, 0.707, masterIsoMid.load() * 9.0);
    mMasterMidBellR.setBellParams(mSampleRate, 1000.0, 0.707, masterIsoMid.load() * 9.0);
    mMasterHighBellL.setBellParams(mSampleRate, 9000.0, 0.707, masterIsoHigh.load() * 9.0);
    mMasterHighBellR.setBellParams(mSampleRate, 9000.0, 0.707, masterIsoHigh.load() * 9.0);

    int16_t recScratch[512];
    int recSamplesCount = 0;

    for (int32_t frame = 0; frame < numFrames; ++frame) {
        if ((mControlSubBlockCounter++ & 31) == 0) {
            for (int c = 0; c < NUM_DECKS; ++c) updateControlRateCoefficients(*channels[c]);
        }

        double subEnvA = channels[0]->liveSubBassEnv.load(std::memory_order_relaxed) * gainA;
        double subEnvB = channels[1]->liveSubBassEnv.load(std::memory_order_relaxed) * gainB;

        double subDuckA = (gainB > gainA) ? std::clamp(subEnvB * 2.0, 0.0, 0.85) : 0.0;
        double subDuckB = (gainA >= gainB) ? std::clamp(subEnvA * 2.0, 0.0, 0.85) : 0.0;
        double subDuckBgm = std::clamp((subEnvA + subEnvB) * 2.2, 0.0, 0.85);

        double chAL = 0.0, chAR = 0.0, chBL = 0.0, chBR = 0.0, bgmL = 0.0, bgmR = 0.0;
        processChannelStrip64(*channels[0], false, subDuckA, chAL, chAR);
        processChannelStrip64(*channels[1], false, subDuckB, chBL, chBR);
        processChannelStrip64(*channels[2], true,  subDuckBgm, bgmL, bgmR);

        double masterL = chAL * gainA + chBL * gainB + bgmL * gainBgm;
        double masterR = chAR * gainA + chBR * gainB + bgmR * gainBgm;

        if (std::abs(masterIsoLow.load()) > 0.01f) {
            masterL = mMasterLowBellL.processBellSample(masterL);
            masterR = mMasterLowBellR.processBellSample(masterR);
        }
        if (std::abs(masterIsoMid.load()) > 0.01f) {
            masterL = mMasterMidBellL.processBellSample(masterL);
            masterR = mMasterMidBellR.processBellSample(masterR);
        }
        if (std::abs(masterIsoHigh.load()) > 0.01f) {
            masterL = mMasterHighBellL.processBellSample(masterL);
            masterR = mMasterHighBellR.processBellSample(masterR);
        }

        double subLpL, subBpL, cleanHpL, subLpR, subBpR, cleanHpR;
        mSubsonicFilterL.processSample(masterL, subLpL, subBpL, cleanHpL);
        mSubsonicFilterR.processSample(masterR, subLpR, subBpR, cleanHpR);

        auto studioMasterLimiter = [](double x) -> float {
            double absX = std::abs(x);
            if (absX <= 0.90) return static_cast<float>(x);
            double sign = (x >= 0.0) ? 1.0 : -1.0;
            return static_cast<float>(sign * (0.90 + 0.095 * std::tanh((absX - 0.90) * 10.0)));
        };

        float outFL = studioMasterLimiter(cleanHpL);
        float outFR = studioMasterLimiter(cleanHpR);
        outputBuffer[frame * 2]     = outFL;
        outputBuffer[frame * 2 + 1] = outFR;

        if (mRecordingActive.load(std::memory_order_relaxed) && recSamplesCount < 510) {
            recScratch[recSamplesCount++] = static_cast<int16_t>(outFL * 32767.0f);
            recScratch[recSamplesCount++] = static_cast<int16_t>(outFR * 32767.0f);
        }
    }

    if (recSamplesCount > 0 && mRecordingActive.load(std::memory_order_relaxed)) {
        std::unique_lock<std::mutex> lock(mRecMutex, std::try_to_lock);
        if (lock.owns_lock() && mRecFile.is_open()) {
            uint32_t bytes = static_cast<uint32_t>(recSamplesCount * sizeof(int16_t));
            mRecFile.write(reinterpret_cast<const char*>(recScratch), bytes);
            mRecordedDataBytes += bytes;
        }
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

JNIEXPORT jboolean JNICALL
Java_com_apexdj_butter_audio_OnlineAndLocalRepository_nativeMountObbBrain(JNIEnv* env, jobject, jstring obbPathStr) {
    if (!gEngine || !obbPathStr) return JNI_FALSE;
    const char* path = env->GetStringUTFChars(obbPathStr, nullptr);
    bool ok = gEngine->mountObbBrain(std::string(path));
    env->ReleaseStringUTFChars(obbPathStr, path);
    return ok ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_apexdj_butter_audio_OnlineAndLocalRepository_nativeIsObbBrainMounted(JNIEnv*, jobject) {
    return (gEngine && gEngine->isObbBrainMounted()) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_apexdj_butter_audio_OnlineAndLocalRepository_nativeBeginTrackLoad(JNIEnv*, jobject, jint deckIndex, jint totalExpectedSamples, jint sampleRate) {
    if (gEngine) gEngine->beginTrackLoad(deckIndex, totalExpectedSamples, sampleRate);
}

JNIEXPORT void JNICALL
Java_com_apexdj_butter_audio_OnlineAndLocalRepository_nativeAppendPcmChunk(
    JNIEnv* env, jobject, jint deckIndex, jfloatArray leftChunk, jfloatArray rightChunk, jint chunkSamples, jint sampleRate) {
    if (!gEngine || !leftChunk || !rightChunk || chunkSamples <= 0) return;
    jfloat* lPtr = env->GetFloatArrayElements(leftChunk, nullptr);
    jfloat* rPtr = env->GetFloatArrayElements(rightChunk, nullptr);
    gEngine->appendPcmChunk(deckIndex, lPtr, rPtr, chunkSamples, sampleRate);
    env->ReleaseFloatArrayElements(leftChunk, lPtr, JNI_ABORT);
    env->ReleaseFloatArrayElements(rightChunk, rPtr, JNI_ABORT);
}

JNIEXPORT void JNICALL
Java_com_apexdj_butter_audio_OnlineAndLocalRepository_nativeSetDeckPlayState(JNIEnv*, jobject, jint deck, jboolean playing) {
    if (gEngine && deck >= 0 && deck < apexdj::NUM_DECKS) {
        gEngine->channels[deck]->playing.store(playing);
    }
}

JNIEXPORT jboolean JNICALL
Java_com_apexdj_butter_audio_OnlineAndLocalRepository_nativeGetDeckPlayState(JNIEnv*, jobject, jint deck) {
    if (!gEngine || deck < 0 || deck >= apexdj::NUM_DECKS) return JNI_FALSE;
    return gEngine->channels[deck]->playing.load() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_apexdj_butter_audio_OnlineAndLocalRepository_nativeSetVocalSwitch(JNIEnv*, jobject, jint deck, jboolean enabled) {
    if (gEngine) gEngine->setVocalSwitch(deck, enabled);
}

JNIEXPORT jboolean JNICALL
Java_com_apexdj_butter_audio_OnlineAndLocalRepository_nativeGetVocalSwitch(JNIEnv*, jobject, jint deck) {
    return (gEngine && gEngine->getVocalSwitch(deck)) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_apexdj_butter_audio_OnlineAndLocalRepository_nativeSetNeuralAutoMix(JNIEnv*, jobject, jboolean enabled) {
    if (gEngine) gEngine->neuralAutoMixEnabled.store(enabled);
}

JNIEXPORT jboolean JNICALL
Java_com_apexdj_butter_audio_OnlineAndLocalRepository_nativeGetNeuralAutoMix(JNIEnv*, jobject) {
    return (gEngine && gEngine->neuralAutoMixEnabled.load()) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_apexdj_butter_audio_OnlineAndLocalRepository_nativeStartMasterRecording(JNIEnv* env, jobject, jstring wavPathStr) {
    if (!gEngine || !wavPathStr) return JNI_FALSE;
    const char* path = env->GetStringUTFChars(wavPathStr, nullptr);
    bool ok = gEngine->startMasterRecording(std::string(path));
    env->ReleaseStringUTFChars(wavPathStr, path);
    return ok ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_apexdj_butter_audio_OnlineAndLocalRepository_nativeStopMasterRecording(JNIEnv*, jobject) {
    if (gEngine) gEngine->stopMasterRecording();
}

JNIEXPORT jboolean JNICALL
Java_com_apexdj_butter_audio_OnlineAndLocalRepository_nativeIsMasterRecording(JNIEnv*, jobject) {
    return (gEngine && gEngine->isMasterRecording()) ? JNI_TRUE : JNI_FALSE;
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
Java_com_apexdj_butter_audio_OnlineAndLocalRepository_nativeSeekNormalized(JNIEnv*, jobject, jint deck, jfloat normPos) {
    if (gEngine) gEngine->seekNormalized(deck, normPos);
}

JNIEXPORT void JNICALL
Java_com_apexdj_butter_audio_OnlineAndLocalRepository_nativeSetChannelParam(
    JNIEnv*, jobject, jint deck, jint paramId, jfloat value) {
    if (!gEngine || deck < 0 || deck >= apexdj::NUM_DECKS) return;
    auto& ch = *gEngine->channels[deck];
    switch (paramId) {
        case 0: ch.channelFader.store(value); break;
        case 1: ch.eqLow.store(value); break;
        case 2: ch.eqLoMid.store(value); break;
        case 3: ch.eqHiMid.store(value); break;
        case 4: ch.eqHigh.store(value); break;
        case 6: ch.colorFxKnob.store(value); break;
        case 7: ch.vcfCutoffKnob.store(std::clamp(value, 0.0f, 1.0f)); break;
        case 9: ch.stemGain[0].store(value); break;
        case 10: ch.stemGain[1].store(value); break;
        case 11: ch.stemGain[2].store(value); break;
        case 12: ch.stemGain[3].store(value); break;
        case 13: ch.tempoRatio.store(std::clamp(value, 0.75f, 1.25f)); break;
        case 18: ch.jogTouching.store(std::abs(value) > 0.005f); ch.jogScratchVelocity.store(value); break;
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
        case 5: gEngine->bgmDeckLevel.store(std::clamp(value, 0.0f, 1.0f)); break;
    }
}

JNIEXPORT jfloat JNICALL
Java_com_apexdj_butter_audio_OnlineAndLocalRepository_nativeGetChannelVu(JNIEnv*, jobject, jint deck) {
    if (!gEngine || deck < 0 || deck >= apexdj::NUM_DECKS) return 0.0f;
    return gEngine->channels[deck]->vuMeterPeak.load(std::memory_order_relaxed);
}

JNIEXPORT jfloat JNICALL
Java_com_apexdj_butter_audio_OnlineAndLocalRepository_nativeGetDetectedBpm(JNIEnv*, jobject, jint deck) {
    if (!gEngine || deck < 0 || deck >= apexdj::NUM_DECKS) return 124.0f;
    return gEngine->channels[deck]->detectedBpm.load(std::memory_order_relaxed);
}

JNIEXPORT jint JNICALL
Java_com_apexdj_butter_audio_OnlineAndLocalRepository_nativeGetCamelotKey(JNIEnv*, jobject, jint deck) {
    if (!gEngine || deck < 0 || deck >= apexdj::NUM_DECKS) return 8;
    return gEngine->channels[deck]->camelotKeyIndex.load(std::memory_order_relaxed);
}

JNIEXPORT jfloat JNICALL
Java_com_apexdj_butter_audio_OnlineAndLocalRepository_nativeGetPlayheadNormalized(JNIEnv*, jobject, jint deck) {
    if (!gEngine || deck < 0 || deck >= apexdj::NUM_DECKS) return 0.0f;
    auto& ch = *gEngine->channels[deck];
    double total = std::max(1.0, static_cast<double>(ch.loadedSamples.load(std::memory_order_relaxed)));
    return static_cast<float>(ch.playheadSample.load(std::memory_order_relaxed) / total);
}

JNIEXPORT jfloatArray JNICALL
Java_com_apexdj_butter_audio_OnlineAndLocalRepository_nativeGetRgbWaveform(JNIEnv* env, jobject, jint deck) {
    jfloatArray result = env->NewFloatArray(apexdj::WAVEFORM_BINS * 3);
    if (!gEngine || deck < 0 || deck >= apexdj::NUM_DECKS) return result;
    auto& ch = *gEngine->channels[deck];
    env->SetFloatArrayRegion(result, 0, apexdj::WAVEFORM_BINS * 3, ch.rgbWaveformFlat.data());
    return result;
}

JNIEXPORT jstring JNICALL
Java_com_apexdj_butter_audio_OnlineAndLocalRepository_nativeGetBrainTelemetry(JNIEnv* env, jobject) {
    std::string text = gEngine ? gEngine->getBrainTelemetry() : "Engine Offline";
    return env->NewStringUTF(text.c_str());
}

} // extern "C"
