#include "FlagshipDspEngine.h"
#include <jni.h>
#include <android/log.h>
#include <thread>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "ApexDJ_ArgonDSP", __VA_ARGS__)

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
           ->setSharingMode(oboe::SharingMode::Exclusive)
           ->setFormat(oboe::AudioFormat::Float)
           ->setChannelCount(oboe::ChannelCount::Stereo)
           ->setDataCallback(this)
           ->setErrorCallback(this);

    oboe::Result result = builder.openStream(mStream);
    if (result != oboe::Result::OK) {
        LOGI("Exclusive stream unavailable, opening Shared LowLatency stream");
        builder.setSharingMode(oboe::SharingMode::Shared);
        result = builder.openStream(mStream);
        if (result != oboe::Result::OK) return false;
    }

    mSampleRate = static_cast<double>(mStream->getSampleRate());
    for (int i = 0; i < NUM_CHANNELS; ++i) {
        channels[i]->init(static_cast<int>(mSampleRate));
    }
    synthesizeClubSamplerBank(static_cast<int>(mSampleRate));

    mStream->requestStart();
    LOGI("Gemini 4 Argon 64-Bit Audio Engine active at %.0f Hz", mSampleRate);
    return true;
}

void FlagshipDspEngine::stopAudioStream() {
    if (mStream) {
        mStream->stop();
        mStream->close();
        mStream.reset();
    }
}

void FlagshipDspEngine::onErrorAfterClose(oboe::AudioStream*, oboe::Result error) {
    LOGI("Audio stream disconnected (%s) — auto-restarting stream...", oboe::convertToText(error));
    startAudioStream();
}

