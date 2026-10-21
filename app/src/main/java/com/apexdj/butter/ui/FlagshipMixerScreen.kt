package com.apexdj.butter.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.apexdj.butter.audio.ObbDownloadState
import com.apexdj.butter.audio.OnlineAndLocalRepository
import com.apexdj.butter.audio.SongTrack
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

@Composable
fun FlagshipMixerScreen(repo: OnlineAndLocalRepository) {
    val scope = rememberCoroutineScope()

    var themeMode by remember { mutableStateOf(AppThemeMode.DARK) }
    val palette = getThemePalette(themeMode)

    val obbMounted by repo.obbBrainMounted.collectAsState()
    val obbProgress by repo.obbManager.progressFlow.collectAsState()
    var showObbVaultPanel by remember { mutableStateOf(false) }

    val rgbWaveA by repo.waveDeckA.collectAsState()
    val rgbWaveB by repo.waveDeckB.collectAsState()
    val rgbWaveBgm by repo.waveDeckBgm.collectAsState()

    val titleA by repo.titleDeckA.collectAsState()
    val titleB by repo.titleDeckB.collectAsState()
    val titleBgm by repo.titleDeckBgm.collectAsState()
    val localSongs by repo.localDeviceSongs.collectAsState()

    var searchQuery by remember { mutableStateOf("") }
    var onlineSearchResults by remember { mutableStateOf<List<SongTrack>>(emptyList()) }
    var showLocalInAppBrowser by remember { mutableStateOf(true) }
    var statusBanner by remember { mutableStateOf("1.50 GB OBB Neural Brain Active • 0ms Instant Deck Load Ready") }
    var brainTelemetry by remember { mutableStateOf("OBB Neural Brain Standby") }

    var neuralAutoMix by remember { mutableStateOf(false) }
    var isRecordingWav by remember { mutableStateOf(false) }

    var crossfader by remember { mutableFloatStateOf(0f) }
    var bgmDeckGain by remember { mutableFloatStateOf(0.45f) }
    var neveDrive by remember { mutableFloatStateOf(0.65f) }
    var isoLow by remember { mutableFloatStateOf(0f) }
    var isoMid by remember { mutableFloatStateOf(0f) }
    var isoHigh by remember { mutableFloatStateOf(0f) }

    var vuDeckA by remember { mutableFloatStateOf(0f) }
    var vuDeckB by remember { mutableFloatStateOf(0f) }
    var vuDeckBgm by remember { mutableFloatStateOf(0f) }
    var bpmDeckA by remember { mutableFloatStateOf(128f) }
    var bpmDeckB by remember { mutableFloatStateOf(128f) }
    var bpmDeckBgm by remember { mutableFloatStateOf(128f) }
    var playheadA by remember { mutableFloatStateOf(0f) }
    var playheadB by remember { mutableFloatStateOf(0f) }
    var playheadBgm by remember { mutableFloatStateOf(0f) }

    LaunchedEffect(Unit) {
        repo.obbManager.scanAndVerifyExistingObb()
        repo.refreshInAppLocalSongs()
        while (true) {
            vuDeckA = repo.nativeGetChannelVu(0)
            vuDeckB = repo.nativeGetChannelVu(1)
            vuDeckBgm = repo.nativeGetChannelVu(2)
            bpmDeckA = repo.nativeGetDetectedBpm(0)
            bpmDeckB = repo.nativeGetDetectedBpm(1)
            bpmDeckBgm = repo.nativeGetDetectedBpm(2)
            playheadA = repo.nativeGetPlayheadNormalized(0)
            playheadB = repo.nativeGetPlayheadNormalized(1)
            playheadBgm = repo.nativeGetPlayheadNormalized(2)
            brainTelemetry = repo.nativeGetBrainTelemetry()
            isRecordingWav = repo.nativeIsMasterRecording()
            delay(33)
        }
    }

    // HARD GATEKEEPER: App will NOT work without the 1.50 GB OBB file!
    if (!obbMounted || showObbVaultPanel) {
        ObbGatekeeperScreen(
            obbProgress = obbProgress,
            obbMounted = obbMounted,
            palette = palette,
            onStartOrResume = { repo.obbManager.startOrResumeDownload() },
            onPause = { repo.obbManager.pauseDownload() },
            onRescan = { scope.launch { repo.obbManager.scanAndVerifyExistingObb() } },
            onEnterMixer = { if (obbMounted) showObbVaultPanel = false }
        )
        return
    }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .background(palette.bgPrimary)
            .statusBarsPadding()
            .navigationBarsPadding()
            .displayCutoutPadding()
            .padding(horizontal = 6.dp, vertical = 4.dp)
            .verticalScroll(rememberScrollState())
    ) {
        // Top Command Bar: Title, Neural Telemetry, REC Button, OBB Vault Badge, Theme Switcher, and Autonomous Auto-Mix
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .background(palette.cardSurface, RoundedCornerShape(8.dp))
                .border(1.dp, palette.cardBorder, RoundedCornerShape(8.dp))
                .padding(horizontal = 8.dp, vertical = 5.dp),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Column(modifier = Modifier.weight(1f)) {
                Text(
                    text = "APEX-DJ V12 // 3-DECK OBB NEURAL BRAIN WORKSTATION",
                    color = palette.accentA,
                    fontFamily = FontFamily.Monospace,
                    fontWeight = FontWeight.Bold,
                    fontSize = 11.sp,
                    maxLines = 1
                )
                Text(
                    text = "$statusBanner • $brainTelemetry",
                    color = palette.textSecondary,
                    fontSize = 8.sp,
                    fontFamily = FontFamily.Monospace,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis
                )
            }

            Row(horizontalArrangement = Arrangement.spacedBy(4.dp), verticalAlignment = Alignment.CenterVertically) {
                // Live Master WAV Recorder Button
                Button(
                    onClick = {
                        val (recState, msg) = repo.toggleMasterWavRecording()
                        isRecordingWav = recState
                        statusBanner = msg
                    },
                    modifier = Modifier.height(26.dp),
                    contentPadding = PaddingValues(horizontal = 8.dp, vertical = 0.dp),
                    colors = ButtonDefaults.buttonColors(
                        containerColor = if (isRecordingWav) Color(0xFFFF1744) else Color(0xFF263238)
                    )
                ) {
                    Text(
                        text = if (isRecordingWav) "● REC SAVING" else "● REC MIX",
                        color = Color.White,
                        fontSize = 8.sp,
                        fontWeight = FontWeight.Bold,
                        fontFamily = FontFamily.Monospace
                    )
                }

                // OBB Status Button (Shows Already Downloaded 1.50 GB)
                Button(
                    onClick = { showObbVaultPanel = true },
                    modifier = Modifier.height(26.dp),
                    contentPadding = PaddingValues(horizontal = 8.dp, vertical = 0.dp),
                    colors = ButtonDefaults.buttonColors(containerColor = Color(0xFF1B5E20))
                ) {
                    Text(
                        text = "OBB 1.5GB ✓",
                        color = Color.White,
                        fontSize = 8.sp,
                        fontWeight = FontWeight.Bold,
                        fontFamily = FontFamily.Monospace
                    )
                }

                // 4-Theme Cycle Button (Dark -> Light -> Glassmorphism -> Neon Cyan)
                Button(
                    onClick = {
                        val all = AppThemeMode.values()
                        themeMode = all[(themeMode.ordinal + 1) % all.size]
                    },
                    modifier = Modifier.height(26.dp),
                    contentPadding = PaddingValues(horizontal = 8.dp, vertical = 0.dp),
                    colors = ButtonDefaults.buttonColors(containerColor = palette.accentBgm)
                ) {
                    Text(
                        text = "THEME: ${themeMode.title}",
                        color = Color.White,
                        fontSize = 8.sp,
                        fontWeight = FontWeight.Bold,
                        fontFamily = FontFamily.Monospace
                    )
                }

                // 100% Autonomous OBB Neural Brain Auto-Mix Toggle (Zero manual transition type selection needed!)
                Button(
                    onClick = {
                        neuralAutoMix = !neuralAutoMix
                        repo.nativeSetNeuralAutoMix(neuralAutoMix)
                        statusBanner = if (neuralAutoMix) {
                            "OBB Neural Auto-Mix ON: Brain is autonomously mixing Deck A + Deck B + Background Deck"
                        } else {
                            "OBB Neural Auto-Mix OFF"
                        }
                    },
                    modifier = Modifier.height(26.dp),
                    contentPadding = PaddingValues(horizontal = 10.dp, vertical = 0.dp),
                    colors = ButtonDefaults.buttonColors(
                        containerColor = if (neuralAutoMix) Color(0xFFF50057) else Color(0xFF37474F)
                    )
                ) {
                    Text(
                        text = if (neuralAutoMix) "AUTO-MIX: ON (BRAIN)" else "AUTO-MIX: OFF",
                        color = Color.White,
                        fontSize = 8.sp,
                        fontWeight = FontWeight.Bold,
                        fontFamily = FontFamily.Monospace
                    )
                }
            }
        }

        Spacer(modifier = Modifier.height(4.dp))

        // 3-DECK PROFESSIONAL BOOTH: DECK A (Lead 1) + BACKGROUND DECK (Continuous BGM/Groove) + DECK B (Lead 2)
        Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(4.dp)) {
            ThreeDeckChannelStrip(
                deckIndex = 0,
                deckBadge = "DECK A (LEAD 1)",
                trackTitle = titleA,
                accent = palette.accentA,
                palette = palette,
                bpm = bpmDeckA,
                playheadNorm = playheadA,
                rgbWave = rgbWaveA,
                repo = repo,
                modifier = Modifier.weight(1f)
            )

            ThreeDeckChannelStrip(
                deckIndex = 2,
                deckBadge = "BACKGROUND DECK (BGM)",
                trackTitle = titleBgm,
                accent = palette.accentBgm,
                palette = palette,
                bpm = bpmDeckBgm,
                playheadNorm = playheadBgm,
                rgbWave = rgbWaveBgm,
                repo = repo,
                modifier = Modifier.weight(0.88f)
            )

            ThreeDeckChannelStrip(
                deckIndex = 1,
                deckBadge = "DECK B (LEAD 2)",
                trackTitle = titleB,
                accent = palette.accentB,
                palette = palette,
                bpm = bpmDeckB,
                playheadNorm = playheadB,
                rgbWave = rgbWaveB,
                repo = repo,
                modifier = Modifier.weight(1f)
            )
        }

        Spacer(modifier = Modifier.height(4.dp))

        // Master Isolator, Background Deck Level, Spectrum Analyzer & Crossfader Bar
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .background(palette.cardSurface, RoundedCornerShape(8.dp))
                .border(1.dp, palette.cardBorder, RoundedCornerShape(8.dp))
                .padding(6.dp)
        ) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(4.dp)
            ) {
                RotaryKnob3D("ISO LOW", isoLow, true, palette.accentA, palette, Modifier.weight(1f)) {
                    isoLow = it; repo.nativeSetMasterParam(1, it)
                }
                RotaryKnob3D("ISO MID", isoMid, true, palette.accentBgm, palette, Modifier.weight(1f)) {
                    isoMid = it; repo.nativeSetMasterParam(2, it)
                }
                RotaryKnob3D("ISO HIGH", isoHigh, true, palette.accentB, palette, Modifier.weight(1f)) {
                    isoHigh = it; repo.nativeSetMasterParam(3, it)
                }
                RotaryKnob3D("BGM LEVEL", bgmDeckGain, false, palette.accentBgm, palette, Modifier.weight(1f)) {
                    bgmDeckGain = it; repo.nativeSetMasterParam(5, it)
                }
                RotaryKnob3D("NEVE SILK", neveDrive, false, Color(0xFFFCC934), palette, Modifier.weight(1f)) {
                    neveDrive = it; repo.nativeSetMasterParam(4, it)
                }
                MasterSpectrumAnalyzer(
                    vuA = vuDeckA,
                    vuB = vuDeckB,
                    vuBgm = vuDeckBgm,
                    palette = palette,
                    modifier = Modifier.weight(2.2f).height(34.dp)
                )
            }

            Row(modifier = Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                Text("DECK A", color = palette.accentA, fontSize = 8.sp, fontWeight = FontWeight.Bold, fontFamily = FontFamily.Monospace)
                Slider(
                    value = crossfader,
                    onValueChange = { crossfader = it; repo.nativeSetMasterParam(0, it) },
                    valueRange = -1f..1f,
                    modifier = Modifier.weight(1f).height(24.dp)
                )
                Text("DECK B", color = palette.accentB, fontSize = 8.sp, fontWeight = FontWeight.Bold, fontFamily = FontFamily.Monospace)
            }
        }

        Spacer(modifier = Modifier.height(4.dp))

        // Unified 0ms Song Browser: Any Online Song Search + In-App Local Device Music Library (Never opens external apps!)
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .background(palette.cardSurface, RoundedCornerShape(8.dp))
                .border(1.dp, palette.cardBorder, RoundedCornerShape(8.dp))
                .padding(6.dp)
        ) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(4.dp)
            ) {
                Button(
                    onClick = {
                        showLocalInAppBrowser = true
                        scope.launch { repo.refreshInAppLocalSongs() }
                    },
                    modifier = Modifier.height(28.dp),
                    contentPadding = PaddingValues(horizontal = 10.dp, vertical = 0.dp),
                    colors = ButtonDefaults.buttonColors(
                        containerColor = if (showLocalInAppBrowser) palette.accentA else Color(0xFF263238)
                    )
                ) {
                    Text("IN-APP LOCAL SONGS (${localSongs.size})", fontSize = 8.sp, fontWeight = FontWeight.Bold, color = Color.White)
                }

                Button(
                    onClick = { showLocalInAppBrowser = false },
                    modifier = Modifier.height(28.dp),
                    contentPadding = PaddingValues(horizontal = 10.dp, vertical = 0.dp),
                    colors = ButtonDefaults.buttonColors(
                        containerColor = if (!showLocalInAppBrowser) palette.accentB else Color(0xFF263238)
                    )
                ) {
                    Text("ONLINE SEARCH (ANY SONG)", fontSize = 8.sp, fontWeight = FontWeight.Bold, color = Color.White)
                }

                OutlinedTextField(
                    value = searchQuery,
                    onValueChange = { searchQuery = it },
                    placeholder = {
                        Text(
                            text = "Search any song, movie, artist, or remix...",
                            color = palette.textSecondary,
                            fontSize = 9.sp
                        )
                    },
                    modifier = Modifier.weight(1f).height(46.dp),
                    singleLine = true
                )

                Button(
                    onClick = {
                        showLocalInAppBrowser = false
                        scope.launch {
                            statusBanner = "Searching all songs for '$searchQuery'..."
                            onlineSearchResults = repo.searchAnyOnlineSongs(searchQuery)
                            statusBanner = "Found ${onlineSearchResults.size} songs."
                        }
                    },
                    modifier = Modifier.height(34.dp),
                    colors = ButtonDefaults.buttonColors(containerColor = palette.accentA)
                ) {
                    Text("SEARCH", fontSize = 9.sp, fontWeight = FontWeight.Bold, color = Color.White)
                }
            }

            Spacer(modifier = Modifier.height(4.dp))

            val displayedSongs = if (showLocalInAppBrowser) {
                if (searchQuery.isBlank()) localSongs else localSongs.filter {
                    it.title.contains(searchQuery, true) || it.artist.contains(searchQuery, true)
                }
            } else {
                onlineSearchResults
            }

            LazyColumn(
                modifier = Modifier
                    .fillMaxWidth()
                    .height(175.dp)
                    .background(palette.knobSurface, RoundedCornerShape(6.dp))
                    .padding(4.dp)
            ) {
                if (displayedSongs.isEmpty()) {
                    item {
                        Text(
                            text = if (showLocalInAppBrowser) {
                                "No local audio files found yet. Place MP3/WAV/FLAC songs on your phone or switch to ONLINE SEARCH."
                            } else {
                                "Type any song name above and tap SEARCH."
                            },
                            color = palette.textSecondary,
                            fontSize = 10.sp,
                            modifier = Modifier.padding(8.dp)
                        )
                    }
                } else {
                    items(displayedSongs) { song ->
                        Row(
                            modifier = Modifier
                                .fillMaxWidth()
                                .padding(vertical = 2.dp),
                            horizontalArrangement = Arrangement.SpaceBetween,
                            verticalAlignment = Alignment.CenterVertically
                        ) {
                            Column(modifier = Modifier.weight(1f)) {
                                Text(
                                    text = song.title,
                                    color = palette.textPrimary,
                                    fontSize = 10.sp,
                                    fontWeight = FontWeight.Bold,
                                    maxLines = 1,
                                    overflow = TextOverflow.Ellipsis
                                )
                                Text(
                                    text = "${song.artist} • ${song.sourceBadge} • ${song.durationSec}s",
                                    color = palette.textSecondary,
                                    fontSize = 8.sp,
                                    fontFamily = FontFamily.Monospace,
                                    maxLines = 1
                                )
                            }
                            Row(horizontalArrangement = Arrangement.spacedBy(3.dp)) {
                                Button(
                                    onClick = {
                                        repo.loadTrackIntoDeckInstant(0, song, autoPlay = true) { statusBanner = it }
                                    },
                                    modifier = Modifier.height(24.dp),
                                    contentPadding = PaddingValues(horizontal = 8.dp, vertical = 0.dp),
                                    colors = ButtonDefaults.buttonColors(containerColor = palette.accentA)
                                ) {
                                    Text("DECK A", fontSize = 8.sp, fontWeight = FontWeight.Bold, color = Color.White)
                                }
                                Button(
                                    onClick = {
                                        repo.loadTrackIntoDeckInstant(2, song, autoPlay = true) { statusBanner = it }
                                    },
                                    modifier = Modifier.height(24.dp),
                                    contentPadding = PaddingValues(horizontal = 8.dp, vertical = 0.dp),
                                    colors = ButtonDefaults.buttonColors(containerColor = palette.accentBgm)
                                ) {
                                    Text("BGM DECK", fontSize = 8.sp, fontWeight = FontWeight.Bold, color = Color.White)
                                }
                                Button(
                                    onClick = {
                                        repo.loadTrackIntoDeckInstant(1, song, autoPlay = true) { statusBanner = it }
                                    },
                                    modifier = Modifier.height(24.dp),
                                    contentPadding = PaddingValues(horizontal = 8.dp, vertical = 0.dp),
                                    colors = ButtonDefaults.buttonColors(containerColor = palette.accentB)
                                ) {
                                    Text("DECK B", fontSize = 8.sp, fontWeight = FontWeight.Bold, color = Color.White)
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

@Composable
fun ThreeDeckChannelStrip(
    deckIndex: Int,
    deckBadge: String,
    trackTitle: String,
    accent: Color,
    palette: ApexThemePalette,
    bpm: Float,
    playheadNorm: Float,
    rgbWave: FloatArray,
    repo: OnlineAndLocalRepository,
    modifier: Modifier = Modifier
) {
    var playing by remember { mutableStateOf(false) }
    var fader by remember { mutableFloatStateOf(1f) }
    var eqLo by remember { mutableFloatStateOf(0f) }
    var eqLm by remember { mutableFloatStateOf(0f) }
    var eqHm by remember { mutableFloatStateOf(0f) }
    var eqHi by remember { mutableFloatStateOf(0f) }
    var vcfCutoff by remember { mutableFloatStateOf(0.5f) }
    var vcfCrunch by remember { mutableFloatStateOf(0f) }
    var colorFx by remember { mutableFloatStateOf(0f) }
    var tempoRatio by remember { mutableFloatStateOf(1f) }

    var stemDrums by remember { mutableFloatStateOf(1f) }
    var stemBass by remember { mutableFloatStateOf(1f) }
    var stemOther by remember { mutableFloatStateOf(1f) }
    var stemVocals by remember { mutableFloatStateOf(1f) }

    LaunchedEffect(Unit) {
        while (true) {
            playing = repo.nativeGetDeckPlayState(deckIndex)
            delay(100)
        }
    }

    Column(
        modifier = modifier
            .background(palette.cardSurface, RoundedCornerShape(8.dp))
            .border(1.dp, accent.copy(alpha = 0.6f), RoundedCornerShape(8.dp))
            .padding(5.dp)
    ) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Column(modifier = Modifier.weight(1f)) {
                Text(
                    text = deckBadge,
                    color = accent,
                    fontWeight = FontWeight.Bold,
                    fontSize = 9.sp,
                    fontFamily = FontFamily.Monospace,
                    maxLines = 1
                )
                Text(
                    text = trackTitle,
                    color = palette.textPrimary,
                    fontSize = 8.sp,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis
                )
                Text(
                    text = "%.1f BPM".format(bpm * tempoRatio),
                    color = palette.textSecondary,
                    fontSize = 7.sp,
                    fontFamily = FontFamily.Monospace
                )
            }
            Button(
                onClick = {
                    val next = !playing
                    playing = next
                    repo.nativeSetDeckPlayState(deckIndex, next)
                },
                modifier = Modifier.height(25.dp),
                contentPadding = PaddingValues(horizontal = 8.dp, vertical = 0.dp),
                colors = ButtonDefaults.buttonColors(
                    containerColor = if (playing) Color(0xFF00C853) else Color(0xFF37474F)
                )
            ) {
                Text(
                    text = if (playing) "PAUSE" else "PLAY",
                    color = Color.White,
                    fontSize = 8.sp,
                    fontWeight = FontWeight.Bold
                )
            }
        }

        Spacer(modifier = Modifier.height(2.dp))

        RgbScrollingWaveformView(
            rgbFlatArray = rgbWave,
            playheadNormalized = playheadNorm,
            accentColor = accent,
            palette = palette,
            modifier = Modifier.fillMaxWidth().height(32.dp),
            onSeek = { norm -> repo.nativeSeekNormalized(deckIndex, norm) }
        )

        Spacer(modifier = Modifier.height(2.dp))

        Row(
            modifier = Modifier.fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(3.dp)
        ) {
            Box(
                modifier = Modifier
                    .weight(1f)
                    .height(30.dp)
                    .background(palette.knobSurface, CircleShape)
                    .border(1.dp, accent, CircleShape)
                    .pointerInput(Unit) {
                        detectDragGestures(
                            onDragStart = { repo.nativeSetChannelParam(deckIndex, 18, 1.2f) },
                            onDragEnd = { repo.nativeSetChannelParam(deckIndex, 18, 0f) },
                            onDragCancel = { repo.nativeSetChannelParam(deckIndex, 18, 0f) }
                        ) { _, dragAmount ->
                            val vel = (dragAmount.x * 0.05f).coerceIn(-3f, 3f)
                            repo.nativeSetChannelParam(deckIndex, 18, vel)
                        }
                    },
                contentAlignment = Alignment.Center
            ) {
                Text("JOG SCRATCH", color = accent, fontSize = 7.sp, fontWeight = FontWeight.Bold, fontFamily = FontFamily.Monospace)
            }
            RotaryKnob3D("TEMPO", (tempoRatio - 1f) * 2f, true, accent, palette) {
                tempoRatio = (1f + it * 0.25f).coerceIn(0.75f, 1.25f)
                repo.nativeSetChannelParam(deckIndex, 13, tempoRatio)
            }
        }

        Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(1.dp)) {
            RotaryKnob3D("DRUM", stemDrums, false, Color(0xFF00E5FF), palette, Modifier.weight(1f)) {
                stemDrums = it; repo.nativeSetChannelParam(deckIndex, 9, it)
            }
            RotaryKnob3D("BASS", stemBass, false, Color(0xFFFF5252), palette, Modifier.weight(1f)) {
                stemBass = it; repo.nativeSetChannelParam(deckIndex, 10, it)
            }
            RotaryKnob3D("HARM", stemOther, false, Color(0xFF69F0AE), palette, Modifier.weight(1f)) {
                stemOther = it; repo.nativeSetChannelParam(deckIndex, 11, it)
            }
            RotaryKnob3D("VOCAL", stemVocals, false, Color(0xFFF50057), palette, Modifier.weight(1f)) {
                stemVocals = it; repo.nativeSetChannelParam(deckIndex, 12, it)
            }
        }

        Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(1.dp)) {
            RotaryKnob3D("LO", eqLo, true, accent, palette, Modifier.weight(1f)) {
                eqLo = it; repo.nativeSetChannelParam(deckIndex, 1, it)
            }
            RotaryKnob3D("LM", eqLm, true, accent, palette, Modifier.weight(1f)) {
                eqLm = it; repo.nativeSetChannelParam(deckIndex, 2, it)
            }
            RotaryKnob3D("HM", eqHm, true, accent, palette, Modifier.weight(1f)) {
                eqHm = it; repo.nativeSetChannelParam(deckIndex, 3, it)
            }
            RotaryKnob3D("HI", eqHi, true, accent, palette, Modifier.weight(1f)) {
                eqHi = it; repo.nativeSetChannelParam(deckIndex, 4, it)
            }
        }

        Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(1.dp)) {
            RotaryKnob3D("VCF", vcfCutoff, false, Color(0xFFFCC934), palette, Modifier.weight(1f)) {
                vcfCutoff = it; repo.nativeSetChannelParam(deckIndex, 7, it)
            }
            RotaryKnob3D("CRCH", vcfCrunch, false, Color(0xFFFCC934), palette, Modifier.weight(1f)) {
                vcfCrunch = it; repo.nativeSetChannelParam(deckIndex, 8, it)
            }
            RotaryKnob3D("FX", colorFx, true, Color(0xFFF50057), palette, Modifier.weight(1f)) {
                colorFx = it; repo.nativeSetChannelParam(deckIndex, 6, it)
            }
            RotaryKnob3D("VOL", fader, false, Color(0xFF00C853), palette, Modifier.weight(1f)) {
                fader = it; repo.nativeSetChannelParam(deckIndex, 0, it)
            }
        }

        Spacer(modifier = Modifier.height(2.dp))

        Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(2.dp)) {
            for (c in 0 until 3) {
                Button(
                    onClick = { repo.nativeTriggerHotCue(deckIndex, c) },
                    modifier = Modifier.weight(1f).height(18.dp),
                    contentPadding = PaddingValues(0.dp),
                    colors = ButtonDefaults.buttonColors(containerColor = Color(0xFF263238))
                ) {
                    Text("C${c + 1}", fontSize = 7.sp, color = Color.White)
                }
            }
            Button(
                onClick = { repo.nativeToggleBeatLoop(deckIndex, 4.0) },
                modifier = Modifier.weight(1.2f).height(18.dp),
                contentPadding = PaddingValues(0.dp),
                colors = ButtonDefaults.buttonColors(containerColor = Color(0xFF1B5E20))
            ) {
                Text("LOOP", fontSize = 7.sp, color = Color.White)
            }
        }
    }
}

