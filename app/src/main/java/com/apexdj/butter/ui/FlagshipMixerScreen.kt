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
    var searchQuery by remember { mutableStateOf("") }
    var searchResults by remember { mutableStateOf<List<SongTrack>>(emptyList()) }
    var statusBanner by remember {
        mutableStateOf("SELF-NEURAL 64-BIT ENGINE ACTIVE • 5 NEURAL STEMS • CAMELOT KEY-LOCK • NEURAL SUB/AIR MASTERING")
    }

    var crossfader by remember { mutableFloatStateOf(0f) }
    var neveDrive by remember { mutableFloatStateOf(0.65f) }
    var isoLow by remember { mutableFloatStateOf(0f) }
    var isoMid by remember { mutableFloatStateOf(0f) }
    var isoHigh by remember { mutableFloatStateOf(0f) }
    var masterBeatFx by remember { mutableFloatStateOf(0f) }
    var masterBeatWet by remember { mutableFloatStateOf(0f) }

    // Self-Neural Global Bus Controls
    var neuralSidechain by remember { mutableFloatStateOf(0.35f) }
    var neuralSubBoom by remember { mutableFloatStateOf(0.25f) }
    var neuralAirGlow by remember { mutableFloatStateOf(0.30f) }
    var selectedMorphStyle by remember { mutableIntStateOf(0) }

    var vuDeckA by remember { mutableFloatStateOf(0f) }
    var vuDeckB by remember { mutableFloatStateOf(0f) }
    var bpmDeckA by remember { mutableFloatStateOf(128f) }
    var bpmDeckB by remember { mutableFloatStateOf(128f) }
    var keyDeckA by remember { mutableIntStateOf(8) }
    var keyDeckB by remember { mutableIntStateOf(8) }

    LaunchedEffect(Unit) {
        while (true) {
            vuDeckA = repo.nativeGetChannelVu(0)
            vuDeckB = repo.nativeGetChannelVu(1)
            bpmDeckA = repo.nativeGetDetectedBpm(0)
            bpmDeckB = repo.nativeGetDetectedBpm(1)
            keyDeckA = repo.nativeGetDetectedCamelotKey(0)
            keyDeckB = repo.nativeGetDetectedCamelotKey(1)
            delay(16)
        }
    }

    var targetLocalDeck by remember { mutableIntStateOf(0) }
    val localPicker = rememberLauncherForActivityResult(ActivityResultContracts.GetContent()) { uri: Uri? ->
        if (uri != null) {
            scope.launch {
                statusBanner = "Decoding Audio & Running C++20 Self-Neural 5-Stem Decomposition on Deck ${targetLocalDeck + 1}..."
                repo.loadTrackIntoDeck(
                    targetLocalDeck,
                    SongTrack("local", "Local Audio", "Storage", 240, "", isLocal = true, localUri = uri)
                )
                statusBanner = "Deck ${targetLocalDeck + 1} Ready with 5 Self-Neural Stems & Harmonic Key Lock!"
            }
        }
    }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .background(Color(0xFF07090E))
            .padding(8.dp)
            .verticalScroll(rememberScrollState())
    ) {
        // Top Flagship Telemetry Header
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .background(Color(0xFF111622), RoundedCornerShape(8.dp))
                .border(1.dp, Color(0xFF1F2937), RoundedCornerShape(8.dp))
                .padding(8.dp),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Column(modifier = Modifier.weight(1f)) {
                Text(
                    "APEX-DJ // SELF-NEURAL 1-LAKH FEATURE MATRIX (CDJ-3000 + V10 + EUPHONIA)",
                    color = Color(0xFF00E5FF),
                    fontFamily = FontFamily.Monospace,
                    fontWeight = FontWeight.Bold,
                    fontSize = 12.sp
                )
                Text(statusBanner, color = Color(0xFF9CA3AF), fontSize = 9.sp, fontFamily = FontFamily.Monospace)
            }
            Canvas(modifier = Modifier.size(105.dp, 32.dp).background(Color(0xFF05070A))) {
                val w = size.width
                val h = size.height
                drawLine(Color(0xFF00E5FF), Offset(0f, h * 0.35f), Offset(w * vuDeckA.coerceIn(0f, 1f), h * 0.35f), strokeWidth = 8f)
                drawLine(Color(0xFFFA903E), Offset(0f, h * 0.75f), Offset(w * vuDeckB.coerceIn(0f, 1f), h * 0.75f), strokeWidth = 8f)
            }
        }

        Spacer(modifier = Modifier.height(6.dp))

        // Search & Instant Neural Synth Loader Bar
        Row(modifier = Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            OutlinedTextField(
                value = searchQuery,
                onValueChange = { searchQuery = it },
                label = { Text("Search Online Songs or Load Built-in Neural Synth", color = Color.LightGray, fontSize = 10.sp) },
                modifier = Modifier.weight(1f),
                singleLine = true
            )
            Spacer(modifier = Modifier.width(4.dp))
            Button(
                onClick = {
                    scope.launch {
                        statusBanner = "Searching global streams + Self-Neural synth generators..."
                        searchResults = repo.searchOnlineSongs(searchQuery)
                        statusBanner = "Loaded ${searchResults.size} tracks ready for Self-Neural 5-Stem separation."
                    }
                },
                colors = ButtonDefaults.buttonColors(containerColor = Color(0xFF1A73E8))
            ) {
                Text("Search", fontSize = 10.sp)
            }
            Spacer(modifier = Modifier.width(4.dp))
            Button(
                onClick = { targetLocalDeck = 0; localPicker.launch("audio/*") },
                colors = ButtonDefaults.buttonColors(containerColor = Color(0xFF1F2937))
            ) {
                Text("Local A", fontSize = 10.sp)
            }
            Spacer(modifier = Modifier.width(2.dp))
            Button(
                onClick = { targetLocalDeck = 1; localPicker.launch("audio/*") },
                colors = ButtonDefaults.buttonColors(containerColor = Color(0xFF1F2937))
            ) {
                Text("Local B", fontSize = 10.sp)
            }
        }

        if (searchResults.isNotEmpty()) {
            LazyColumn(
                modifier = Modifier
                    .fillMaxWidth()
                    .height(110.dp)
                    .background(Color(0xFF0D1117), RoundedCornerShape(6.dp))
                    .padding(4.dp)
            ) {
                items(searchResults) { song ->
                    Row(
                        modifier = Modifier.fillMaxWidth().padding(vertical = 2.dp),
                        horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Text("${song.title} — ${song.artist}", color = Color.White, fontSize = 11.sp, modifier = Modifier.weight(1f))
                        Row {
                            TextButton(onClick = {
                                scope.launch {
                                    statusBanner = "Running Self-Neural 5-Stem Split on '${song.title}' (Deck A)..."
                                    repo.loadTrackIntoDeck(0, song)
                                    statusBanner = "Deck A Ready (${song.title})"
                                }
                            }) { Text("Load A", color = Color(0xFF00E5FF), fontSize = 10.sp) }

                            TextButton(onClick = {
                                scope.launch {
                                    statusBanner = "Running Self-Neural 5-Stem Split on '${song.title}' (Deck B)..."
                                    repo.loadTrackIntoDeck(1, song)
                                    statusBanner = "Deck B Ready (${song.title})"
                                }
                            }) { Text("Load B", color = Color(0xFFFA903E), fontSize = 10.sp) }
                        }
                    }
                }
            }
        }

        Spacer(modifier = Modifier.height(6.dp))

        // Dual CDJ-3000 + Self-Neural Deck Consoles
        Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            DeckConsoleCard(0, "DECK A // NEURAL CORE", Color(0xFF00E5FF), bpmDeckA, keyDeckA, repo, Modifier.weight(1f))
            DeckConsoleCard(1, "DECK B // NEURAL CORE", Color(0xFFFA903E), bpmDeckB, keyDeckB, repo, Modifier.weight(1f))
        }

        Spacer(modifier = Modifier.height(6.dp))

        // Master Self-Neural Mixing Matrix & Rupert Neve Console
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .background(Color(0xFF111622), RoundedCornerShape(8.dp))
                .border(1.dp, Color(0xFF30363D), RoundedCornerShape(8.dp))
                .padding(8.dp)
        ) {
            Text(
                "SELF-NEURAL MASTER MATRIX // CROSS-DECK SIDECHAIN • SUB-BASS SYNTH • AIR EXCITER • 5 AI MORPH MODES",
                color = Color(0xFFFCC934),
                fontSize = 10.sp,
                fontFamily = FontFamily.Monospace,
                fontWeight = FontWeight.Bold
            )

            Spacer(modifier = Modifier.height(4.dp))

            // 5 Neural Butter Morph Style Selector Buttons
            val morphNames = listOf("OIL & WATER", "ACAPELLA RIDE", "BASS SWAP", "FESTIVAL SLAM", "HYPNOSIS")
            Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                morphNames.forEachIndexed { idx, name ->
                    Button(
                        onClick = {
                            selectedMorphStyle = idx
                            repo.nativeSetMasterParam(14, idx.toFloat())
                            statusBanner = "Neural Transition Style set to: $name"
                        },
                        modifier = Modifier.weight(1f).height(26.dp),
                        contentPadding = PaddingValues(0.dp),
                        colors = ButtonDefaults.buttonColors(
                            containerColor = if (selectedMorphStyle == idx) Color(0xFFF538A0) else Color(0xFF1F2937)
                        )
                    ) {
                        Text(name, fontSize = 8.sp, color = Color.White, fontWeight = FontWeight.Bold)
                    }
                }
            }

            Spacer(modifier = Modifier.height(4.dp))

            Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                MasterSlider("ISO LOW", isoLow, -1f..1f, Modifier.weight(1f)) { isoLow = it; repo.nativeSetMasterParam(1, it) }
                MasterSlider("ISO MID", isoMid, -1f..1f, Modifier.weight(1f)) { isoMid = it; repo.nativeSetMasterParam(2, it) }
                MasterSlider("ISO HIGH", isoHigh, -1f..1f, Modifier.weight(1f)) { isoHigh = it; repo.nativeSetMasterParam(3, it) }
                MasterSlider("NEVE DRIVE", neveDrive, 0f..1f, Modifier.weight(1f)) { neveDrive = it; repo.nativeSetMasterParam(4, it) }
            }

            Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                MasterSlider("NEURAL SIDECHAIN", neuralSidechain, 0f..1f, Modifier.weight(1f)) {
                    neuralSidechain = it; repo.nativeSetMasterParam(11, it)
                }
                MasterSlider("NEURAL SUB BOOM", neuralSubBoom, 0f..1f, Modifier.weight(1f)) {
                    neuralSubBoom = it; repo.nativeSetMasterParam(12, it)
                }
                MasterSlider("NEURAL AIR GLOW", neuralAirGlow, 0f..1f, Modifier.weight(1f)) {
                    neuralAirGlow = it; repo.nativeSetMasterParam(13, it)
                }
                MasterSlider("14 BEAT FX WET", masterBeatWet, 0f..1f, Modifier.weight(1f)) {
                    masterBeatWet = it; repo.nativeSetMasterParam(9, it)
                }
            }

            Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                MasterSlider("BEAT FX SELECTOR (1-14)", masterBeatFx, 0f..1f, Modifier.weight(1f)) {
                    masterBeatFx = it
                    repo.nativeSetMasterParam(8, it * 14f)
                }
            }

            Spacer(modifier = Modifier.height(4.dp))

            Row(modifier = Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                Button(
                    onClick = {
                        repo.nativeTriggerButterMorph(0, 1, 12.0)
                        statusBanner = "Self-Neural Harmonic Butter Morph A ➔ B Active!"
                    },
                    colors = ButtonDefaults.buttonColors(containerColor = Color(0xFFF538A0))
                ) {
                    Text("NEURAL MORPH A ➔ B", fontSize = 10.sp, fontWeight = FontWeight.Bold)
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
                        repo.nativeTriggerButterMorph(1, 0, 12.0)
                        statusBanner = "Self-Neural Harmonic Butter Morph B ➔ A Active!"
                    },
                    colors = ButtonDefaults.buttonColors(containerColor = Color(0xFF1A73E8))
                ) {
                    Text("NEURAL MORPH B ➔ A", fontSize = 10.sp, fontWeight = FontWeight.Bold)
                }
            }
        }
    }
}

