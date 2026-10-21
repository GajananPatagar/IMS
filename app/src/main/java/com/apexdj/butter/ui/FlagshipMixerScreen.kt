package com.apexdj.butter.ui

import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
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
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.apexdj.butter.audio.OnlineAndLocalRepository
import com.apexdj.butter.audio.SongTrack
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

@Composable
fun FlagshipMixerScreen(repo: OnlineAndLocalRepository) {
    val scope = rememberCoroutineScope()
    var selectedTab by remember { mutableIntStateOf(0) }
    var searchQuery by remember { mutableStateOf("") }
    var searchResults by remember { mutableStateOf<List<SongTrack>>(emptyList()) }
    var statusBanner by remember { mutableStateOf("Gemini 4 Argon V8 Engine • Lock-Free 96kHz/64-Bit DSP Ready") }
    var obbStatusText by remember { mutableStateOf("Auto-Scanning OBB Expansion...") }

    // Cached waveforms & titles (updated ONLY on track load — zero 60fps JNI array allocation stutter!)
    val rgbWaveA by repo.waveDeckA.collectAsState()
    val rgbWaveB by repo.waveDeckB.collectAsState()
    val titleA by repo.trackTitleDeckA.collectAsState()
    val titleB by repo.trackTitleDeckB.collectAsState()

    var crossfader by remember { mutableFloatStateOf(0f) }
    var neveDrive by remember { mutableFloatStateOf(0.65f) }
    var isoLow by remember { mutableFloatStateOf(0f) }
    var isoMid by remember { mutableFloatStateOf(0f) }
    var isoHigh by remember { mutableFloatStateOf(0f) }
    var boostLow by remember { mutableStateOf(false) }
    var boostMid by remember { mutableStateOf(false) }
    var boostHigh by remember { mutableStateOf(false) }
    var masterBeatFx by remember { mutableFloatStateOf(0f) }
    var masterBeatWet by remember { mutableFloatStateOf(0f) }

    var vuDeckA by remember { mutableFloatStateOf(0f) }
    var vuDeckB by remember { mutableFloatStateOf(0f) }
    var bpmDeckA by remember { mutableFloatStateOf(128f) }
    var bpmDeckB by remember { mutableFloatStateOf(130f) }
    var keyDeckA by remember { mutableIntStateOf(8) }
    var keyDeckB by remember { mutableIntStateOf(8) }
    var playheadA by remember { mutableFloatStateOf(0f) }
    var playheadB by remember { mutableFloatStateOf(0f) }
    var morphProgress by remember { mutableFloatStateOf(-1f) }

    LaunchedEffect(Unit) {
        val st = repo.obbManager.autoScanCopyAndMountObb()
        obbStatusText = st.statusMessage

        // Lightweight 30fps scalar telemetry loop (no array allocations, zero GC pause!)
        while (true) {
            vuDeckA = repo.nativeGetChannelVu(0)
            vuDeckB = repo.nativeGetChannelVu(1)
            bpmDeckA = repo.nativeGetDetectedBpm(0)
            bpmDeckB = repo.nativeGetDetectedBpm(1)
            keyDeckA = repo.nativeGetCamelotKey(0)
            keyDeckB = repo.nativeGetCamelotKey(1)
            playheadA = repo.nativeGetPlayheadNormalized(0)
            playheadB = repo.nativeGetPlayheadNormalized(1)
            morphProgress = repo.nativeGetMorphProgress()
            delay(33)
        }
    }

    var targetLocalDeck by remember { mutableIntStateOf(0) }
    val localPicker = rememberLauncherForActivityResult(ActivityResultContracts.GetContent()) { uri: Uri? ->
        if (uri != null) {
            val track = repo.resolveLocalUriToTrack(uri)
            repo.loadTrackIntoDeckAsync(targetLocalDeck, track) { msg -> statusBanner = msg }
        }
    }

    val obbFilePicker = rememberLauncherForActivityResult(ActivityResultContracts.GetContent()) { uri: Uri? ->
        if (uri != null) {
            scope.launch {
                obbStatusText = "Copying .OBB to /Android/obb/com.apexdj.butter/..."
                val st = repo.obbManager.importAndCopyObbFromUri(uri)
                obbStatusText = st.statusMessage
            }
        }
    }

    // Safe Window Insets: Never overlaps the top Android notification bar or navigation bar!
    Column(
        modifier = Modifier
            .fillMaxSize()
            .background(Color(0xFF07090E))
            .statusBarsPadding()
            .navigationBarsPadding()
            .displayCutoutPadding()
            .padding(horizontal = 6.dp, vertical = 4.dp)
            .verticalScroll(rememberScrollState())
    ) {
        // Notification-Safe Top Telemetry Header
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .background(Color(0xFF10141F), RoundedCornerShape(8.dp))
                .border(1.dp, Color(0xFF1F2738), RoundedCornerShape(8.dp))
                .padding(horizontal = 8.dp, vertical = 6.dp),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Column(modifier = Modifier.weight(1f)) {
                Text(
                    text = "APEX-DJ V8 // GEMINI 4 ARGON (V10-LF × DJM-A9 × EUPHONIA × XONE:96)",
                    color = Color(0xFF00E5FF),
                    fontFamily = FontFamily.Monospace,
                    fontWeight = FontWeight.Bold,
                    fontSize = 11.sp
                )
                Text(
                    text = statusBanner,
                    color = Color(0xFFD0D7DE),
                    fontSize = 9.sp,
                    fontFamily = FontFamily.Monospace,
                    maxLines = 1
                )
                Text(
                    text = obbStatusText,
                    color = Color(0xFF57AB5A),
                    fontSize = 8.sp,
                    fontFamily = FontFamily.Monospace,
                    maxLines = 1
                )
            }
            Canvas(
                modifier = Modifier
                    .size(78.dp, 26.dp)
                    .background(Color(0xFF05070B), RoundedCornerShape(4.dp))
            ) {
                val w = size.width
                val h = size.height
                drawLine(Color(0xFF00E5FF), Offset(0f, h * 0.35f), Offset(w * vuDeckA.coerceIn(0f, 1f), h * 0.35f), strokeWidth = 6f)
                drawLine(Color(0xFFFA903E), Offset(0f, h * 0.75f), Offset(w * vuDeckB.coerceIn(0f, 1f), h * 0.75f), strokeWidth = 6f)
            }
        }

        if (morphProgress >= 0f) {
            Spacer(modifier = Modifier.height(3.dp))
            LinearProgressIndicator(
                progress = { morphProgress.coerceIn(0f, 1f) },
                modifier = Modifier.fillMaxWidth().height(5.dp),
                color = Color(0xFFF538A0),
                trackColor = Color(0xFF1F2430)
            )
        }

        Spacer(modifier = Modifier.height(4.dp))

        // 4-Workspace Navigation Bar
        Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(4.dp)) {
            TabButton("1. PRO CONSOLE", selectedTab == 0, Modifier.weight(1f)) { selectedTab = 0 }
            TabButton("2. 6-ENGINE REMIX CRATE", selectedTab == 1, Modifier.weight(1f)) { selectedTab = 1 }
            TabButton("3. 8-PAD SAMPLER & OBB", selectedTab == 2, Modifier.weight(1f)) { selectedTab = 2 }
            TabButton("4. ARGON AI COPILOT", selectedTab == 3, Modifier.weight(1f)) { selectedTab = 3 }
        }

        Spacer(modifier = Modifier.height(4.dp))

        when (selectedTab) {
            0 -> {
                Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                    DeckConsoleCard(
                        deckIndex = 0,
                        title = "DECK A // $titleA",
                        accent = Color(0xFF00E5FF),
                        bpm = bpmDeckA,
                        camelotKey = keyDeckA,
                        playheadNorm = playheadA,
                        rgbWave = rgbWaveA,
                        repo = repo,
                        modifier = Modifier.weight(1f)
                    )
                    DeckConsoleCard(
                        deckIndex = 1,
                        title = "DECK B // $titleB",
                        accent = Color(0xFFFA903E),
                        bpm = bpmDeckB,
                        camelotKey = keyDeckB,
                        playheadNorm = playheadB,
                        rgbWave = rgbWaveB,
                        repo = repo,
                        modifier = Modifier.weight(1f)
                    )
                }

                Spacer(modifier = Modifier.height(4.dp))

                Column(
                    modifier = Modifier
                        .fillMaxWidth()
                        .background(Color(0xFF0E121B), RoundedCornerShape(8.dp))
                        .border(1.dp, Color(0xFF21262D), RoundedCornerShape(8.dp))
                        .padding(6.dp)
                ) {
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Text(
                            text = "MASTER BUS // RUPERT NEVE TRANSFORMER • EUPHONIA BOOST SEND • COSINE BUTTER MORPH",
                            color = Color(0xFFFCC934),
                            fontSize = 9.sp,
                            fontFamily = FontFamily.Monospace,
                            fontWeight = FontWeight.Bold
                        )
                        Row(horizontalArrangement = Arrangement.spacedBy(3.dp)) {
                            BoostSendChip("BOOST LO", boostLow) {
                                boostLow = !boostLow
                                repo.nativeSetMasterParam(5, if (boostLow) 1f else 0f)
                            }
                            BoostSendChip("BOOST MID", boostMid) {
                                boostMid = !boostMid
                                repo.nativeSetMasterParam(6, if (boostMid) 1f else 0f)
                            }
                            BoostSendChip("BOOST HI", boostHigh) {
                                boostHigh = !boostHigh
                                repo.nativeSetMasterParam(7, if (boostHigh) 1f else 0f)
                            }
                        }
                    }

                    Spacer(modifier = Modifier.height(3.dp))

                    Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                        RotaryKnob3D("ISO LOW", isoLow, true, Color(0xFF00E5FF), Modifier.weight(1f)) { isoLow = it; repo.nativeSetMasterParam(1, it) }
                        RotaryKnob3D("ISO MID", isoMid, true, Color(0xFF57AB5A), Modifier.weight(1f)) { isoMid = it; repo.nativeSetMasterParam(2, it) }
                        RotaryKnob3D("ISO HIGH", isoHigh, true, Color(0xFFFF5252), Modifier.weight(1f)) { isoHigh = it; repo.nativeSetMasterParam(3, it) }
                        RotaryKnob3D("NEVE SILK", neveDrive, false, Color(0xFFFCC934), Modifier.weight(1f)) { neveDrive = it; repo.nativeSetMasterParam(4, it) }
                        RotaryKnob3D("14 BEAT FX", masterBeatFx, false, Color(0xFFD29922), Modifier.weight(1f)) { masterBeatFx = it; repo.nativeSetMasterParam(8, it * 13f) }
                        RotaryKnob3D("FX LEVEL", masterBeatWet, false, Color(0xFFD29922), Modifier.weight(1f)) { masterBeatWet = it; repo.nativeSetMasterParam(9, it) }
                    }

                    Spacer(modifier = Modifier.height(3.dp))
                    MasterSpectrumAnalyzer(vuDeckA, vuDeckB, modifier = Modifier.fillMaxWidth().height(20.dp))

                    Spacer(modifier = Modifier.height(3.dp))
                    Row(modifier = Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                        Button(
                            onClick = {
                                repo.nativeTriggerButterMorph(0, 1, 12.0, 0)
                                statusBanner = "Cosine S-Curve Butter Morph A ➔ B Active (Beat-Phase Locked)"
                            },
                            colors = ButtonDefaults.buttonColors(containerColor = Color(0xFFF538A0))
                        ) {
                            Text("BUTTER MORPH A ➔ B", fontSize = 9.sp, fontWeight = FontWeight.Bold)
                        }
                        Spacer(modifier = Modifier.width(6.dp))
                        Slider(
                            value = crossfader,
                            onValueChange = { crossfader = it; repo.nativeSetMasterParam(0, it) },
                            valueRange = -1f..1f,
                            modifier = Modifier.weight(1f)
                        )
                        Spacer(modifier = Modifier.width(6.dp))
                        Button(
                            onClick = {
                                repo.nativeTriggerButterMorph(1, 0, 12.0, 0)
                                statusBanner = "Cosine S-Curve Butter Morph B ➔ A Active (Beat-Phase Locked)"
                            },
                            colors = ButtonDefaults.buttonColors(containerColor = Color(0xFF1A73E8))
                        ) {
                            Text("BUTTER MORPH B ➔ A", fontSize = 9.sp, fontWeight = FontWeight.Bold)
                        }
                    }
                }
            }
            1 -> {
                Column(
                    modifier = Modifier
                        .fillMaxWidth()
                        .background(Color(0xFF0E121B), RoundedCornerShape(8.dp))
                        .border(1.dp, Color(0xFF21262D), RoundedCornerShape(8.dp))
                        .padding(6.dp)
                ) {
                    Text(
                        text = "6-ENGINE PARALLEL REMIX SEARCH (HEARTHIS • JIOSAAVN • AUDIUS • PIPED • ARCHIVE • LOCAL)",
                        color = Color(0xFF00E5FF),
                        fontSize = 10.sp,
                        fontFamily = FontFamily.Monospace,
                        fontWeight = FontWeight.Bold
                    )
                    Spacer(modifier = Modifier.height(4.dp))
                    Row(modifier = Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                        OutlinedTextField(
                            value = searchQuery,
                            onValueChange = { searchQuery = it },
                            label = { Text("Search Global & Indian DJ Remixes or Paste Stream URL", color = Color.LightGray, fontSize = 9.sp) },
                            modifier = Modifier.weight(1f),
                            singleLine = true
                        )
                        Spacer(modifier = Modifier.width(4.dp))
                        Button(
                            onClick = {
                                scope.launch {
                                    statusBanner = "Searching 6 parallel remix networks for '$searchQuery'..."
                                    searchResults = repo.searchOnlineSongs(searchQuery)
                                    statusBanner = "Found ${searchResults.size} playable DJ remixes."
                                }
                            },
                            colors = ButtonDefaults.buttonColors(containerColor = Color(0xFF1A73E8))
                        ) {
                            Text("Search", fontSize = 10.sp)
                        }
                        Spacer(modifier = Modifier.width(4.dp))
                        Button(onClick = { targetLocalDeck = 0; localPicker.launch("audio/*") }, colors = ButtonDefaults.buttonColors(containerColor = Color(0xFF21262D))) {
                            Text("Local A", fontSize = 9.sp)
                        }
                        Spacer(modifier = Modifier.width(2.dp))
                        Button(onClick = { targetLocalDeck = 1; localPicker.launch("audio/*") }, colors = ButtonDefaults.buttonColors(containerColor = Color(0xFF21262D))) {
                            Text("Local B", fontSize = 9.sp)
                        }
                    }

                    Spacer(modifier = Modifier.height(4.dp))
                    LazyColumn(
                        modifier = Modifier
                            .fillMaxWidth()
                            .height(270.dp)
                            .background(Color(0xFF06080D), RoundedCornerShape(6.dp))
                            .padding(4.dp)
                    ) {
                        val displayTracks = if (searchResults.isEmpty()) repo.getBuiltInClubRemixCrate() else searchResults
                        items(displayTracks) { song ->
                            Row(
                                modifier = Modifier.fillMaxWidth().padding(vertical = 3.dp),
                                horizontalArrangement = Arrangement.SpaceBetween,
                                verticalAlignment = Alignment.CenterVertically
                            ) {
                                Column(modifier = Modifier.weight(1f)) {
                                    Text(song.title, color = Color.White, fontSize = 10.sp, fontWeight = FontWeight.Bold)
                                    Text("${song.artist} • ${song.sourceBadge} • ${song.estBpm} BPM", color = Color(0xFF8B949E), fontSize = 8.sp, fontFamily = FontFamily.Monospace)
                                }
                                Row {
                                    TextButton(onClick = {
                                        repo.loadTrackIntoDeckAsync(0, song) { msg -> statusBanner = msg }
                                        selectedTab = 0
                                    }) {
                                        Text("Load A", color = Color(0xFF00E5FF), fontSize = 9.sp, fontWeight = FontWeight.Bold)
                                    }
                                    TextButton(onClick = {
                                        repo.loadTrackIntoDeckAsync(1, song) { msg -> statusBanner = msg }
                                        selectedTab = 0
                                    }) {
                                        Text("Load B", color = Color(0xFFFA903E), fontSize = 9.sp, fontWeight = FontWeight.Bold)
                                    }
                                }
                            }
                        }
                    }
                }
            }
            2 -> {
                Column(
                    modifier = Modifier
                        .fillMaxWidth()
                        .background(Color(0xFF0E121B), RoundedCornerShape(8.dp))
                        .border(1.dp, Color(0xFF21262D), RoundedCornerShape(8.dp))
                        .padding(8.dp)
                ) {
                    Text(
                        text = "8-PAD 96kHz CLUB SAMPLER & AUTOMATIC .OBB EXPANSION MANAGER",
                        color = Color(0xFFFCC934),
                        fontSize = 10.sp,
                        fontFamily = FontFamily.Monospace,
                        fontWeight = FontWeight.Bold
                    )
                    Spacer(modifier = Modifier.height(6.dp))
                    val samplers = listOf("1. Airhorn", "2. 808 Drop", "3. Noise Riser", "4. Laser Zap", "5. Snare Hit", "6. Sub Suck", "7. Dub Siren", "8. Backspin")

                    Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
                        for (row in 0 until 2) {
                            Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                                for (col in 0 until 4) {
                                    val idx = row * 4 + col
                                    Button(
                                        onClick = { repo.nativeTriggerSamplerPad(idx); statusBanner = "Sampler Pad: ${samplers[idx]}" },
                                        modifier = Modifier.weight(1f).height(38.dp),
                                        colors = ButtonDefaults.buttonColors(containerColor = Color(0xFF1F2430))
                                    ) {
                                        Text(samplers[idx], fontSize = 9.sp, color = Color.White, fontWeight = FontWeight.Bold)
                                    }
                                }
                            }
                        }
                    }

                    HorizontalDivider(color = Color(0xFF21262D), modifier = Modifier.padding(vertical = 8.dp))
                    Text("PUBG / FREEFIRE STYLE .OBB AUTO-SCANNER & AUTO-COPIER", color = Color(0xFF00E5FF), fontSize = 10.sp, fontFamily = FontFamily.Monospace, fontWeight = FontWeight.Bold)
                    Text(obbStatusText, color = Color(0xFF57AB5A), fontSize = 9.sp, fontFamily = FontFamily.Monospace)
                    Spacer(modifier = Modifier.height(6.dp))
                    Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                        Button(
                            onClick = {
                                scope.launch {
                                    obbStatusText = "Scanning Download/ApexDJ_Build & copying to /Android/obb/..."
                                    val st = repo.obbManager.autoScanCopyAndMountObb()
                                    obbStatusText = st.statusMessage
                                }
                            },
                            colors = ButtonDefaults.buttonColors(containerColor = Color(0xFF1A73E8))
                        ) {
                            Text("AUTO-SCAN & COPY .OBB NOW", fontSize = 9.sp, fontWeight = FontWeight.Bold)
                        }
                        Button(
                            onClick = { obbFilePicker.launch("*/*") },
                            colors = ButtonDefaults.buttonColors(containerColor = Color(0xFF2D333B))
                        ) {
                            Text("SELECT .OBB FILE MANUALLY", fontSize = 9.sp)
                        }
                    }
                }
            }
            3 -> {
                Column(
                    modifier = Modifier
                        .fillMaxWidth()
                        .background(Color(0xFF0E121B), RoundedCornerShape(8.dp))
                        .border(1.dp, Color(0xFF21262D), RoundedCornerShape(8.dp))
                        .padding(8.dp)
                ) {
                    Text(
                        text = "GEMINI 4 ARGON // AUTONOMOUS AI DJ TRANSITION & CAMELOT HARMONIC COPILOT",
                        color = Color(0xFF00E5FF),
                        fontSize = 10.sp,
                        fontFamily = FontFamily.Monospace,
                        fontWeight = FontWeight.Bold
                    )
                    Spacer(modifier = Modifier.height(6.dp))

                    val aiRec = repo.computeArgonAiRecommendation(bpmDeckA, keyDeckA, bpmDeckB, keyDeckB)

                    Text("Harmonic Match Score: ${aiRec.compatibilityScore}%", color = Color(0xFF57AB5A), fontSize = 13.sp, fontWeight = FontWeight.Bold)
                    Text("Camelot Wheel Relation: ${aiRec.harmonicRelation}", color = Color.White, fontSize = 10.sp, fontFamily = FontFamily.Monospace)
                    Text("Selected Morph Engine: ${aiRec.recommendedStyleName}", color = Color(0xFFFCC934), fontSize = 10.sp, fontFamily = FontFamily.Monospace)
                    Text(aiRec.summaryText, color = Color(0xFF8B949E), fontSize = 9.sp)

                    Spacer(modifier = Modifier.height(8.dp))
                    Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                        Button(
                            onClick = {
                                repo.nativeTriggerButterMorph(0, 1, 14.0, aiRec.recommendedStyleId)
                                statusBanner = "Argon AI Mixing Deck A ➔ Deck B (${aiRec.recommendedStyleName})"
                                selectedTab = 0
                            },
                            modifier = Modifier.weight(1f),
                            colors = ButtonDefaults.buttonColors(containerColor = Color(0xFFF538A0))
                        ) {
                            Text("AI AUTO-MIX A ➔ B (14s)", fontWeight = FontWeight.Bold, fontSize = 9.sp)
                        }
                        Button(
                            onClick = {
                                repo.nativeTriggerButterMorph(1, 0, 14.0, aiRec.recommendedStyleId)
                                statusBanner = "Argon AI Mixing Deck B ➔ Deck A (${aiRec.recommendedStyleName})"
                                selectedTab = 0
                            },
                            modifier = Modifier.weight(1f),
                            colors = ButtonDefaults.buttonColors(containerColor = Color(0xFF1A73E8))
                        ) {
                            Text("AI AUTO-MIX B ➔ A (14s)", fontWeight = FontWeight.Bold, fontSize = 9.sp)
                        }
                    }
                }
            }
        }
    }
}

