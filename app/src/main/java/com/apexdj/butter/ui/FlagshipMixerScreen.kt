package com.apexdj.butter.ui

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
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.apexdj.butter.audio.OnlineAndLocalRepository
import com.apexdj.butter.audio.SongTrack
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

@Composable
fun FlagshipMixerScreen(repo: OnlineAndLocalRepository) {
    val scope = rememberCoroutineScope()

    var themeMode by remember { mutableStateOf(AppThemeMode.DARK) }
    val palette = getThemePalette(themeMode)

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
    var statusBanner by remember { mutableStateOf("5000W PA Bit-Pure Engine • 0-Vocal BGM Deck • 0ms Load Ready") }
    var brainTelemetry by remember { mutableStateOf("5000W PA Studio Engine Ready") }

    var neuralAutoMix by remember { mutableStateOf(false) }
    var isRecordingWav by remember { mutableStateOf(false) }

    var crossfader by remember { mutableFloatStateOf(-1f) }
    var bgmDeckGain by remember { mutableFloatStateOf(0.38f) }
    var neveDrive by remember { mutableFloatStateOf(0.15f) }
    var isoLow by remember { mutableFloatStateOf(0f) }
    var isoMid by remember { mutableFloatStateOf(0f) }
    var isoHigh by remember { mutableFloatStateOf(0f) }

    var vuDeckA by remember { mutableFloatStateOf(0f) }
    var vuDeckB by remember { mutableFloatStateOf(0f) }
    var vuDeckBgm by remember { mutableFloatStateOf(0f) }
    var bpmDeckA by remember { mutableFloatStateOf(124f) }
    var bpmDeckB by remember { mutableFloatStateOf(124f) }
    var bpmDeckBgm by remember { mutableFloatStateOf(124f) }
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
        // Top Command Bar
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
                    text = "APEX-DJ V15 // 5000W PA STUDIO 3-DECK WORKSTATION",
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

                Button(
                    onClick = {
                        neuralAutoMix = !neuralAutoMix
                        repo.nativeSetNeuralAutoMix(neuralAutoMix)
                        statusBanner = if (neuralAutoMix) {
                            "5000W PA Auto-Mix ON: Clean Unidirectional Bass-Swap + 0-Vocal BGM"
                        } else {
                            "Studio Auto-Mix OFF (Manual Control)"
                        }
                    },
                    modifier = Modifier.height(26.dp),
                    contentPadding = PaddingValues(horizontal = 10.dp, vertical = 0.dp),
                    colors = ButtonDefaults.buttonColors(
                        containerColor = if (neuralAutoMix) Color(0xFFF50057) else Color(0xFF37474F)
                    )
                ) {
                    Text(
                        text = if (neuralAutoMix) "AUTO-MIX: ON" else "AUTO-MIX: OFF",
                        color = Color.White,
                        fontSize = 8.sp,
                        fontWeight = FontWeight.Bold,
                        fontFamily = FontFamily.Monospace
                    )
                }
            }
        }

        Spacer(modifier = Modifier.height(4.dp))

        // 3-Deck Booth: Deck A + Background Deck (0-Vocal Instrumental BGM + Vocal Switch) + Deck B
        Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(4.dp)) {
            ThreeDeckChannelStrip(
                deckIndex = 0,
                deckBadge = "DECK A (LEAD 1)",
                defaultVocalOn = true,
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
                deckBadge = "BGM DECK (0-VOCAL MUSIC)",
                defaultVocalOn = false,
                trackTitle = titleBgm,
                accent = palette.accentBgm,
                palette = palette,
                bpm = bpmDeckBgm,
                playheadNorm = playheadBgm,
                rgbWave = rgbWaveBgm,
                repo = repo,
                modifier = Modifier.weight(0.92f)
            )

            ThreeDeckChannelStrip(
                deckIndex = 1,
                deckBadge = "DECK B (LEAD 2)",
                defaultVocalOn = true,
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

        // Master 5000W PA Bus
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
                RotaryKnob3D("SUB 80Hz", isoLow, true, palette.accentA, palette, Modifier.weight(1f)) {
                    isoLow = it; repo.nativeSetMasterParam(1, it)
                }
                RotaryKnob3D("MID 1kHz", isoMid, true, palette.accentBgm, palette, Modifier.weight(1f)) {
                    isoMid = it; repo.nativeSetMasterParam(2, it)
                }
                RotaryKnob3D("AIR 9kHz", isoHigh, true, palette.accentB, palette, Modifier.weight(1f)) {
                    isoHigh = it; repo.nativeSetMasterParam(3, it)
                }
                RotaryKnob3D("BGM GAIN", bgmDeckGain, false, palette.accentBgm, palette, Modifier.weight(1f)) {
                    bgmDeckGain = it; repo.nativeSetMasterParam(5, it)
                }
                RotaryKnob3D("PA WARMTH", neveDrive, false, Color(0xFFFCC934), palette, Modifier.weight(1f)) {
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

        // In-App Local Music Library + Universal Online Search
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
                    Text("LOCAL SONGS (${localSongs.size})", fontSize = 8.sp, fontWeight = FontWeight.Bold, color = Color.White)
                }

                Button(
                    onClick = { showLocalInAppBrowser = false },
                    modifier = Modifier.height(28.dp),
                    contentPadding = PaddingValues(horizontal = 10.dp, vertical = 0.dp),
                    colors = ButtonDefaults.buttonColors(
                        containerColor = if (!showLocalInAppBrowser) palette.accentB else Color(0xFF263238)
                    )
                ) {
                    Text("ONLINE SEARCH", fontSize = 8.sp, fontWeight = FontWeight.Bold, color = Color.White)
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
                                Text("BGM (0-VOCAL)", fontSize = 8.sp, fontWeight = FontWeight.Bold, color = Color.White)
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

@Composable
fun ThreeDeckChannelStrip(
    deckIndex: Int,
    deckBadge: String,
    defaultVocalOn: Boolean,
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
    var vocalEnabled by remember { mutableStateOf(defaultVocalOn) }
    var fader by remember { mutableFloatStateOf(1f) }
    var eqLo by remember { mutableFloatStateOf(0f) }
    var eqLm by remember { mutableFloatStateOf(0f) }
    var eqHm by remember { mutableFloatStateOf(0f) }
    var eqHi by remember { mutableFloatStateOf(0f) }
    var vcfCutoff by remember { mutableFloatStateOf(0.5f) }
    var tempoRatio by remember { mutableFloatStateOf(1f) }

    var stemDrums by remember { mutableFloatStateOf(1f) }
    var stemBass by remember { mutableFloatStateOf(1f) }
    var stemOther by remember { mutableFloatStateOf(1f) }
    var stemVocals by remember { mutableFloatStateOf(if (defaultVocalOn) 1f else 0f) }

    LaunchedEffect(Unit) {
        while (true) {
            playing = repo.nativeGetDeckPlayState(deckIndex)
            vocalEnabled = repo.nativeGetVocalSwitch(deckIndex)
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
                    fontSize = 8.sp,
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
            }

            Row(horizontalArrangement = Arrangement.spacedBy(2.dp)) {
                // Dedicated VOCAL ON / OFF Switch (Default OFF [0 Vocals] on Background Deck!)
                Button(
                    onClick = {
                        val nextVox = !vocalEnabled
                        vocalEnabled = nextVox
                        stemVocals = if (nextVox) 1f else 0f
                        repo.nativeSetVocalSwitch(deckIndex, nextVox)
                    },
                    modifier = Modifier.height(24.dp),
                    contentPadding = PaddingValues(horizontal = 5.dp, vertical = 0.dp),
                    colors = ButtonDefaults.buttonColors(
                        containerColor = if (vocalEnabled) Color(0xFFD81B60) else Color(0xFF1E293B)
                    )
                ) {
                    Text(
                        text = if (vocalEnabled) "VOCAL: ON" else "0-VOCAL",
                        color = Color.White,
                        fontSize = 7.sp,
                        fontWeight = FontWeight.Bold,
                        fontFamily = FontFamily.Monospace
                    )
                }

                Button(
                    onClick = {
                        val next = !playing
                        playing = next
                        repo.nativeSetDeckPlayState(deckIndex, next)
                    },
                    modifier = Modifier.height(24.dp),
                    contentPadding = PaddingValues(horizontal = 7.dp, vertical = 0.dp),
                    colors = ButtonDefaults.buttonColors(
                        containerColor = if (playing) Color(0xFF00C853) else Color(0xFF37474F)
                    )
                ) {
                    Text(
                        text = if (playing) "PAUSE" else "PLAY",
                        color = Color.White,
                        fontSize = 7.sp,
                        fontWeight = FontWeight.Bold
                    )
                }
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
                    .height(28.dp)
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
                Text(
                    text = "%.1f BPM • JOG".format(bpm * tempoRatio),
                    color = accent,
                    fontSize = 7.sp,
                    fontWeight = FontWeight.Bold,
                    fontFamily = FontFamily.Monospace
                )
            }
            RotaryKnob3D("PITCH", (tempoRatio - 1f) * 4f, true, accent, palette) {
                tempoRatio = (1f + it * 0.15f).coerceIn(0.85f, 1.15f)
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
            RotaryKnob3D("MUSIC", stemOther, false, Color(0xFF69F0AE), palette, Modifier.weight(1f)) {
                stemOther = it; repo.nativeSetChannelParam(deckIndex, 11, it)
            }
            RotaryKnob3D("VOCAL", stemVocals, false, Color(0xFFF50057), palette, Modifier.weight(1f)) {
                stemVocals = it
                vocalEnabled = it > 0.05f
                repo.nativeSetVocalSwitch(deckIndex, vocalEnabled)
                repo.nativeSetChannelParam(deckIndex, 12, it)
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
            RotaryKnob3D("FILTER", vcfCutoff, false, Color(0xFFFCC934), palette, Modifier.weight(1f)) {
                vcfCutoff = it; repo.nativeSetChannelParam(deckIndex, 7, it)
            }
            RotaryKnob3D("VOL", fader, false, Color(0xFF00C853), palette, Modifier.weight(1f)) {
                fader = it; repo.nativeSetChannelParam(deckIndex, 0, it)
            }
        }
    }
}