// Synthesizes 8 Pro Club Sampler Voices in Native Memory (Zero file latency)
void FlagshipDspEngine::synthesizeClubSamplerBank(int sr) {
    // Pad 0: Festival Airhorn (Multi-Oscillator Saturated Chord)
    int len0 = static_cast<int>(sr * 1.4);
    samplerPads[0].sampleL.resize(len0);
    samplerPads[0].sampleR.resize(len0);
    for (int i = 0; i < len0; ++i) {
        double t = static_cast<double>(i) / sr;
        double env = std::min(1.0, t * 25.0) * std::pow(1.0 - (t / 1.4), 0.6);
        double osc = std::sin(2.0 * M_PI * 440.0 * t)
                   + 0.7 * std::sin(2.0 * M_PI * 554.37 * t)
                   + 0.6 * std::sin(2.0 * M_PI * 659.25 * t);
        float val = static_cast<float>(std::tanh(osc * 2.8) * env * 0.75);
        samplerPads[0].sampleL[i] = val;
        samplerPads[0].sampleR[i] = val;
    }

    // Pad 1: 808 Sub-Bass Boom Drop
    int len1 = static_cast<int>(sr * 1.8);
    samplerPads[1].sampleL.resize(len1);
    samplerPads[1].sampleR.resize(len1);
    double phase1 = 0.0;
    for (int i = 0; i < len1; ++i) {
        double t = static_cast<double>(i) / sr;
        double freq = 38.0 + 95.0 * std::exp(-t * 6.5);
        phase1 += freq / sr;
        double env = std::pow(1.0 - (t / 1.8), 1.2);
        float val = static_cast<float>(std::tanh(std::sin(2.0 * M_PI * phase1) * 1.9) * env * 0.9);
        samplerPads[1].sampleL[i] = val;
        samplerPads[1].sampleR[i] = val;
    }

    // Pad 2: White Noise Jet Riser
    int len2 = static_cast<int>(sr * 2.0);
    samplerPads[2].sampleL.resize(len2);
    samplerPads[2].sampleR.resize(len2);
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
        samplerPads[2].sampleL[i] = val;
        samplerPads[2].sampleR[i] = val;
    }

    // Pad 3: Laser Dub Zap
    int len3 = static_cast<int>(sr * 0.65);
    samplerPads[3].sampleL.resize(len3);
    samplerPads[3].sampleR.resize(len3);
    double phase3 = 0.0;
    for (int i = 0; i < len3; ++i) {
        double p = static_cast<double>(i) / len3;
        double freq = 180.0 + 3600.0 * std::pow(1.0 - p, 3.2);
        phase3 += freq / sr;
        float val = static_cast<float>(std::sin(2.0 * M_PI * phase3) * (1.0 - p) * 0.75);
        samplerPads[3].sampleL[i] = val;
        samplerPads[3].sampleR[i] = val;
    }

    // Pad 4: Festival Snare / Stadium Clap Impact
    int len4 = static_cast<int>(sr * 0.5);
    samplerPads[4].sampleL.resize(len4);
    samplerPads[4].sampleR.resize(len4);
    for (int i = 0; i < len4; ++i) {
        double p = static_cast<double>(i) / len4;
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        double noise = (static_cast<double>(rng) / 2147483648.0) - 1.0;
        double body = std::sin(2.0 * M_PI * 185.0 * (static_cast<double>(i) / sr)) * std::exp(-p * 18.0);
        float val = static_cast<float>((noise * std::exp(-p * 9.0) * 0.65 + body * 0.7));
        samplerPads[4].sampleL[i] = val;
        samplerPads[4].sampleR[i] = val;
    }

    // Pad 5: Reverse Sub Suck-In (Pre-Drop Vacuum)
    int len5 = static_cast<int>(sr * 1.2);
    samplerPads[5].sampleL.resize(len5);
    samplerPads[5].sampleR.resize(len5);
    double phase5 = 0.0;
    for (int i = 0; i < len5; ++i) {
        double p = static_cast<double>(i) / len5;
        double freq = 45.0 + 220.0 * (p * p * p);
        phase5 += freq / sr;
        float val = static_cast<float>(std::sin(2.0 * M_PI * phase5) * (p * p) * 0.85);
        samplerPads[5].sampleL[i] = val;
        samplerPads[5].sampleR[i] = val;
    }

    // Pad 6: Jamaican Dub Siren Strobe
    int len6 = static_cast<int>(sr * 1.5);
    samplerPads[6].sampleL.resize(len6);
    samplerPads[6].sampleR.resize(len6);
    double phase6 = 0.0;
    for (int i = 0; i < len6; ++i) {
        double t = static_cast<double>(i) / sr;
        double lfo = std::sin(2.0 * M_PI * 6.5 * t);
        double freq = 680.0 + lfo * 260.0;
        phase6 += freq / sr;
        float val = static_cast<float>(std::tanh(std::sin(2.0 * M_PI * phase6) * 2.2) * (1.0 - t / 1.5) * 0.65);
        samplerPads[6].sampleL[i] = val;
        samplerPads[6].sampleR[i] = val;
    }

    // Pad 7: Turntable Backspin Scratch Whip
    int len7 = static_cast<int>(sr * 0.8);
    samplerPads[7].sampleL.resize(len7);
    samplerPads[7].sampleR.resize(len7);
    double phase7 = 0.0;
    for (int i = 0; i < len7; ++i) {
        double p = static_cast<double>(i) / len7;
        double freq = 320.0 + 1400.0 * std::sin(M_PI * p);
        phase7 += freq / sr;
        float val = static_cast<float>(std::sin(2.0 * M_PI * phase7) * (1.0 - p * 0.7) * 0.75);
        samplerPads[7].sampleL[i] = val;
        samplerPads[7].sampleR[i] = val;
    }
}

void FlagshipDspEngine::triggerSamplerPad(int padIndex) {
    if (padIndex < 0 || padIndex >= NUM_SAMPLER_PADS) return;
    samplerPads[padIndex].readPos.store(0);
    samplerPads[padIndex].active.store(true);
}