@Composable
fun DeckConsoleCard(
    deckIndex: Int,
    title: String,
    accent: Color,
    bpm: Float,
    camelotKey: Int,
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
    var scratchVel by remember { mutableFloatStateOf(0f) }
    var pitchSemi by remember { mutableFloatStateOf(0f) }
    var transientPunch by remember { mutableFloatStateOf(0.25f) }
    var stereoWiden by remember { mutableFloatStateOf(0.2f) }

    var drumsStem by remember { mutableFloatStateOf(1f) }
    var bassStem by remember { mutableFloatStateOf(1f) }
    var otherStem by remember { mutableFloatStateOf(1f) }
    var vocalStem by remember { mutableFloatStateOf(1f) }
    var ghostStem by remember { mutableFloatStateOf(1f) }

    Column(
        modifier = modifier
            .background(Color(0xFF11141D), RoundedCornerShape(8.dp))
            .border(1.dp, accent.copy(alpha = 0.5f), RoundedCornerShape(8.dp))
            .padding(6.dp)
    ) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Column {
                Text(title, color = accent, fontWeight = FontWeight.Bold, fontSize = 11.sp, fontFamily = FontFamily.Monospace)
                Text(
                    "BPM: %.1f  |  KEY: %dA".format(bpm, camelotKey),
                    color = Color(0xFF8B949E),
                    fontSize = 9.sp,
                    fontFamily = FontFamily.Monospace
                )
            }
            Button(
                onClick = { playing = !playing; repo.nativeSetDeckPlayState(deckIndex, playing) },
                colors = ButtonDefaults.buttonColors(containerColor = if (playing) Color(0xFF2EA043) else Color(0xFF21262D))
            ) {
                Text(if (playing) "PAUSE" else "PLAY", color = Color.White, fontSize = 10.sp, fontWeight = FontWeight.Bold)
            }
        }

        Spacer(modifier = Modifier.height(4.dp))

        // CDJ-3000 Touch Jogwheel Scratch & Neural Pitch Bend Simulator
        Box(
            modifier = Modifier
                .fillMaxWidth()
                .height(48.dp)
                .background(Color(0xFF07090E), CircleShape)
                .border(2.dp, accent, CircleShape)
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
            Text(
                "CDJ-3000 JOGWHEEL SCRATCH",
                color = accent.copy(alpha = 0.8f),
                fontSize = 9.sp,
                fontWeight = FontWeight.Bold,
                fontFamily = FontFamily.Monospace
            )
        }

        Spacer(modifier = Modifier.height(4.dp))

        // 8 Instant Self-Neural Performance Macro Pads
        val macroLabels = listOf("ACAPELLA", "INST ONLY", "DRUM+BASS", "STEM 100%", "RISER FX", "SUB DROP", "1/4 ROLL", "+2 SEMI")
        Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(2.dp)) {
            for (m in 0 until 4) {
                Button(
                    onClick = { repo.nativeTriggerNeuralPadMacro(deckIndex, m) },
                    modifier = Modifier.weight(1f).height(22.dp),
                    contentPadding = PaddingValues(0.dp),
                    colors = ButtonDefaults.buttonColors(containerColor = Color(0xFF1E293B))
                ) {
                    Text(macroLabels[m], fontSize = 7.sp, color = accent, fontWeight = FontWeight.Bold)
                }
            }
        }
        Row(modifier = Modifier.fillMaxWidth().padding(top = 2.dp), horizontalArrangement = Arrangement.spacedBy(2.dp)) {
            for (m in 4 until 8) {
                Button(
                    onClick = { repo.nativeTriggerNeuralPadMacro(deckIndex, m) },
                    modifier = Modifier.weight(1f).height(22.dp),
                    contentPadding = PaddingValues(0.dp),
                    colors = ButtonDefaults.buttonColors(containerColor = Color(0xFF281E3B))
                ) {
                    Text(macroLabels[m], fontSize = 7.sp, color = Color(0xFFF472B6), fontWeight = FontWeight.Bold)
                }
            }
        }

        // Hot Cues & Beat Loop Row
        Row(modifier = Modifier.fillMaxWidth().padding(top = 2.dp), horizontalArrangement = Arrangement.spacedBy(2.dp)) {
            for (c in 0 until 2) {
                Button(
                    onClick = { repo.nativeTriggerHotCue(deckIndex, c) },
                    modifier = Modifier.weight(1f).height(22.dp),
                    contentPadding = PaddingValues(0.dp),
                    colors = ButtonDefaults.buttonColors(containerColor = Color(0xFF1F2430))
                ) {
                    Text("CUE ${c + 1}", fontSize = 8.sp, color = Color.White)
                }
            }
            Button(
                onClick = { repo.nativeToggleBeatLoop(deckIndex, 4.0) },
                modifier = Modifier.weight(1f).height(22.dp),
                contentPadding = PaddingValues(0.dp),
                colors = ButtonDefaults.buttonColors(containerColor = Color(0xFF2D333B))
            ) {
                Text("4-LOOP", fontSize = 8.sp, color = Color(0xFF57AB5A))
            }
            Button(
                onClick = { repo.nativeSetChannelParam(deckIndex, 16, 1f) },
                modifier = Modifier.weight(1f).height(22.dp),
                contentPadding = PaddingValues(0.dp),
                colors = ButtonDefaults.buttonColors(containerColor = Color(0xFF3B232A))
            ) {
                Text("BRAKE", fontSize = 8.sp, color = Color(0xFFF85149))
            }
        }

        HorizontalDivider(color = Color(0xFF21262D), modifier = Modifier.padding(vertical = 3.dp))
        Text("SELF-NEURAL 5-STEM ISOLATION", color = accent, fontSize = 8.sp, fontFamily = FontFamily.Monospace)
        MiniSlider("DRUMS", drumsStem, 0f..1.3f) { drumsStem = it; repo.nativeSetChannelParam(deckIndex, 9, it) }
        MiniSlider("BASS", bassStem, 0f..1.3f) { bassStem = it; repo.nativeSetChannelParam(deckIndex, 10, it) }
        MiniSlider("SYNTH/OTHER", otherStem, 0f..1.3f) { otherStem = it; repo.nativeSetChannelParam(deckIndex, 11, it) }
        MiniSlider("VOCALS", vocalStem, 0f..1.3f) { vocalStem = it; repo.nativeSetChannelParam(deckIndex, 12, it) }
        MiniSlider("GHOST SNAP", ghostStem, 0f..1.5f) { ghostStem = it; repo.nativeSetChannelParam(deckIndex, 22, it) }

        HorizontalDivider(color = Color(0xFF21262D), modifier = Modifier.padding(vertical = 3.dp))
        Text("NEURAL KEY SHIFT • PUNCH • 4-BAND EQ • XONE VCF", color = Color.Gray, fontSize = 8.sp, fontFamily = FontFamily.Monospace)
        MiniSlider("KEY SHIFT", pitchSemi, -6f..6f) { pitchSemi = it; repo.nativeSetChannelParam(deckIndex, 19, it) }
        MiniSlider("NEURAL PUNCH", transientPunch, 0f..1f) { transientPunch = it; repo.nativeSetChannelParam(deckIndex, 20, it) }
        MiniSlider("3D WIDENER", stereoWiden, 0f..1f) { stereoWiden = it; repo.nativeSetChannelParam(deckIndex, 21, it) }
        MiniSlider("EQ HIGH", eqHi, -1f..1f) { eqHi = it; repo.nativeSetChannelParam(deckIndex, 4, it) }
        MiniSlider("EQ HI-MID", eqHm, -1f..1f) { eqHm = it; repo.nativeSetChannelParam(deckIndex, 3, it) }
        MiniSlider("EQ LO-MID", eqLm, -1f..1f) { eqLm = it; repo.nativeSetChannelParam(deckIndex, 2, it) }
        MiniSlider("EQ LOW", eqLo, -1f..1f) { eqLo = it; repo.nativeSetChannelParam(deckIndex, 1, it) }
        MiniSlider("VCF CUTOFF", vcfCutoff, 0f..1f) { vcfCutoff = it; repo.nativeSetChannelParam(deckIndex, 7, it) }
        MiniSlider("VCF CRUNCH", vcfCrunch, 0f..1f) { vcfCrunch = it; repo.nativeSetChannelParam(deckIndex, 8, it) }
        MiniSlider("CH VOLUME", fader, 0f..1f) { fader = it; repo.nativeSetChannelParam(deckIndex, 0, it) }
    }
}

@Composable
fun MiniSlider(label: String, value: Float, range: ClosedFloatingPointRange<Float>, onChange: (Float) -> Unit) {
    Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier.fillMaxWidth().height(20.dp)) {
        Text(label, color = Color(0xFFC9D1D9), fontSize = 8.sp, modifier = Modifier.width(68.dp), fontFamily = FontFamily.Monospace)
        Slider(value = value, onValueChange = onChange, valueRange = range, modifier = Modifier.weight(1f))
    }
}

@Composable
fun MasterSlider(
    label: String,
    value: Float,
    range: ClosedFloatingPointRange<Float>,
    modifier: Modifier = Modifier,
    onChange: (Float) -> Unit
) {
    Column(modifier = modifier, horizontalAlignment = Alignment.CenterHorizontally) {
        Text(label, color = Color.LightGray, fontSize = 8.sp, fontFamily = FontFamily.Monospace)
        Slider(value = value, onValueChange = onChange, valueRange = range)
    }
}
