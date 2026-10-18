#!/data/data/com.termux/files/usr/bin/bash
set -e

echo "[1/4] Writing app/src/main/java/com/apexdj/butter/audio/OnlineAndLocalRepository.kt..."
mkdir -p app/src/main/java/com/apexdj/butter/audio
cat << 'EOF' > app/src/main/java/com/apexdj/butter/audio/OnlineAndLocalRepository.kt
package com.apexdj.butter.audio

import android.content.Context
import android.media.MediaCodec
import android.media.MediaExtractor
import android.media.MediaFormat
import android.net.Uri
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import okhttp3.OkHttpClient
import okhttp3.Request
import org.json.JSONObject
import java.io.File
import java.io.FileOutputStream
import java.net.URLEncoder
import java.nio.ByteOrder
import java.util.concurrent.TimeUnit
import kotlin.math.PI
import kotlin.math.sin

data class SongTrack(
    val id: String,
    val title: String,
    val artist: String,
    val durationSec: Int,
    val streamUrl: String,
    val isLocal: Boolean = false,
    val studioPresetIndex: Int = -1,
    val localUri: Uri? = null
)

class OnlineAndLocalRepository(private val context: Context) {

    init {
        System.loadLibrary("apexdj_engine")
        nativeInitEngine(context.assets)
    }

    private val httpClient = OkHttpClient.Builder()
        .connectTimeout(10, TimeUnit.SECONDS)
        .readTimeout(30, TimeUnit.SECONDS)
        .followRedirects(true)
        .build()

    suspend fun searchOnlineSongs(query: String): List<SongTrack> = withContext(Dispatchers.IO) {
        val results = mutableListOf<SongTrack>()
        val encoded = URLEncoder.encode(query.ifBlank { "dance" }, "UTF-8")

        // 1. Apple iTunes Music Search API (High Quality AAC Direct Streams)
        runCatching {
            val url = "https://itunes.apple.com/search?term=$encoded&entity=song&limit=15"
            val req = Request.Builder().url(url).build()
            httpClient.newCall(req).execute().use { resp ->
                if (resp.isSuccessful) {
                    val json = JSONObject(resp.body?.string() ?: "{}")
                    val resultsArr = json.optJSONArray("results")
                    if (resultsArr != null) {
                        for (i in 0 until resultsArr.length()) {
                            val item = resultsArr.getJSONObject(i)
                            val trackId = item.optLong("trackId", i.toLong()).toString()
                            val title = item.optString("trackName", "Apple Music Track")
                            val artist = item.optString("artistName", "Artist")
                            val previewUrl = item.optString("previewUrl", "")
                            val durationMs = item.optInt("trackTimeMillis", 180000)
                            if (previewUrl.isNotEmpty()) {
                                results.add(SongTrack(trackId, title, artist, durationMs / 1000, previewUrl))
                            }
                        }
                    }
                }
            }
        }

        // 2. Audius Discovery API
        runCatching {
            val url = "https://discoveryprovider.audius.co/v1/tracks/search?query=$encoded&app_name=ApexDJFlagship"
            val req = Request.Builder().url(url).build()
            httpClient.newCall(req).execute().use { resp ->
                if (resp.isSuccessful) {
                    val json = JSONObject(resp.body?.string() ?: "{}")
                    val data = json.optJSONArray("data")
                    if (data != null) {
                        for (i in 0 until minOf(data.length(), 10)) {
                            val item = data.getJSONObject(i)
                            val trackId = item.optString("id")
                            val title = item.optString("title", "Audius Track")
                            val user = item.optJSONObject("user")
                            val artist = user?.optString("name", "Artist") ?: "Artist"
                            val duration = item.optInt("duration", 200)
                            val streamEndpoint = "https://discoveryprovider.audius.co/v1/tracks/$trackId/stream?app_name=ApexDJFlagship"
                            results.add(SongTrack(trackId, title, artist, duration, streamEndpoint))
                        }
                    }
                }
            }
        }

        // 3. Built-in Multi-Stem Studio Club Tracks (Guaranteed instant zero-latency loading)
        val studioPresets = listOf(
            Triple("128 BPM Peak-Time Festival Tech House", "ApexDJ Neural Studio #1", 0),
            Triple("140 BPM Acid Techno Underground Anthem", "ApexDJ Neural Studio #2", 1),
            Triple("124 BPM Deep Melodic Progressive Groove", "ApexDJ Neural Studio #3", 2),
            Triple("150 BPM Hard Psytrance Energy", "ApexDJ Neural Studio #4", 3),
            Triple("96 BPM Trap & Sub-Bass Heavy Banger", "ApexDJ Neural Studio #5", 4),
            Triple("138 BPM Euphoric Trance Anthem", "ApexDJ Neural Studio #6", 5),
            Triple("174 BPM Drum & Bass Liquid Roller", "ApexDJ Neural Studio #7", 6),
            Triple("110 BPM Nu-Disco Funk Bounce", "ApexDJ Neural Studio #8", 7)
        )

        for ((title, artist, idx) in studioPresets) {
            results.add(
                SongTrack(
                    id = "studio_preset_$idx",
                    title = title,
                    artist = artist,
                    durationSec = 64,
                    streamUrl = "STUDIO_PRESET://$idx",
                    studioPresetIndex = idx
                )
            )
        }
        results
    }

