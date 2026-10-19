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

// Pro Club Hardware Palette
private val BgObsidian = Color(0xFF06080D)
private val PanelGunmetal = Color(0xFF0E131D)
private val PanelElevated = Color(0xFF151C28)
private val BorderSteel = Color(0xFF263248)
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
        mutableStateOf("1.02 GB SELF-NEURAL ENGINE READY • 24dB SUB-BASS EXTRACTOR • CONTINUOUS AUTO-DJ")
    }

    // Track Titles on Decks
    var deckATitle by remember { mutableStateOf("Peak-Time Tech House Roller") }
    var deckBTitle by remember { mutableStateOf("Acid Techno Underground Sub") }
    var deckATrackId by remember { mutableStateOf("studio_0") }
    var deckBTrackId by remember { mutableStateOf("studio_1") }

    // Continuous Auto-DJ Queue State
    var autoDjEnabled by remember { mutableStateOf(true) }
    val remixQueue = remember { mutableStateListOf<SongTrack>() }
    var masterActiveDeck by remember { mutableIntStateOf(0) } // 0 = Deck A, 1 = Deck B
    var morphInProgress by remember { mutableStateOf(false) }
    var morphProgress by remember { mutableFloatStateOf(0f) }

    // Master Studio WAV Recorder State
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
    var neuralSidechain by remember { mutableFloatStateOf(0.40f) }
    var neuralSubBoom by remember { mutableFloatStateOf(0.30f) }
    var neuralAirGlow by remember { mutableFloatStateOf(0.30f) }
    var beatFxWet by remember { mutableFloatStateOf(0f) }
    var selectedMorphStyle by remember { mutableIntStateOf(0) }

    // Real-Time Deck Telemetry
    var vuDeckA by remember { mutableFloatStateOf(0f) }
    var vuDeckB by remember { mutableFloatStateOf(0f) }
    var bpmDeckA by remember { mutableFloatStateOf(128f) }
    var bpmDeckB by remember { mutableFloatStateOf(130f) }
    var keyDeckA by remember { mutableIntStateOf(8) }
    var keyDeckB by remember { mutableIntStateOf(9) }
    var playingDeckA by remember { mutableStateOf(false) }
    var playingDeckB by remember { mutableStateOf(false) }

    // Local File Picker Routing (0 = Deck A, 1 = Deck B, 2 = Auto-DJ Queue)
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
                        statusBanner = "Loading Local Audio into DECK A with 5-Stem Neural Split..."
                        deckATitle = pickedTrack.title
                        deckATrackId = pickedTrack.id
                        repo.loadTrackIntoDeck(0, pickedTrack, autoPlay = true)
                        masterActiveDeck = 0
                        statusBanner = "DECK A Playing Local Track • Sub-Bass Isolated!"
                    }
                    1 -> {
                        statusBanner = "Loading Local Audio into DECK B with 5-Stem Neural Split..."
                        deckBTitle = pickedTrack.title
                        deckBTrackId = pickedTrack.id
                        repo.loadTrackIntoDeck(1, pickedTrack, autoPlay = true)
                        masterActiveDeck = 1
                        statusBanner = "DECK B Playing Local Track • Sub-Bass Isolated!"
                    }
                    else -> {
                        remixQueue.add(pickedTrack)
                        statusBanner = "Added Local Track to Continuous Auto-Remix Queue (${remixQueue.size} queued)"
                        if (autoDjEnabled && !repo.nativeGetDeckPlayState(0) && !repo.nativeGetDeckPlayState(1)) {
                            val first = remixQueue.removeAt(0)
                            deckATitle = first.title
                            deckATrackId = first.id
                            repo.loadTrackIntoDeck(0, first, autoPlay = true)
                            masterActiveDeck = 0
                        }
                    }
                }
            }
        }
    }

    // Startup Initialization + Continuous Auto-DJ Neural Loop
    LaunchedEffect(Unit) {
        offlineTracks = repo.scanOfflineLibrary()
        onlineTracks = repo.getBuiltInStudioTracks()
        // Pre-populate Queue with 3 harmonic studio tracks so Auto-DJ works immediately
        remixQueue.addAll(repo.getBuiltInStudioTracks().drop(2).take(4))

        while (true) {
            vuDeckA = repo.nativeGetChannelVu(0)
            vuDeckB = repo.nativeGetChannelVu(1)
            bpmDeckA = repo.nativeGetDetectedBpm(0)
            bpmDeckB = repo.nativeGetDetectedBpm(1)
            keyDeckA = repo.nativeGetDetectedCamelotKey(0)
            keyDeckB = repo.nativeGetDetectedCamelotKey(1)
            playingDeckA = repo.nativeGetDeckPlayState(0)
            playingDeckB = repo.nativeGetDeckPlayState(1)
            crossfader = repo.nativeGetCrossfader()
            morphInProgress = repo.nativeIsMorphActive()
            morphProgress = repo.nativeGetMorphProgress()
            isRecording = repo.nativeIsRecording()
            isRecordPaused = repo.nativeIsRecordPaused()
            if (isRecording) {
                recordSeconds = repo.nativeGetRecordDurationSec()
            }

            // Autonomous Continuous Neural Auto-DJ Engine
            if (autoDjEnabled && !morphInProgress) {
                // If user added tracks to queue and nothing is playing, auto-start Deck A
                if (!playingDeckA && !playingDeckB && remixQueue.isNotEmpty()) {
                    val starter = remixQueue.removeAt(0)
                    deckATitle = starter.title
                    deckATrackId = starter.id
                    repo.loadTrackIntoDeck(0, starter, autoPlay = true)
                    repo.nativeSetMasterParam(0, -1f)
                    masterActiveDeck = 0
                    statusBanner = "Auto-DJ Started: Playing '${starter.title}' on DECK A"
                }

                val remA = repo.nativeGetDeckRemainingSeconds(0)
                val remB = repo.nativeGetDeckRemainingSeconds(1)

                if (playingDeckA && masterActiveDeck == 0 && remA in 0.4f..15.0f) {
                    val nextTrack = if (remixQueue.isNotEmpty()) {
                        remixQueue.removeAt(0)
                    } else {
                        repo.pickBestNextAutoDjTrack(
                            candidates = (onlineTracks + offlineTracks).distinctBy { it.id },
                            refBpm = bpmDeckA,
                            refKey = keyDeckA,
                            excludeId = deckATrackId
                        )
                    }
                    if (nextTrack != null) {
                        statusBanner = "Auto-DJ Neural Bass-Carry Mix: DECK A ➔ DECK B ('${nextTrack.title}')"
                        deckBTitle = nextTrack.title
                        deckBTrackId = nextTrack.id
                        repo.loadTrackIntoDeck(1, nextTrack, autoPlay = true)
                        repo.nativeTriggerButterMorph(0, 1, 12.0)
                        masterActiveDeck = 1
                    }
                } else if (playingDeckB && masterActiveDeck == 1 && remB in 0.4f..15.0f) {
                    val nextTrack = if (remixQueue.isNotEmpty()) {
                        remixQueue.removeAt(0)
                    } else {
                        repo.pickBestNextAutoDjTrack(
                            candidates = (onlineTracks + offlineTracks).distinctBy { it.id },
                            refBpm = bpmDeckB,
                            refKey = keyDeckB,
                            excludeId = deckBTrackId
                        )
                    }
                    if (nextTrack != null) {
                        statusBanner = "Auto-DJ Neural Bass-Carry Mix: DECK B ➔ DECK A ('${nextTrack.title}')"
                        deckATitle = nextTrack.title
                        deckATrackId = nextTrack.id
                        repo.loadTrackIntoDeck(0, nextTrack, autoPlay = true)
                        repo.nativeTriggerButterMorph(1, 0, 12.0)
                        masterActiveDeck = 0
                    }
                }
            }
            delay(60)
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
        // 1. TOP PIONEER / ALPHATHETA HARDWARE COMMAND & MASTER RECORDER BAR
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .background(
                    Brush.horizontalGradient(listOf(Color(0xFF0F172A), Color(0xFF162032), Color(0xFF0F172A))),
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
                            .background(CyanDeckA.copy(alpha = 0.18f), RoundedCornerShape(4.dp))
                            .border(1.dp, CyanDeckA, RoundedCornerShape(4.dp))
                            .padding(horizontal = 5.dp, vertical = 1.dp)
                    ) {
                        Text("1.02 GB NEURAL CORE", color = CyanDeckA, fontSize = 8.sp, fontWeight = FontWeight.ExtraBold, fontFamily = FontFamily.Monospace)
                    }
                    Spacer(modifier = Modifier.width(6.dp))
                    Text(
                        "APEX-DJ // CDJ-3000 + DJM-V10 + EUPHONIA WORKSTATION",
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

            // Master WAV Recorder Controls (REC, PAUSE REC, STOP/SAVE)
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
                            text = if (isRecordPaused) "▶ RESUME REC" else "⏸ PAUSE REC",
                            color = if (isRecordPaused) Color.Black else Color.White,
                            fontSize = 9.sp,
                            fontWeight = FontWeight.ExtraBold,
                            fontFamily = FontFamily.Monospace
                        )
                    }
                }

                // Dual Segmented LED Master VU Meters
                SegmentedDualVuMeter(vuA = vuDeckA, vuB = vuDeckB)
            }
        }

        Spacer(modifier = Modifier.height(5.dp))

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
                placeholder = { Text("Search songs, artists, remixes...", color = Color(0xFF94A3B8), fontSize = 10.sp) },
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
                    statusBanner = "Searching Online Streams & Harmonic Matches..."
                    onlineTracks = repo.searchOnlineSongs(searchQuery)
                    statusBanner = "Found ${onlineTracks.size} tracks • Ranked by Camelot Key & BPM!"
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
                statusBanner = if (autoDjEnabled) {
                    "🟢 Continuous Neural Auto-DJ ON (Bass-Carry & Harmonic Queue Active)"
                } else {
                    "⚪ Continuous Auto-DJ Paused (Manual Club Control)"
                }
            }
        }

        Spacer(modifier = Modifier.height(5.dp))

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
                        HardwareActionButton("⚡ MIX NEXT QUEUE NOW", CrimsonBass, Color(0xFF450A0A)) {
                            val nextSong = remixQueue.removeAt(0)
                            val fromDeck = if (playingDeckA && !playingDeckB) 0 else if (playingDeckB && !playingDeckA) 1 else masterActiveDeck
                            val toDeck = if (fromDeck == 0) 1 else 0
                            scope.launch {
                                if (toDeck == 0) { deckATitle = nextSong.title; deckATrackId = nextSong.id }
                                else { deckBTitle = nextSong.title; deckBTrackId = nextSong.id }
                                statusBanner = "⚡ Instant Neural Bass-Swap Mix ➔ '${nextSong.title}' on DECK ${if (toDeck == 0) "A" else "B"}"
                                repo.loadTrackIntoDeck(toDeck, nextSong, autoPlay = true)
                                repo.nativeTriggerButterMorph(fromDeck, toDeck, 10.0)
                                masterActiveDeck = toDeck
                            }
                        }
                    }
                    HardwareActionButton(if (crateExpanded) "▲ HIDE CRATE" else "▼ SHOW CRATE", BorderSteel, PanelElevated) {
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

                // Rank Online & Offline lists by Harmonic Compatibility so the #1 Perfect Song is highlighted first
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

                // Top "NEXT PERFECT SONG" Banner for Online Search & Offline Library
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
                                "👑 AI NEXT PERFECT SONG RECOMMENDATION • ${topBadge.targetLabel} • [${topBadge.transitionAdvice}]",
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
                                label = "⚡ SMART AUTO-MIX TO ${if (topBadge.recommendedDeck == 0) "DECK A" else "DECK B"}",
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
                                    statusBanner = "👑 Smart-Mixed Perfect Match '${topSong.title}' into DECK ${if (toDeck == 0) "A" else "B"}!"
                                }
                            }
                            HardwareActionButton("+ QUEUE", AmberGold, Color(0xFF1E293B)) {
                                remixQueue.add(topSong)
                                statusBanner = "Queued Perfect Match '${topSong.title}' (#${remixQueue.size})"
                            }
                        }
                    }
                    Spacer(modifier = Modifier.height(4.dp))
                }

                LazyColumn(
                    modifier = Modifier
                        .fillMaxWidth()
                        .height(108.dp)
                        .background(Color(0xFF080B11), RoundedCornerShape(6.dp))
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
                                        statusBanner = "Added '${song.title}' to Continuous Auto-DJ Queue (${remixQueue.size})"
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

        Spacer(modifier = Modifier.height(6.dp))

        // 4. DUAL PIONEER CDJ-3000 + NEURAL 5-STEM HARDWARE DECKS
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(6.dp)
        ) {
            ProClubDeckConsole(
                deckIndex = 0,
                deckTitle = "DECK A // CDJ-3000",
                trackTitle = deckATitle,
                accent = CyanDeckA,
                bpm = bpmDeckA,
                camelotKey = keyDeckA,
                repo = repo,
                modifier = Modifier.weight(1f)
            )
            ProClubDeckConsole(
                deckIndex = 1,
                deckTitle = "DECK B // CDJ-3000",
                trackTitle = deckBTitle,
                accent = OrangeDeckB,
                bpm = bpmDeckB,
                camelotKey = keyDeckB,
                repo = repo,
                modifier = Modifier.weight(1f)
            )
        }

        Spacer(modifier = Modifier.height(6.dp))

        // 5. MASTER RUPERT NEVE TRANSFORMER & NEURAL BASS-SWAP TRANSITION CONSOLE
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
                    "DJM-V10 + EUPHONIA MASTER BUS // 24dB SUB-BASS BRIDGE • RUPERT NEVE TRANSFORMER",
                    color = AmberGold,
                    fontSize = 10.sp,
                    fontFamily = FontFamily.Monospace,
                    fontWeight = FontWeight.ExtraBold
                )
                if (morphInProgress) {
                    Text(
                        "⚡ NEURAL BASS-CARRY & SWAP ACTIVE: ${(morphProgress * 100).toInt()}%",
                        color = EmeraldVocal,
                        fontSize = 9.sp,
                        fontFamily = FontFamily.Monospace,
                        fontWeight = FontWeight.ExtraBold
                    )
                }
            }

            Spacer(modifier = Modifier.height(4.dp))

            // 6 AI Transition Style Selector Pads
            val morphNames = listOf(
                "1. BASS-CARRY & SWAP",
                "2. ACAPELLA RIDE",
                "3. OIL & WATER",
                "4. FESTIVAL SLAM",
                "5. TECHNO HYPNOSIS",
                "6. SHIMMER WASH"
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
                                statusBanner = "Neural Transition Engine Mode: $name"
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

            // Custom Hardware Master Faders (Zero Default Purple Material Sliders!)
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
                ProHardwareBarFader("NEURAL DUCKING", neuralSidechain, 0f..1f, CyanDeckA, Modifier.weight(1f)) {
                    neuralSidechain = it; repo.nativeSetMasterParam(11, it)
                }
                ProHardwareBarFader("SUB-BASS SYNTH", neuralSubBoom, 0f..1f, CrimsonBass, Modifier.weight(1f)) {
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

            // Magvel Pro Crossfader + Instant Neural Morph Triggers
            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(6.dp)
            ) {
                HardwareActionButton("⚡ NEURAL BASS-SWAP A ➔ B", CyanDeckA, Color(0xFF082F49)) {
                    repo.nativeTriggerButterMorph(0, 1, 12.0)
                    masterActiveDeck = 1
                    statusBanner = "⚡ Carrying DECK A Sub-Bass & Harmonic Morphing into DECK B..."
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

                HardwareActionButton("⚡ NEURAL BASS-SWAP B ➔ A", OrangeDeckB, Color(0xFF431407)) {
                    repo.nativeTriggerButterMorph(1, 0, 12.0)
                    masterActiveDeck = 0
                    statusBanner = "⚡ Carrying DECK B Sub-Bass & Harmonic Morphing into DECK A..."
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
    var activeDeckRackTab by remember { mutableIntStateOf(0) } // 0: 5-Stems & Pads, 1: Surgical EQ & VCF
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
    var waveformBins by remember { mutableStateOf(FloatArray(64) { 0.35f }) }

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
                platterAngle = (platterAngle + (bpm / 120f) * 5.5f) % 360f
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
        // Deck Header & OLED Telemetry Readout
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

        // TRUE CIRCULAR CDJ-3000 SPINNING JOGWHEEL + RGB 3-BAND WAVEFORM SCOPE
        Row(
            modifier = Modifier.fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(6.dp)
        ) {
            // Circular Spinning Platter (Never stretches into an oval!)
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

                    // Outer metallic ring
                    drawCircle(color = Color(0xFF070A10), radius = radius)
                    drawCircle(color = accent, radius = radius - 2f, style = Stroke(width = 3f))
                    drawCircle(color = BorderSteel, radius = radius * 0.75f, style = Stroke(width = 1.5f))

                    // Rotating strobe ticks & needle vector
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

            // Live 3-Band RGB Club Waveform Scrubber + Stem Activity Meters
            Column(modifier = Modifier.weight(1f)) {
                Text(
                    "RGB 3-BAND WAVEFORM (TAP/DRAG TO SEEK) • RED=SUB  GRN=VOCAL  CYN=DRUMS",
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

                    // Playhead red/white laser line
                    val playheadX = (trackProgress * w).coerceIn(0f, w)
                    drawLine(Color.White, Offset(playheadX, 0f), Offset(playheadX, h), strokeWidth = 3f)
                }

                Spacer(modifier = Modifier.height(3.dp))

                // Quick Transport & Loop Strip under Waveform
                Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(3.dp)) {
                    HardwareMiniPad("CUE 1", accent) { repo.nativeTriggerHotCue(deckIndex, 0) }
                    HardwareMiniPad("CUE 2", accent) { repo.nativeTriggerHotCue(deckIndex, 1) }
                    HardwareMiniPad("4-BEAT LOOP", EmeraldVocal) { repo.nativeToggleBeatLoop(deckIndex, 4.0) }
                    HardwareMiniPad("1/2 STUTTER", AmberGold) { repo.nativeTriggerNeuralPadMacro(deckIndex, 6) }
                    HardwareMiniPad("BRAKE", CrimsonBass) { repo.nativeSetChannelParam(deckIndex, 16, 1f) }
                }
            }
        }

        Spacer(modifier = Modifier.height(5.dp))

        // Tabbed Rack Switcher: [ 🎛 5 NEURAL STEMS & MACRO PADS ] vs [ 🎚 SURGICAL EQ, VCF & KEY ]
        Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(4.dp)) {
            CrateTabChip("🎛 5 NEURAL STEMS & PADS", activeDeckRackTab == 0, accent) { activeDeckRackTab = 0 }
            CrateTabChip("🎚 4-BAND EQ • VCF • KEY", activeDeckRackTab == 1, AmberGold) { activeDeckRackTab = 1 }
        }

        Spacer(modifier = Modifier.height(5.dp))

        if (activeDeckRackTab == 0) {
            // 5 VERTICAL HARDWARE STEM FADERS WITH LIVE BOUNCING LED VU BARS
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
                // Channel Master Volume Fader
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

            // 8 Instant Neural Performance Pads
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
            // Surgical 4-Band EQ, Allen & Heath Xone VCF & Neural Key Shift Rack
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

                // Dark Recessed Slot
                drawRoundRect(
                    color = Color(0xFF141B29),
                    topLeft = Offset(w * 0.2f, 0f),
                    size = Size(w * 0.6f, h),
                    cornerRadius = CornerRadius(4f, 4f)
                )

                // Live Audio Envelope Glow Inside Fader
                val envH = h * envNorm
                drawRoundRect(
                    color = color.copy(alpha = 0.38f),
                    topLeft = Offset(w * 0.22f, h - envH),
                    size = Size(w * 0.56f, envH),
                    cornerRadius = CornerRadius(4f, 4f)
                )

                // Active Fader Level Bar
                val activeH = h * normGain
                drawRoundRect(
                    color = color,
                    topLeft = Offset(w * 0.35f, h - activeH),
                    size = Size(w * 0.30f, activeH),
                    cornerRadius = CornerRadius(3f, 3f)
                )

                // Metallic DJM Fader Cap
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