@Composable
fun ObbGatekeeperScreen(
    obbProgress: com.apexdj.butter.audio.ObbVaultProgress,
    obbMounted: Boolean,
    palette: ApexThemePalette,
    onStartOrResume: () -> Unit,
    onPause: () -> Unit,
    onRescan: () -> Unit,
    onEnterMixer: () -> Unit
) {
    Box(
        modifier = Modifier
            .fillMaxSize()
            .background(palette.bgPrimary)
            .statusBarsPadding()
            .navigationBarsPadding()
            .padding(16.dp),
        contentAlignment = Alignment.Center
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth(0.88f)
                .background(palette.cardSurface, RoundedCornerShape(12.dp))
                .border(2.dp, palette.accentA, RoundedCornerShape(12.dp))
                .padding(18.dp),
            horizontalAlignment = Alignment.CenterHorizontally
        ) {
            Text(
                text = "APEX-DJ V12 // MANDATORY 1.50 GB NEURAL BRAIN OBB VAULT",
                color = palette.accentA,
                fontFamily = FontFamily.Monospace,
                fontWeight = FontWeight.Bold,
                fontSize = 14.sp
            )
            Spacer(modifier = Modifier.height(6.dp))
            Text(
                text = if (obbProgress.state == ObbDownloadState.ALREADY_DOWNLOADED || obbMounted) {
                    "STATUS: ALREADY DOWNLOADED & VERIFIED (1.50 GB OBB BRAIN ACTIVE)"
                } else {
                    "App is locked until the full 1.50 GB Neural Brain OBB file is downloaded or detected."
                },
                color = if (obbMounted) Color(0xFF00E676) else palette.textPrimary,
                fontWeight = FontWeight.Bold,
                fontSize = 11.sp,
                fontFamily = FontFamily.Monospace
            )

            Spacer(modifier = Modifier.height(10.dp))

            LinearProgressIndicator(
                progress = { (obbProgress.percent / 100f).coerceIn(0f, 1f) },
                modifier = Modifier.fillMaxWidth().height(10.dp),
                color = if (obbMounted) Color(0xFF00E676) else palette.accentA,
                trackColor = Color(0xFF1E293B)
            )

            Spacer(modifier = Modifier.height(6.dp))
            Text(
                text = obbProgress.message,
                color = palette.textSecondary,
                fontSize = 10.sp,
                fontFamily = FontFamily.Monospace
            )

            Spacer(modifier = Modifier.height(14.dp))

            Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                when (obbProgress.state) {
                    ObbDownloadState.ALREADY_DOWNLOADED -> {
                        Button(
                            onClick = onEnterMixer,
                            colors = ButtonDefaults.buttonColors(containerColor = Color(0xFF00C853))
                        ) {
                            Text("ALREADY DOWNLOADED ✓ — LAUNCH 3-DECK MIXER", fontWeight = FontWeight.Bold, fontSize = 11.sp, color = Color.White)
                        }
                    }
                    ObbDownloadState.DOWNLOADING -> {
                        Button(
                            onClick = onPause,
                            colors = ButtonDefaults.buttonColors(containerColor = Color(0xFFFF9100))
                        ) {
                            Text("PAUSE DOWNLOAD", fontWeight = FontWeight.Bold, fontSize = 11.sp, color = Color.White)
                        }
                    }
                    ObbDownloadState.PAUSED -> {
                        Button(
                            onClick = onStartOrResume,
                            colors = ButtonDefaults.buttonColors(containerColor = palette.accentA)
                        ) {
                            Text("PLAY / RESUME DOWNLOAD (${obbProgress.percent}%)", fontWeight = FontWeight.Bold, fontSize = 11.sp, color = Color.White)
                        }
                    }
                    else -> {
                        Button(
                            onClick = onStartOrResume,
                            colors = ButtonDefaults.buttonColors(containerColor = palette.accentA)
                        ) {
                            Text("DOWNLOAD 1.50 GB NEURAL OBB", fontWeight = FontWeight.Bold, fontSize = 11.sp, color = Color.White)
                        }
                    }
                }

                Button(
                    onClick = onRescan,
                    colors = ButtonDefaults.buttonColors(containerColor = Color(0xFF334155))
                ) {
                    Text("SCAN LOCAL OBB", fontWeight = FontWeight.Bold, fontSize = 11.sp, color = Color.White)
                }

                if (obbMounted && obbProgress.state != ObbDownloadState.ALREADY_DOWNLOADED) {
                    Button(
                        onClick = onEnterMixer,
                        colors = ButtonDefaults.buttonColors(containerColor = Color(0xFF00C853))
                    ) {
                        Text("OPEN MIXER", fontWeight = FontWeight.Bold, fontSize = 11.sp, color = Color.White)
                    }
                }
            }
        }
    }
}