    suspend fun loadTrackIntoDeck(deckIndex: Int, track: SongTrack, autoPlay: Boolean = true) = withContext(Dispatchers.IO) {
        if (track.studioPresetIndex >= 0) {
            nativeLoadBuiltInStudioPreset(deckIndex, track.studioPresetIndex, autoPlay)
            return@withContext
        }

        val cacheFile = File(context.cacheDir, "deck_${deckIndex}_cached_audio.tmp")
        if (track.isLocal && track.localUri != null) {
            context.contentResolver.openInputStream(track.localUri)?.use { input ->
                FileOutputStream(cacheFile).use { output -> input.copyTo(output) }
            }
        } else {
            var resolvedUrl = track.streamUrl
            if (resolvedUrl.startsWith("PIPED://")) {
                val vid = resolvedUrl.removePrefix("PIPED://")
                val metaReq = Request.Builder().url("https://pipedapi.kavin.rocks/streams/$vid").build()
                httpClient.newCall(metaReq).execute().use { resp ->
                    val json = JSONObject(resp.body?.string() ?: "{}")
                    val audioStreams = json.optJSONArray("audioStreams")
                    if (audioStreams != null && audioStreams.length() > 0) {
                        resolvedUrl = audioStreams.getJSONObject(0).optString("url")
                    }
                }
            }
            val req = Request.Builder().url(resolvedUrl).build()
            httpClient.newCall(req).execute().use { resp ->
                resp.body?.byteStream()?.use { input ->
                    FileOutputStream(cacheFile).use { output -> input.copyTo(output) }
                }
            }
        }

        val (leftPcm, rightPcm, sampleRate) = decodeAudioFileToFloatStereo(cacheFile.absolutePath)
        nativeLoadPcmToDeck(deckIndex, leftPcm, rightPcm, sampleRate, autoPlay)
    }

    private fun decodeAudioFileToFloatStereo(filePath: String): Triple<FloatArray, FloatArray, Int> {
        val extractor = MediaExtractor()
        runCatching { extractor.setDataSource(filePath) }.onFailure {
            return Triple(FloatArray(44100), FloatArray(44100), 44100)
        }

        var audioTrackIndex = -1
        var format: MediaFormat? = null

        for (i in 0 until extractor.trackCount) {
            val f = extractor.getTrackFormat(i)
            val mime = f.getString(MediaFormat.KEY_MIME) ?: ""
            if (mime.startsWith("audio/")) {
                audioTrackIndex = i
                format = f
                break
            }
        }
        if (audioTrackIndex < 0 || format == null) {
            extractor.release()
            return Triple(FloatArray(44100), FloatArray(44100), 44100)
        }

        extractor.selectTrack(audioTrackIndex)
        val mime = format.getString(MediaFormat.KEY_MIME)!!
        val sampleRate = format.getInteger(MediaFormat.KEY_SAMPLE_RATE)
        val channels = format.getInteger(MediaFormat.KEY_CHANNEL_COUNT)

        val codec = MediaCodec.createDecoderByType(mime)
        codec.configure(format, null, null, 0)
        codec.start()

        val maxSamples = sampleRate * 300
        val leftList = FloatArray(maxSamples)
        val rightList = FloatArray(maxSamples)
        var sampleCursor = 0

        val bufferInfo = MediaCodec.BufferInfo()
        var isEOS = false

        while (!isEOS && sampleCursor < maxSamples - 4096) {
            val inIdx = codec.dequeueInputBuffer(5000)
            if (inIdx >= 0) {
                val inBuf = codec.getInputBuffer(inIdx)!!
                val read = extractor.readSampleData(inBuf, 0)
                if (read < 0) {
                    codec.queueInputBuffer(inIdx, 0, 0, 0, MediaCodec.BUFFER_FLAG_END_OF_STREAM)
                    isEOS = true
                } else {
                    codec.queueInputBuffer(inIdx, 0, read, extractor.sampleTime, 0)
                    extractor.advance()
                }
            }

            var outIdx = codec.dequeueOutputBuffer(bufferInfo, 5000)
            while (outIdx >= 0) {
                val outBuf = codec.getOutputBuffer(outIdx)!!
                outBuf.order(ByteOrder.nativeOrder())
                val shortBuf = outBuf.asShortBuffer()
                while (shortBuf.hasRemaining() && sampleCursor < maxSamples) {
                    val sL = shortBuf.get() / 32768.0f
                    val sR = if (channels > 1 && shortBuf.hasRemaining()) shortBuf.get() / 32768.0f else sL
                    leftList[sampleCursor] = sL
                    rightList[sampleCursor] = sR
                    sampleCursor++
                }
                codec.releaseOutputBuffer(outIdx, false)
                outIdx = codec.dequeueOutputBuffer(bufferInfo, 0)
            }
        }

        codec.stop()
        codec.release()
        extractor.release()

        return Triple(leftList.copyOfRange(0, sampleCursor), rightList.copyOfRange(0, sampleCursor), sampleRate)
    }

