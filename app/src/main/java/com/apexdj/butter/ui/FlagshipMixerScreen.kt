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
    var statusBanner by remember { mutableStateOf("Gemini 4 Argon AI Engine Active • 96kHz / 64-Bit V10 + Euphonia Matrix Ready") }
    var obbStatusText by remember { mutableStateOf("Scanning PUBG / FreeFire Style .OBB Expansion Pack...") }

    var crossfader by remember { mutableFloatStateOf(0f) }
    var neveDrive by remember { mutableFloatStateOf(0.65f) }
    var isoLow by remember { mutableFloatStateOf(0f) }
    var isoMid by remember { mutableFloatStateOf(0f) }
    var isoHigh by remember { mutableFloatStateOf(0f) }
    var masterBeatFx by remember { mutableFloatStateOf(0f) }
    var masterBeatWet by remember { mutableFloatStateOf(0f) }

    var vuDeckA by remember { mutableFloatStateOf(0f) }
    var vuDeckB by remember { mutableFloatStateOf(0f) }
    var bpmDeckA by remember { mutableFloatStateOf(128f) }
    var bpmDeckB by remember { mutableFloatStateOf(128f) }
    var keyDeckA by remember { mutableIntStateOf(8) }
    var keyDeckB by remember { mutableIntStateOf(8) }
    var playheadA by remember { mutableFloatStateOf(0f) }
    var playheadB by remember { mutableFloatStateOf(0f) }
    var rgbWaveA by remember { mutableStateOf(FloatArray(256 * 3)) }
    var rgbWaveB by remember { mutableStateOf(FloatArray(256 * 3)) }

    val spectrumState = remember { Array(32) { 0.2f } }

    LaunchedEffect(Unit) {
        val st = repo.obbManager.mountAndVerifyObbExpansion()
        obbStatusText = st.statusMessage

        while (true) {
            vuDeckA = repo.nativeGetChannelVu(0)
            vuDeckB = repo.nativeGetChannelVu(1)
            bpmDeckA = repo.nativeGetDetectedBpm(0)
            bpmDeckB = repo.nativeGetDetectedBpm(1)
            keyDeckA = repo.nativeGetCamelotKey(0)
            keyDeckB = repo.nativeGetCamelotKey(1)
            playheadA = repo.nativeGetPlayheadNormalized(0)
            playheadB = repo.nativeGetPlayheadNormalized(1)
            rgbWaveA = repo.nativeGetRgbWaveform(0)
            rgbWaveB = repo.nativeGetRgbWaveform(1)
            for (i in 0 until 32) {
                spectrumState[i] = (0.2f + Math.random() * 0.6f).toFloat()
            }
            delay(16)
        }
    }

    var targetLocalDeck by remember { mutableIntStateOf(0) }
    val localPicker = rememberLauncherForActivityResult(ActivityResultContracts.GetContent()) { uri: Uri? ->
        if (uri != null) {
            scope.launch {
                val track = repo.resolveLocalUriToTrack(uri)
                repo.loadTrackIntoDeckAsync(targetLocalDeck, track) { msg -> statusBanner = msg }
            }
        }
    }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .background(Color(0xFF07090E))
            .padding(6.dp)
            .verticalScroll(rememberScrollState())
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .background(Color(0xFF10131C), RoundedCornerShape(8.dp))
                .padding(8.dp),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Column {
                Text(
                    "APEX-DJ // GEMINI 4 ARGON (1-CRORE TOURBION CONSOLE + OBB)",
                    color = Color(0xFF00E5FF),
                    fontFamily = FontFamily.Monospace,
                    fontWeight = FontWeight.Bold,
                    fontSize = 12.sp
                )
                Text("$statusBanner • $obbStatusText", color = Color(0xFF8B949E), fontSize = 9.sp, fontFamily = FontFamily.Monospace)
            }
            Canvas(modifier = Modifier.size(75.dp, 28.dp).background(Color(0xFF04060A))) {
                val w = size.width
                val h = size.height
                drawLine(Color(0xFF00E5FF), Offset(0f, h * 0.35f), Offset(w * vuDeckA.coerceIn(0f, 1f), h * 0.35f), strokeWidth = 6f)
                drawLine(Color(0xFFFA903E), Offset(0f, h * 0.75f), Offset(w * vuDeckB.coerceIn(0f, 1f), h * 0.75f), strokeWidth = 6f)
            }
        }

        Spacer(modifier = Modifier.height(4.dp))

        Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(4.dp)) {
            TabButton("1. PRO CONSOLE", selectedTab == 0, Modifier.weight(1f)) { selectedTab = 0 }
            TabButton("2. 6-ENGINE REMIX CRATE", selectedTab == 1, Modifier.weight(1f)) { selectedTab = 1 }
            TabButton("3. 8-PAD SAMPLER & STEMS", selectedTab == 2, Modifier.weight(1f)) { selectedTab = 2 }
            TabButton("4. ARGON AI COPILOT", selectedTab == 3, Modifier.weight(1f)) { selectedTab = 3 }
        }

        Spacer(modifier = Modifier.height(4.dp))

        when (selectedTab) {
            0 -> {
                Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                    DeckConsoleCard(0, "DECK A (CDJ-3000)", Color(0xFF00E5FF), bpmDeckA, keyDeckA, playheadA, rgbWaveA, repo, Modifier.weight(1f))
                    DeckConsoleCard(1, "DECK B (CDJ-3000)", Color(0xFFFA903E), bpmDeckB, keyDeckB, playheadB, rgbWaveB, repo, Modifier.weight(1f))
                }

                Spacer(modifier = Modifier.height(4.dp))

                Column(
                    modifier = Modifier
                        .fillMaxWidth()
                        .background(Color(0xFF0E1118), RoundedCornerShape(8.dp))
                        .border(1.dp, Color(0xFF21262D), RoundedCornerShape(8.dp))
                        .padding(6.dp)
                ) {
                    Text("MASTER CONSOLE // 14 BEAT FX • NEVE TRANSFORMER • 3-BAND ISOLATOR • 32-BAND SPECTRUM", color = Color(0xFFFCC934), fontSize = 10.sp, fontFamily = FontFamily.Monospace, fontWeight = FontWeight.Bold)
                    Spacer(modifier = Modifier.height(4.dp))

                    Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                        RotaryKnob3D("ISO LOW", isoLow, true, Color(0xFF00E5FF), Modifier.weight(1f)) { isoLow = it; repo.nativeSetMasterParam(1, it) }
                        RotaryKnob3D("ISO MID", isoMid, true, Color(0xFF57AB5A), Modifier.weight(1f)) { isoMid = it; repo.nativeSetMasterParam(2, it) }
                        RotaryKnob3D("ISO HIGH", isoHigh, true, Color(0xFFFF5252), Modifier.weight(1f)) { isoHigh = it; repo.nativeSetMasterParam(3, it) }
                        RotaryKnob3D("NEVE DRIVE", neveDrive, false, Color(0xFFFCC934), Modifier.weight(1f)) { neveDrive = it; repo.nativeSetMasterParam(4, it) }
                        RotaryKnob3D("FX TYPE", masterBeatFx, false, Color(0xFFD29922), Modifier.weight(1f)) { masterBeatFx = it; repo.nativeSetMasterParam(8, it * 13f) }
                        RotaryKnob3D("FX WET/DRY", masterBeatWet, false, Color(0xFFD29922), Modifier.weight(1f)) { masterBeatWet = it; repo.nativeSetMasterParam(9, it) }
                    }

                    Spacer(modifier = Modifier.height(4.dp))
                    MasterSpectrumAnalyzer(spectrumState, modifier = Modifier.fillMaxWidth().height(24.dp))

                    Spacer(modifier = Modifier.height(4.dp))
                    Row(modifier = Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                        Button(onClick = { repo.nativeTriggerButterMorph(0, 1, 12.0, 0); statusBanner = "Butter Morph A ➔ B Active" }, colors = ButtonDefaults.buttonColors(containerColor = Color(0xFFF538A0))) {
                            Text("BUTTER MORPH A ➔ B", fontSize = 9.sp, fontWeight = FontWeight.Bold)
                        }
                        Spacer(modifier = Modifier.width(4.dp))
                        Slider(value = crossfader, onValueChange = { crossfader = it; repo.nativeSetMasterParam(0, it) }, valueRange = -1f..1f, modifier = Modifier.weight(1f))
                        Spacer(modifier = Modifier.width(4.dp))
                        Button(onClick = { repo.nativeTriggerButterMorph(1, 0, 12.0, 0); statusBanner = "Butter Morph B ➔ A Active" }, colors = ButtonDefaults.buttonColors(containerColor = Color(0xFF1A73E8))) {
                            Text("BUTTER MORPH B ➔ A", fontSize = 9.sp, fontWeight = FontWeight.Bold)
                        }
                    }
                }
            }
            1 -> {
                Column(
                    modifier = Modifier
                        .fillMaxWidth()
                        .background(Color(0xFF0E1118), RoundedCornerShape(8.dp))
                        .border(1.dp, Color(0xFF21262D), RoundedCornerShape(8.dp))
                        .padding(6.dp)
                ) {
                    Text("6-ENGINE FEDERATED REMIX SEARCH & .OBB EXPANSION VAULT", color = Color(0xFF00E5FF), fontSize = 11.sp, fontFamily = FontFamily.Monospace, fontWeight = FontWeight.Bold)
                    Spacer(modifier = Modifier.height(4.dp))
                    Row(modifier = Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                        OutlinedTextField(
                            value = searchQuery,
                            onValueChange = { searchQuery = it },
                            label = { Text("Search Trillions of Global & Indian Remixes (Hearthis, JioSaavn, Audius, Piped)", color = Color.LightGray, fontSize = 9.sp) },
                            modifier = Modifier.weight(1f),
                            singleLine = true
                        )
                        Spacer(modifier = Modifier.width(4.dp))
                        Button(onClick = {
                            scope.launch {
                                statusBanner = "Querying 6 parallel federated remix networks..."
                                searchResults = repo.searchOnlineSongs(searchQuery)
                                statusBanner = "Found ${searchResults.size} club remixes & bootlegs."
                            }
                        }, colors = ButtonDefaults.buttonColors(containerColor = Color(0xFF1A73E8))) {
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
                            .height(280.dp)
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
                                    TextButton(onClick = { repo.loadTrackIntoDeckAsync(0, song) { msg -> statusBanner = msg } }) {
                                        Text("Deck A", color = Color(0xFF00E5FF), fontSize = 9.sp)
                                    }
                                    TextButton(onClick = { repo.loadTrackIntoDeckAsync(1, song) { msg -> statusBanner = msg } }) {
                                        Text("Deck B", color = Color(0xFFFA903E), fontSize = 9.sp)
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
                        .background(Color(0xFF0E1118), RoundedCornerShape(8.dp))
                        .border(1.dp, Color(0xFF21262D), RoundedCornerShape(8.dp))
                        .padding(6.dp)
                ) {
                    Text("8-PAD CLUB SAMPLER & 4-STEM VIRTUAL CONSOLE", color = Color(0xFFFCC934), fontSize = 11.sp, fontFamily = FontFamily.Monospace, fontWeight = FontWeight.Bold)
                    Spacer(modifier = Modifier.height(6.dp))
                    val samplers = listOf("1. Airhorn", "2. 808 Drop", "3. Noise Riser", "4. Laser Zap", "5. Snare Hit", "6. Sub Suck", "7. Dub Siren", "8. Backspin")

                    Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
                        for (row in 0 until 2) {
                            Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                                for (col in 0 until 4) {
                                    val idx = row * 4 + col
                                    Button(
                                        onClick = { repo.nativeTriggerSamplerPad(idx); statusBanner = "Triggered Sampler Pad: ${samplers[idx]}" },
                                        modifier = Modifier.weight(1f).height(40.dp),
                                        colors = ButtonDefaults.buttonColors(containerColor = Color(0xFF1F2430))
                                    ) {
                                        Text(samplers[idx], fontSize = 9.sp, color = Color.White, fontWeight = FontWeight.Bold)
                                    }
                                }
                            }
                        }
                    }
                }
            }
            3 -> {
                Column(
                    modifier = Modifier
                        .fillMaxWidth()
                        .background(Color(0xFF0E1118), RoundedCornerShape(8.dp))
                        .border(1.dp, Color(0xFF21262D), RoundedCornerShape(8.dp))
                        .padding(8.dp)
                ) {
                    Text("GEMINI 4 ARGON // AUTONOMOUS AI DJ TRANSITION & HARMONIC COPILOT", color = Color(0xFF00E5FF), fontSize = 11.sp, fontFamily = FontFamily.Monospace, fontWeight = FontWeight.Bold)
                    Spacer(modifier = Modifier.height(6.dp))

                    val aiRec = repo.computeArgonAiRecommendation(bpmDeckA, keyDeckA, bpmDeckB, keyDeckB)

                    Text("Harmonic Match: ${aiRec.compatibilityScore}%", color = Color(0xFF57AB5A), fontSize = 13.sp, fontWeight = FontWeight.Bold)
                    Text("Camelot Relation: ${aiRec.harmonicRelation}", color = Color.White, fontSize = 10.sp, fontFamily = FontFamily.Monospace)
                    Text("Recommended Style: ${aiRec.recommendedStyleName}", color = Color(0xFFFCC934), fontSize = 10.sp, fontFamily = FontFamily.Monospace)
                    Text(aiRec.summaryText, color = Color(0xFF8B949E), fontSize = 9.sp)

                    Spacer(modifier = Modifier.height(8.dp))
                    Button(
                        onClick = {
                            repo.nativeTriggerButterMorph(0, 1, 14.0, aiRec.recommendedStyleId)
                            statusBanner = "Gemini 4 Argon AI Executing '${aiRec.recommendedStyleName}'..."
                        },
                        modifier = Modifier.fillMaxWidth(),
                        colors = ButtonDefaults.buttonColors(containerColor = Color(0xFFF538A0))
                    ) {
                        Text("EXECUTE GEMINI 4 ARGON AI TRANSITION NOW", fontWeight = FontWeight.Bold, fontSize = 10.sp)
                    }
                }
            }
        }
    }
}

