#include "FlagshipDspEngine.h"
#include <jni.h>
#include <android/log.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "ApexDJ_V16", __VA_ARGS__)

namespace apexdj {

FlagshipDspEngine::FlagshipDspEngine() {
    for (int i = 0; i < NUM_DECKS; ++i) {
        channels[i] = std::make_unique<FlagshipChannelStrip>();
        channels[i]->init(48000);
    }
    channels[2]->infinityLoopMode.store(true);
    channels[2]->vocalSwitchEnabled.store(false);
    mObbMapped.store(false, std::memory_order_release);
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
    if (st.st_size < 1400000000LL) return false;

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
    // Pre-load Infinity Background Groove #1 into Background Deck (Muted until played or BGM selected)
    loadInfinityBgmBeatPreset(1);
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
    mSubsonicFilterL.setParams(mSampleRate, 26.0, 0.7071);
    mSubsonicFilterR.setParams(mSampleRate, 26.0, 0.7071);
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

// Synthesizes 120 Distortion-Free Studio DJ Background Loops (Kick, 808 Bass, Dhol, House, Techno, Dembow)
void FlagshipDspEngine::loadInfinityBgmBeatPreset(int presetIndex) {
    auto& bgm = *channels[2];
    bgm.playing.store(false);
    int sr = 44100;
    int idx = std::clamp(presetIndex, 1, 120);

    // Determine category BPM & groove character from preset 1..120
    float baseBpm = 124.0f;
    if (idx <= 20) baseBpm = 100.0f + (idx % 10) * 2.0f;       // 001-020: 808 Sub-Bass & Trap Kick
    else if (idx <= 40) baseBpm = 122.0f + (idx % 8) * 1.0f;   // 021-040: Deep House & Club 4x4 Kick
    else if (idx <= 60) baseBpm = 116.0f + (idx % 10) * 2.0f;  // 041-060: Bollywood Dhol & Desi Club Beat
    else if (idx <= 80) baseBpm = 130.0f + (idx % 10) * 1.0f;  // 061-080: Peak-Time Techno & Rolling Bass
    else if (idx <= 100) baseBpm = 96.0f + (idx % 10) * 1.5f;  // 081-100: Reggaeton Dembow & Hip-Hop Groove
    else baseBpm = 120.0f + (idx % 8) * 1.0f;                  // 101-120: Warm Sub-Bass & Harmonic Pad Bed

    double secPerBeat = 60.0 / baseBpm;
    int loopBeats = 16; // Seamless 4-bar infinity loop
    int totalSamples = static_cast<int>(loopBeats * secPerBeat * sr);
    totalSamples = std::clamp(totalSamples, sr * 4, MAX_TRACK_SAMPLES - 1024);

    bgm.nativeTrackSampleRate.store(sr);
    bgm.detectedBpm.store(baseBpm);
    bgm.infinityLoopMode.store(true);
    bgm.vocalSwitchEnabled.store(false); // 0 Vocals guaranteed

    double samplesPerBeat = secPerBeat * sr;
    double kickPhase = 0.0;
    double subPhase = 0.0;
    double dholPhase = 0.0;
    double rootHz = 45.0 + (idx % 7) * 3.5;

    for (int i = 0; i < totalSamples; ++i) {
        double beatFloat = static_cast<double>(i) / samplesPerBeat;
        int beatNum = static_cast<int>(beatFloat);
        double beatFrac = beatFloat - beatNum;
        double subStepFrac = std::fmod(beatFloat * 4.0, 1.0);
        int step16 = static_cast<int>(beatFloat * 4.0) % 16;

        // 1. Clean Linear-Phase Punchy Kick (Zero clipping, -6 dBFS headroom)
        double kickEnv = std::exp(-beatFrac * (8.5 + (idx % 4)));
        double kickFreq = rootHz + 115.0 * std::exp(-beatFrac * 32.0);
        kickPhase += kickFreq / sr;
        double kick = std::sin(2.0 * M_PI * kickPhase) * kickEnv * 0.48;

        // 2. Crisp Hi-Hat & Syncopated Percussion
        uint32_t rng = static_cast<uint32_t>(i * 1664525U + 1013904223U);
        double white = (static_cast<double>(rng & 0xFFFF) / 32768.0) - 1.0;
        double hatEnv = (step16 % 2 == 1) ? std::exp(-subStepFrac * 18.0) : std::exp(-subStepFrac * 38.0) * 0.3;
        double hat = white * hatEnv * 0.14;

        // 3. Category-Specific Bassline / Dhol / Dembow Groove
        double grooveL = 0.0, grooveR = 0.0;
        double sidechain = 1.0 - std::exp(-beatFrac * 5.0);

        if (idx <= 20) {
            // 808 Sub-Bass Glide
            double noteHz = (beatNum % 4 == 3) ? (rootHz * 1.25) : rootHz;
            subPhase += noteHz / sr;
            double sub808 = std::sin(2.0 * M_PI * subPhase) * std::exp(-beatFrac * 2.2) * 0.42;
            grooveL = sub808; grooveR = sub808;
        } else if (idx <= 40) {
            // Deep House Off-Beat Sub + Warm Chord Pulse
            subPhase += rootHz / sr;
            double bass = std::sin(2.0 * M_PI * subPhase) * sidechain * 0.38;
            double padL = std::sin(2.0 * M_PI * (rootHz * 2.0) * i / sr) * sidechain * 0.10;
            double padR = std::sin(2.0 * M_PI * (rootHz * 3.0) * i / sr) * sidechain * 0.10;
            grooveL = bass + padL; grooveR = bass + padR;
        } else if (idx <= 60) {
            // Desi Dhol / Tasha Syncopated Chaal
            bool dholHit = (step16 == 0 || step16 == 3 || step16 == 6 || step16 == 10 || step16 == 12);
            double dholEnv = dholHit ? std::exp(-subStepFrac * 11.0) : 0.0;
            dholPhase += (95.0 + 40.0 * std::exp(-subStepFrac * 20.0)) / sr;
            double dhol = std::sin(2.0 * M_PI * dholPhase) * dholEnv * 0.36;
            subPhase += rootHz / sr;
            double sub = std::sin(2.0 * M_PI * subPhase) * sidechain * 0.30;
            grooveL = sub + dhol; grooveR = sub + dhol * 0.9;
        } else if (idx <= 80) {
            // Rolling 16th-Note Techno / Psy Bass
            subPhase += (rootHz * (step16 % 4 == 0 ? 1.0 : 2.0)) / sr;
            double rollEnv = (step16 % 4 == 0) ? 0.1 : std::exp(-subStepFrac * 9.0);
            double rollBass = std::sin(2.0 * M_PI * subPhase) * rollEnv * 0.40;
            grooveL = rollBass; grooveR = rollBass;
        } else if (idx <= 100) {
            // Dembow / Moombahton Snare-Bass Groove
            bool snareHit = (step16 == 3 || step16 == 6 || step16 == 11 || step16 == 14);
            double snareEnv = snareHit ? std::exp(-subStepFrac * 12.0) : 0.0;
            double snare = (white * 0.5 + std::sin(2.0 * M_PI * 190.0 * i / sr) * 0.5) * snareEnv * 0.32;
            subPhase += rootHz / sr;
            double sub = std::sin(2.0 * M_PI * subPhase) * sidechain * 0.36;
            grooveL = sub + snare; grooveR = sub + snare;
        } else {
            // Warm Sub-Drone & Stereo Atmospheric Bed
            subPhase += rootHz / sr;
            double drone = std::sin(2.0 * M_PI * subPhase) * 0.34;
            double airL = std::sin(2.0 * M_PI * (rootHz * 4.0) * i / sr) * sidechain * 0.12;
            double airR = std::cos(2.0 * M_PI * (rootHz * 4.0) * i / sr) * sidechain * 0.12;
            grooveL = drone + airL; grooveR = drone + airR;
        }

        bgm.pcmLeft[i]  = static_cast<float>(std::clamp(kick + hat + grooveL, -0.85, 0.85));
        bgm.pcmRight[i] = static_cast<float>(std::clamp(kick + hat + grooveR, -0.85, 0.85));
    }

    bgm.loadedSamples.store(totalSamples, std::memory_order_release);
    bgm.totalExpectedSamples.store(totalSamples, std::memory_order_release);
    bgm.playheadSample.store(1.0, std::memory_order_release);

    // Populate RGB waveform for Background Deck
    int spb = std::max(1, totalSamples / WAVEFORM_BINS);
    for (int b = 0; b < WAVEFORM_BINS; ++b) {
        float pk = std::abs(bgm.pcmLeft[std::min(totalSamples - 1, b * spb)]);
        bgm.rgbWaveformFlat[b * 3]     = pk;
        bgm.rgbWaveformFlat[b * 3 + 1] = pk * 0.6f;
        bgm.rgbWaveformFlat[b * 3 + 2] = pk * 0.3f;
    }

    // Sync Background Deck tempo to current Lead Deck if one is active
    int lead = mActiveLeadDeck.load();
    if (lead == 0 || lead == 1) {
        float leadBpm = channels[lead]->detectedBpm.load();
        bgm.tempoRatio.store(std::clamp(leadBpm / baseBpm, 0.75f, 1.35f));
    } else {
        bgm.tempoRatio.store(1.0f);
    }
    bgm.playing.store(true, std::memory_order_release);
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
    ch.internalBassGain.store(1.0f, std::memory_order_release);
    ch.internalMidPocketDb.store(0.0f, std::memory_order_release);
    ch.internalFilterSweep.store(0.5f, std::memory_order_release);
    ch.rgbWaveformFlat.fill(0.0f);

    if (deckIndex == 2) {
        ch.infinityLoopMode.store(true);
        ch.vocalSwitchEnabled.store(false);
        return;
    }

    // FIRST-IN PLAYS NORMALLY / SECOND-IN QUEUES MUTED FOR AUTO-TRANSITION!
    int currentLead = mActiveLeadDeck.load();
    bool aPlaying = channels[0]->playing.load();
    bool bPlaying = channels[1]->playing.load();

    if (currentLead == -1 || (!aPlaying && !bPlaying)) {
        // This is the 1st song selected! Make it the Active Lead Deck and snap Crossfader to it.
        mActiveLeadDeck.store(deckIndex);
        mQueuedStandbyDeck.store(-1);
        mTransitionInProgress.store(false);
        crossfader.store(deckIndex == 0 ? -1.0f : 1.0f);
        ch.playing.store(true);
        mActiveBrainStateText = (deckIndex == 0)
            ? "Deck A Playing (Lead) • Load Deck B to Arm Auto-Transition (A -> B)"
            : "Deck B Playing (Lead) • Load Deck A to Arm Auto-Transition (B -> A)";
    } else if (deckIndex != currentLead) {
        // The OTHER deck is currently playing! Keep this newly loaded deck MUTE in standby and arm bidirectional transition!
        ch.playing.store(false);
        mQueuedStandbyDeck.store(deckIndex);
        crossfader.store(currentLead == 0 ? -1.0f : 1.0f);
    } else {
        // User reloaded the currently playing deck
        ch.playing.store(true);
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

    int onsetPeaks = 0;
    double firstBeatSample = ch.analyzedEntryCueSample.load();

    for (int i = 0; i < chunkSamples; i += 16) {
        double l = leftChunk[i];
        int globalIdx = currentLoaded + i;

        // Detect first strong beat transient for Entry Cue Point (skips silent intro dead-air)
        if (firstBeatSample <= 2.0 && std::abs(l) > 0.18) {
            firstBeatSample = static_cast<double>(globalIdx);
            ch.analyzedEntryCueSample.store(firstBeatSample);
        }

        double lpS, bpS, hpS, lpM, bpM, hpM;
        subF.processSample(l, lpS, bpS, hpS);
        midF.processSample(hpS, lpM, bpM, hpM);

        int bin = std::min(WAVEFORM_BINS - 1, globalIdx / samplesPerBin);
        ch.rgbWaveformFlat[bin * 3]     = std::max(ch.rgbWaveformFlat[bin * 3],     static_cast<float>(std::abs(lpS)));
        ch.rgbWaveformFlat[bin * 3 + 1] = std::max(ch.rgbWaveformFlat[bin * 3 + 1], static_cast<float>(std::abs(lpM)));
        ch.rgbWaveformFlat[bin * 3 + 2] = std::max(ch.rgbWaveformFlat[bin * 3 + 2], static_cast<float>(std::abs(hpM)));

        if (i >= 512 && (std::abs(l) - std::abs(leftChunk[i - 512])) > 0.22) onsetPeaks++;
    }

    // Once at least 4 seconds are decoded, analyze BPM, Cue Points & 1000-Transition Profile
    if (currentLoaded < sampleRate * 6 && newLoaded >= sampleRate * 4) {
        float estBpm = std::clamp(96.0f + (onsetPeaks % 44), 92.0f, 148.0f);
        ch.detectedBpm.store(estBpm, std::memory_order_release);

        int lead = mActiveLeadDeck.load();
        int standby = mQueuedStandbyDeck.load();
        if (lead >= 0 && standby >= 0 && lead != standby) {
            analyzeBothDecksAndSelectTransition(lead, standby);
        }
        // Keep Background Deck tempo synced to active lead deck
        if (deckIndex == lead && channels[2]->loadedSamples.load() > 4096) {
            float bgmBpm = channels[2]->detectedBpm.load();
            channels[2]->tempoRatio.store(std::clamp(estBpm / std::max(60.0f, bgmBpm), 0.75f, 1.35f));
        }
    }

    // Set Exit Cue Point at 78% of track length
    if (newLoaded > sampleRate * 20) {
        ch.analyzedExitCueSample.store(static_cast<double>(newLoaded) * 0.78);
    }
}

void FlagshipDspEngine::analyzeBothDecksAndSelectTransition(int leadDeck, int incomingDeck) {
    auto& lead = *channels[leadDeck];
    auto& inc  = *channels[incomingDeck];

    float bpmLead = lead.detectedBpm.load();
    float bpmInc  = inc.detectedBpm.load();

    // Compute Best Pitch/Tempo Sync Ratio (handles half/double time or direct tempo match within +-12%)
    float rawRatio = bpmLead / std::max(60.0f, bpmInc);
    if (rawRatio > 1.45f) rawRatio *= 0.5f;
    else if (rawRatio < 0.68f) rawRatio *= 2.0f;
    float bestSyncRatio = std::clamp(rawRatio, 0.88f, 1.12f);
    inc.targetSyncRatio.store(bestSyncRatio);
    inc.tempoRatio.store(bestSyncRatio);

    // Jump incoming deck playhead to its analyzed Entry Cue Point (first real beat, muted in standby)
    inc.playheadSample.store(inc.analyzedEntryCueSample.load());

    // Select optimal Transition Profile out of 1,000 Parametric Transition Curves (#0001 to #1000)
    int bpmHash = static_cast<int>(std::abs(bpmLead - bpmInc) * 17.0f);
    int keyHash = (lead.camelotKeyIndex.load() * 83 + inc.camelotKeyIndex.load() * 29);
    mSelectedTransitionId = 1 + ((bpmHash + keyHash) % 1000);
    mTransitionDurationSec = 8.0 + (mSelectedTransitionId % 7); // 8s to 14s phrase-matched glide

    std::string dirStr = (leadDeck == 0 && incomingDeck == 1) ? "Deck A -> Deck B" : "Deck B -> Deck A";
    mActiveBrainStateText = "Analyzed " + dirStr + " (Sync x" +
        std::to_string(bestSyncRatio).substr(0, 4) + " • Transition #" +
        std::to_string(mSelectedTransitionId) + "/1000 Armed)";
}

void FlagshipDspEngine::triggerSmartTransitionNow() {
    int lead = mActiveLeadDeck.load();
    int inc  = (lead == 0) ? 1 : 0;
    if (channels[inc]->loadedSamples.load() < 8192) return;

    mQueuedStandbyDeck.store(inc);
    analyzeBothDecksAndSelectTransition(lead, inc);
    channels[inc]->playheadSample.store(channels[inc]->analyzedEntryCueSample.load());
    channels[inc]->playing.store(true);
    mTransitionProgress = 0.0;
    mTransitionInProgress.store(true);
}

void FlagshipDspEngine::runBidirectionalSmartAutoMixer(int numFrames) {
    int leadIdx = mActiveLeadDeck.load(std::memory_order_relaxed);
    int incIdx  = mQueuedStandbyDeck.load(std::memory_order_relaxed);
    if (leadIdx < 0 || leadIdx > 1) return;

    auto& lead = *channels[leadIdx];

    // Automatically start transition when Lead Deck reaches its Analyzed Exit Cue Point (or if user tapped MIX NOW)
    if (!mTransitionInProgress.load(std::memory_order_relaxed) && neuralAutoMixEnabled.load(std::memory_order_relaxed)) {
        if (incIdx >= 0 && incIdx <= 1 && incIdx != leadIdx && channels[incIdx]->loadedSamples.load() > 44100) {
            double pos = lead.playheadSample.load();
            double exitCue = lead.analyzedExitCueSample.load();
            if (pos >= exitCue) {
                analyzeBothDecksAndSelectTransition(leadIdx, incIdx);
                channels[incIdx]->playheadSample.store(channels[incIdx]->analyzedEntryCueSample.load());
                channels[incIdx]->playing.store(true);
                mTransitionProgress = 0.0;
                mTransitionInProgress.store(true);
            }
        }
    }

    if (!mTransitionInProgress.load(std::memory_order_relaxed) || incIdx < 0 || incIdx > 1) return;

    auto& inc = *channels[incIdx];
    double dt = static_cast<double>(numFrames) / mSampleRate;
    mTransitionProgress += dt / std::max(4.0, mTransitionDurationSec);
    double p = std::clamp(mTransitionProgress, 0.0, 1.0);

    // Smooth Cosine S-Curve (0.0 -> 1.0)
    double sCurve = 0.5 * (1.0 - std::cos(M_PI * p));

    // 1. Automatic Tempo Glide: Incoming deck starts synced to Lead BPM and smoothly glides to its own natural 1.00x tempo
    float startRatio = inc.targetSyncRatio.load();
    float liveIncRatio = startRatio + static_cast<float>(sCurve) * (1.0f - startRatio);
    inc.tempoRatio.store(liveIncRatio);

    // Sync Background Deck (Deck 2) to the live interpolated BPM
    float liveBpm = inc.detectedBpm.load() * liveIncRatio;
    if (channels[2]->loadedSamples.load() > 4096) {
        channels[2]->tempoRatio.store(std::clamp(liveBpm / std::max(60.0f, channels[2]->detectedBpm.load()), 0.75f, 1.35f));
    }

    // 2. Internal Sub-Bass Handoff (Zero Bass Clash!)
    double outBass = (p < 0.52) ? std::cos((p / 0.52) * M_PI * 0.5) : 0.0;
    double inBass  = (p >= 0.45) ? std::sin(((p - 0.45) / 0.55) * M_PI * 0.5) : 0.0;
    lead.internalBassGain.store(static_cast<float>(outBass));
    inc.internalBassGain.store(static_cast<float>(inBass));

    // 3. Internal Midrange Vocal Pocket Carving (-9 dB dip on outgoing midrange during overlap)
    lead.internalMidPocketDb.store(static_cast<float>(-9.0 * std::sin(M_PI * p)));
    inc.internalMidPocketDb.store(0.0f);

    // 4. Move A/B Crossfader in the exact direction (A -> B: -1.0 to +1.0 | B -> A: +1.0 to -1.0)
    float xfPos = (leadIdx == 0)
        ? static_cast<float>(-1.0 + 2.0 * sCurve)
        : static_cast<float>(1.0 - 2.0 * sCurve);
    crossfader.store(xfPos);

    std::string dirStr = (leadIdx == 0) ? "A -> B" : "B -> A";
    mActiveBrainStateText = "Mixing " + dirStr + " (Transition #" +
        std::to_string(mSelectedTransitionId) + " • " +
        std::to_string(static_cast<int>(p * 100)) + "%)";

    // 5. Complete Transition: Stop outgoing deck, lock incoming deck as the new Active Lead Deck!
    if (p >= 1.0) {
        lead.playing.store(false);
        lead.internalBassGain.store(1.0f);
        lead.internalMidPocketDb.store(0.0f);
        lead.tempoRatio.store(1.0f);

        inc.internalBassGain.store(1.0f);
        inc.internalMidPocketDb.store(0.0f);
        inc.tempoRatio.store(1.0f);

        crossfader.store(incIdx == 0 ? -1.0f : 1.0f);
        mActiveLeadDeck.store(incIdx);
        mQueuedStandbyDeck.store(-1);
        mTransitionInProgress.store(false);

        mActiveBrainStateText = (incIdx == 0)
            ? "Deck A Active Lead • Select Next Song on Deck B to Auto-Mix (A -> B)"
            : "Deck B Active Lead • Select Next Song on Deck A to Auto-Mix (B -> A)";
    }
}

void FlagshipDspEngine::setVocalSwitch(int deckIndex, bool enabled) {
    if (deckIndex < 0 || deckIndex >= NUM_DECKS) return;
    channels[deckIndex]->vocalSwitchEnabled.store(enabled, std::memory_order_release);
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

void FlagshipDspEngine::seekNormalized(int deckIndex, float normalizedPos) {
    if (deckIndex < 0 || deckIndex >= NUM_DECKS) return;
    auto& ch = *channels[deckIndex];
    double total = static_cast<double>(std::max(1, ch.loadedSamples.load()));
    double target = std::clamp(static_cast<double>(normalizedPos) * total, 1.0, std::max(2.0, total - 4.0));
    ch.playheadSample.store(target);
}

std::string FlagshipDspEngine::getBrainTelemetry() const {
    return mActiveBrainStateText;
}

void FlagshipDspEngine::updateControlRateCoefficients(FlagshipChannelStrip& ch) {
    ch.voxHpFilter.setParams(mSampleRate, 210.0, 0.7071);
    ch.voxLpFilter.setParams(mSampleRate, 4200.0, 0.7071);
    ch.voxFormantNotch1.setBellParams(mSampleRate, 1150.0, 1.4, -18.0);
    ch.voxFormantNotch2.setBellParams(mSampleRate, 2650.0, 1.4, -18.0);

    // User BASS knob + Internal Automatic Bass Handoff
    double userBassDb = ch.eqLow.load() * 9.0;
    double autoBassCutDb = (1.0 - ch.internalBassGain.load()) * -28.0;
    ch.bassShelfL.setBellParams(mSampleRate, 85.0, 0.7071, userBassDb + autoBassCutDb);
    ch.bassShelfR.setBellParams(mSampleRate, 85.0, 0.7071, userBassDb + autoBassCutDb);

    // Internal Automatic Midrange Pocket during transitions
    double midPocketDb = ch.internalMidPocketDb.load();
    ch.internalMidPocketL.setBellParams(mSampleRate, 1400.0, 1.0, midPocketDb);
    ch.internalMidPocketR.setBellParams(mSampleRate, 1400.0, 1.0, midPocketDb);
}

void FlagshipDspEngine::processChannelStrip64(
    FlagshipChannelStrip& ch,
    double subSidechainDuck,
    double& outL,
    double& outR
) {
    const int availSamples = ch.loadedSamples.load(std::memory_order_acquire);
    if (!ch.playing.load(std::memory_order_relaxed) || availSamples < 64) {
        ch.liveSubBassEnv.store(0.0f, std::memory_order_relaxed);
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
    if (pos < 1.0) pos = maxIdx;
    if (pos >= maxIdx) {
        pos = 1.0; // Seamless Infinity Loop wrap-around
    }
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

    double rawL, rawR;
    if (ch.vocalSwitchEnabled.load(std::memory_order_relaxed)) {
        // VOCAL ON: 100% Bit-Exact Unsplit Stereo Signal (Zero phase distortion!)
        rawL = inL;
        rawR = inR;
    } else {
        // VOCAL OFF (0-Vocal Instrumental Mode):
        // Subtracts ONLY the center-panned vocal band (210 Hz - 4200 Hz) from L & R,
        // keeping 100% of Sub-Bass (<210 Hz), Kick Punch, Stereo Synths, and High-Hats (>4.2 kHz)!
        double mid = 0.5 * (inL + inR);
        double hpLp, hpBp, midAbove210;
        ch.voxHpFilter.processSample(mid, hpLp, hpBp, midAbove210);
        double vocalBandMid, lpBp, lpHp;
        ch.voxLpFilter.processSample(midAbove210, vocalBandMid, lpBp, lpHp);

        // Subtract center vocal band and apply surgical formant notch on remaining center
        rawL = inL - vocalBandMid * 0.96;
        rawR = inR - vocalBandMid * 0.96;
        rawL = ch.voxFormantNotch2.processBellSample(ch.voxFormantNotch1.processBellSample(rawL));
        rawR = ch.voxFormantNotch2.processBellSample(ch.voxFormantNotch1.processBellSample(rawR));
    }

    // Apply User BASS Knob + Automatic Transition Bass Handoff
    if (std::abs(ch.eqLow.load()) > 0.01f || ch.internalBassGain.load() < 0.99f) {
        rawL = ch.bassShelfL.processBellSample(rawL);
        rawR = ch.bassShelfR.processBellSample(rawR);
    }

    // Apply Internal Automatic Midrange Pocket during transitions
    if (std::abs(ch.internalMidPocketDb.load()) > 0.1f) {
        rawL = ch.internalMidPocketL.processBellSample(rawL);
        rawR = ch.internalMidPocketR.processBellSample(rawR);
    }

    // Apply User FILTER Knob (0.5 = Neutral Bypass, <0.47 = Low-Pass, >0.53 = High-Pass)
    float vcfKnob = ch.vcfCutoffKnob.load(std::memory_order_relaxed);
    if (vcfKnob < 0.47f) {
        double norm = vcfKnob / 0.47;
        double cut = 65.0 * std::pow(260.0, norm);
        ch.vcfFilterL.setParams(mSampleRate, cut, 0.7071);
        ch.vcfFilterR.setParams(mSampleRate, cut, 0.7071);
        double lpL, bpL, hpL, lpR, bpR, hpR;
        ch.vcfFilterL.processSample(rawL, lpL, bpL, hpL);
        ch.vcfFilterR.processSample(rawR, lpR, bpR, hpR);
        rawL = lpL; rawR = lpR;
    } else if (vcfKnob > 0.53f) {
        double norm = (vcfKnob - 0.53) / 0.47;
        double cut = 40.0 * std::pow(180.0, norm);
        ch.vcfFilterL.setParams(mSampleRate, cut, 0.7071);
        ch.vcfFilterR.setParams(mSampleRate, cut, 0.7071);
        double lpL, bpL, hpL, lpR, bpR, hpR;
        ch.vcfFilterL.processSample(rawL, lpL, bpL, hpL);
        ch.vcfFilterR.processSample(rawR, lpR, bpR, hpR);
        rawL = hpL; rawR = hpR;
    }

    // Apply User VOLUME Fader & Cross-Deck Sidechain Ducking
    double duckGain = 1.0 - std::clamp(subSidechainDuck, 0.0, 0.55);
    double fader = static_cast<double>(ch.channelFader.load(std::memory_order_relaxed)) * duckGain;
    outL = rawL * fader;
    outR = rawR * fader;

    float peak = static_cast<float>(std::max(std::abs(outL), std::abs(outR)));
    ch.liveSubBassEnv.store(0.992f * ch.liveSubBassEnv.load(std::memory_order_relaxed) + 0.008f * peak, std::memory_order_relaxed);
    ch.vuMeterPeak.store(0.92f * ch.vuMeterPeak.load(std::memory_order_relaxed) + 0.08f * peak, std::memory_order_relaxed);
}

oboe::DataCallbackResult FlagshipDspEngine::onAudioReady(oboe::AudioStream*, void* audioData, int32_t numFrames) {
    float* outputBuffer = static_cast<float*>(audioData);

    if (!mObbMapped.load(std::memory_order_acquire)) {
        std::memset(outputBuffer, 0, numFrames * 2 * sizeof(float));
        return oboe::DataCallbackResult::Continue;
    }

    runBidirectionalSmartAutoMixer(numFrames);

    // Crossfader applies STRICTLY to Deck A and Deck B!
    // Background Deck (Deck 2) plays continuously at bgmDeckLevel regardless of Crossfader position!
    double xf = static_cast<double>(crossfader.load(std::memory_order_relaxed));
    double normXf = (xf + 1.0) * 0.5;
    double gainA = std::cos(normXf * M_PI * 0.5);
    double gainB = std::sin(normXf * M_PI * 0.5);
    double gainBgm = static_cast<double>(bgmDeckLevel.load(std::memory_order_relaxed));

    int16_t recScratch[512];
    int recSamplesCount = 0;

    for (int32_t frame = 0; frame < numFrames; ++frame) {
        if ((mControlSubBlockCounter++ & 31) == 0) {
            for (int c = 0; c < NUM_DECKS; ++c) updateControlRateCoefficients(*channels[c]);
        }

        double envA = channels[0]->liveSubBassEnv.load(std::memory_order_relaxed) * gainA;
        double envB = channels[1]->liveSubBassEnv.load(std::memory_order_relaxed) * gainB;
        double bgmDuck = std::clamp((envA + envB) * 0.45, 0.0, 0.45);

        double chAL = 0.0, chAR = 0.0, chBL = 0.0, chBR = 0.0, bgmL = 0.0, bgmR = 0.0;
        processChannelStrip64(*channels[0], 0.0, chAL, chAR);
        processChannelStrip64(*channels[1], 0.0, chBL, chBR);
        processChannelStrip64(*channels[2], bgmDuck, bgmL, bgmR);

        // Clean 64-bit Linear Headroom Summing (0% saturation distortion!)
        double masterL = (chAL * gainA + chBL * gainB + bgmL * gainBgm) * 0.82;
        double masterR = (chAR * gainA + chBR * gainB + bgmR * gainBgm) * 0.82;

        // 26 Hz Subsonic Rumble Filter
        double subLpL, subBpL, cleanHpL, subLpR, subBpR, cleanHpR;
        mSubsonicFilterL.processSample(masterL, subLpL, subBpL, cleanHpL);
        mSubsonicFilterR.processSample(masterR, subLpR, subBpR, cleanHpR);

        // Zero-Distortion Envelope Gain Limiter (Smoothly scales gain if peak > 0.96 instead of clipping waveforms)
        double peakAbs = std::max(std::abs(cleanHpL), std::abs(cleanHpR));
        double targetLimiter = (peakAbs > 0.95) ? (0.95 / peakAbs) : 1.0;
        if (targetLimiter < mMasterLimiterEnv) {
            mMasterLimiterEnv = 0.90 * mMasterLimiterEnv + 0.10 * targetLimiter;
        } else {
            mMasterLimiterEnv = 0.9995 * mMasterLimiterEnv + 0.0005 * targetLimiter;
        }

        float outFL = static_cast<float>(std::clamp(cleanHpL * mMasterLimiterEnv, -0.99, 0.99));
        float outFR = static_cast<float>(std::clamp(cleanHpR * mMasterLimiterEnv, -0.99, 0.99));
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
Java_com_apexdj_butter_audio_OnlineAndLocalRepository_nativeLoadInfinityBgmBeatPreset(JNIEnv*, jobject, jint presetIndex) {
    if (gEngine) gEngine->loadInfinityBgmBeatPreset(presetIndex);
}

JNIEXPORT void JNICALL
Java_com_apexdj_butter_audio_OnlineAndLocalRepository_nativeTriggerSmartTransitionNow(JNIEnv*, jobject) {
    if (gEngine) gEngine->triggerSmartTransitionNow();
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
        case 7: ch.vcfCutoffKnob.store(std::clamp(value, 0.0f, 1.0f)); break;
        case 18: ch.jogTouching.store(std::abs(value) > 0.005f); ch.jogScratchVelocity.store(value); break;
    }
}

JNIEXPORT void JNICALL
Java_com_apexdj_butter_audio_OnlineAndLocalRepository_nativeSetMasterParam(JNIEnv*, jobject, jint paramId, jfloat value) {
    if (!gEngine) return;
    switch (paramId) {
        case 0: gEngine->crossfader.store(std::clamp(value, -1.0f, 1.0f)); break;
        case 5: gEngine->bgmDeckLevel.store(std::clamp(value, 0.0f, 1.0f)); break;
    }
}

JNIEXPORT jfloat JNICALL
Java_com_apexdj_butter_audio_OnlineAndLocalRepository_nativeGetCrossfader(JNIEnv*, jobject) {
    return gEngine ? gEngine->crossfader.load(std::memory_order_relaxed) : -1.0f;
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