    external fun nativeInitEngine(assetManager: android.content.res.AssetManager)
    external fun nativeLoadPcmToDeck(deckIndex: Int, leftPcm: FloatArray, rightPcm: FloatArray, sampleRate: Int, autoPlay: Boolean)
    external fun nativeLoadBuiltInStudioPreset(deckIndex: Int, presetIndex: Int, autoPlay: Boolean)
    external fun nativeSetDeckPlayState(deck: Int, playing: Boolean)
    external fun nativeGetDeckPlayState(deck: Int): Boolean
    external fun nativeGetDeckProgress(deck: Int): Float
    external fun nativeGetDeckRemainingSeconds(deck: Int): Float
    external fun nativeSeekDeckProgress(deck: Int, normProgress: Float)
    external fun nativeTriggerButterMorph(fromDeck: Int, toDeck: Int, seconds: Double)
    external fun nativeIsMorphActive(): Boolean
    external fun nativeGetCrossfader(): Float
    external fun nativeTriggerHotCue(deck: Int, cueIndex: Int)
    external fun nativeToggleBeatLoop(deck: Int, beats: Double)
    external fun nativeTriggerNeuralPadMacro(deck: Int, macroIndex: Int)
    external fun nativeSetChannelParam(deck: Int, paramId: Int, value: Float)
    external fun nativeSetMasterParam(paramId: Int, value: Float)
    external fun nativeGetChannelVu(deck: Int): Float
    external fun nativeGetDetectedBpm(deck: Int): Float
    external fun nativeGetDetectedCamelotKey(deck: Int): Int
}
EOF