@Composable
fun TabButton(text: String, isSelected: Boolean, modifier: Modifier = Modifier, onClick: () -> Unit) {
    Button(
        onClick = onClick,
        modifier = modifier.height(28.dp),
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
    var eqLo by remember { mutableFloatStateOf(0f) }
    var eqLm by remember { mutableFloatStateOf(0f) }
    var eqHm by remember { mutableFloatStateOf(0f) }
    var eqHi by remember { mutableFloatStateOf(0f) }
    var vcfCutoff by remember { mutableFloatStateOf(0.5f) }
    var vcfCrunch by remember { mutableFloatStateOf(0f) }
    var colorFx by remember { mutableFloatStateOf(0f) }
    var scratchVel by remember { mutableFloatStateOf(0f) }

    Column(
        modifier = modifier
            .background(Color(0xFF0F131C), RoundedCornerShape(8.dp))
            .border(1.dp, accent.copy(alpha = 0.5f), RoundedCornerShape(8.dp))
            .padding(6.dp)
    ) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Column {
                Text(title, color = accent, fontWeight = FontWeight.Bold, fontSize = 10.sp, fontFamily = FontFamily.Monospace)
                Text("%.1f BPM • %dA".format(bpm, camelotKey), color = Color(0xFF8B949E), fontSize = 9.sp, fontFamily = FontFamily.Monospace)
            }
            Button(
                onClick = {
                    playing = !playing
                    repo.nativeSetDeckPlayState(deckIndex, playing)
                },
                colors = ButtonDefaults.buttonColors(containerColor = if (playing) Color(0xFF57AB5A) else Color(0xFF21262D))
            ) {
                Text(if (playing) "PAUSE" else "PLAY", color = Color.White, fontSize = 9.sp, fontWeight = FontWeight.Bold)
            }
        }

        Spacer(modifier = Modifier.height(2.dp))
        RgbScrollingWaveformView(rgbWave, playheadNorm, accent, modifier = Modifier.fillMaxWidth().height(40.dp))

        Spacer(modifier = Modifier.height(2.dp))

        Box(
            modifier = Modifier
                .fillMaxWidth()
                .height(45.dp)
                .background(Color(0xFF06080D), CircleShape)
                .border(1.5.dp, accent, CircleShape)
                .pointerInput(Unit) {
                    detectDragGestures(
                        onDragStart = { scratchVel = 1.5f; repo.nativeSetChannelParam(deckIndex, 18, 1.5f) },
                        onDragEnd = { scratchVel = 0f; repo.nativeSetChannelParam(deckIndex, 18, 0f) },
                        onDragCancel = { scratchVel = 0f; repo.nativeSetChannelParam(deckIndex, 18, 0f) }
                    ) { _, dragAmount ->
                        scratchVel = (dragAmount.x * 0.05f).coerceIn(-3f, 3f)
                        repo.nativeSetChannelParam(deckIndex, 18, scratchVel)
                    }
                },
            contentAlignment = Alignment.Center
        ) {
            Text("JOGWHEEL SCRATCH", color = accent.copy(alpha = 0.7f), fontSize = 9.sp, fontWeight = FontWeight.Bold, fontFamily = FontFamily.Monospace)
        }

        Spacer(modifier = Modifier.height(3.dp))

        Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(2.dp)) {
            for (c in 0 until 4) {
                Button(
                    onClick = { repo.nativeTriggerHotCue(deckIndex, c) },
                    modifier = Modifier.weight(1f).height(20.dp),
                    contentPadding = PaddingValues(0.dp),
                    colors = ButtonDefaults.buttonColors(containerColor = Color(0xFF1F2430))
                ) {
                    Text("CUE ${c + 1}", fontSize = 8.sp, color = Color.White)
                }
            }
        }
        Row(modifier = Modifier.fillMaxWidth().padding(top = 2.dp), horizontalArrangement = Arrangement.spacedBy(2.dp)) {
            Button(onClick = { repo.nativeToggleBeatLoop(deckIndex, 4.0) }, modifier = Modifier.weight(1f).height(20.dp), contentPadding = PaddingValues(0.dp), colors = ButtonDefaults.buttonColors(containerColor = Color(0xFF2D333B))) {
                Text("4-BEAT", fontSize = 8.sp, color = Color(0xFF57AB5A))
            }
            Button(onClick = { repo.nativeToggleBeatLoop(deckIndex, 8.0) }, modifier = Modifier.weight(1f).height(20.dp), contentPadding = PaddingValues(0.dp), colors = ButtonDefaults.buttonColors(containerColor = Color(0xFF2D333B))) {
                Text("8-BEAT", fontSize = 8.sp, color = Color(0xFF57AB5A))
            }
            Button(onClick = { repo.nativeSetChannelParam(deckIndex, 16, 1f) }, modifier = Modifier.weight(1f).height(20.dp), contentPadding = PaddingValues(0.dp), colors = ButtonDefaults.buttonColors(containerColor = Color(0xFF3B232A))) {
                Text("BRAKE", fontSize = 8.sp, color = Color(0xFFF85149))
            }
            Button(onClick = { repo.nativeSetChannelParam(deckIndex, 15, 1f) }, modifier = Modifier.weight(1f).height(20.dp), contentPadding = PaddingValues(0.dp), colors = ButtonDefaults.buttonColors(containerColor = Color(0xFF2D233B))) {
                Text("REV", fontSize = 8.sp, color = Color(0xFFD29922))
            }
        }

        Spacer(modifier = Modifier.height(4.dp))
        Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(2.dp)) {
            RotaryKnob3D("LO", eqLo, true, accent, Modifier.weight(1f)) { eqLo = it; repo.nativeSetChannelParam(deckIndex, 1, it) }
            RotaryKnob3D("LM", eqLm, true, accent, Modifier.weight(1f)) { eqLm = it; repo.nativeSetChannelParam(deckIndex, 2, it) }
            RotaryKnob3D("HM", eqHm, true, accent, Modifier.weight(1f)) { eqHm = it; repo.nativeSetChannelParam(deckIndex, 3, it) }
            RotaryKnob3D("HI", eqHi, true, accent, Modifier.weight(1f)) { eqHi = it; repo.nativeSetChannelParam(deckIndex, 4, it) }
        }
        Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(2.dp)) {
            RotaryKnob3D("VCF", vcfCutoff, false, Color(0xFFFCC934), Modifier.weight(1f)) { vcfCutoff = it; repo.nativeSetChannelParam(deckIndex, 7, it) }
            RotaryKnob3D("CRUNCH", vcfCrunch, false, Color(0xFFFCC934), Modifier.weight(1f)) { vcfCrunch = it; repo.nativeSetChannelParam(deckIndex, 8, it) }
            RotaryKnob3D("COLOR", colorFx, true, Color(0xFFF538A0), Modifier.weight(1f)) { colorFx = it; repo.nativeSetChannelParam(deckIndex, 6, it) }
            RotaryKnob3D("FADER", fader, false, Color(0xFF57AB5A), Modifier.weight(1f)) { fader = it; repo.nativeSetChannelParam(deckIndex, 0, it) }
        }
    }
}
