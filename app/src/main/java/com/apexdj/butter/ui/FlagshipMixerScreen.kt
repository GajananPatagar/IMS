package com.apexdj.butter.ui

import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.gestures.detectTapGestures
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
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.Stroke
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
import kotlin.math.cos
import kotlin.math.sin

// Ultra-Luxury Rekordbox 7 / CDJ-3000X Hardware Palette
private val BgObsidian = Color(0xFF04060A)
private val PanelGunmetal = Color(0xFF0B1019)
private val PanelElevated = Color(0xFF111723)
private val BorderSteel = Color(0xFF1E293B)
private val CyanDeckA = Color(0xFF00F0FF)
private val OrangeDeckB = Color(0xFFFF6D00)
private val CrimsonBass = Color(0xFFFF1744)
private val EmeraldVocal = Color(0xFF00E676)
private val AmberGold = Color(0xFFFFC400)
private val VioletNeural = Color(0xFFD500F9)

@Composable
fun FlagshipMixerScreen(repo: OnlineAndLocalRepository) {
    val scope = rememberCoroutineScope()
    var searchQuery by remember { mutableStateOf("") }
    var onlineTracks by remember { mutableStateOf<List<SongTrack>>(emptyList()) }
    var offlineTracks by remember { mutableStateOf<List<SongTrack>>(emptyList()) }
    var activeCrateTab by remember { mutableIntStateOf(0) } // 0: Online, 1: Offline, 2: Auto-Remix Queue
    var crateExpanded by remember { mutableStateOf(true) }

    var statusBanner by remember {
        mutableStateOf("1.28 GB INVISIBLE GHOST-MIX ENGAGED • SUB-MILLISECOND BEAT-GRID LOCK ACTIVE")
    }

    var deckATitle by remember { mutableStateOf("Peak-Time Tech House Roller") }
    var deckBTitle by remember { mutableStateOf("Acid Techno Underground Sub") }
    var deckATrackId by remember { mutableStateOf("studio_0") }
    var deckBTrackId by remember { mutableStateOf("studio_1") }

    // Continuous Auto-DJ Queue State
    var autoDjEnabled by remember { mutableStateOf(true) }
    val remixQueue = remember { mutableStateListOf<SongTrack>() }
    var masterActiveDeck by remember { mutableIntStateOf(0) }
    var morphInProgress by remember { mutableStateOf(false) }
    var morphProgress by remember { mutableFloatStateOf(0f) }

    // Master WAV Recorder State
    var isRecording by remember { mutableStateOf(false) }
    var isRecordPaused by remember { mutableStateOf(false) }
    var recordSeconds by remember { mutableDoubleStateOf(0.0) }
    var lastSavedWavPath by remember { mutableStateOf("") }

    // Master Console Parameters
    var crossfader by remember { mutableFloatStateOf(0f) }
    var neveDrive by remember { mutableFloatStateOf(0.60f) }
    var isoLow by remember { mutableFloatStateOf(0f) }
    var isoMid by remember { mutableFloatStateOf(0f) }
    var isoHigh by remember { mutableFloatStateOf(0f) }
    var neuralSidechain by remember { mutableFloatStateOf(0.45f) }
    var neuralSubBoom by remember { mutableFloatStateOf(0.30f) }
    var neuralAirGlow by remember { mutableFloatStateOf(0.30f) }
    var beatFxWet by remember { mutableFloatStateOf(0f) }
    var selectedMorphStyle by remember { mutableIntStateOf(0) }

    // Real-Time Telemetry
    var vuDeckA by remember { mutableFloatStateOf(0f) }
    var vuDeckB by remember { mutableFloatStateOf(0f) }
    var bpmDeckA by remember { mutableFloatStateOf(128f) }
    var bpmDeckB by remember { mutableFloatStateOf(128f) }
    var keyDeckA by remember { mutableIntStateOf(8) }
    var keyDeckB by remember { mutableIntStateOf(8) }
    var beatPhaseA by remember { mutableFloatStateOf(0f) }
    var beatPhaseB by remember { mutableFloatStateOf(0f) }
    var playingDeckA by remember { mutableStateOf(false) }
    var playingDeckB by remember { mutableStateOf(false) }

    var localTargetMode by remember { mutableIntStateOf(0) }
    val localPicker = rememberLauncherForActivityResult(ActivityResultContracts.GetContent()) { uri: Uri? ->
        if (uri != null) {
            val pickedTrack = SongTrack(
                id = "local_${System.currentTimeMillis()}",
                title = "Local Audio Mix #${(100..999).random()}",
                artist = "Device Storage",
                durationSec = 210,
                streamUrl = "",
                isLocal = true,
                estimatedBpm = 128f,
                camelotKey = 8,
                localUri = uri
            )
            offlineTracks = listOf(pickedTrack) + offlineTracks
            scope.launch {
                when (localTargetMode) {
                    0 -> {
                        deckATitle = pickedTrack.title
                        deckATrackId = pickedTrack.id
                        repo.loadTrackIntoDeck(0, pickedTrack, autoPlay = true)
                        masterActiveDeck = 0
                        statusBanner = "DECK A Loaded Local Track • Invisible Ghost-Mix Ready!"
                    }
                    1 -> {
                        deckBTitle = pickedTrack.title
                        deckBTrackId = pickedTrack.id
                        repo.loadTrackIntoDeck(1, pickedTrack, autoPlay = true)
                        masterActiveDeck = 1
                        statusBanner = "DECK B Loaded Local Track • Invisible Ghost-Mix Ready!"
                    }
                    else -> {
                        remixQueue.add(pickedTrack)
                        repo.prebufferTrackInBackground(pickedTrack)
                        statusBanner = "Added to Queue & Pre-buffered in RAM (0ms latency)"
                    }
                }
            }
        }
    }

    // Main Telemetry & Background RAM Pre-Buffer Loop
    LaunchedEffect(Unit) {
        offlineTracks = repo.scanOfflineLibrary()
        onlineTracks = repo.getBuiltInStudioTracks()
        remixQueue.addAll(repo.getBuiltInStudioTracks().drop(2).take(4))

        // Pre-buffer upcoming queue tracks in background for instant zero-lag transitions
        scope.launch {
            remixQueue.forEach { repo.prebufferTrackInBackground(it) }
        }

        while (true) {
            vuDeckA = repo.nativeGetChannelVu(0)
            vuDeckB = repo.nativeGetChannelVu(1)
            bpmDeckA = repo.nativeGetDetectedBpm(0)
            bpmDeckB = repo.nativeGetDetectedBpm(1)
            keyDeckA = repo.nativeGetDetectedCamelotKey(0)
            keyDeckB = repo.nativeGetDetectedCamelotKey(1)
            playingDeckA = repo.nativeGetDeckPlayState(0)
            playingDeckB = repo.nativeGetDeckPlayState(1)
            beatPhaseA = repo.nativeGetBeatPhase(0)
            beatPhaseB = repo.nativeGetBeatPhase(1)
            crossfader = repo.nativeGetCrossfader()
            morphInProgress = repo.nativeIsMorphActive()
            morphProgress = repo.nativeGetMorphProgress()
            isRecording = repo.nativeIsRecording()
            isRecordPaused = repo.nativeIsRecordPaused()
            if (isRecording) {
                recordSeconds = repo.nativeGetRecordDurationSec()
            }

            // Autonomous Invisible Ghost-Mix Auto-DJ Loop
            if (autoDjEnabled && !morphInProgress) {
                if (!playingDeckA && !playingDeckB && remixQueue.isNotEmpty()) {
                    val starter = remixQueue.removeAt(0)
                    deckATitle = starter.title
                    deckATrackId = starter.id
                    repo.loadTrackIntoDeck(0, starter, autoPlay = true)
                    repo.nativeSetMasterParam(0, -1f)
                    masterActiveDeck = 0
                    statusBanner = "Auto-DJ Ghost-Mix Started: '${starter.title}' on DECK A"
                }

                val remA = repo.nativeGetDeckRemainingSeconds(0)
                val remB = repo.nativeGetDeckRemainingSeconds(1)

                if (playingDeckA && masterActiveDeck == 0 && remA in 0.4f..14.0f) {
                    val nextTrack = if (remixQueue.isNotEmpty()) {
                        remixQueue.removeAt(0)
                    } else {
                        repo.pickBestNextAutoDjTrack(onlineTracks + offlineTracks, bpmDeckA, keyDeckA, deckATrackId, deckBTrackId)
                    }
                    if (nextTrack != null) {
                        scope.launch { repo.prebufferTrackInBackground(nextTrack) }
                        statusBanner = "Invisible Ghost-Mix: DECK A ➔ DECK B ('${nextTrack.title}')"
                        deckBTitle = nextTrack.title
                        deckBTrackId = nextTrack.id
                        repo.loadTrackIntoDeck(1, nextTrack, autoPlay = true)
                        repo.nativeTriggerButterMorph(0, 1, 12.0)
                        masterActiveDeck = 1
                    }
                } else if (playingDeckB && masterActiveDeck == 1 && remB in 0.4f..14.0f) {
                    val nextTrack = if (remixQueue.isNotEmpty()) {
                        remixQueue.removeAt(0)
                    } else {
                        repo.pickBestNextAutoDjTrack(onlineTracks + offlineTracks, bpmDeckB, keyDeckB, deckATrackId, deckBTrackId)
                    }
                    if (nextTrack != null) {
                        scope.launch { repo.prebufferTrackInBackground(nextTrack) }
                        statusBanner = "Invisible Ghost-Mix: DECK B ➔ DECK A ('${nextTrack.title}')"
                        deckATitle = nextTrack.title
                        deckATrackId = nextTrack.id
                        repo.loadTrackIntoDeck(0, nextTrack, autoPlay = true)
                        repo.nativeTriggerButterMorph(1, 0, 12.0)
                        masterActiveDeck = 0
                    }
                }
            }
            delay(50)
        }
    }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .background(BgObsidian)
            .windowInsetsPadding(WindowInsets.systemBars)
            .padding(horizontal = 8.dp, vertical = 4.dp)
            .verticalScroll(rememberScrollState())
    ) {
        // 1. REKORDBOX 7 / CDJ-3000X TOP COCKPIT & RECORDER BAR
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .background(
                    Brush.horizontalGradient(listOf(Color(0xFF0B132B), Color(0xFF111D38), Color(0xFF0B132B))),
                    RoundedCornerShape(8.dp)
                )
                .border(1.dp, BorderSteel, RoundedCornerShape(8.dp))
                .padding(horizontal = 10.dp, vertical = 6.dp),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Column(modifier = Modifier.weight(1f)) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Box(
                        modifier = Modifier
                            .background(CyanDeckA.copy(alpha = 0.22f), RoundedCornerShape(4.dp))
                            .border(1.dp, CyanDeckA, RoundedCornerShape(4.dp))
                            .padding(horizontal = 5.dp, vertical = 1.dp)
                    ) {
                        Text("1.28 GB GHOST-MIX PRO", color = CyanDeckA, fontSize = 8.sp, fontWeight = FontWeight.ExtraBold, fontFamily = FontFamily.Monospace)
                    }
                    Spacer(modifier = Modifier.width(6.dp))
                    Text(
                        "APEX-DJ // CDJ-3000X + DJM-V10 ULTRA-LUXURY WORKSTATION",
                        color = Color.White,
                        fontFamily = FontFamily.Monospace,
                        fontWeight = FontWeight.ExtraBold,
                        fontSize = 11.sp,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis
                    )
                }
                Text(
                    text = if (lastSavedWavPath.isNotEmpty() && !isRecording) "Saved WAV: $lastSavedWavPath" else statusBanner,
                    color = AmberGold,
                    fontSize = 9.sp,
                    fontFamily = FontFamily.Monospace,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis
                )
            }

            Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                Box(
                    modifier = Modifier
                        .clip(RoundedCornerShape(6.dp))
                        .background(if (isRecording) CrimsonBass else Color(0xFF1E293B))
                        .border(1.dp, CrimsonBass, RoundedCornerShape(6.dp))
                        .clickable {
                            if (!isRecording) {
                                lastSavedWavPath = repo.startNewMasterRecording()
                                isRecording = true
                                isRecordPaused = false
                                statusBanner = "🔴 MASTER WAV RECORDING STARTED ➔ $lastSavedWavPath"
                            } else {
                                repo.nativeStopMasterRecording()
                                isRecording = false
                                isRecordPaused = false
                                statusBanner = "✅ MASTER WAV SAVED: $lastSavedWavPath"
                            }
                        }
                        .padding(horizontal = 8.dp, vertical = 5.dp)
                ) {
                    val mins = (recordSeconds.toInt() / 60)
                    val secs = (recordSeconds.toInt() % 60)
                    Text(
                        text = if (isRecording) "⏹ SAVE WAV (%02d:%02d)".format(mins, secs) else "⏺ REC MIX",
                        color = Color.White,
                        fontSize = 9.sp,
                        fontWeight = FontWeight.ExtraBold,
                        fontFamily = FontFamily.Monospace
                    )
                }

                if (isRecording) {
                    Box(
                        modifier = Modifier
                            .clip(RoundedCornerShape(6.dp))
                            .background(if (isRecordPaused) AmberGold else Color(0xFF334155))
                            .border(1.dp, AmberGold, RoundedCornerShape(6.dp))
                            .clickable {
                                repo.nativePauseMasterRecording()
                                isRecordPaused = !isRecordPaused
                                statusBanner = if (isRecordPaused) "⏸ Master Recording PAUSED" else "🔴 Master Recording RESUMED"
                            }
                            .padding(horizontal = 8.dp, vertical = 5.dp)
                    ) {
                        Text(
                            text = if (isRecordPaused) "▶ RESUME" else "⏸ PAUSE",
                            color = if (isRecordPaused) Color.Black else Color.White,
                            fontSize = 9.sp,
                            fontWeight = FontWeight.ExtraBold,
                            fontFamily = FontFamily.Monospace
                        )
                    }
                }

                SegmentedDualVuMeter(vuA = vuDeckA, vuB = vuDeckB)
            }
        }

        Spacer(modifier = Modifier.height(4.dp))

        // DUAL STACKED PARALLEL BEAT-GRID WAVEFORMS (Rekordbox 7 Phase-Lock View)
        Box(
            modifier = Modifier
                .fillMaxWidth()
                .height(42.dp)
                .background(PanelGunmetal, RoundedCornerShape(6.dp))
                .border(1.dp, BorderSteel, RoundedCornerShape(6.dp))
                .padding(horizontal = 6.dp, vertical = 2.dp)
        ) {
            Canvas(modifier = Modifier.fillMaxSize()) {
                val w = size.width
                val h = size.height
                val midY = h / 2f

                // Deck A waveform (Cyan) on top half
                val binsA = repo.nativeGetWaveformOverview(0)
                val barWA = w / binsA.size
                for (i in binsA.indices) {
                    val bh = binsA[i] * (h * 0.38f)
                    val x = i * barWA
                    drawLine(CyanDeckA.copy(alpha = 0.8f), Offset(x, midY - 2f), Offset(x, midY - 2f - bh), strokeWidth = 2.5f)
                }

                // Deck B waveform (Orange) on bottom half
                val binsB = repo.nativeGetWaveformOverview(1)
                val barWB = w / binsB.size
                for (i in binsB.indices) {
                    val bh = binsB[i] * (h * 0.38f)
                    val x = i * barWB
                    drawLine(OrangeDeckB.copy(alpha = 0.8f), Offset(x, midY + 2f), Offset(x, midY + 2f + bh), strokeWidth = 2.5f)
                }

                // Sub-millisecond Beat-Grid Phase Lock Cursors
                val curX = (0.5f + (beatPhaseA - beatPhaseB) * 0.25f) * w
                drawLine(EmeraldVocal, Offset(curX, 0f), Offset(curX, h), strokeWidth = 2f)
            }
        }

        Spacer(modifier = Modifier.height(4.dp))

        // 2. SEARCH, LOCAL ADD (DECK A / DECK B / QUEUE) & AUTO-DJ TRANSPORT BAR
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .background(PanelGunmetal, RoundedCornerShape(8.dp))
                .border(1.dp, BorderSteel, RoundedCornerShape(8.dp))
                .padding(6.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(4.dp)
        ) {
            OutlinedTextField(
                value = searchQuery,
                onValueChange = { searchQuery = it },
                placeholder = { Text("Search Apple Music AAC, Deezer, Audius...", color = Color(0xFF94A3B8), fontSize = 10.sp) },
                modifier = Modifier
                    .weight(1f)
                    .height(46.dp),
                singleLine = true,
                colors = OutlinedTextFieldDefaults.colors(
                    focusedBorderColor = CyanDeckA,
                    unfocusedBorderColor = BorderSteel,
                    focusedTextColor = Color.White,
                    unfocusedTextColor = Color.White
                )
            )

            HardwareActionButton("🔍 SEARCH", CyanDeckA, Color(0xFF082F49)) {
                activeCrateTab = 0
                crateExpanded = true
                scope.launch {
                    statusBanner = "Searching Global Libraries & Pre-buffering Harmonic Matches..."
                    onlineTracks = repo.searchOnlineSongs(searchQuery)
                    onlineTracks.forEach { repo.prebufferTrackInBackground(it) }
                    statusBanner = "Found ${onlineTracks.size} tracks • 0ms RAM Pre-buffered!"
                }
            }

            HardwareActionButton("📂 LOCAL A", CyanDeckA, Color(0xFF0F172A)) {
                localTargetMode = 0
                localPicker.launch("audio/*")
            }

            HardwareActionButton("📂 LOCAL B", OrangeDeckB, Color(0xFF0F172A)) {
                localTargetMode = 1
                localPicker.launch("audio/*")
            }

            HardwareActionButton("+ LOCAL Q", AmberGold, Color(0xFF0F172A)) {
                localTargetMode = 2
                localPicker.launch("audio/*")
            }

            HardwareActionButton(
                label = if (autoDjEnabled) "🤖 AUTO-DJ: ON" else "🤖 AUTO-DJ: OFF",
                borderColor = if (autoDjEnabled) EmeraldVocal else BorderSteel,
                bgColor = if (autoDjEnabled) Color(0xFF064E3B) else Color(0xFF1E293B)
            ) {
                autoDjEnabled = !autoDjEnabled
                statusBanner = if (autoDjEnabled) "🟢 Invisible Ghost-Mix Auto-DJ ENGAGED" else "🔴 Auto-DJ OFF"
            }
        }

        Spacer(modifier = Modifier.height(4.dp))

        // 3. INTELLIGENT CRATE BROWSER (ONLINE SEARCH | OFFLINE LIBRARY | AUTO-REMIX QUEUE)
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .background(PanelGunmetal, RoundedCornerShape(8.dp))
                .border(1.dp, BorderSteel, RoundedCornerShape(8.dp))
                .padding(6.dp)
        ) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Row(horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                    CrateTabChip("🌐 ONLINE SEARCH (${onlineTracks.size})", activeCrateTab == 0, CyanDeckA) {
                        activeCrateTab = 0; crateExpanded = true
                    }
                    CrateTabChip("💾 OFFLINE LIBRARY (${offlineTracks.size})", activeCrateTab == 1, EmeraldVocal) {
                        activeCrateTab = 1; crateExpanded = true
                        scope.launch { offlineTracks = repo.scanOfflineLibrary() }
                    }
                    CrateTabChip("🔥 AUTO-REMIX QUEUE (${remixQueue.size})", activeCrateTab == 2, AmberGold) {
                        activeCrateTab = 2; crateExpanded = true
                    }
                }

                Row(horizontalArrangement = Arrangement.spacedBy(4.dp), verticalAlignment = Alignment.CenterVertically) {
                    if (remixQueue.isNotEmpty()) {
                        HardwareActionButton("⚡ INVISIBLE GHOST-MIX NEXT", CrimsonBass, Color(0xFF450A0A)) {
                            val nextSong = remixQueue.removeAt(0)
                            val fromDeck = if (playingDeckA && !playingDeckB) 0 else if (playingDeckB && !playingDeckA) 1 else masterActiveDeck
                            val toDeck = if (fromDeck == 0) 1 else 0
                            scope.launch {
                                if (toDeck == 0) { deckATitle = nextSong.title; deckATrackId = nextSong.id }
                                else { deckBTitle = nextSong.title; deckBTrackId = nextSong.id }
                                statusBanner = "⚡ Instant Ghost-Mix ➔ '${nextSong.title}' on DECK ${if (toDeck == 0) "A" else "B"}"
                                repo.loadTrackIntoDeck(toDeck, nextSong, autoPlay = true)
                                repo.nativeTriggerButterMorph(fromDeck, toDeck, 10.0)
                                masterActiveDeck = toDeck
                            }
                        }
                    }
                    HardwareActionButton(if (crateExpanded) "▲ HIDE" else "▼ SHOW", BorderSteel, PanelElevated) {
                        crateExpanded = !crateExpanded
                    }
                }
            }

            if (crateExpanded) {
                Spacer(modifier = Modifier.height(4.dp))

                val displayedList = when (activeCrateTab) {
                    0 -> onlineTracks
                    1 -> offlineTracks
                    else -> remixQueue.toList()
                }

                val rankedPairs = displayedList.map { song ->
                    val badge = repo.evaluateMixCompatibility(
                        track = song,
                        deckABpm = bpmDeckA,
                        deckAKey = keyDeckA,
                        deckAPlaying = playingDeckA,
                        deckBBpm = bpmDeckB,
                        deckBKey = keyDeckB,
                        deckBPlaying = playingDeckB,
                        queueTail = remixQueue.lastOrNull()
                    )
                    song to badge
                }.let { list ->
                    if (activeCrateTab == 2) list else list.sortedByDescending { it.second.matchScorePercent }
                }

                val bestCandidate = rankedPairs.firstOrNull { it.first.id != deckATrackId && it.first.id != deckBTrackId }
                if (bestCandidate != null && activeCrateTab != 2) {
                    val (topSong, topBadge) = bestCandidate
                    Row(
                        modifier = Modifier
                            .fillMaxWidth()
                            .background(
                                Brush.horizontalGradient(listOf(Color(0xFF064E3B), Color(0xFF0F172A))),
                                RoundedCornerShape(6.dp)
                            )
                            .border(1.dp, EmeraldVocal, RoundedCornerShape(6.dp))
                            .padding(horizontal = 8.dp, vertical = 4.dp),
                        horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Column(modifier = Modifier.weight(1f)) {
                            Text(
                                "👑 NEXT PERFECT SONG RECOMMENDED • ${topBadge.targetLabel} • [${topBadge.transitionAdvice}]",
                                color = EmeraldVocal,
                                fontSize = 8.sp,
                                fontWeight = FontWeight.ExtraBold,
                                fontFamily = FontFamily.Monospace
                            )
                            Text(
                                "${topSong.title} — ${topSong.artist} (${topSong.estimatedBpm.toInt()} BPM • Key ${topSong.camelotKey}A)",
                                color = Color.White,
                                fontSize = 10.sp,
                                fontWeight = FontWeight.Bold,
                                maxLines = 1,
                                overflow = TextOverflow.Ellipsis
                            )
                        }
                        Row(horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                            HardwareActionButton(
                                label = "⚡ SMART-MIX TO ${if (topBadge.recommendedDeck == 0) "DECK A" else "DECK B"}",
                                borderColor = EmeraldVocal,
                                bgColor = Color(0xFF047857)
                            ) {
                                val toDeck = topBadge.recommendedDeck
                                val fromDeck = if (toDeck == 0) 1 else 0
                                scope.launch {
                                    if (toDeck == 0) { deckATitle = topSong.title; deckATrackId = topSong.id }
                                    else { deckBTitle = topSong.title; deckBTrackId = topSong.id }
                                    repo.loadTrackIntoDeck(toDeck, topSong, autoPlay = true)
                                    if (playingDeckA || playingDeckB) {
                                        repo.nativeTriggerButterMorph(fromDeck, toDeck, 11.0)
                                    }
                                    masterActiveDeck = toDeck
                                    statusBanner = "👑 Smart-Mixed '${topSong.title}' into DECK ${if (toDeck == 0) "A" else "B"}!"
                                }
                            }
                            HardwareActionButton("+ QUEUE", AmberGold, Color(0xFF1E293B)) {
                                remixQueue.add(topSong)
                                scope.launch { repo.prebufferTrackInBackground(topSong) }
                                statusBanner = "Queued '${topSong.title}' (#${remixQueue.size})"
                            }
                        }
                    }
                    Spacer(modifier = Modifier.height(4.dp))
                }

                LazyColumn(
                    modifier = Modifier
                        .fillMaxWidth()
                        .height(102.dp)
                        .background(Color(0xFF05070B), RoundedCornerShape(6.dp))
                        .padding(4.dp),
                    verticalArrangement = Arrangement.spacedBy(3.dp)
                ) {
                    items(rankedPairs) { (song, badge) ->
                        Row(
                            modifier = Modifier
                                .fillMaxWidth()
                                .background(PanelElevated, RoundedCornerShape(5.dp))
                                .border(
                                    width = 1.dp,
                                    color = if (badge.isTopPick) EmeraldVocal.copy(alpha = 0.55f) else BorderSteel,
                                    shape = RoundedCornerShape(5.dp)
                                )
                                .padding(horizontal = 6.dp, vertical = 3.dp),
                            horizontalArrangement = Arrangement.SpaceBetween,
                            verticalAlignment = Alignment.CenterVertically
                        ) {
                            Column(modifier = Modifier.weight(1f)) {
                                Text(
                                    text = "${song.title} — ${song.artist}",
                                    color = Color.White,
                                    fontSize = 10.sp,
                                    fontWeight = FontWeight.Bold,
                                    maxLines = 1,
                                    overflow = TextOverflow.Ellipsis
                                )
                                Text(
                                    text = "${badge.targetLabel} • ${badge.transitionAdvice}",
                                    color = if (badge.recommendedDeck == 0) CyanDeckA else OrangeDeckB,
                                    fontSize = 8.sp,
                                    fontFamily = FontFamily.Monospace
                                )
                            }
                            Row(horizontalArrangement = Arrangement.spacedBy(3.dp)) {
                                HardwareActionButton("DECK A", CyanDeckA, Color(0xFF082F49)) {
                                    scope.launch {
                                        deckATitle = song.title
                                        deckATrackId = song.id
                                        repo.loadTrackIntoDeck(0, song, autoPlay = true)
                                        masterActiveDeck = 0
                                        statusBanner = "Loaded '${song.title}' on DECK A"
                                    }
                                }
                                HardwareActionButton("DECK B", OrangeDeckB, Color(0xFF431407)) {
                                    scope.launch {
                                        deckBTitle = song.title
                                        deckBTrackId = song.id
                                        repo.loadTrackIntoDeck(1, song, autoPlay = true)
                                        masterActiveDeck = 1
                                        statusBanner = "Loaded '${song.title}' on DECK B"
                                    }
                                }
                                if (activeCrateTab != 2) {
                                    HardwareActionButton("+ QUEUE", AmberGold, Color(0xFF1E293B)) {
                                        remixQueue.add(song)
                                        scope.launch { repo.prebufferTrackInBackground(song) }
                                        statusBanner = "Added '${song.title}' to Queue (${remixQueue.size})"
                                    }
                                } else {
                                    HardwareActionButton("✕ REMOVE", CrimsonBass, Color(0xFF1E293B)) {
                                        remixQueue.remove(song)
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        Spacer(modifier = Modifier.height(5.dp))

        // 4. DUAL ULTRA-LUXURY CDJ-3000X HARDWARE DECKS
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(6.dp)
        ) {
            ProClubDeckConsole(
                deckIndex = 0,
                deckTitle = "DECK A // CDJ-3000X",
                trackTitle = deckATitle,
                accent = CyanDeckA,
                bpm = bpmDeckA,
                camelotKey = keyDeckA,
                repo = repo,
                modifier = Modifier.weight(1f)
            )
            ProClubDeckConsole(
                deckIndex = 1,
                deckTitle = "DECK B // CDJ-3000X",
                trackTitle = deckBTitle,
                accent = OrangeDeckB,
                bpm = bpmDeckB,
                camelotKey = keyDeckB,
                repo = repo,
                modifier = Modifier.weight(1f)
            )
        }

        Spacer(modifier = Modifier.height(5.dp))

        // 5. MASTER RUPERT NEVE TRANSFORMER & INVISIBLE GHOST-MIX TRANSITION CONSOLE
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .background(PanelGunmetal, RoundedCornerShape(8.dp))
                .border(1.dp, BorderSteel, RoundedCornerShape(8.dp))
                .padding(8.dp)
        ) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text(
                    "DJM-V10 MASTER BUS // INVISIBLE GHOST-MIX ENGINE • RUPERT NEVE GLUE",
                    color = AmberGold,
                    fontSize = 10.sp,
                    fontFamily = FontFamily.Monospace,
                    fontWeight = FontWeight.ExtraBold
                )
                if (morphInProgress) {
                    Text(
                        "⚡ GHOST-MIX IN PROGRESS: ${(morphProgress * 100).toInt()}%",
                        color = EmeraldVocal,
                        fontSize = 9.sp,
                        fontFamily = FontFamily.Monospace,
                        fontWeight = FontWeight.ExtraBold
                    )
                }
            }

            Spacer(modifier = Modifier.height(4.dp))

            val morphNames = listOf(
                "1. INVISIBLE GHOST",
                "2. BASS SWAP",
                "3. VOCAL BRIDGE",
                "4. HARMONIC MELT",
                "5. PHASE LOCK",
                "6. DROP SPLICE"
            )
            Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                morphNames.forEachIndexed { idx, name ->
                    val selected = selectedMorphStyle == idx
                    Box(
                        modifier = Modifier
                            .weight(1f)
                            .clip(RoundedCornerShape(5.dp))
                            .background(if (selected) VioletNeural.copy(alpha = 0.28f) else PanelElevated)
                            .border(1.dp, if (selected) VioletNeural else BorderSteel, RoundedCornerShape(5.dp))
                            .clickable {
                                selectedMorphStyle = idx
                                repo.nativeSetMasterParam(14, idx.toFloat())
                                statusBanner = "Transition Architecture Mode: $name"
                            }
                            .padding(vertical = 5.dp),
                        contentAlignment = Alignment.Center
                    ) {
                        Text(
                            name,
                            color = if (selected) Color.White else Color(0xFF94A3B8),
                            fontSize = 8.sp,
                            fontWeight = FontWeight.Bold,
                            fontFamily = FontFamily.Monospace
                        )
                    }
                }
            }

            Spacer(modifier = Modifier.height(6.dp))

            Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                ProHardwareBarFader("ISO LOW", isoLow, -1f..1f, CrimsonBass, Modifier.weight(1f)) {
                    isoLow = it; repo.nativeSetMasterParam(1, it)
                }
                ProHardwareBarFader("ISO MID", isoMid, -1f..1f, EmeraldVocal, Modifier.weight(1f)) {
                    isoMid = it; repo.nativeSetMasterParam(2, it)
                }
                ProHardwareBarFader("ISO HIGH", isoHigh, -1f..1f, CyanDeckA, Modifier.weight(1f)) {
                    isoHigh = it; repo.nativeSetMasterParam(3, it)
                }
                ProHardwareBarFader("NEVE DRIVE", neveDrive, 0f..1f, AmberGold, Modifier.weight(1f)) {
                    neveDrive = it; repo.nativeSetMasterParam(4, it)
                }
            }

            Spacer(modifier = Modifier.height(4.dp))

            Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                ProHardwareBarFader("SPECTRAL CARVING", neuralSidechain, 0f..1f, CyanDeckA, Modifier.weight(1f)) {
                    neuralSidechain = it; repo.nativeSetMasterParam(11, it)
                }
                ProHardwareBarFader("SUB-BASS BOOM", neuralSubBoom, 0f..1f, CrimsonBass, Modifier.weight(1f)) {
                    neuralSubBoom = it; repo.nativeSetMasterParam(12, it)
                }
                ProHardwareBarFader("AIR EXCITER", neuralAirGlow, 0f..1f, VioletNeural, Modifier.weight(1f)) {
                    neuralAirGlow = it; repo.nativeSetMasterParam(13, it)
                }
                ProHardwareBarFader("14 BEAT FX WET", beatFxWet, 0f..1f, OrangeDeckB, Modifier.weight(1f)) {
                    beatFxWet = it
                    repo.nativeSetMasterParam(8, 2f)
                    repo.nativeSetMasterParam(9, it)
                }
            }

            Spacer(modifier = Modifier.height(6.dp))

            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(6.dp)
            ) {
                HardwareActionButton("⚡ INVISIBLE GHOST-MIX A ➔ B", CyanDeckA, Color(0xFF082F49)) {
                    repo.nativeTriggerButterMorph(0, 1, 12.0)
                    masterActiveDeck = 1
                    statusBanner = "⚡ Executing Invisible Ghost-Mix Transition: A ➔ B..."
                }

                Box(modifier = Modifier.weight(1f)) {
                    ProHardwareBarFader(
                        label = "MAGVEL PRO CROSSFADER (A ◄───► B)",
                        value = crossfader,
                        range = -1f..1f,
                        accent = AmberGold,
                        modifier = Modifier.fillMaxWidth()
                    ) {
                        crossfader = it
                        repo.nativeSetMasterParam(0, it)
                    }
                }

                HardwareActionButton("⚡ INVISIBLE GHOST-MIX B ➔ A", OrangeDeckB, Color(0xFF431407)) {
                    repo.nativeTriggerButterMorph(1, 0, 12.0)
                    masterActiveDeck = 0
                    statusBanner = "⚡ Executing Invisible Ghost-Mix Transition: B ➔ A..."
                }
            }
        }
    }
}

