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
    var browserTab by remember { mutableIntStateOf(0) } // 0: Local Songs, 1: 120 Infinity BGM Beats, 2: Online Search
    var statusBanner by remember { mutableStateOf("1st Deck Plays • 2nd Deck Queues Muted & Auto-Transitions (A ⇄ B)") }
    var brainTelemetry by remember { mutableStateOf("Load Deck A or Deck B to Start") }

    var neuralAutoMix by remember { mutableStateOf(true) }
    var isRecordingWav by remember { mutableStateOf(false) }

    var crossfader by remember { mutableFloatStateOf(-1f) }
    var bgmDeckGain by remember { mutableFloatStateOf(0.42f) }
    var activeBgmPreset by remember { mutableIntStateOf(1) }

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
            crossfader = repo.nativeGetCrossfader()
            brainTelemetry = repo.nativeGetBrainTelemetry()
            isRecordingWav = repo.nativeIsMasterRecording()
            delay(33)
        }
    }

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
                    text = "APEX-DJ V16 // 1000-TRANSITION SMART AUTO-MIX + ∞ INFINITY BGM",
                    color = palette.accentA,
                    fontFamily = FontFamily.Monospace,
                    fontWeight = FontWeight.Bold,
                    fontSize = 11.sp,
                    maxLines = 1
                )
                Text(
                    text = "$brainTelemetry • $statusBanner",
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
                        text = if (isRecordingWav) "● REC SAVING" else "● REC",
                        color = Color.White,
                        fontSize = 8.sp,
                        fontWeight = FontWeight.Bold,
                        fontFamily = FontFamily.Monospace
                    )
                }

                Button(
                    onClick = { showObbVaultPanel = true },
                    modifier = Modifier.height(26.dp),
                    contentPadding = PaddingValues(horizontal = 7.dp, vertical = 0.dp),
                    colors = ButtonDefaults.buttonColors(containerColor = Color(0xFF1B5E20))
                ) {
                    Text("OBB ✓", color = Color.White, fontSize = 8.sp, fontWeight = FontWeight.Bold, fontFamily = FontFamily.Monospace)
                }

                Button(
                    onClick = {
                        val all = AppThemeMode.values()
                        themeMode = all[(themeMode.ordinal + 1) % all.size]
                    },
                    modifier = Modifier.height(26.dp),
                    contentPadding = PaddingValues(horizontal = 7.dp, vertical = 0.dp),
                    colors = ButtonDefaults.buttonColors(containerColor = palette.accentBgm)
                ) {
                    Text(themeMode.title, color = Color.White, fontSize = 8.sp, fontWeight = FontWeight.Bold, fontFamily = FontFamily.Monospace)
                }

                Button(
                    onClick = {
                        neuralAutoMix = !neuralAutoMix
                        repo.nativeSetNeuralAutoMix(neuralAutoMix)
                    },
                    modifier = Modifier.height(26.dp),
                    contentPadding = PaddingValues(horizontal = 9.dp, vertical = 0.dp),
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

        // Clean 3-Deck Booth: ONLY Essential Knobs (BASS, FILTER, VOLUME) — All Complex Mixing Handled Internally!
        Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(4.dp)) {
            CleanDeckCard(
                deckIndex = 0,
                deckBadge = "DECK A",
                isBgmDeck = false,
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

            CleanDeckCard(
                deckIndex = 2,
                deckBadge = "∞ BGM DECK (120 BEATS)",
                isBgmDeck = true,
                defaultVocalOn = false,
                trackTitle = titleBgm,
                accent = palette.accentBgm,
                palette = palette,
                bpm = bpmDeckBgm,
                playheadNorm = playheadBgm,
                rgbWave = rgbWaveBgm,
                repo = repo,
                onPrevBgm = {
                    activeBgmPreset = if (activeBgmPreset <= 1) 120 else activeBgmPreset - 1
                    repo.selectInfinityBgmPreset(activeBgmPreset)
                },
                onNextBgm = {
                    activeBgmPreset = if (activeBgmPreset >= 120) 1 else activeBgmPreset + 1
                    repo.selectInfinityBgmPreset(activeBgmPreset)
                },
                modifier = Modifier.weight(0.95f)
            )

            CleanDeckCard(
                deckIndex = 1,
                deckBadge = "DECK B",
                isBgmDeck = false,
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

        // Master Bar: Crossfader Applies ONLY to Deck A & Deck B; BGM Deck Plays Independently in Infinity Mode!
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .background(palette.cardSurface, RoundedCornerShape(8.dp))
                .border(1.dp, palette.cardBorder, RoundedCornerShape(8.dp))
                .padding(horizontal = 8.dp, vertical = 4.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(6.dp)
        ) {
            Button(
                onClick = { repo.nativeTriggerSmartTransitionNow() },
                modifier = Modifier.height(28.dp),
                colors = ButtonDefaults.buttonColors(containerColor = Color(0xFFF50057))
            ) {
                Text("MIX NOW (A ⇄ B)", fontSize = 9.sp, fontWeight = FontWeight.Bold, color = Color.White)
            }

            Text("DECK A", color = palette.accentA, fontSize = 8.sp, fontWeight = FontWeight.Bold, fontFamily = FontFamily.Monospace)
            Slider(
                value = crossfader,
                onValueChange = { crossfader = it; repo.nativeSetMasterParam(0, it) },
                valueRange = -1f..1f,
                modifier = Modifier.weight(1.4f).height(24.dp)
            )
            Text("DECK B", color = palette.accentB, fontSize = 8.sp, fontWeight = FontWeight.Bold, fontFamily = FontFamily.Monospace)

            RotaryKnob3D("∞ BGM VOL", bgmDeckGain, false, palette.accentBgm, palette) {
                bgmDeckGain = it; repo.nativeSetMasterParam(5, it)
            }

            MasterSpectrumAnalyzer(
                vuA = vuDeckA,
                vuB = vuDeckB,
                vuBgm = vuDeckBgm,
                palette = palette,
                modifier = Modifier.weight(0.9f).height(28.dp)
            )
        }

        Spacer(modifier = Modifier.height(4.dp))

        // Unified Song & 120 Infinity BGM Beats Browser
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
                        browserTab = 0
                        scope.launch { repo.refreshInAppLocalSongs() }
                    },
                    modifier = Modifier.height(28.dp),
                    contentPadding = PaddingValues(horizontal = 8.dp, vertical = 0.dp),
                    colors = ButtonDefaults.buttonColors(
                        containerColor = if (browserTab == 0) palette.accentA else Color(0xFF263238)
                    )
                ) {
                    Text("LOCAL SONGS (${localSongs.size})", fontSize = 8.sp, fontWeight = FontWeight.Bold, color = Color.White)
                }

                Button(
                    onClick = { browserTab = 1 },
                    modifier = Modifier.height(28.dp),
                    contentPadding = PaddingValues(horizontal = 8.dp, vertical = 0.dp),
                    colors = ButtonDefaults.buttonColors(
                        containerColor = if (browserTab == 1) palette.accentBgm else Color(0xFF263238)
                    )
                ) {
                    Text("∞ 120 BGM KICK & BASS BEATS", fontSize = 8.sp, fontWeight = FontWeight.Bold, color = Color.White)
                }

                Button(
                    onClick = { browserTab = 2 },
                    modifier = Modifier.height(28.dp),
                    contentPadding = PaddingValues(horizontal = 8.dp, vertical = 0.dp),
                    colors = ButtonDefaults.buttonColors(
                        containerColor = if (browserTab == 2) palette.accentB else Color(0xFF263238)
                    )
                ) {
                    Text("ONLINE SEARCH", fontSize = 8.sp, fontWeight = FontWeight.Bold, color = Color.White)
                }

                OutlinedTextField(
                    value = searchQuery,
                    onValueChange = { searchQuery = it },
                    placeholder = {
                        Text("Search any song, beat, or artist...", color = palette.textSecondary, fontSize = 9.sp)
                    },
                    modifier = Modifier.weight(1f).height(44.dp),
                    singleLine = true
                )

                Button(
                    onClick = {
                        browserTab = 2
                        scope.launch {
                            statusBanner = "Searching songs for '$searchQuery'..."
                            onlineSearchResults = repo.searchAnyOnlineSongs(searchQuery)
                            statusBanner = "Found ${onlineSearchResults.size} songs."
                        }
                    },
                    modifier = Modifier.height(32.dp),
                    colors = ButtonDefaults.buttonColors(containerColor = palette.accentA)
                ) {
                    Text("SEARCH", fontSize = 8.sp, fontWeight = FontWeight.Bold, color = Color.White)
                }
            }

            Spacer(modifier = Modifier.height(4.dp))

            val displayedSongs = when (browserTab) {
                0 -> if (searchQuery.isBlank()) localSongs else localSongs.filter {
                    it.title.contains(searchQuery, true) || it.artist.contains(searchQuery, true)
                }
                1 -> if (searchQuery.isBlank()) repo.infinityBgmCatalog else repo.infinityBgmCatalog.filter {
                    it.title.contains(searchQuery, true)
                }
                else -> onlineSearchResults
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
                                text = "${song.artist} • ${song.sourceBadge} • ${song.estBpm} BPM",
                                color = palette.textSecondary,
                                fontSize = 8.sp,
                                fontFamily = FontFamily.Monospace,
                                maxLines = 1
                            )
                        }
                        Row(horizontalArrangement = Arrangement.spacedBy(3.dp)) {
                            if (song.bgmPresetIndex <= 0) {
                                Button(
                                    onClick = { repo.loadTrackIntoDeckInstant(0, song) { statusBanner = it } },
                                    modifier = Modifier.height(24.dp),
                                    contentPadding = PaddingValues(horizontal = 8.dp, vertical = 0.dp),
                                    colors = ButtonDefaults.buttonColors(containerColor = palette.accentA)
                                ) {
                                    Text("DECK A", fontSize = 8.sp, fontWeight = FontWeight.Bold, color = Color.White)
                                }
                            }
                            Button(
                                onClick = { repo.loadTrackIntoDeckInstant(2, song) { statusBanner = it } },
                                modifier = Modifier.height(24.dp),
                                contentPadding = PaddingValues(horizontal = 8.dp, vertical = 0.dp),
                                colors = ButtonDefaults.buttonColors(containerColor = palette.accentBgm)
                            ) {
                                Text("∞ BGM DECK", fontSize = 8.sp, fontWeight = FontWeight.Bold, color = Color.White)
                            }
                            if (song.bgmPresetIndex <= 0) {
                                Button(
                                    onClick = { repo.loadTrackIntoDeckInstant(1, song) { statusBanner = it } },
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
fun CleanDeckCard(
    deckIndex: Int,
    deckBadge: String,
    isBgmDeck: Boolean,
    defaultVocalOn: Boolean,
    trackTitle: String,
    accent: Color,
    palette: ApexThemePalette,
    bpm: Float,
    playheadNorm: Float,
    rgbWave: FloatArray,
    repo: OnlineAndLocalRepository,
    onPrevBgm: () -> Unit = {},
    onNextBgm: () -> Unit = {},
    modifier: Modifier = Modifier
) {
    var playing by remember { mutableStateOf(false) }
    var vocalEnabled by remember { mutableStateOf(defaultVocalOn) }
    var bassKnob by remember { mutableFloatStateOf(0f) }
    var filterKnob by remember { mutableFloatStateOf(0.5f) }
    var volumeKnob by remember { mutableFloatStateOf(1f) }

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
            .padding(6.dp)
    ) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Column(modifier = Modifier.weight(1f)) {
                Text(
                    text = "$deckBadge • %.0f BPM".format(bpm),
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
            }

            Row(horizontalArrangement = Arrangement.spacedBy(3.dp)) {
                Button(
                    onClick = {
                        val nextVox = !vocalEnabled
                        vocalEnabled = nextVox
                        repo.nativeSetVocalSwitch(deckIndex, nextVox)
                    },
                    modifier = Modifier.height(24.dp),
                    contentPadding = PaddingValues(horizontal = 6.dp, vertical = 0.dp),
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
                    contentPadding = PaddingValues(horizontal = 8.dp, vertical = 0.dp),
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

        Spacer(modifier = Modifier.height(3.dp))

        RgbScrollingWaveformView(
            rgbFlatArray = rgbWave,
            playheadNormalized = playheadNorm,
            accentColor = accent,
            palette = palette,
            modifier = Modifier.fillMaxWidth().height(34.dp),
            onSeek = { norm -> repo.nativeSeekNormalized(deckIndex, norm) }
        )

        Spacer(modifier = Modifier.height(3.dp))

        if (isBgmDeck) {
            // Next / Prev Infinity Beat Selector for Background Deck
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(4.dp),
                verticalAlignment = Alignment.CenterVertically
            ) {
                Button(
                    onClick = onPrevBgm,
                    modifier = Modifier.weight(1f).height(24.dp),
                    contentPadding = PaddingValues(0.dp),
                    colors = ButtonDefaults.buttonColors(containerColor = Color(0xFF263238))
                ) {
                    Text("◀ PREV BEAT", fontSize = 7.sp, fontWeight = FontWeight.Bold, color = Color.White)
                }
                Button(
                    onClick = onNextBgm,
                    modifier = Modifier.weight(1f).height(24.dp),
                    contentPadding = PaddingValues(0.dp),
                    colors = ButtonDefaults.buttonColors(containerColor = accent)
                ) {
                    Text("NEXT BEAT ▶", fontSize = 7.sp, fontWeight = FontWeight.Bold, color = Color.White)
                }
            }
        } else {
            // Touch Jog Scratch Bar for Deck A & Deck B
            Box(
                modifier = Modifier
                    .fillMaxWidth()
                    .height(24.dp)
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
                    text = "JOG SCRATCH • AUTO-SYNC INTERNAL",
                    color = accent,
                    fontSize = 7.sp,
                    fontWeight = FontWeight.Bold,
                    fontFamily = FontFamily.Monospace
                )
            }
        }

        Spacer(modifier = Modifier.height(3.dp))

        // ONLY 3 Required Knobs: BASS, FILTER, VOLUME (All other parameters controlled internally!)
        Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceEvenly) {
            RotaryKnob3D("BASS", bassKnob, true, Color(0xFFFF5252), palette) {
                bassKnob = it; repo.nativeSetChannelParam(deckIndex, 1, it)
            }
            RotaryKnob3D("FILTER", filterKnob, false, Color(0xFFFCC934), palette) {
                filterKnob = it; repo.nativeSetChannelParam(deckIndex, 7, it)
            }
            RotaryKnob3D("VOLUME", volumeKnob, false, Color(0xFF00C853), palette) {
                volumeKnob = it; repo.nativeSetChannelParam(deckIndex, 0, it)
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
                text = "APEX-DJ V16 // MANDATORY 1.50 GB NEURAL BRAIN OBB VAULT",
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
                            Text("ALREADY DOWNLOADED ✓ — OPEN MIXER", fontWeight = FontWeight.Bold, fontSize = 11.sp, color = Color.White)
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
            }
        }
    }
}