echo "[2/4] Writing app/src/main/java/com/apexdj/butter/ui/FlagshipMixerScreen.kt..."
mkdir -p app/src/main/java/com/apexdj/butter/ui
cat << 'EOF' > app/src/main/java/com/apexdj/butter/ui/FlagshipMixerScreen.kt
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
        mutableStateOf("1.02 GB 1-CRORE FLAGSHIP ENGINE • 16 NEURAL TENSOR BANKS • CONTINUOUS AUTO-DJ QUEUE ACTIVE")
    }

    // Auto-DJ Continuous Remix Queue State
    var autoDjEnabled by remember { mutableStateOf(false) }
    var remixQueue = remember { mutableStateListOf<SongTrack>() }
    var activeDeckPlaying by remember { mutableIntStateOf(0) } // 0 = Deck A, 1 = Deck B

    var crossfader by remember { mutableFloatStateOf(0f) }
    var neveDrive by remember { mutableFloatStateOf(0.60f) }
    var isoLow by remember { mutableFloatStateOf(0f) }
    var isoMid by remember { mutableFloatStateOf(0f) }
    var isoHigh by remember { mutableFloatStateOf(0f) }
    var masterBeatFx by remember { mutableFloatStateOf(0f) }
    var masterBeatWet by remember { mutableFloatStateOf(0f) }

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

    // Real-time Telemetry & Auto-DJ Continuous Transition Polling Loop
    LaunchedEffect(Unit) {
        // Load initial studio presets into Deck A and Deck B instantly
        repo.loadTrackIntoDeck(0, SongTrack("preset_0", "Peak-Time Tech House", "Studio #1", 64, "", studioPresetIndex = 0), autoPlay = false)
        repo.loadTrackIntoDeck(1, SongTrack("preset_1", "Acid Techno Anthem", "Studio #2", 64, "", studioPresetIndex = 1), autoPlay = false)

        while (true) {
            vuDeckA = repo.nativeGetChannelVu(0)
            vuDeckB = repo.nativeGetChannelVu(1)
            bpmDeckA = repo.nativeGetDetectedBpm(0)
            bpmDeckB = repo.nativeGetDetectedBpm(1)
            keyDeckA = repo.nativeGetDetectedCamelotKey(0)
            keyDeckB = repo.nativeGetDetectedCamelotKey(1)
            crossfader = repo.nativeGetCrossfader()

            // Continuous Auto-DJ Intelligence
            if (autoDjEnabled && !repo.nativeIsMorphActive()) {
                val remA = repo.nativeGetDeckRemainingSeconds(0)
                val remB = repo.nativeGetDeckRemainingSeconds(1)
                if (activeDeckPlaying == 0 && remA < 14.0f && remA > 0.5f) {
                    if (remixQueue.isNotEmpty()) {
                        val nextTrack = remixQueue.removeAt(0)
                        statusBanner = "Auto-DJ: Transitioning Deck A ➔ Deck B with '${nextTrack.title}'"
                        repo.loadTrackIntoDeck(1, nextTrack, autoPlay = true)
                        repo.nativeTriggerButterMorph(0, 1, 12.0)
                        activeDeckPlaying = 1
                    }
                } else if (activeDeckPlaying == 1 && remB < 14.0f && remB > 0.5f) {
                    if (remixQueue.isNotEmpty()) {
                        val nextTrack = remixQueue.removeAt(0)
                        statusBanner = "Auto-DJ: Transitioning Deck B ➔ Deck A with '${nextTrack.title}'"
                        repo.loadTrackIntoDeck(0, nextTrack, autoPlay = true)
                        repo.nativeTriggerButterMorph(1, 0, 12.0)
                        activeDeckPlaying = 0
                    }
                }
            }
            delay(100)
        }
    }

    var targetLocalDeck by remember { mutableIntStateOf(0) }
    val localPicker = rememberLauncherForActivityResult(ActivityResultContracts.GetContent()) { uri: Uri? ->
        if (uri != null) {
            scope.launch {
                statusBanner = "Decoding Local File on Deck ${targetLocalDeck + 1}..."
                repo.loadTrackIntoDeck(
                    targetLocalDeck,
                    SongTrack("local", "Local Audio", "Storage", 240, "", isLocal = true, localUri = uri),
                    autoPlay = true
                )
                statusBanner = "Deck ${targetLocalDeck + 1} Playing Local Audio with 5 Neural Stems!"
            }
        }
    }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .background(Color(0xFF07090E))
            .windowInsetsPadding(WindowInsets.statusBars)
            .padding(8.dp)
            .verticalScroll(rememberScrollState())
    ) {
        // Top Header Banner
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
                    "APEX-DJ // 1.02 GB FLAGSHIP 1-CRORE EDITION (16 NEURAL TENSOR BANKS)",
                    color = Color(0xFF00E5FF),
                    fontFamily = FontFamily.Monospace,
                    fontWeight = FontWeight.Bold,
                    fontSize = 11.sp
                )
                Text(statusBanner, color = Color(0xFF9CA3AF), fontSize = 9.sp, fontFamily = FontFamily.Monospace)
            }
            Canvas(modifier = Modifier.size(95.dp, 30.dp).background(Color(0xFF05070A))) {
                val w = size.width
                val h = size.height
                drawLine(Color(0xFF00E5FF), Offset(0f, h * 0.35f), Offset(w * vuDeckA.coerceIn(0f, 1f), h * 0.35f), strokeWidth = 7f)
                drawLine(Color(0xFFFA903E), Offset(0f, h * 0.75f), Offset(w * vuDeckB.coerceIn(0f, 1f), h * 0.75f), strokeWidth = 7f)
            }
        }

        Spacer(modifier = Modifier.height(4.dp))

        // Search, Local Pickers & Auto-DJ Toggle Bar
        Row(modifier = Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            OutlinedTextField(
                value = searchQuery,
                onValueChange = { searchQuery = it },
                label = { Text("Search Apple Music AAC / Audius / Studio Stems", color = Color.LightGray, fontSize = 9.sp) },
                modifier = Modifier.weight(1f),
                singleLine = true
            )
            Spacer(modifier = Modifier.width(3.dp))
            Button(
                onClick = {
                    scope.launch {
                        statusBanner = "Searching Apple Music & Studio Stem Libraries..."
                        searchResults = repo.searchOnlineSongs(searchQuery)
                        statusBanner = "Found ${searchResults.size} tracks ready."
                    }
                },
                colors = ButtonDefaults.buttonColors(containerColor = Color(0xFF1A73E8))
            ) {
                Text("Search", fontSize = 10.sp)
            }
            Spacer(modifier = Modifier.width(3.dp))
            Button(
                onClick = {
                    autoDjEnabled = !autoDjEnabled
                    statusBanner = if (autoDjEnabled) "🟢 Auto-DJ Continuous Remix Queue ENGAGED!" else "🔴 Auto-DJ OFF"
                },
                colors = ButtonDefaults.buttonColors(containerColor = if (autoDjEnabled) Color(0xFF2EA043) else Color(0xFF21262D))
            ) {
                Text(if (autoDjEnabled) "Auto-DJ: ON" else "Auto-DJ: OFF", fontSize = 9.sp, fontWeight = FontWeight.Bold)
            }
        }

        // Search Results & Queue Management Drawer
        if (searchResults.isNotEmpty()) {
            LazyColumn(
                modifier = Modifier
                    .fillMaxWidth()
                    .height(115.dp)
                    .background(Color(0xFF0D1117), RoundedCornerShape(6.dp))
                    .padding(4.dp)
            ) {
                items(searchResults) { song ->
                    Row(
                        modifier = Modifier.fillMaxWidth().padding(vertical = 2.dp),
                        horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Text("${song.title} — ${song.artist}", color = Color.White, fontSize = 10.sp, modifier = Modifier.weight(1f))
                        Row {
                            TextButton(onClick = {
                                scope.launch { repo.loadTrackIntoDeck(0, song, autoPlay = true) }
                            }) { Text("Deck A", color = Color(0xFF00E5FF), fontSize = 9.sp) }

                            TextButton(onClick = {
                                scope.launch { repo.loadTrackIntoDeck(1, song, autoPlay = true) }
                            }) { Text("Deck B", color = Color(0xFFFA903E), fontSize = 9.sp) }

                            TextButton(onClick = {
                                remixQueue.add(song)
                                statusBanner = "Added '${song.title}' to Auto-DJ Remix Queue (${remixQueue.size} queued)"
                            }) { Text("+ Queue", color = Color(0xFFFCC934), fontSize = 9.sp) }
                        }
                    }
                }
            }
        }

        if (remixQueue.isNotEmpty()) {
            Text(
                "Remix Queue (${remixQueue.size} songs waiting for Auto-DJ): " + remixQueue.joinToString(" ➔ ") { it.title },
                color = Color(0xFFFCC934),
                fontSize = 9.sp,
                fontFamily = FontFamily.Monospace,
                modifier = Modifier.padding(vertical = 2.dp)
            )
        }

        Spacer(modifier = Modifier.height(4.dp))

        // Dual CDJ-3000 Decks
        Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            DeckConsoleCard(0, "DECK A // CDJ-3000", Color(0xFF00E5FF), bpmDeckA, keyDeckA, repo, Modifier.weight(1f))
            DeckConsoleCard(1, "DECK B // CDJ-3000", Color(0xFFFA903E), bpmDeckB, keyDeckB, repo, Modifier.weight(1f))
        }

        Spacer(modifier = Modifier.height(4.dp))

        // Master Console & AI Transition Matrix
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .background(Color(0xFF111622), RoundedCornerShape(8.dp))
                .border(1.dp, Color(0xFF30363D), RoundedCornerShape(8.dp))
                .padding(8.dp)
        ) {
            Text(
                "MASTER CONSOLE // RUPERT NEVE TRANSFORMER • SUB BOOM • AIR GLOW • 6 AI MORPH MODES",
                color = Color(0xFFFCC934),
                fontSize = 10.sp,
                fontFamily = FontFamily.Monospace,
                fontWeight = FontWeight.Bold
            )

            Spacer(modifier = Modifier.height(3.dp))

            val morphNames = listOf("OIL & WATER", "VOCAL RIDE", "BASS SWAP", "FESTIVAL SLAM", "HYPNOSIS", "SHIMMER WASH")
            Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(3.dp)) {
                morphNames.forEachIndexed { idx, name ->
                    Button(
                        onClick = {
                            selectedMorphStyle = idx
                            repo.nativeSetMasterParam(14, idx.toFloat())
                            statusBanner = "AI Transition Style: $name"
                        },
                        modifier = Modifier.weight(1f).height(24.dp),
                        contentPadding = PaddingValues(0.dp),
                        colors = ButtonDefaults.buttonColors(
                            containerColor = if (selectedMorphStyle == idx) Color(0xFFF538A0) else Color(0xFF1F2937)
                        )
                    ) {
                        Text(name, fontSize = 7.sp, color = Color.White, fontWeight = FontWeight.Bold)
                    }
                }
            }

            Spacer(modifier = Modifier.height(3.dp))

            Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                MasterSlider("ISO LOW", isoLow, -1f..1f, Modifier.weight(1f)) { isoLow = it; repo.nativeSetMasterParam(1, it) }
                MasterSlider("ISO MID", isoMid, -1f..1f, Modifier.weight(1f)) { isoMid = it; repo.nativeSetMasterParam(2, it) }
                MasterSlider("ISO HIGH", isoHigh, -1f..1f, Modifier.weight(1f)) { isoHigh = it; repo.nativeSetMasterParam(3, it) }
                MasterSlider("NEVE DRIVE", neveDrive, 0f..1f, Modifier.weight(1f)) { neveDrive = it; repo.nativeSetMasterParam(4, it) }
            }

            Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                MasterSlider("SIDECHAIN", neuralSidechain, 0f..1f, Modifier.weight(1f)) { neuralSidechain = it; repo.nativeSetMasterParam(11, it) }
                MasterSlider("SUB BOOM", neuralSubBoom, 0f..1f, Modifier.weight(1f)) { neuralSubBoom = it; repo.nativeSetMasterParam(12, it) }
                MasterSlider("AIR GLOW", neuralAirGlow, 0f..1f, Modifier.weight(1f)) { neuralAirGlow = it; repo.nativeSetMasterParam(13, it) }
                MasterSlider("BEAT FX WET", masterBeatWet, 0f..1f, Modifier.weight(1f)) { masterBeatWet = it; repo.nativeSetMasterParam(9, it) }
            }

            Spacer(modifier = Modifier.height(3.dp))

            Row(modifier = Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                Button(
                    onClick = {
                        repo.nativeTriggerButterMorph(0, 1, 12.0)
                        statusBanner = "AI Neural Morph A ➔ B Active!"
                    },
                    colors = ButtonDefaults.buttonColors(containerColor = Color(0xFFF538A0))
                ) {
                    Text("MORPH A ➔ B", fontSize = 9.sp, fontWeight = FontWeight.Bold)
                }
                Spacer(modifier = Modifier.width(4.dp))
                Slider(
                    value = crossfader,
                    onValueChange = { crossfader = it; repo.nativeSetMasterParam(0, it) },
                    valueRange = -1f..1f,
                    modifier = Modifier.weight(1f)
                )
                Spacer(modifier = Modifier.width(4.dp))
                Button(
                    onClick = {
                        repo.nativeTriggerButterMorph(1, 0, 12.0)
                        statusBanner = "AI Neural Morph B ➔ A Active!"
                    },
                    colors = ButtonDefaults.buttonColors(containerColor = Color(0xFF1A73E8))
                ) {
                    Text("MORPH B ➔ A", fontSize = 9.sp, fontWeight = FontWeight.Bold)
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
    var trackProgress by remember { mutableFloatStateOf(0f) }
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

    // Sync play state and track progress every 80ms
    LaunchedEffect(Unit) {
        while (true) {
            playing = repo.nativeGetDeckPlayState(deckIndex)
            trackProgress = repo.nativeGetDeckProgress(deckIndex)
            delay(80)
        }
    }

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
                onClick = {
                    val newState = !playing
                    playing = newState
                    repo.nativeSetDeckPlayState(deckIndex, newState)
                },
                colors = ButtonDefaults.buttonColors(containerColor = if (playing) Color(0xFF2EA043) else Color(0xFF21262D))
            ) {
                Text(if (playing) "PLAYING" else "PAUSE", color = Color.White, fontSize = 9.sp, fontWeight = FontWeight.Bold)
            }
        }

        Spacer(modifier = Modifier.height(3.dp))

        // Interactive Track Progress Scrubber Bar
        LinearProgressIndicator(
            progress = { trackProgress },
            modifier = Modifier
                .fillMaxWidth()
                .height(8.dp)
                .pointerInput(Unit) {
                    detectDragGestures { change, _ ->
                        val newProg = (change.position.x / size.width).coerceIn(0f, 1f)
                        trackProgress = newProg
                        repo.nativeSeekDeckProgress(deckIndex, newProg)
                    }
                },
            color = accent,
            trackColor = Color(0xFF1F2937),
        )

        Spacer(modifier = Modifier.height(3.dp))

        // CDJ-3000 Touch Jogwheel Scratch
        Box(
            modifier = Modifier
                .fillMaxWidth()
                .height(42.dp)
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
                fontSize = 8.sp,
                fontWeight = FontWeight.Bold,
                fontFamily = FontFamily.Monospace
            )
        }

        Spacer(modifier = Modifier.height(3.dp))

        // 8 Instant Performance Macro Pads
        val macroLabels = listOf("ACAPELLA", "INST ONLY", "DRUM+BASS", "STEM 100%", "RISER FX", "SUB DROP", "1/4 ROLL", "+2 SEMI")
        Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(2.dp)) {
            for (m in 0 until 4) {
                Button(
                    onClick = { repo.nativeTriggerNeuralPadMacro(deckIndex, m) },
                    modifier = Modifier.weight(1f).height(20.dp),
                    contentPadding = PaddingValues(0.dp),
                    colors = ButtonDefaults.buttonColors(containerColor = Color(0xFF1E293B))
                ) {
                    Text(macroLabels[m], fontSize = 6.sp, color = accent, fontWeight = FontWeight.Bold)
                }
            }
        }
        Row(modifier = Modifier.fillMaxWidth().padding(top = 2.dp), horizontalArrangement = Arrangement.spacedBy(2.dp)) {
            for (m in 4 until 8) {
                Button(
                    onClick = { repo.nativeTriggerNeuralPadMacro(deckIndex, m) },
                    modifier = Modifier.weight(1f).height(20.dp),
                    contentPadding = PaddingValues(0.dp),
                    colors = ButtonDefaults.buttonColors(containerColor = Color(0xFF281E3B))
                ) {
                    Text(macroLabels[m], fontSize = 6.sp, color = Color(0xFFF472B6), fontWeight = FontWeight.Bold)
                }
            }
        }

        // Hot Cues & Loop Row
        Row(modifier = Modifier.fillMaxWidth().padding(top = 2.dp), horizontalArrangement = Arrangement.spacedBy(2.dp)) {
            for (c in 0 until 2) {
                Button(
                    onClick = { repo.nativeTriggerHotCue(deckIndex, c) },
                    modifier = Modifier.weight(1f).height(20.dp),
                    contentPadding = PaddingValues(0.dp),
                    colors = ButtonDefaults.buttonColors(containerColor = Color(0xFF1F2430))
                ) {
                    Text("CUE ${c + 1}", fontSize = 7.sp, color = Color.White)
                }
            }
            Button(
                onClick = { repo.nativeToggleBeatLoop(deckIndex, 4.0) },
                modifier = Modifier.weight(1f).height(20.dp),
                contentPadding = PaddingValues(0.dp),
                colors = ButtonDefaults.buttonColors(containerColor = Color(0xFF2D333B))
            ) {
                Text("4-LOOP", fontSize = 7.sp, color = Color(0xFF57AB5A))
            }
            Button(
                onClick = { repo.nativeSetChannelParam(deckIndex, 16, 1f) },
                modifier = Modifier.weight(1f).height(20.dp),
                contentPadding = PaddingValues(0.dp),
                colors = ButtonDefaults.buttonColors(containerColor = Color(0xFF3B232A))
            ) {
                Text("BRAKE", fontSize = 7.sp, color = Color(0xFFF85149))
            }
        }

        HorizontalDivider(color = Color(0xFF21262D), modifier = Modifier.padding(vertical = 2.dp))
        Text("5 SELF-NEURAL STEMS", color = accent, fontSize = 7.sp, fontFamily = FontFamily.Monospace)
        MiniSlider("DRUMS", drumsStem, 0f..1.3f) { drumsStem = it; repo.nativeSetChannelParam(deckIndex, 9, it) }
        MiniSlider("BASS", bassStem, 0f..1.3f) { bassStem = it; repo.nativeSetChannelParam(deckIndex, 10, it) }
        MiniSlider("SYNTH", otherStem, 0f..1.3f) { otherStem = it; repo.nativeSetChannelParam(deckIndex, 11, it) }
        MiniSlider("VOCALS", vocalStem, 0f..1.3f) { vocalStem = it; repo.nativeSetChannelParam(deckIndex, 12, it) }
        MiniSlider("GHOST", ghostStem, 0f..1.5f) { ghostStem = it; repo.nativeSetChannelParam(deckIndex, 22, it) }

        HorizontalDivider(color = Color(0xFF21262D), modifier = Modifier.padding(vertical = 2.dp))
        Text("KEY • PUNCH • 4-BAND EQ • VCF", color = Color.Gray, fontSize = 7.sp, fontFamily = FontFamily.Monospace)
        MiniSlider("KEY", pitchSemi, -6f..6f) { pitchSemi = it; repo.nativeSetChannelParam(deckIndex, 19, it) }
        MiniSlider("PUNCH", transientPunch, 0f..1f) { transientPunch = it; repo.nativeSetChannelParam(deckIndex, 20, it) }
        MiniSlider("WIDEN", stereoWiden, 0f..1f) { stereoWiden = it; repo.nativeSetChannelParam(deckIndex, 21, it) }
        MiniSlider("EQ HI", eqHi, -1f..1f) { eqHi = it; repo.nativeSetChannelParam(deckIndex, 4, it) }
        MiniSlider("EQ HM", eqHm, -1f..1f) { eqHm = it; repo.nativeSetChannelParam(deckIndex, 3, it) }
        MiniSlider("EQ LM", eqLm, -1f..1f) { eqLm = it; repo.nativeSetChannelParam(deckIndex, 2, it) }
        MiniSlider("EQ LO", eqLo, -1f..1f) { eqLo = it; repo.nativeSetChannelParam(deckIndex, 1, it) }
        MiniSlider("VCF CUT", vcfCutoff, 0f..1f) { vcfCutoff = it; repo.nativeSetChannelParam(deckIndex, 7, it) }
        MiniSlider("VCF CR", vcfCrunch, 0f..1f) { vcfCrunch = it; repo.nativeSetChannelParam(deckIndex, 8, it) }
        MiniSlider("FADER", fader, 0f..1f) { fader = it; repo.nativeSetChannelParam(deckIndex, 0, it) }
    }
}