@Composable
fun ProClubDeckConsole(
    deckIndex: Int,
    deckTitle: String,
    trackTitle: String,
    accent: Color,
    bpm: Float,
    camelotKey: Int,
    repo: OnlineAndLocalRepository,
    modifier: Modifier = Modifier
) {
    var playing by remember { mutableStateOf(false) }
    var trackProgress by remember { mutableFloatStateOf(0f) }
    var remSeconds by remember { mutableFloatStateOf(64f) }
    var activeDeckRackTab by remember { mutableIntStateOf(0) }
    var platterAngle by remember { mutableFloatStateOf(0f) }

    var fader by remember { mutableFloatStateOf(1f) }
    var eqLo by remember { mutableFloatStateOf(0f) }
    var eqLm by remember { mutableFloatStateOf(0f) }
    var eqHm by remember { mutableFloatStateOf(0f) }
    var eqHi by remember { mutableFloatStateOf(0f) }
    var vcfCutoff by remember { mutableFloatStateOf(0.85f) }
    var vcfCrunch by remember { mutableFloatStateOf(0f) }
    var pitchSemi by remember { mutableFloatStateOf(0f) }
    var transientPunch by remember { mutableFloatStateOf(0.30f) }
    var stereoWiden by remember { mutableFloatStateOf(0.25f) }

    val stemGains = remember { mutableStateListOf(1f, 1f, 1f, 1f, 1f) }
    val stemEnvs = remember { mutableStateListOf(0f, 0f, 0f, 0f, 0f) }
    var waveformBins by remember { mutableStateOf(FloatArray(64) { 0.40f }) }

    LaunchedEffect(deckIndex) {
        while (true) {
            playing = repo.nativeGetDeckPlayState(deckIndex)
            trackProgress = repo.nativeGetDeckProgress(deckIndex)
            remSeconds = repo.nativeGetDeckRemainingSeconds(deckIndex)
            waveformBins = repo.nativeGetWaveformOverview(deckIndex)
            for (s in 0 until 5) {
                stemGains[s] = repo.nativeGetStemGain(deckIndex, s)
                stemEnvs[s] = repo.nativeGetStemEnvelope(deckIndex, s)
            }
            if (playing) {
                platterAngle = (platterAngle + (bpm / 120f) * 6f) % 360f
            }
            delay(40)
        }
    }

    Column(
        modifier = modifier
            .background(PanelGunmetal, RoundedCornerShape(8.dp))
            .border(1.dp, accent.copy(alpha = 0.65f), RoundedCornerShape(8.dp))
            .padding(6.dp)
    ) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Column(modifier = Modifier.weight(1f)) {
                Text(deckTitle, color = accent, fontWeight = FontWeight.ExtraBold, fontSize = 10.sp, fontFamily = FontFamily.Monospace)
                Text(
                    trackTitle,
                    color = Color.White,
                    fontWeight = FontWeight.Bold,
                    fontSize = 10.sp,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis
                )
                Text(
                    "%.1f BPM  •  KEY %dA  •  REM -%02d:%02d".format(
                        bpm, camelotKey, (remSeconds.toInt() / 60), (remSeconds.toInt() % 60)
                    ),
                    color = AmberGold,
                    fontSize = 8.sp,
                    fontFamily = FontFamily.Monospace
                )
            }

            Box(
                modifier = Modifier
                    .clip(RoundedCornerShape(6.dp))
                    .background(if (playing) EmeraldVocal else Color(0xFF1E293B))
                    .border(1.dp, if (playing) EmeraldVocal else accent, RoundedCornerShape(6.dp))
                    .clickable {
                        val nextState = !playing
                        playing = nextState
                        repo.nativeSetDeckPlayState(deckIndex, nextState)
                    }
                    .padding(horizontal = 12.dp, vertical = 6.dp)
            ) {
                Text(
                    text = if (playing) "▶ PLAYING" else "⏸ CUE / PLAY",
                    color = if (playing) Color.Black else Color.White,
                    fontSize = 9.sp,
                    fontWeight = FontWeight.ExtraBold,
                    fontFamily = FontFamily.Monospace
                )
            }
        }

        Spacer(modifier = Modifier.height(5.dp))

        // 3D SPINNING CDJ-3000X JOGWHEEL + HIGH-RES RGB WAVEFORM SCOPE
        Row(
            modifier = Modifier.fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(6.dp)
        ) {
            Box(
                modifier = Modifier
                    .size(86.dp)
                    .pointerInput(Unit) {
                        detectDragGestures(
                            onDragStart = { repo.nativeSetChannelParam(deckIndex, 18, 1.4f) },
                            onDragEnd = { repo.nativeSetChannelParam(deckIndex, 18, 0f) },
                            onDragCancel = { repo.nativeSetChannelParam(deckIndex, 18, 0f) }
                        ) { _, dragAmount ->
                            val vel = (dragAmount.x * 0.06f).coerceIn(-3f, 3f)
                            platterAngle = (platterAngle + dragAmount.x * 1.4f) % 360f
                            repo.nativeSetChannelParam(deckIndex, 18, vel)
                        }
                    },
                contentAlignment = Alignment.Center
            ) {
                Canvas(modifier = Modifier.fillMaxSize()) {
                    val radius = size.minDimension / 2f
                    val center = Offset(size.width / 2f, size.height / 2f)

                    drawCircle(color = Color(0xFF070A10), radius = radius)
                    drawCircle(color = accent, radius = radius - 2f, style = Stroke(width = 3f))
                    drawCircle(color = BorderSteel, radius = radius * 0.75f, style = Stroke(width = 1.5f))

                    val rad = Math.toRadians(platterAngle.toDouble())
                    val markerEnd = Offset(
                        x = center.x + (radius * 0.85f) * cos(rad).toFloat(),
                        y = center.y + (radius * 0.85f) * sin(rad).toFloat()
                    )
                    drawLine(color = accent, start = center, end = markerEnd, strokeWidth = 4f, cap = StrokeCap.Round)
                    drawCircle(color = Color(0xFF111827), radius = radius * 0.42f)
                    drawCircle(color = CrimsonBass, radius = radius * 0.42f, style = Stroke(width = 2f))
                }
                Text(
                    text = "${ camelotKey }A",
                    color = Color.White,
                    fontSize = 9.sp,
                    fontWeight = FontWeight.ExtraBold,
                    fontFamily = FontFamily.Monospace
                )
            }

            Column(modifier = Modifier.weight(1f)) {
                Text(
                    "RGB WAVEFORM (TAP TO SEEK) • RED=SUB  GRN=VOCAL  CYN=DRUMS",
                    color = Color(0xFF94A3B8),
                    fontSize = 7.sp,
                    fontFamily = FontFamily.Monospace
                )
                Spacer(modifier = Modifier.height(2.dp))
                Canvas(
                    modifier = Modifier
                        .fillMaxWidth()
                        .height(54.dp)
                        .clip(RoundedCornerShape(6.dp))
                        .background(Color(0xFF06090F))
                        .border(1.dp, BorderSteel, RoundedCornerShape(6.dp))
                        .pointerInput(Unit) {
                            detectTapGestures { offset ->
                                val prog = (offset.x / size.width).coerceIn(0f, 0.99f)
                                repo.nativeSeekDeckProgress(deckIndex, prog)
                            }
                        }
                        .pointerInput(Unit) {
                            detectDragGestures { change, _ ->
                                val prog = (change.position.x / size.width).coerceIn(0f, 0.99f)
                                repo.nativeSeekDeckProgress(deckIndex, prog)
                            }
                        }
                ) {
                    val w = size.width
                    val h = size.height
                    val midY = h / 2f
                    val binCount = waveformBins.size
                    val barW = w / binCount

                    for (i in 0 until binCount) {
                        val normIdx = i.toFloat() / binCount
                        val amp = (waveformBins[i] * (0.65f + 0.35f * stemEnvs[1])).coerceIn(0.08f, 0.96f)
                        val barH = amp * (h * 0.85f)
                        val x = i * barW

                        val barColor = when {
                            normIdx < trackProgress -> when (i % 3) {
                                0 -> CrimsonBass
                                1 -> EmeraldVocal
                                else -> accent
                            }
                            else -> Color(0xFF334155)
                        }
                        drawLine(
                            color = barColor,
                            start = Offset(x + barW * 0.5f, midY - barH * 0.5f),
                            end = Offset(x + barW * 0.5f, midY + barH * 0.5f),
                            strokeWidth = (barW * 0.72f).coerceAtLeast(2f),
                            cap = StrokeCap.Round
                        )
                    }

                    val playheadX = (trackProgress * w).coerceIn(0f, w)
                    drawLine(Color.White, Offset(playheadX, 0f), Offset(playheadX, h), strokeWidth = 3f)
                }

                Spacer(modifier = Modifier.height(3.dp))

                Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(3.dp)) {
                    HardwareMiniPad("CUE 1", accent) { repo.nativeTriggerHotCue(deckIndex, 0) }
                    HardwareMiniPad("CUE 2", accent) { repo.nativeTriggerHotCue(deckIndex, 1) }
                    HardwareMiniPad("4-BEAT", EmeraldVocal) { repo.nativeToggleBeatLoop(deckIndex, 4.0) }
                    HardwareMiniPad("1/2 STUTTER", AmberGold) { repo.nativeTriggerNeuralPadMacro(deckIndex, 6) }
                    HardwareMiniPad("BRAKE", CrimsonBass) { repo.nativeSetChannelParam(deckIndex, 16, 1f) }
                }
            }
        }

        Spacer(modifier = Modifier.height(5.dp))

        Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(4.dp)) {
            CrateTabChip("🎛 5 NEURAL STEMS & PADS", activeDeckRackTab == 0, accent) { activeDeckRackTab = 0 }
            CrateTabChip("🎚 4-BAND EQ • VCF • KEY", activeDeckRackTab == 1, AmberGold) { activeDeckRackTab = 1 }
        }

        Spacer(modifier = Modifier.height(5.dp))

        if (activeDeckRackTab == 0) {
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .height(110.dp)
                    .background(Color(0xFF080B12), RoundedCornerShape(6.dp))
                    .border(1.dp, BorderSteel, RoundedCornerShape(6.dp))
                    .padding(6.dp),
                horizontalArrangement = Arrangement.spacedBy(5.dp)
            ) {
                val stemConfigs = listOf(
                    Triple("DRUMS", CyanDeckA, 9),
                    Triple("SUB-BASS", CrimsonBass, 10),
                    Triple("SYNTH", AmberGold, 11),
                    Triple("VOCALS", EmeraldVocal, 12),
                    Triple("AIR/SNAP", VioletNeural, 22)
                )
                stemConfigs.forEachIndexed { sIdx, (label, col, paramId) ->
                    VerticalStemHardwareFader(
                        label = label,
                        gainValue = stemGains[sIdx],
                        liveEnvelope = stemEnvs[sIdx],
                        color = col,
                        modifier = Modifier.weight(1f)
                    ) { newGain ->
                        stemGains[sIdx] = newGain
                        repo.nativeSetChannelParam(deckIndex, paramId, newGain)
                    }
                }
                VerticalStemHardwareFader(
                    label = "CH VOL",
                    gainValue = fader,
                    liveEnvelope = (stemEnvs[0] + stemEnvs[1]) * 0.5f,
                    color = Color.White,
                    modifier = Modifier.weight(1f)
                ) { newFader ->
                    fader = newFader
                    repo.nativeSetChannelParam(deckIndex, 0, newFader)
                }
            }

            Spacer(modifier = Modifier.height(4.dp))

            val macroLabels = listOf(
                "MIC ACAPELLA" to EmeraldVocal,
                "INST ONLY" to CyanDeckA,
                "SUB+DRUMS" to CrimsonBass,
                "RESET 100%" to Color.White,
                "RISER SWEEP" to VioletNeural,
                "SUB IMPACT" to OrangeDeckB,
                "1/2 ROLL" to AmberGold,
                "+2 SEMI LIFT" to CyanDeckA
            )
            Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(3.dp)) {
                for (i in 0 until 4) {
                    HardwareMiniPad(macroLabels[i].first, macroLabels[i].second, Modifier.weight(1f)) {
                        repo.nativeTriggerNeuralPadMacro(deckIndex, i)
                    }
                }
            }
            Spacer(modifier = Modifier.height(3.dp))
            Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(3.dp)) {
                for (i in 4 until 8) {
                    HardwareMiniPad(macroLabels[i].first, macroLabels[i].second, Modifier.weight(1f)) {
                        repo.nativeTriggerNeuralPadMacro(deckIndex, i)
                    }
                }
            }
        } else {
            Column(
                modifier = Modifier
                    .fillMaxWidth()
                    .background(Color(0xFF080B12), RoundedCornerShape(6.dp))
                    .border(1.dp, BorderSteel, RoundedCornerShape(6.dp))
                    .padding(6.dp),
                verticalArrangement = Arrangement.spacedBy(4.dp)
            ) {
                Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                    ProHardwareBarFader("EQ LOW (SUB)", eqLo, -1f..1f, CrimsonBass, Modifier.weight(1f)) {
                        eqLo = it; repo.nativeSetChannelParam(deckIndex, 1, it)
                    }
                    ProHardwareBarFader("EQ LO-MID", eqLm, -1f..1f, OrangeDeckB, Modifier.weight(1f)) {
                        eqLm = it; repo.nativeSetChannelParam(deckIndex, 2, it)
                    }
                    ProHardwareBarFader("EQ HI-MID", eqHm, -1f..1f, EmeraldVocal, Modifier.weight(1f)) {
                        eqHm = it; repo.nativeSetChannelParam(deckIndex, 3, it)
                    }
                    ProHardwareBarFader("EQ HIGH", eqHi, -1f..1f, CyanDeckA, Modifier.weight(1f)) {
                        eqHi = it; repo.nativeSetChannelParam(deckIndex, 4, it)
                    }
                }
                Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                    ProHardwareBarFader("XONE VCF CUT", vcfCutoff, 0f..1f, AmberGold, Modifier.weight(1f)) {
                        vcfCutoff = it; repo.nativeSetChannelParam(deckIndex, 7, it)
                    }
                    ProHardwareBarFader("VCF CRUNCH", vcfCrunch, 0f..1f, CrimsonBass, Modifier.weight(1f)) {
                        vcfCrunch = it; repo.nativeSetChannelParam(deckIndex, 8, it)
                    }
                    ProHardwareBarFader("KEY SEMITONE", pitchSemi, -6f..6f, VioletNeural, Modifier.weight(1f)) {
                        pitchSemi = it; repo.nativeSetChannelParam(deckIndex, 19, it)
                    }
                    ProHardwareBarFader("3D WIDENER", stereoWiden, 0f..1f, CyanDeckA, Modifier.weight(1f)) {
                        stereoWiden = it; repo.nativeSetChannelParam(deckIndex, 21, it)
                    }
                }
                Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                    ProHardwareBarFader("TRANSIENT PUNCH", transientPunch, 0f..1f, EmeraldVocal, Modifier.weight(1f)) {
                        transientPunch = it; repo.nativeSetChannelParam(deckIndex, 20, it)
                    }
                    ProHardwareBarFader("CHANNEL FADER", fader, 0f..1f, Color.White, Modifier.weight(1f)) {
                        fader = it; repo.nativeSetChannelParam(deckIndex, 0, it)
                    }
                }
            }
        }
    }
}

