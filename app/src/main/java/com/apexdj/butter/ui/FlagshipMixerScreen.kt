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
    var statusBanner by remember { mutableStateOf("96 kHz / 64-Bit DSP • 14 Beat FX • 8 Hot Cues • Oil & Water Butter Morph Active") }

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

    LaunchedEffect(Unit) {
        while (true) {
            vuDeckA = repo.nativeGetChannelVu(0)
            vuDeckB = repo.nativeGetChannelVu(1)
            bpmDeckA = repo.nativeGetDetectedBpm(0)
            bpmDeckB = repo.nativeGetDetectedBpm(1)
            delay(16)
        }
    }

    var targetLocalDeck by remember { mutableIntStateOf(0) }
    val localPicker = rememberLauncherForActivityResult(ActivityResultContracts.GetContent()) { uri: Uri? ->
        if (uri != null) {
            scope.launch {
                statusBanner = "Decoding Local File & Running HT-Demucs 4-Stem ONNX on Deck ${targetLocalDeck + 1}..."
                repo.loadTrackIntoDeck(
                    targetLocalDeck,
                    SongTrack("local", "Local Audio", "Storage", 240, "", isLocal = true, localUri = uri)
                )
                statusBanner = "Deck ${targetLocalDeck + 1} Ready with Neural Stems!"
            }
        }
    }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .background(Color(0xFF090B0F))
            .padding(8.dp)
            .verticalScroll(rememberScrollState())
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .background(Color(0xFF131720), RoundedCornerShape(8.dp))
                .padding(8.dp),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Column {
                Text(
                    "APEX-DJ // 1-CRORE FLAGSHIP MIXER (CDJ-3000 + V10 + A9 + EUPHONIA)",
                    color = Color(0xFF00E5FF),
                    fontFamily = FontFamily.Monospace,
                    fontWeight = FontWeight.Bold,
                    fontSize = 13.sp
                )
                Text(statusBanner, color = Color(0xFF8B949E), fontSize = 10.sp, fontFamily = FontFamily.Monospace)
            }
            Canvas(modifier = Modifier.size(90.dp, 32.dp).background(Color(0xFF05070A))) {
                val w = size.width
                val h = size.height
                drawLine(Color(0xFF00E5FF), Offset(0f, h * 0.35f), Offset(w * vuDeckA.coerceIn(0f, 1f), h * 0.35f), strokeWidth = 7f)
                drawLine(Color(0xFFFA903E), Offset(0f, h * 0.75f), Offset(w * vuDeckB.coerceIn(0f, 1f), h * 0.75f), strokeWidth = 7f)
            }
        }

        Spacer(modifier = Modifier.height(6.dp))

        Row(modifier = Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            OutlinedTextField(
                value = searchQuery,
                onValueChange = { searchQuery = it },
                label = { Text("Instant Online Song Search (Audius / Piped)", color = Color.LightGray, fontSize = 10.sp) },
                modifier = Modifier.weight(1f),
                singleLine = true
            )
            Spacer(modifier = Modifier.width(4.dp))
            Button(
                onClick = {
                    scope.launch {
                        statusBanner = "Searching global online streams..."
                        searchResults = repo.searchOnlineSongs(searchQuery)
                        statusBanner = "Found ${searchResults.size} online tracks."
                    }
                },
                colors = ButtonDefaults.buttonColors(containerColor = Color(0xFF1A73E8))
            ) {
                Text("Search", fontSize = 11.sp)
            }
            Spacer(modifier = Modifier.width(4.dp))
            Button(onClick = { targetLocalDeck = 0; localPicker.launch("audio/*") }, colors = ButtonDefaults.buttonColors(containerColor = Color(0xFF21262D))) {
                Text("Local A", fontSize = 10.sp)
            }
            Spacer(modifier = Modifier.width(2.dp))
            Button(onClick = { targetLocalDeck = 1; localPicker.launch("audio/*") }, colors = ButtonDefaults.buttonColors(containerColor = Color(0xFF21262D))) {
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
                                    statusBanner = "Streaming & Separating '${song.title}' on Deck A..."
                                    repo.loadTrackIntoDeck(0, song)
                                    statusBanner = "Deck A Ready (${song.title})"
                                }
                            }) { Text("Deck A", color = Color(0xFF00E5FF), fontSize = 10.sp) }

                            TextButton(onClick = {
                                scope.launch {
                                    statusBanner = "Streaming & Separating '${song.title}' on Deck B..."
                                    repo.loadTrackIntoDeck(1, song)
                                    statusBanner = "Deck B Ready (${song.title})"
                                }
                            }) { Text("Deck B", color = Color(0xFFFA903E), fontSize = 10.sp) }
                        }
                    }
                }
            }
        }

        Spacer(modifier = Modifier.height(6.dp))

        Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            DeckConsoleCard(0, "DECK A (CDJ-3000)", Color(0xFF00E5FF), bpmDeckA, repo, Modifier.weight(1f))
            DeckConsoleCard(1, "DECK B (CDJ-3000)", Color(0xFFFA903E), bpmDeckB, repo, Modifier.weight(1f))
        }

        Spacer(modifier = Modifier.height(6.dp))

        Column(
            modifier = Modifier
                .fillMaxWidth()
                .background(Color(0xFF131720), RoundedCornerShape(8.dp))
                .border(1.dp, Color(0xFF30363D), RoundedCornerShape(8.dp))
                .padding(8.dp)
        ) {
            Text(
                "MASTER CONSOLE // 14 BEAT FX • RUPERT NEVE TRANSFORMER • 3-BAND ISOLATOR • BUTTER MORPH",
                color = Color(0xFFFCC934),
                fontSize = 11.sp,
                fontFamily = FontFamily.Monospace,
                fontWeight = FontWeight.Bold
            )

            Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                MasterSlider("ISO LOW", isoLow, Modifier.weight(1f)) { isoLow = it; repo.nativeSetMasterParam(1, it) }
                MasterSlider("ISO MID", isoMid, Modifier.weight(1f)) { isoMid = it; repo.nativeSetMasterParam(2, it) }
                MasterSlider("ISO HIGH", isoHigh, Modifier.weight(1f)) { isoHigh = it; repo.nativeSetMasterParam(3, it) }
                MasterSlider("NEVE DRIVE", neveDrive, Modifier.weight(1f)) { neveDrive = it; repo.nativeSetMasterParam(4, it) }
            }

            Spacer(modifier = Modifier.height(4.dp))

            Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                MasterSlider("BEAT FX TYPE (1-14)", masterBeatFx, Modifier.weight(1f)) {
                    masterBeatFx = it
                    repo.nativeSetMasterParam(8, (it * 13f))
                }
                MasterSlider("BEAT FX WET/DRY", masterBeatWet, Modifier.weight(1f)) {
                    masterBeatWet = it
                    repo.nativeSetMasterParam(9, it)
                }
            }

            Spacer(modifier = Modifier.height(4.dp))

            Row(modifier = Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                Button(
                    onClick = { repo.nativeTriggerButterMorph(0, 1, 12.0); statusBanner = "Butter Morph A ➔ B Active" },
                    colors = ButtonDefaults.buttonColors(containerColor = Color(0xFFF538A0))
                ) {
                    Text("BUTTER MORPH A ➔ B", fontSize = 10.sp, fontWeight = FontWeight.Bold)
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
                    onClick = { repo.nativeTriggerButterMorph(1, 0, 12.0); statusBanner = "Butter Morph B ➔ A Active" },
                    colors = ButtonDefaults.buttonColors(containerColor = Color(0xFF1A73E8))
                ) {
                    Text("BUTTER MORPH B ➔ A", fontSize = 10.sp, fontWeight = FontWeight.Bold)
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
    var scratchVel by remember { mutableFloatStateOf(0f) }
    var tempo by remember { mutableFloatStateOf(1f) }

    var drumsStem by remember { mutableFloatStateOf(1f) }
    var bassStem by remember { mutableFloatStateOf(1f) }
    var otherStem by remember { mutableFloatStateOf(1f) }
    var vocalStem by remember { mutableFloatStateOf(1f) }

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
                Text("BPM: %.1f".format(bpm), color = Color(0xFF8B949E), fontSize = 10.sp, fontFamily = FontFamily.Monospace)
            }
            Button(
                onClick = { playing = !playing; repo.nativeSetDeckPlayState(deckIndex, playing) },
                colors = ButtonDefaults.buttonColors(containerColor = if (playing) Color(0xFF57AB5A) else Color(0xFF21262D))
            ) {
                Text(if (playing) "PAUSE" else "PLAY", color = Color.White, fontSize = 10.sp, fontWeight = FontWeight.Bold)
            }
        }

        Spacer(modifier = Modifier.height(4.dp))

        // CDJ-3000 Touch Jogwheel Scratch Simulator
        Box(
            modifier = Modifier
                .fillMaxWidth()
                .height(55.dp)
                .background(Color(0xFF07090E), CircleShape)
                .border(2.dp, accent, CircleShape)
                .pointerInput(Unit) {
                    detectDragGestures(
                        onDragStart = { scratchVel = 1.5f; repo.nativeSetChannelParam(deckIndex, 18, 1.5f) },
                        onDragEnd = { scratchVel = 0f; repo.nativeSetChannelParam(deckIndex, 18, 0f) },
                        onDragCancel = { scratchVel = 0f; repo.nativeSetChannelParam(deckIndex, 18, 0f) }
                    ) { change, dragAmount ->
                        scratchVel = (dragAmount.x * 0.05f).coerceIn(-3f, 3f)
                        repo.nativeSetChannelParam(deckIndex, 18, scratchVel)
                    }
                },
            contentAlignment = Alignment.Center
        ) {
            Text("JOGWHEEL SCRATCH", color = accent.copy(alpha = 0.7f), fontSize = 10.sp, fontWeight = FontWeight.Bold, fontFamily = FontFamily.Monospace)
        }

        Spacer(modifier = Modifier.height(4.dp))

        // 8 Hot Cues & Loop Pads Grid
        Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(2.dp)) {
            for (c in 0 until 4) {
                Button(
                    onClick = { repo.nativeTriggerHotCue(deckIndex, c) },
                    modifier = Modifier.weight(1f).height(24.dp),
                    contentPadding = PaddingValues(0.dp),
                    colors = ButtonDefaults.buttonColors(containerColor = Color(0xFF1F2430))
                ) {
                    Text("CUE ${c + 1}", fontSize = 9.sp, color = Color.White)
                }
            }
        }
        Row(modifier = Modifier.fillMaxWidth().padding(top = 2.dp), horizontalArrangement = Arrangement.spacedBy(2.dp)) {
            Button(onClick = { repo.nativeToggleBeatLoop(deckIndex, 4.0) }, modifier = Modifier.weight(1f).height(24.dp), contentPadding = PaddingValues(0.dp), colors = ButtonDefaults.buttonColors(containerColor = Color(0xFF2D333B))) {
                Text("4-BEAT", fontSize = 9.sp, color = Color(0xFF57AB5A))
            }
            Button(onClick = { repo.nativeToggleBeatLoop(deckIndex, 8.0) }, modifier = Modifier.weight(1f).height(24.dp), contentPadding = PaddingValues(0.dp), colors = ButtonDefaults.buttonColors(containerColor = Color(0xFF2D333B))) {
                Text("8-BEAT", fontSize = 9.sp, color = Color(0xFF57AB5A))
            }
            Button(onClick = { repo.nativeSetChannelParam(deckIndex, 16, 1f) }, modifier = Modifier.weight(1f).height(24.dp), contentPadding = PaddingValues(0.dp), colors = ButtonDefaults.buttonColors(containerColor = Color(0xFF3B232A))) {
                Text("BRAKE", fontSize = 9.sp, color = Color(0xFFF85149))
            }
            Button(onClick = { repo.nativeSetChannelParam(deckIndex, 15, 1f) }, modifier = Modifier.weight(1f).height(24.dp), contentPadding = PaddingValues(0.dp), colors = ButtonDefaults.buttonColors(containerColor = Color(0xFF2D233B))) {
                Text("REVERSE", fontSize = 9.sp, color = Color(0xFFD29922))
            }
        }

        HorizontalDivider(color = Color(0xFF21262D), modifier = Modifier.padding(vertical = 4.dp))
        Text("HT-DEMUCS 4-STEMS", color = Color.Gray, fontSize = 9.sp, fontFamily = FontFamily.Monospace)
        MiniSlider("DRUMS", drumsStem, 0f..1f) { drumsStem = it; repo.nativeSetChannelParam(deckIndex, 9, it) }
        MiniSlider("BASS", bassStem, 0f..1f) { bassStem = it; repo.nativeSetChannelParam(deckIndex, 10, it) }
        MiniSlider("OTHER", otherStem, 0f..1f) { otherStem = it; repo.nativeSetChannelParam(deckIndex, 11, it) }
        MiniSlider("VOCALS", vocalStem, 0f..1f) { vocalStem = it; repo.nativeSetChannelParam(deckIndex, 12, it) }

        HorizontalDivider(color = Color(0xFF21262D), modifier = Modifier.padding(vertical = 4.dp))
        Text("4-BAND EQ & VCF", color = Color.Gray, fontSize = 9.sp, fontFamily = FontFamily.Monospace)
        MiniSlider("HI", eqHi, -1f..1f) { eqHi = it; repo.nativeSetChannelParam(deckIndex, 4, it) }
        MiniSlider("HM", eqHm, -1f..1f) { eqHm = it; repo.nativeSetChannelParam(deckIndex, 3, it) }
        MiniSlider("LM", eqLm, -1f..1f) { eqLm = it; repo.nativeSetChannelParam(deckIndex, 2, it) }
        MiniSlider("LO", eqLo, -1f..1f) { eqLo = it; repo.nativeSetChannelParam(deckIndex, 1, it) }
        MiniSlider("VCF CUTOFF", vcfCutoff, 0f..1f) { vcfCutoff = it; repo.nativeSetChannelParam(deckIndex, 7, it) }
        MiniSlider("VCF CRUNCH", vcfCrunch, 0f..1f) { vcfCrunch = it; repo.nativeSetChannelParam(deckIndex, 8, it) }
        MiniSlider("VOLUME FADER", fader, 0f..1f) { fader = it; repo.nativeSetChannelParam(deckIndex, 0, it) }
    }
}

@Composable
fun MiniSlider(label: String, value: Float, range: ClosedFloatingPointRange<Float>, onChange: (Float) -> Unit) {
    Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier.fillMaxWidth().height(22.dp)) {
        Text(label, color = Color(0xFFC9D1D9), fontSize = 8.sp, modifier = Modifier.width(75.dp), fontFamily = FontFamily.Monospace)
        Slider(value = value, onValueChange = onChange, valueRange = range, modifier = Modifier.weight(1f))
    }
}

@Composable
fun MasterSlider(label: String, value: Float, modifier: Modifier = Modifier, onChange: (Float) -> Unit) {
    Column(modifier = modifier, horizontalAlignment = Alignment.CenterHorizontally) {
        Text(label, color = Color.LightGray, fontSize = 8.sp, fontFamily = FontFamily.Monospace)
        Slider(value = value, onValueChange = onChange, valueRange = -1f..1f)
    }
}