@Composable
fun MiniSlider(label: String, value: Float, range: ClosedFloatingPointRange<Float>, onChange: (Float) -> Unit) {
    Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier.fillMaxWidth().height(18.dp)) {
        Text(label, color = Color(0xFFC9D1D9), fontSize = 7.sp, modifier = Modifier.width(55.dp), fontFamily = FontFamily.Monospace)
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
        Text(label, color = Color.LightGray, fontSize = 7.sp, fontFamily = FontFamily.Monospace)
        Slider(value = value, onValueChange = onChange, valueRange = range)
    }
}
EOF

echo "[3/4] Writing app/src/main/java/com/apexdj/butter/MainActivity.kt..."
cat << 'EOF' > app/src/main/java/com/apexdj/butter/MainActivity.kt
package com.apexdj.butter

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.core.view.WindowCompat
import com.apexdj.butter.audio.OnlineAndLocalRepository
import com.apexdj.butter.ui.FlagshipMixerScreen

class MainActivity : ComponentActivity() {
    private lateinit var repository: OnlineAndLocalRepository

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        WindowCompat.setDecorFitsSystemWindows(window, false)
        repository = OnlineAndLocalRepository(applicationContext)
        setContent {
            FlagshipMixerScreen(repo = repository)
        }
    }
}
EOF