@Composable
fun VerticalStemHardwareFader(
    label: String,
    gainValue: Float,
    liveEnvelope: Float,
    color: Color,
    modifier: Modifier = Modifier,
    onValueChange: (Float) -> Unit
) {
    Column(
        modifier = modifier.fillMaxHeight(),
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        Text(
            text = "${(gainValue * 100).toInt()}%",
            color = color,
            fontSize = 7.sp,
            fontFamily = FontFamily.Monospace,
            fontWeight = FontWeight.Bold
        )
        Box(
            modifier = Modifier
                .weight(1f)
                .fillMaxWidth()
                .padding(vertical = 2.dp)
                .pointerInput(Unit) {
                    detectDragGestures { change, _ ->
                        val norm = 1.0f - (change.position.y / size.height).coerceIn(0f, 1f)
                        onValueChange((norm * 1.35f).coerceIn(0f, 1.35f))
                    }
                }
                .pointerInput(Unit) {
                    detectTapGestures { offset ->
                        val norm = 1.0f - (offset.y / size.height).coerceIn(0f, 1f)
                        onValueChange((norm * 1.35f).coerceIn(0f, 1.35f))
                    }
                }
        ) {
            Canvas(modifier = Modifier.fillMaxSize()) {
                val w = size.width
                val h = size.height
                val normGain = (gainValue / 1.35f).coerceIn(0f, 1f)
                val envNorm = (liveEnvelope * normGain).coerceIn(0f, 1f)

                drawRoundRect(
                    color = Color(0xFF141B29),
                    topLeft = Offset(w * 0.2f, 0f),
                    size = Size(w * 0.6f, h),
                    cornerRadius = CornerRadius(4f, 4f)
                )

                val envH = h * envNorm
                drawRoundRect(
                    color = color.copy(alpha = 0.38f),
                    topLeft = Offset(w * 0.22f, h - envH),
                    size = Size(w * 0.56f, envH),
                    cornerRadius = CornerRadius(4f, 4f)
                )

                val activeH = h * normGain
                drawRoundRect(
                    color = color,
                    topLeft = Offset(w * 0.35f, h - activeH),
                    size = Size(w * 0.30f, activeH),
                    cornerRadius = CornerRadius(3f, 3f)
                )

                val capY = (h * (1f - normGain)).coerceIn(4f, h - 8f)
                drawRoundRect(
                    color = Color.White,
                    topLeft = Offset(w * 0.08f, capY - 4f),
                    size = Size(w * 0.84f, 8f),
                    cornerRadius = CornerRadius(3f, 3f)
                )
            }
        }
        Text(
            text = label,
            color = Color.White,
            fontSize = 7.sp,
            fontWeight = FontWeight.ExtraBold,
            fontFamily = FontFamily.Monospace,
            maxLines = 1
        )
    }
}