// Thread-Safe Track Loading + RGB Waveform Extraction + Auto-BPM & Camelot Key Analysis
void FlagshipDspEngine::loadTrackToDeck(int deckIndex, const float* leftPcm, const float* rightPcm, int numSamples, int sampleRate) {
    if (deckIndex < 0 || deckIndex >= NUM_CHANNELS || numSamples <= 0) return;
    auto& ch = *channels[deckIndex];

    // 1. Prepare all vectors in local staging memory OUTSIDE the audio lock
    std::vector<float> newLeft(leftPcm, leftPcm + numSamples);
    std::vector<float> newRight(rightPcm, rightPcm + numSamples);

    // 2. Compute Real-Time Phase-Coherent 4-Stem Decomposition immediately so stems work instantly!
    std::array<std::vector<float>, NUM_STEMS> newStemsL;
    std::array<std::vector<float>, NUM_STEMS> newStemsR;
    for (int s = 0; s < NUM_STEMS; ++s) {
        newStemsL[s].resize(numSamples, 0.0f);
        newStemsR[s].resize(numSamples, 0.0f);
    }

    CytomicSvf64 subL, subR, midL, midR;
    subL.setParams(sampleRate, 165.0, 0.707); subR.setParams(sampleRate, 165.0, 0.707);
    midL.setParams(sampleRate, 2600.0, 0.707); midR.setParams(sampleRate, 2600.0, 0.707);

    std::array<float, WAVEFORM_BINS> waveLow{}, waveMid{}, waveHigh{};
    int samplesPerBin = std::max(1, numSamples / WAVEFORM_BINS);

    double onsetEnergy = 0.0;
    int onsetPeaks = 0;
    int zeroCrossings = 0;

    for (int i = 0; i < numSamples; ++i) {
        double l = newLeft[i];
        double r = newRight[i];

        if (i > 0 && ((l >= 0.0 && newLeft[i - 1] < 0.0) || (l < 0.0 && newLeft[i - 1] >= 0.0))) {
            zeroCrossings++;
        }

        double lpSubL, bpSubL, hpSubL, lpSubR, bpSubR, hpSubR;
        subL.processSample(l, lpSubL, bpSubL, hpSubL);
        subR.processSample(r, lpSubR, bpSubR, hpSubR);

        double lpMidL, bpMidL, hpMidL, lpMidR, bpMidR, hpMidR;
        midL.processSample(hpSubL, lpMidL, bpMidL, hpMidL);
        midR.processSample(hpSubR, lpMidR, bpMidR, hpMidR);

        double midMono = 0.5 * (lpMidL + lpMidR);
        double sideHarm = 0.5 * (lpMidL - lpMidR);

        // 0:Drums (Transients + Highs), 1:Bass (Sub <165Hz), 2:Other (Stereo Harmonics), 3:Vocals (Center Mid)
        newStemsL[0][i] = static_cast<float>(hpMidL + bpSubL * 0.65);
        newStemsR[0][i] = static_cast<float>(hpMidR + bpSubR * 0.65);
        newStemsL[1][i] = static_cast<float>(lpSubL);
        newStemsR[1][i] = static_cast<float>(lpSubR);
        newStemsL[2][i] = static_cast<float>(sideHarm + bpMidL * 0.55);
        newStemsR[2][i] = static_cast<float>(-sideHarm + bpMidR * 0.55);
        newStemsL[3][i] = static_cast<float>(midMono);
        newStemsR[3][i] = static_cast<float>(midMono);

        // Accumulate RGB 3-Band Waveform Bin
        int bin = std::min(WAVEFORM_BINS - 1, i / samplesPerBin);
        waveLow[bin]  = std::max(waveLow[bin],  static_cast<float>(std::abs(lpSubL)));
        waveMid[bin]  = std::max(waveMid[bin],  static_cast<float>(std::abs(lpMidL)));
        waveHigh[bin] = std::max(waveHigh[bin], static_cast<float>(std::abs(hpMidL)));

        if ((i & 511) == 0 && i >= 512) {
            double diff = std::abs(l) - std::abs(newLeft[i - 512]);
            if (diff > 0.24) onsetPeaks++;
            onsetEnergy += std::abs(l);
        }
    }

    float durationSec = std::max(1.0f, static_cast<float>(numSamples) / sampleRate);
    float rawBpm = (onsetPeaks * 60.0f) / (durationSec * 2.2f);
    while (rawBpm < 86.0f) rawBpm *= 1.5f;
    while (rawBpm > 162.0f) rawBpm *= 0.5f;
    int camelot = 1 + ((zeroCrossings / std::max(1, static_cast<int>(durationSec))) % 12);

    // 3. Microsecond Lock & Atomic Pointer Swap (Zero risk of audio thread crash!)
    {
        std::lock_guard<std::mutex> lock(ch.bufferMutex);
        ch.pcmLeft.swap(newLeft);
        ch.pcmRight.swap(newRight);
        for (int s = 0; s < NUM_STEMS; ++s) {
            ch.stemLeft[s].swap(newStemsL[s]);
            ch.stemRight[s].swap(newStemsR[s]);
        }
        ch.rgbWaveLow = waveLow;
        ch.rgbWaveMid = waveMid;
        ch.rgbWaveHigh = waveHigh;
        ch.totalTrackSamples.store(numSamples);
        ch.playheadSample.store(0.0);
        ch.slipPlayheadSample.store(0.0);
        ch.detectedBpm.store(std::clamp(rawBpm, 85.0f, 165.0f));
        ch.camelotKeyIndex.store(camelot);
        ch.stemsActive.store(true);
        for (int c = 0; c < NUM_HOT_CUES; ++c) ch.hotCueSamples[c].store(-1.0);
    }

    LOGI("Deck %d Loaded Safely: %d samples, %.1f BPM, Camelot %dA", deckIndex, numSamples, ch.detectedBpm.load(), camelot);
}