echo "[4/4] Writing termux_deploy.sh..."
cat << 'EOF' > termux_deploy.sh
#!/data/data/com.termux/files/usr/bin/bash
set -e

echo "================================================================="
echo " APEX-DJ // 1.02 GB FAT-APK CLOUD BUILD & AUTO-DOWNLOADER"
echo "================================================================="

pkg install -y git gh

git add -A
git commit -m "1.02 GB Flagship Edition with 16 Neural Tensor Banks & Auto-DJ" || true
git push -u origin HEAD

echo "[+] Code pushed to GitHub successfully. Waiting 10 seconds for workflow..."
sleep 10

RUN_ID=$(gh run list --limit 1 --json databaseId --jq '.[0].databaseId')
echo "[+] Watching GitHub Actions Run ID: $RUN_ID"
gh run watch "$RUN_ID"

termux-setup-storage || true
sleep 2

mkdir -p ~/storage/downloads/ApexDJ_Build
gh run download "$RUN_ID" -n "ApexDJ-Flagship-1Crore-Edition-APK" -D ~/storage/downloads/ApexDJ_Build/
echo "[✓] SUCCESS! 1.02 GB Fat-APK downloaded directly to: ~/storage/downloads/ApexDJ_Build/"
EOF
chmod +x termux_deploy.sh

echo "[✓] PART 2 COMPLETE! All UI, Auto-DJ Queue, and 1.02 GB Build scripts generated."