@Composable
fun ProHardwareBarFader(
    label: String,
    value: Float,
    range: ClosedFloatingPointRange<Float>,
    accent: Color,
    modifier: Modifier = Modifier,
    onValueChange: (Float) -> Unit
) {
    val span = (range.endInclusive - range.start).coerceAtLeast(0.001f)
    val norm = ((value - range.start) / span).coerceIn(0f, 1f)

    Column(modifier = modifier) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween
        ) {
            Text(label, color = Color(0xFFCBD5E1), fontSize = 7.sp, fontWeight = FontWeight.Bold, fontFamily = FontFamily.Monospace)
            Text("%.2f".format(value), color = accent, fontSize = 7.sp, fontWeight = FontWeight.Bold, fontFamily = FontFamily.Monospace)
        }
        Spacer(modifier = Modifier.height(2.dp))
        Canvas(
            modifier = Modifier
                .fillMaxWidth()
                .height(16.dp)
                .clip(RoundedCornerShape(4.dp))
                .background(Color(0xFF090D16))
                .border(1.dp, BorderSteel, RoundedCornerShape(4.dp))
                .pointerInput(range) {
                    detectDragGestures { change, _ ->
                        val n = (change.position.x / size.width).coerceIn(0f, 1f)
                        onValueChange(range.start + n * span)
                    }
                }
                .pointerInput(range) {
                    detectTapGestures { offset ->
                        val n = (offset.x / size.width).coerceIn(0f, 1f)
                        onValueChange(range.start + n * span)
                    }
                }
        ) {
            val w = size.width
            val h = size.height
            drawRoundRect(
                color = accent.copy(alpha = 0.78f),
                topLeft = Offset(0f, 0f),
                size = Size(w * norm, h),
                cornerRadius = CornerRadius(4f, 4f)
            )
            val thumbX = (w * norm).coerceIn(4f, w - 4f)
            drawLine(
                color = Color.White,
                start = Offset(thumbX, 1f),
                end = Offset(thumbX, h - 1f),
                strokeWidth = 4f,
                cap = StrokeCap.Round
            )
        }
    }
}