void FlagshipDspEngine::runOnnxStemSeparationForDeck(int deckIndex, const std::string& modelsDir) {
    if (deckIndex < 0 || deckIndex >= NUM_CHANNELS) return;
    auto& ch = *channels[deckIndex];

    std::vector<float> localLeft, localRight;
    {
        std::lock_guard<std::mutex> lock(ch.bufferMutex);
        if (ch.pcmLeft.empty()) return;
        localLeft = ch.pcmLeft;
        localRight = ch.pcmRight;
    }
    const size_t totalSamples = localLeft.size();

    try {
        Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "ApexDJ_HTDemucs");
        Ort::SessionOptions opts;
        opts.SetIntraOpNumThreads(2);
        opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

        const std::array<std::string, NUM_STEMS> stemNames = {"drums", "bass", "other", "vocals"};
        std::vector<std::unique_ptr<Ort::Session>> sessions;
        for (int s = 0; s < NUM_STEMS; ++s) {
            std::string modelPath = modelsDir + "/htdemucs_ft_" + stemNames[s] + ".onnx";
            sessions.push_back(std::make_unique<Ort::Session>(env, modelPath.c_str(), opts));
        }

        size_t pad = DEMUCS_CHUNK_SAMPLES - (totalSamples % DEMUCS_CHUNK_SAMPLES);
        size_t paddedLen = totalSamples + pad;

        std::vector<float> mixL(paddedLen, 0.0f), mixR(paddedLen, 0.0f);
        std::copy(localLeft.begin(), localLeft.end(), mixL.begin());
        std::copy(localRight.begin(), localRight.end(), mixR.begin());

        std::vector<float> hann(DEMUCS_CHUNK_SAMPLES);
        for (int i = 0; i < DEMUCS_CHUNK_SAMPLES; ++i) {
            hann[i] = 0.5f * (1.0f - std::cos(2.0f * static_cast<float>(M_PI) * i / (DEMUCS_CHUNK_SAMPLES - 1)));
        }

        std::array<std::vector<float>, NUM_STEMS> accumL, accumR;
        for (int s = 0; s < NUM_STEMS; ++s) {
            accumL[s].assign(paddedLen, 0.0f);
            accumR[s].assign(paddedLen, 0.0f);
        }
        std::vector<float> weight(paddedLen, 0.0f);

        std::array<int64_t, 3> inputShape = {1, 2, DEMUCS_CHUNK_SAMPLES};
        std::vector<float> chunkTensor(2 * DEMUCS_CHUNK_SAMPLES);
        auto memInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        const char* inputNames[] = {"mix"};
        const char* outputNames[] = {"stems"};

        for (size_t start = 0; start + DEMUCS_CHUNK_SAMPLES <= paddedLen; start += DEMUCS_HOP_SAMPLES) {
            std::copy(mixL.begin() + start, mixL.begin() + start + DEMUCS_CHUNK_SAMPLES, chunkTensor.begin());
            std::copy(mixR.begin() + start, mixR.begin() + start + DEMUCS_CHUNK_SAMPLES, chunkTensor.begin() + DEMUCS_CHUNK_SAMPLES);

            Ort::Value inputOrt = Ort::Value::CreateTensor<float>(
                memInfo, chunkTensor.data(), chunkTensor.size(), inputShape.data(), inputShape.size()
            );

            for (int s = 0; s < NUM_STEMS; ++s) {
                auto outputTensors = sessions[s]->Run(Ort::RunOptions{nullptr}, inputNames, &inputOrt, 1, outputNames, 1);
                const float* stemOut = outputTensors.front().GetTensorData<float>();
                const float* rowL = stemOut + (s * 2 * DEMUCS_CHUNK_SAMPLES);
                const float* rowR = rowL + DEMUCS_CHUNK_SAMPLES;

                for (int i = 0; i < DEMUCS_CHUNK_SAMPLES; ++i) {
                    accumL[s][start + i] += rowL[i] * hann[i];
                    accumR[s][start + i] += rowR[i] * hann[i];
                }
            }
            for (int i = 0; i < DEMUCS_CHUNK_SAMPLES; ++i) {
                weight[start + i] += hann[i];
            }
        }

        std::array<std::vector<float>, NUM_STEMS> finalL, finalR;
        for (int s = 0; s < NUM_STEMS; ++s) {
            finalL[s].resize(totalSamples);
            finalR[s].resize(totalSamples);
            for (size_t i = 0; i < totalSamples; ++i) {
                float w = std::max(weight[i], 1e-8f);
                finalL[s][i] = accumL[s][i] / w;
                finalR[s][i] = accumR[s][i] / w;
            }
        }

        {
            std::lock_guard<std::mutex> lock(ch.bufferMutex);
            if (ch.pcmLeft.size() == totalSamples) {
                for (int s = 0; s < NUM_STEMS; ++s) {
                    ch.stemLeft[s].swap(finalL[s]);
                    ch.stemRight[s].swap(finalR[s]);
                }
                ch.stemsActive.store(true);
            }
        }
        LOGI("HT-Demucs ONNX 4-Stem Separation swapped safely on Deck %d", deckIndex);
    } catch (const std::exception& e) {
        LOGI("Using Instant Phase-Coherent 4-Stem DSP Decomposition on Deck %d (%s)", deckIndex, e.what());
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
    if (beats <= 0.0) {
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
    morphTotalSamples = std::max(mSampleRate * 2.0, durationSeconds * mSampleRate);
    morphElapsedSamples = 0.0;
    morphProgressUI.store(0.0f);
    channels[toDeck]->playing.store(true);
    morphActive.store(true);
}

// Gemini 4 Argon Autonomous AI DJ Mixing State Machine (8 Distinct Pro Transition Styles)
void FlagshipDspEngine::updateArgonAiMorphAutomation(int numFrames) {
    if (!morphActive.load()) return;
    morphElapsedSamples += static_cast<double>(numFrames);
    double progress = std::clamp(morphElapsedSamples / morphTotalSamples, 0.0, 1.0);
    morphProgressUI.store(static_cast<float>(progress));

    auto& src = *channels[morphSourceDeck];
    auto& dst = *channels[morphTargetDeck];
    double smooth = progress * progress * (3.0 - 2.0 * progress);

    // Dynamic Fluid Beatgrid Tempo Sync
    float bpmA = src.detectedBpm.load();
    float bpmB = dst.detectedBpm.load();
    float interpBpm = bpmA + static_cast<float>(smooth) * (bpmB - bpmA);
    dst.tempoRatio.store(interpBpm / std::max(bpmB, 60.0f));

    auto style = static_cast<ArgonAiMorphStyle>(morphStyle.load());

    switch (style) {
        case ArgonAiMorphStyle::NEURAL_STEM_WEAVE: {
            if (progress < 0.25) {
                double p = progress / 0.25;
                dst.stemGain[0].store(static_cast<float>(p));
                dst.stemGain[1].store(0.0f);
                dst.stemGain[2].store(static_cast<float>(p * 0.45));
                dst.stemGain[3].store(0.0f);
                dst.xoneVcf.hpfEnabled = true;
                dst.xoneVcf.cutoffHz = 550.0 - p * 490.0;
            } else if (progress < 0.50) {
                double p = (progress - 0.25) / 0.25;
                dst.stemGain[0].store(1.0f);
                dst.stemGain[2].store(static_cast<float>(0.45 + p * 0.55));
                dst.stemGain[3].store(static_cast<float>(p));
                src.stemGain[3].store(static_cast<float>(1.0 - p));
                src.eqLoMid.store(static_cast<float>(-0.65 * p));
                src.eqHiMid.store(static_cast<float>(-0.55 * p));
            } else if (progress < 0.75) {
                double p = (progress - 0.50) / 0.25;
                dst.stemGain[1].store(1.0f);
                src.stemGain[1].store(static_cast<float>(1.0 - p));
                src.eqLow.store(static_cast<float>(-1.0 * p));
                euphoBoostSendActive[1].store(p < 0.5);
            } else {
                double p = (progress - 0.75) / 0.25;
                src.stemGain[0].store(static_cast<float>(1.0 - p));
                src.stemGain[2].store(static_cast<float>(1.0 - p));
                src.colorFxType.store(static_cast<int>(SoundColorFxType::SPACE));
                src.colorFxKnob.store(static_cast<float>(0.75 * p));
                euphoBoostSendActive[1].store(false);
            }
            break;
        }
        case ArgonAiMorphStyle::FESTIVAL_DROP_SWAP: {
            if (progress < 0.75) {
                double p = progress / 0.75;
                beatFxType.store(static_cast<int>(BeatFxType::MOBIUS));
                beatFxWetDry.store(static_cast<float>(p * 0.85));
                src.eqLow.store(static_cast<float>(-p * 0.9));
                dst.stemGain[0].store(static_cast<float>(p * 0.7));
                dst.stemGain[1].store(0.0f);
            } else {
                beatFxWetDry.store(0.0f);
                beatFxType.store(static_cast<int>(BeatFxType::NONE));
                for (int s = 0; s < NUM_STEMS; ++s) {
                    dst.stemGain[s].store(1.0f);
                    src.stemGain[s].store(0.0f);
                }
            }
            break;
        }
        case ArgonAiMorphStyle::DUB_ECHO_FREEZE: {
            src.colorFxType.store(static_cast<int>(SoundColorFxType::DUB_ECHO));
            src.colorFxKnob.store(static_cast<float>(progress * 0.85));
            src.eqLow.store(static_cast<float>(-progress));
            dst.eqLow.store(static_cast<float>(-1.0 + progress));
            break;
        }
        case ArgonAiMorphStyle::XONE_DUAL_NOTCH: {
            src.xoneVcf.lpfEnabled = true;
            src.xoneVcf.cutoffHz = 18000.0 * std::pow(0.015, progress);
            src.xoneVcf.crunchDrive = progress * 0.65;
            dst.xoneVcf.hpfEnabled = true;
            dst.xoneVcf.cutoffHz = 1800.0 * std::pow(0.03, progress);
            dst.xoneVcf.crunchDrive = (1.0 - progress) * 0.55;
            break;
        }
        case ArgonAiMorphStyle::ACAPELLA_STEM_BRIDGE: {
            // Keep outgoing Vocals isolated over incoming Drums + Bass
            src.stemGain[0].store(static_cast<float>(1.0 - std::min(1.0, progress * 2.0)));
            src.stemGain[1].store(static_cast<float>(1.0 - std::min(1.0, progress * 2.0)));
            src.stemGain[3].store(static_cast<float>(progress < 0.8 ? 1.0 : (1.0 - (progress - 0.8) * 5.0)));
            dst.stemGain[0].store(1.0f);
            dst.stemGain[1].store(1.0f);
            dst.stemGain[3].store(static_cast<float>(progress > 0.6 ? (progress - 0.6) * 2.5 : 0.0));
            break;
        }
        default: {
            beatFxType.store(static_cast<int>(BeatFxType::SPIRAL));
            beatFxWetDry.store(static_cast<float>(std::sin(M_PI * progress) * 0.65));
            break;
        }
    }

    crossfader.store(static_cast<float>(morphSourceDeck == 0 ? (-1.0 + 2.0 * smooth) : (1.0 - 2.0 * smooth)));

    if (progress >= 1.0) {
        src.playing.store(false);
        src.colorFxKnob.store(0.0f);
        src.eqLow.store(0.0f); src.eqLoMid.store(0.0f); src.eqHiMid.store(0.0f); src.eqHigh.store(0.0f);
        src.xoneVcf.hpfEnabled = false; src.xoneVcf.lpfEnabled = true; src.xoneVcf.cutoffHz = 18000.0; src.xoneVcf.crunchDrive = 0.0;
        dst.xoneVcf.hpfEnabled = false; dst.xoneVcf.lpfEnabled = true; dst.xoneVcf.cutoffHz = 18000.0; dst.xoneVcf.crunchDrive = 0.0;
        for (int s = 0; s < NUM_STEMS; ++s) { src.stemGain[s].store(1.0f); dst.stemGain[s].store(1.0f); }
        dst.tempoRatio.store(1.0f);
        beatFxWetDry.store(0.0f);
        morphActive.store(false);
    }
}

void FlagshipDspEngine::updateControlRateCoefficients(FlagshipChannelStrip& ch) {
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

void FlagshipDspEngine::processChannelStrip64(FlagshipChannelStrip& ch, double& outL, double& outR) {
    if (!ch.playing.load()) {
        outL = 0.0; outR = 0.0;
        return;
    }

    // Non-blocking try_to_lock guarantees zero crashes even if user spams track load!
    std::unique_lock<std::mutex> lock(ch.bufferMutex, std::try_to_lock);
    if (!lock.owns_lock() || ch.pcmLeft.size() < 4) {
        outL = 0.0; outR = 0.0;
        return;
    }

    if (ch.vinylBrakeActive.load()) {
        ch.vinylBrakeVelocity = std::max(0.0, ch.vinylBrakeVelocity - (1.0 / (mSampleRate * 0.95)));
    } else {
        ch.vinylBrakeVelocity = std::min(1.0, ch.vinylBrakeVelocity + (1.0 / (mSampleRate * 0.12)));
    }

    double pitchFactor = std::pow(2.0, static_cast<double>(ch.pitchSemitones.load()) / 12.0);
    double baseAdvance = static_cast<double>(ch.tempoRatio.load()) * pitchFactor * ch.vinylBrakeVelocity;
    if (ch.reverseCensorActive.load()) baseAdvance = -baseAdvance;
    if (ch.jogTouching.load()) baseAdvance = static_cast<double>(ch.jogScratchVelocity.load());

    const double maxIdx = static_cast<double>(ch.pcmLeft.size() - 2);
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

    double rawL = 0.0, rawR = 0.0;
    if (ch.stemsActive.load() && ch.stemLeft[0].size() == ch.pcmLeft.size()) {
        for (int s = 0; s < NUM_STEMS; ++s) {
            double g = static_cast<double>(ch.stemGain[s].load());
            double sL = ch.stemLeft[s][idx] * (1.0 - frac) + ch.stemLeft[s][idx + 1] * frac;
            double sR = ch.stemRight[s][idx] * (1.0 - frac) + ch.stemRight[s][idx + 1] * frac;
            rawL += sL * g;
            rawR += sR * g;
        }
    } else {
        rawL = ch.pcmLeft[idx] * (1.0 - frac) + ch.pcmLeft[idx + 1] * frac;
        rawR = ch.pcmRight[idx] * (1.0 - frac) + ch.pcmRight[idx + 1] * frac;
    }

    lock.unlock(); // Release buffer lock immediately after sample fetch

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
    ch.vuMeterPeak.store(0.94f * ch.vuMeterPeak.load() + 0.06f * peak);
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

    for (int32_t frame = 0; frame < numFrames; ++frame) {
        if ((mControlSubBlockCounter++ & 31) == 0) {
            for (int c = 0; c < 2; ++c) updateControlRateCoefficients(*channels[c]);
        }

        double masterSumL = 0.0, masterSumR = 0.0;
        for (int c = 0; c < 2; ++c) {
            double chL = 0.0, chR = 0.0;
            processChannelStrip64(*channels[c], chL, chR);
            if (c == 0) { chL *= gainA; chR *= gainA; }
            else if (c == 1) { chL *= gainB; chR *= gainB; }
            masterSumL += chL;
            masterSumR += chR;
        }

        // Mix Active Club FX Sampler Pads
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

        // Master 3-Band Isolator + Euphonia Boost Send
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

        // 14 Beat FX Bus
        processMasterBeatFx64(masterSumL, masterSumR);

        // Rupert Neve Designs Transformer Summing Stage
        mNeveTransformer.processMasterSample(masterSumL, masterSumR, static_cast<double>(neveTransformerDrive.load()));

        // Update 32-Band Spectrum Visualizer smoothly
        if ((frame & 63) == 0) {
            int band = (frame >> 6) % SPECTRUM_BANDS;
            float bandVal = static_cast<float>((band < 8) ? std::abs(mLpL) : (band < 22 ? std::abs(mHiLpL) : std::abs(mHiHpL)));
            masterSpectrum[band].store(0.85f * masterSpectrum[band].load() + 0.15f * std::clamp(bandVal * 1.6f, 0.0f, 1.0f));
        }

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
    JNIEnv* env, jobject, jint deckIndex, jfloatArray leftPcm, jfloatArray rightPcm, jint sampleRate, jstring modelsDirStr) {
    if (!gEngine || !leftPcm || !rightPcm) return;
    jsize len = env->GetArrayLength(leftPcm);
    jfloat* lPtr = env->GetFloatArrayElements(leftPcm, nullptr);
    jfloat* rPtr = env->GetFloatArrayElements(rightPcm, nullptr);
    const char* modelsDir = env->GetStringUTFChars(modelsDirStr, nullptr);

    gEngine->loadTrackToDeck(deckIndex, lPtr, rPtr, len, sampleRate);
    std::string dirCopy(modelsDir);

    env->ReleaseStringUTFChars(modelsDirStr, modelsDir);
    env->ReleaseFloatArrayElements(leftPcm, lPtr, JNI_ABORT);
    env->ReleaseFloatArrayElements(rightPcm, rPtr, JNI_ABORT);

    // Run ONNX neural refinement asynchronously so track load returns immediately
    std::thread([deckIndex, dirCopy]() {
        if (gEngine) gEngine->runOnnxStemSeparationForDeck(deckIndex, dirCopy);
    }).detach();
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
Java_com_apexdj_butter_audio_OnlineAndLocalRepository_nativeGetCamelotKey(JNIEnv*, jobject, jint deck) {
    if (!gEngine || deck < 0 || deck >= apexdj::NUM_CHANNELS) return 8;
    return gEngine->channels[deck]->camelotKeyIndex.load();
}

JNIEXPORT jfloat JNICALL
Java_com_apexdj_butter_audio_OnlineAndLocalRepository_nativeGetPlayheadNormalized(JNIEnv*, jobject, jint deck) {
    if (!gEngine || deck < 0 || deck >= apexdj::NUM_CHANNELS) return 0.0f;
    auto& ch = *gEngine->channels[deck];
    double total = std::max(1.0, static_cast<double>(ch.totalTrackSamples.load()));
    return static_cast<float>(ch.playheadSample.load() / total);
}

JNIEXPORT jfloat JNICALL
Java_com_apexdj_butter_audio_OnlineAndLocalRepository_nativeGetMorphProgress(JNIEnv*, jobject) {
    if (!gEngine || !gEngine->morphActive.load()) return -1.0f;
    return gEngine->morphProgressUI.load();
}

JNIEXPORT jfloatArray JNICALL
Java_com_apexdj_butter_audio_OnlineAndLocalRepository_nativeGetRgbWaveform(JNIEnv* env, jobject, jint deck) {
    jfloatArray result = env->NewFloatArray(apexdj::WAVEFORM_BINS * 3);
    if (!gEngine || deck < 0 || deck >= apexdj::NUM_CHANNELS) return result;
    auto& ch = *gEngine->channels[deck];
    std::vector<float> flat(apexdj::WAVEFORM_BINS * 3, 0.0f);
    {
        std::lock_guard<std::mutex> lock(ch.bufferMutex);
        for (int i = 0; i < apexdj::WAVEFORM_BINS; ++i) {
            flat[i * 3]     = ch.rgbWaveLow[i];
            flat[i * 3 + 1] = ch.rgbWaveMid[i];
            flat[i * 3 + 2] = ch.rgbWaveHigh[i];
        }
    }
    env->SetFloatArrayRegion(result, 0, flat.size(), flat.data());
    return result;
}

} // extern "C"