@Composable
fun BoostSendChip(label: String, active: Boolean, onClick: () -> Unit) {
    Button(
        onClick = onClick,
        modifier = Modifier.height(22.dp),
        contentPadding = PaddingValues(horizontal = 6.dp, vertical = 0.dp),
        colors = ButtonDefaults.buttonColors(containerColor = if (active) Color(0xFFF538A0) else Color(0xFF1F2430))
    ) {
        Text(label, fontSize = 7.sp, color = Color.White, fontWeight = FontWeight.Bold, fontFamily = FontFamily.Monospace)
    }
}

@Composable
fun TabButton(text: String, isSelected: Boolean, modifier: Modifier = Modifier, onClick: () -> Unit) {
    Button(
        onClick = onClick,
        modifier = modifier.height(26.dp),
        contentPadding = PaddingValues(0.dp),
        colors = ButtonDefaults.buttonColors(containerColor = if (isSelected) Color(0xFF1A73E8) else Color(0xFF151922))
    ) {
        Text(text, fontSize = 8.sp, color = Color.White, fontWeight = FontWeight.Bold, fontFamily = FontFamily.Monospace)
    }
}

@Composable
fun DeckConsoleCard(
    deckIndex: Int,
    title: String,
    accent: Color,
    bpm: Float,
    camelotKey: Int,
    playheadNorm: Float,
    rgbWave: FloatArray,
    repo: OnlineAndLocalRepository,
    modifier: Modifier = Modifier
) {
    var playing by remember { mutableStateOf(false) }
    var fader by remember { mutableFloatStateOf(1f) }
    var comp by remember { mutableFloatStateOf(0.2f) }
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

    Column(
        modifier = modifier
            .background(Color(0xFF0E121B), RoundedCornerShape(8.dp))
            .border(1.dp, accent.copy(alpha = 0.5f), RoundedCornerShape(8.dp))
            .padding(5.dp)
    ) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Column(modifier = Modifier.weight(1f)) {
                Text(title, color = accent, fontWeight = FontWeight.Bold, fontSize = 9.sp, fontFamily = FontFamily.Monospace, maxLines = 1)
                Text("%.1f BPM (x%.2f) • Key %dA".format(bpm * tempoRatio, tempoRatio, camelotKey), color = Color(0xFF8B949E), fontSize = 8.sp, fontFamily = FontFamily.Monospace)
            }
            Button(
                onClick = {
                    playing = !playing
                    repo.nativeSetDeckPlayState(deckIndex, playing)
                },
                modifier = Modifier.height(26.dp),
                contentPadding = PaddingValues(horizontal = 10.dp, vertical = 0.dp),
                colors = ButtonDefaults.buttonColors(containerColor = if (playing) Color(0xFF57AB5A) else Color(0xFF21262D))
            ) {
                Text(if (playing) "PAUSE" else "PLAY", color = Color.White, fontSize = 9.sp, fontWeight = FontWeight.Bold)
            }
        }

        Spacer(modifier = Modifier.height(2.dp))
        RgbScrollingWaveformView(
            rgbFlatArray = rgbWave,
            playheadNormalized = playheadNorm,
            accentColor = accent,
            modifier = Modifier.fillMaxWidth().height(36.dp),
            onSeek = { norm -> repo.nativeSeekNormalized(deckIndex, norm) }
        )

        Spacer(modifier = Modifier.height(2.dp))

        // CDJ-3000 Touch Jogwheel + Pitch Slider Row
        Row(modifier = Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(4.dp)) {
            Box(
                modifier = Modifier
                    .weight(1f)
                    .height(36.dp)
                    .background(Color(0xFF06080D), CircleShape)
                    .border(1.5.dp, accent, CircleShape)
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
                Text("CDJ-3000 JOG SCRATCH", color = accent, fontSize = 8.sp, fontWeight = FontWeight.Bold, fontFamily = FontFamily.Monospace)
            }
            RotaryKnob3D("TEMPO", (tempoRatio - 1f) * 2f, true, accent) {
                tempoRatio = (1f + it * 0.25f).coerceIn(0.75f, 1.25f)
                repo.nativeSetChannelParam(deckIndex, 13, tempoRatio)
            }
        }

        Spacer(modifier = Modifier.height(2.dp))

        // Real-Time 4-Stem Knobs + 4-Band Surgical EQ Knobs
        Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(2.dp)) {
            RotaryKnob3D("DRUMS", stemDrums, false, Color(0xFF00E5FF), Modifier.weight(1f)) { stemDrums = it; repo.nativeSetChannelParam(deckIndex, 9, it) }
            RotaryKnob3D("BASS", stemBass, false, Color(0xFFFF5252), Modifier.weight(1f)) { stemBass = it; repo.nativeSetChannelParam(deckIndex, 10, it) }
            RotaryKnob3D("OTHER", stemOther, false, Color(0xFF57AB5A), Modifier.weight(1f)) { stemOther = it; repo.nativeSetChannelParam(deckIndex, 11, it) }
            RotaryKnob3D("VOCALS", stemVocals, false, Color(0xFFF538A0), Modifier.weight(1f)) { stemVocals = it; repo.nativeSetChannelParam(deckIndex, 12, it) }
        }

        Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(2.dp)) {
            RotaryKnob3D("LO 220", eqLo, true, accent, Modifier.weight(1f)) { eqLo = it; repo.nativeSetChannelParam(deckIndex, 1, it) }
            RotaryKnob3D("LM 320", eqLm, true, accent, Modifier.weight(1f)) { eqLm = it; repo.nativeSetChannelParam(deckIndex, 2, it) }
            RotaryKnob3D("HM 1.8k", eqHm, true, accent, Modifier.weight(1f)) { eqHm = it; repo.nativeSetChannelParam(deckIndex, 3, it) }
            RotaryKnob3D("HI 2.4k", eqHi, true, accent, Modifier.weight(1f)) { eqHi = it; repo.nativeSetChannelParam(deckIndex, 4, it) }
        }

        Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(2.dp)) {
            RotaryKnob3D("V10 COMP", comp, false, Color(0xFF58A6FF), Modifier.weight(1f)) { comp = it; repo.nativeSetChannelParam(deckIndex, 5, it) }
            RotaryKnob3D("XONE VCF", vcfCutoff, false, Color(0xFFFCC934), Modifier.weight(1f)) { vcfCutoff = it; repo.nativeSetChannelParam(deckIndex, 7, it) }
            RotaryKnob3D("CRUNCH", vcfCrunch, false, Color(0xFFFCC934), Modifier.weight(1f)) { vcfCrunch = it; repo.nativeSetChannelParam(deckIndex, 8, it) }
            RotaryKnob3D("A9 COLOR", colorFx, true, Color(0xFFF538A0), Modifier.weight(1f)) { colorFx = it; repo.nativeSetChannelParam(deckIndex, 6, it) }
            RotaryKnob3D("FADER", fader, false, Color(0xFF57AB5A), Modifier.weight(1f)) { fader = it; repo.nativeSetChannelParam(deckIndex, 0, it) }
        }

        Spacer(modifier = Modifier.height(2.dp))

        // Hot Cues & Beat Loops Row
        Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(2.dp)) {
            for (c in 0 until 4) {
                Button(
                    onClick = { repo.nativeTriggerHotCue(deckIndex, c) },
                    modifier = Modifier.weight(1f).height(19.dp),
                    contentPadding = PaddingValues(0.dp),
                    colors = ButtonDefaults.buttonColors(containerColor = Color(0xFF1F2430))
                ) {
                    Text("CUE ${c + 1}", fontSize = 7.sp, color = Color.White)
                }
            }
            Button(onClick = { repo.nativeToggleBeatLoop(deckIndex, 4.0) }, modifier = Modifier.weight(1f).height(19.dp), contentPadding = PaddingValues(0.dp), colors = ButtonDefaults.buttonColors(containerColor = Color(0xFF2D333B))) {
                Text("4-LOOP", fontSize = 7.sp, color = Color(0xFF57AB5A))
            }
            Button(onClick = { repo.nativeToggleBeatLoop(deckIndex, 0.0) }, modifier = Modifier.weight(1f).height(19.dp), contentPadding = PaddingValues(0.dp), colors = ButtonDefaults.buttonColors(containerColor = Color(0xFF3B232A))) {
                Text("EXIT", fontSize = 7.sp, color = Color(0xFFF85149))
            }
        }
    }
}