@Composable
fun SegmentedDualVuMeter(vuA: Float, vuB: Float) {
    Canvas(
        modifier = Modifier
            .size(84.dp, 26.dp)
            .clip(RoundedCornerShape(4.dp))
            .background(Color(0xFF05070B))
            .border(1.dp, BorderSteel, RoundedCornerShape(4.dp))
            .padding(3.dp)
    ) {
        val segments = 12
        val segW = size.width / segments
        val h = size.height
        for (i in 0 until segments) {
            val threshold = (i + 1).toFloat() / segments
            val col = when {
                i >= 10 -> CrimsonBass
                i >= 7 -> AmberGold
                else -> EmeraldVocal
            }
            val activeA = vuA >= threshold
            val activeB = vuB >= threshold
            drawRect(
                color = if (activeA) col else Color(0xFF1E293B),
                topLeft = Offset(i * segW + 1f, 1f),
                size = Size(segW - 2f, h * 0.42f)
            )
            drawRect(
                color = if (activeB) col else Color(0xFF1E293B),
                topLeft = Offset(i * segW + 1f, h * 0.54f),
                size = Size(segW - 2f, h * 0.42f)
            )
        }
    }
}

@Composable
fun HardwareActionButton(
    label: String,
    borderColor: Color,
    bgColor: Color,
    onClick: () -> Unit
) {
    Box(
        modifier = Modifier
            .clip(RoundedCornerShape(6.dp))
            .background(bgColor)
            .border(1.dp, borderColor, RoundedCornerShape(6.dp))
            .clickable { onClick() }
            .padding(horizontal = 8.dp, vertical = 6.dp),
        contentAlignment = Alignment.Center
    ) {
        Text(
            text = label,
            color = Color.White,
            fontSize = 8.sp,
            fontWeight = FontWeight.ExtraBold,
            fontFamily = FontFamily.Monospace,
            maxLines = 1
        )
    }
}

@Composable
fun HardwareMiniPad(
    label: String,
    accent: Color,
    modifier: Modifier = Modifier,
    onClick: () -> Unit
) {
    Box(
        modifier = modifier
            .clip(RoundedCornerShape(5.dp))
            .background(accent.copy(alpha = 0.14f))
            .border(1.dp, accent.copy(alpha = 0.8f), RoundedCornerShape(5.dp))
            .clickable { onClick() }
            .padding(horizontal = 5.dp, vertical = 4.dp),
        contentAlignment = Alignment.Center
    ) {
        Text(
            text = label,
            color = Color.White,
            fontSize = 7.sp,
            fontWeight = FontWeight.ExtraBold,
            fontFamily = FontFamily.Monospace,
            maxLines = 1
        )
    }
}

@Composable
fun CrateTabChip(
    label: String,
    selected: Boolean,
    accent: Color,
    onClick: () -> Unit
) {
    Box(
        modifier = Modifier
            .clip(RoundedCornerShape(6.dp))
            .background(if (selected) accent.copy(alpha = 0.22f) else PanelElevated)
            .border(1.dp, if (selected) accent else BorderSteel, RoundedCornerShape(6.dp))
            .clickable { onClick() }
            .padding(horizontal = 8.dp, vertical = 4.dp)
    ) {
        Text(
            text = label,
            color = if (selected) accent else Color(0xFF94A3B8),
            fontSize = 8.sp,
            fontWeight = FontWeight.ExtraBold,
            fontFamily = FontFamily.Monospace
        )
    }
}
