package com.apexdj.butter.audio

import android.content.ContentUris
import android.content.Context
import android.media.MediaCodec
import android.media.MediaExtractor
import android.media.MediaFormat
import android.net.Uri
import android.os.Environment
import android.provider.MediaStore
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import okhttp3.OkHttpClient
import okhttp3.Request
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.io.FileOutputStream
import java.net.URLEncoder
import java.nio.ByteOrder
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.TimeUnit

data class SongTrack(
    val id: String,
    val title: String,
    val artist: String,
    val durationSec: Int,
    val streamUrl: String,
    val sourceBadge: String,
    val estBpm: Int = 124,
    val isLocal: Boolean = false,
    val localUri: Uri? = null
)

class OnlineAndLocalRepository(private val context: Context) {

    private val repoScope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    private val deckLoadJobs = ConcurrentHashMap<Int, Job>()

    val obbManager = ObbExpansionManager(context) { obbPath ->
        val ok = nativeMountObbBrain(obbPath)
        _obbBrainMounted.value = ok
        ok
    }

    private val _obbBrainMounted = MutableStateFlow(false)
    val obbBrainMounted: StateFlow<Boolean> = _obbBrainMounted

    // 3 Decks: 0 = Deck A, 1 = Deck B, 2 = Background Deck (Continuous BGM/Groove)
    private val _waveDeckA = MutableStateFlow(FloatArray(256 * 3))
    val waveDeckA: StateFlow<FloatArray> = _waveDeckA

    private val _waveDeckB = MutableStateFlow(FloatArray(256 * 3))
    val waveDeckB: StateFlow<FloatArray> = _waveDeckB

    private val _waveDeckBgm = MutableStateFlow(FloatArray(256 * 3))
    val waveDeckBgm: StateFlow<FloatArray> = _waveDeckBgm

    private val _titleDeckA = MutableStateFlow("Deck A — Tap Any Song Below to Load (0ms)")
    val titleDeckA: StateFlow<String> = _titleDeckA

    private val _titleDeckB = MutableStateFlow("Deck B — Tap Any Song Below to Load (0ms)")
    val titleDeckB: StateFlow<String> = _titleDeckB

    private val _titleDeckBgm = MutableStateFlow("Background Deck — Load BGM / Groove Track")
    val titleDeckBgm: StateFlow<String> = _titleDeckBgm

    private val _localDeviceSongs = MutableStateFlow<List<SongTrack>>(emptyList())
    val localDeviceSongs: StateFlow<List<SongTrack>> = _localDeviceSongs

    init {
        System.loadLibrary("apexdj_engine")
        nativeInitEngine()
        // Zero automatic sample playback on boot! Only verify OBB and scan local MediaStore songs.
        repoScope.launch {
            obbManager.scanAndVerifyExistingObb()
            refreshInAppLocalSongs()
        }
    }

    private val httpClient = OkHttpClient.Builder()
        .connectTimeout(7, TimeUnit.SECONDS)
        .readTimeout(25, TimeUnit.SECONDS)
        .followRedirects(true)
        .followSslRedirects(true)
        .build()

    private val pipedClusterNodes = listOf(
        "https://pipedapi.kavin.rocks",
        "https://pipedapi.tokhmi.xyz",
        "https://piped-api.garudalinux.org",
        "https://api.piped.projectsegfau.lt",
        "https://pipedapi.leptons.xyz"
    )

    private val saavnClusterNodes = listOf(
        "https://saavn.dev/api/search/songs?query=",
        "https://jiosaavn-api-privatecvc2.vercel.app/search/songs?query="
    )

    /**
     * In-App Local Music Scanner (Queries Android MediaStore directly so NO external app is ever opened!)
     */
    suspend fun refreshInAppLocalSongs(): List<SongTrack> = withContext(Dispatchers.IO) {
        val localList = mutableListOf<SongTrack>()
        runCatching {
            val projection = arrayOf(
                MediaStore.Audio.Media._ID,
                MediaStore.Audio.Media.TITLE,
                MediaStore.Audio.Media.ARTIST,
                MediaStore.Audio.Media.DURATION,
                MediaStore.Audio.Media.DISPLAY_NAME
            )
            val selection = "${MediaStore.Audio.Media.DURATION} >= 15000"
            val sortOrder = "${MediaStore.Audio.Media.DATE_ADDED} DESC"

            context.contentResolver.query(
                MediaStore.Audio.Media.EXTERNAL_CONTENT_URI,
                projection,
                selection,
                null,
                sortOrder
            )?.use { cursor ->
                val idCol = cursor.getColumnIndexOrThrow(MediaStore.Audio.Media._ID)
                val titleCol = cursor.getColumnIndexOrThrow(MediaStore.Audio.Media.TITLE)
                val artistCol = cursor.getColumnIndexOrThrow(MediaStore.Audio.Media.ARTIST)
                val durCol = cursor.getColumnIndexOrThrow(MediaStore.Audio.Media.DURATION)
                val nameCol = cursor.getColumnIndexOrThrow(MediaStore.Audio.Media.DISPLAY_NAME)

                while (cursor.moveToNext() && localList.size < 400) {
                    val id = cursor.getLong(idCol)
                    val rawTitle = cursor.getString(titleCol) ?: cursor.getString(nameCol) ?: "Local Song"
                    val rawArtist = cursor.getString(artistCol)?.takeIf { it != "<unknown>" } ?: "Local Storage"
                    val durSec = ((cursor.getLong(durCol)) / 1000L).toInt().coerceAtLeast(1)
                    val contentUri = ContentUris.withAppendedId(MediaStore.Audio.Media.EXTERNAL_CONTENT_URI, id)

                    localList.add(
                        SongTrack(
                            id = "ms_$id",
                            title = rawTitle,
                            artist = rawArtist,
                            durationSec = durSec,
                            streamUrl = "",
                            sourceBadge = "IN-APP LOCAL",
                            estBpm = 124,
                            isLocal = true,
                            localUri = contentUri
                        )
                    )
                }
            }
        }
        _localDeviceSongs.value = localList
        localList
    }

    /**
     * Unfiltered Universal Song Search: Returns ANY song (Original tracks, Film songs, Pop, Hip-Hop, OR Remixes)
     * without forcing "DJ Remix" into the query!
     */
    suspend fun searchAnyOnlineSongs(rawQuery: String): List<SongTrack> = withContext(Dispatchers.IO) {
        val trimmed = rawQuery.trim()
        if (trimmed.isEmpty()) return@withContext _localDeviceSongs.value

        if (trimmed.startsWith("http://") || trimmed.startsWith("https://")) {
            return@withContext listOf(
                SongTrack(
                    id = "url_${trimmed.hashCode()}",
                    title = trimmed.substringAfterLast('/').take(45).ifEmpty { "Direct Stream" },
                    artist = "Direct Audio Link",
                    durationSec = 240,
                    streamUrl = trimmed,
                    sourceBadge = "DIRECT STREAM"
                )
            )
        }

        val encoded = URLEncoder.encode(trimmed, "UTF-8")

        supervisorScope {
            val saavnDef = async { searchSaavnSongs(encoded) }
            val pipedDef = async { searchPipedSongs(encoded) }
            val audiusDef = async { searchAudiusSongs(encoded) }
            val hearthisDef = async { searchHearthisSongs(encoded) }

            val combined = mutableListOf<SongTrack>()
            // Include matching in-app local songs first!
            combined.addAll(
                _localDeviceSongs.value.filter {
                    it.title.contains(trimmed, ignoreCase = true) || it.artist.contains(trimmed, ignoreCase = true)
                }
            )
            runCatching { combined.addAll(saavnDef.await()) }
            runCatching { combined.addAll(pipedDef.await()) }
            runCatching { combined.addAll(audiusDef.await()) }
            runCatching { combined.addAll(hearthisDef.await()) }

            combined.distinctBy { "${it.title.lowercase().take(22)}_${it.artist.lowercase().take(12)}" }
        }
    }

    private fun searchSaavnSongs(encodedQuery: String): List<SongTrack> {
        val list = mutableListOf<SongTrack>()
        for (base in saavnClusterNodes) {
            runCatching {
                val req = Request.Builder().url("$base$encodedQuery").header("User-Agent", "ApexDJArgon/12.0").build()
                httpClient.newCall(req).execute().use { resp ->
                    if (resp.isSuccessful) {
                        val root = JSONObject(resp.body?.string() ?: "{}")
                        val resultsArr = root.optJSONObject("data")?.optJSONArray("results") ?: root.optJSONArray("results")
                        if (resultsArr != null) {
                            for (i in 0 until minOf(resultsArr.length(), 18)) {
                                val item = resultsArr.getJSONObject(i)
                                val dlArr = item.optJSONArray("downloadUrl")
                                var bestStream = ""
                                if (dlArr != null && dlArr.length() > 0) {
                                    val lastObj = dlArr.getJSONObject(dlArr.length() - 1)
                                    bestStream = lastObj.optString("url", "").ifEmpty { lastObj.optString("link", "") }
                                }
                                if (bestStream.startsWith("http")) {
                                    val title = item.optString("name", "Song").replace("&quot;", "\"")
                                    val artist = item.optString("primaryArtists", "Artist").ifEmpty { "Artist" }
                                    val dur = item.optString("duration", "210").toIntOrNull() ?: 210
                                    list.add(SongTrack("sv_${item.optString("id", "$i")}", title, artist, dur, bestStream, "HQ SONG"))
                                }
                            }
                        }
                    }
                }
            }
            if (list.isNotEmpty()) break
        }
        return list
    }

    private fun searchPipedSongs(encodedQuery: String): List<SongTrack> {
        val list = mutableListOf<SongTrack>()
        for (node in pipedClusterNodes) {
            runCatching {
                val req = Request.Builder().url("$node/search?q=$encodedQuery&filter=all").build()
                httpClient.newCall(req).execute().use { resp ->
                    if (resp.isSuccessful) {
                        val items = JSONObject(resp.body?.string() ?: "{}").optJSONArray("items")
                        if (items != null) {
                            for (i in 0 until minOf(items.length(), 15)) {
                                val item = items.getJSONObject(i)
                                val vid = item.optString("url", "").substringAfter("v=", "")
                                if (vid.isNotEmpty()) {
                                    list.add(
                                        SongTrack(
                                            id = "yt_$vid",
                                            title = item.optString("title", "Online Song"),
                                            artist = item.optString("uploaderName", "Artist"),
                                            durationSec = item.optInt("duration", 210),
                                            streamUrl = "PIPED://$vid",
                                            sourceBadge = "GLOBAL AUDIO"
                                        )
                                    )
                                }
                            }
                        }
                    }
                }
            }
            if (list.isNotEmpty()) break
        }
        return list
    }

    private fun searchAudiusSongs(encodedQuery: String): List<SongTrack> {
        val list = mutableListOf<SongTrack>()
        var host = "https://discoveryprovider.audius.co"
        runCatching {
            httpClient.newCall(Request.Builder().url("https://api.audius.co").build()).execute().use { r ->
                val d = JSONObject(r.body?.string() ?: "{}").optJSONArray("data")
                if (d != null && d.length() > 0) host = d.getString(0)
            }
        }
        runCatching {
            val req = Request.Builder().url("$host/v1/tracks/search?query=$encodedQuery&app_name=ApexDJArgon").build()
            httpClient.newCall(req).execute().use { resp ->
                if (resp.isSuccessful) {
                    val data = JSONObject(resp.body?.string() ?: "{}").optJSONArray("data")
                    if (data != null) {
                        for (i in 0 until minOf(data.length(), 12)) {
                            val item = data.getJSONObject(i)
                            val id = item.optString("id")
                            val title = item.optString("title", "Track")
                            val artist = item.optJSONObject("user")?.optString("name", "Artist") ?: "Artist"
                            val dur = item.optInt("duration", 190)
                            list.add(SongTrack("au_$id", title, artist, dur, "$host/v1/tracks/$id/stream?app_name=ApexDJArgon", "AUDIUS"))
                        }
                    }
                }
            }
        }
        return list
    }

    private fun searchHearthisSongs(encodedQuery: String): List<SongTrack> {
        val list = mutableListOf<SongTrack>()
        runCatching {
            val req = Request.Builder().url("https://api-v2.hearthis.at/search?t=$encodedQuery&page=1&count=12").build()
            httpClient.newCall(req).execute().use { resp ->
                if (resp.isSuccessful) {
                    val arr = JSONArray(resp.body?.string() ?: "[]")
                    for (i in 0 until arr.length()) {
                        val item = arr.getJSONObject(i)
                        val stream = item.optString("stream_url", "").ifEmpty { item.optString("preview_url", "") }
                        if (stream.startsWith("http")) {
                            list.add(
                                SongTrack(
                                    id = "ht_${item.optString("id")}",
                                    title = item.optString("title", "Track"),
                                    artist = item.optJSONObject("user")?.optString("username", "Artist") ?: "Artist",
                                    durationSec = item.optString("duration", "210").toIntOrNull() ?: 210,
                                    streamUrl = stream,
                                    sourceBadge = "HEARTHIS"
                                )
                            )
                        }
                    }
                }
            }
        }
        return list
    }

    /**
     * 0ms Instant Deck Loader & Progressive Stream Decoder:
     * Updates deck title & play state in 0ms, then feeds decoded PCM in 1-second chunks so music plays immediately!
     */
    fun loadTrackIntoDeckInstant(
        deckIndex: Int,
        track: SongTrack,
        autoPlay: Boolean = true,
        onStatusUpdate: (String) -> Unit
    ) {
        val deckName = when (deckIndex) {
            0 -> { _titleDeckA.value = "${track.title} — ${track.artist}"; "Deck A" }
            1 -> { _titleDeckB.value = "${track.title} — ${track.artist}"; "Deck B" }
            else -> { _titleDeckBgm.value = "${track.title} — ${track.artist}"; "Background Deck" }
        }
        onStatusUpdate("$deckName Locked (0ms): ${track.title}")

        deckLoadJobs[deckIndex]?.cancel()
        deckLoadJobs[deckIndex] = repoScope.launch {
            try {
                val cacheFile = File(context.cacheDir, "deck_${deckIndex}_instant.tmp")
                if (track.isLocal && track.localUri != null) {
                    context.contentResolver.openInputStream(track.localUri)?.use { input ->
                        FileOutputStream(cacheFile).use { output -> input.copyTo(output, 128 * 1024) }
                    }
                } else {
                    val resolvedUrl = resolveStreamUrl(track.streamUrl)
                    val req = Request.Builder().url(resolvedUrl).header("User-Agent", "Mozilla/5.0 ApexDJArgon/12.0").build()
                    httpClient.newCall(req).execute().use { resp ->
                        resp.body?.byteStream()?.use { input ->
                            FileOutputStream(cacheFile).use { output ->
                                val buf = ByteArray(128 * 1024)
                                var total = 0L
                                while (isActive) {
                                    val r = input.read(buf)
                                    if (r <= 0) break
                                    output.write(buf, 0, r)
                                    total += r
                                    if (total >= 25L * 1024L * 1024L) break
                                }
                            }
                        }
                    }
                }

                if (!isActive || !cacheFile.exists() || cacheFile.length() < 1024L) return@launch
                streamDecodeFileToNativeChunks(deckIndex, cacheFile.absolutePath, track.durationSec, autoPlay)
            } catch (_: CancellationException) {
            } catch (e: Throwable) {
                onStatusUpdate("$deckName Stream Error: ${e.localizedMessage ?: "Check connection"}")
            }
        }
    }

    private fun resolveStreamUrl(rawUrl: String): String {
        if (rawUrl.startsWith("PIPED://")) {
            val vid = rawUrl.removePrefix("PIPED://")
            for (node in pipedClusterNodes) {
                runCatching {
                    httpClient.newCall(Request.Builder().url("$node/streams/$vid").build()).execute().use { resp ->
                        if (resp.isSuccessful) {
                            val streams = JSONObject(resp.body?.string() ?: "{}").optJSONArray("audioStreams")
                            if (streams != null && streams.length() > 0) {
                                return streams.getJSONObject(0).optString("url")
                            }
                        }
                    }
                }
            }
        }
        return rawUrl
    }

    private suspend fun streamDecodeFileToNativeChunks(
        deckIndex: Int,
        filePath: String,
        estDurationSec: Int,
        autoPlay: Boolean
    ) = withContext(Dispatchers.IO) {
        val extractor = MediaExtractor()
        var codec: MediaCodec? = null
        try {
            extractor.setDataSource(filePath)
            var audioTrackIndex = -1
            var format: MediaFormat? = null
            for (i in 0 until extractor.trackCount) {
                val f = extractor.getTrackFormat(i)
                if ((f.getString(MediaFormat.KEY_MIME) ?: "").startsWith("audio/")) {
                    audioTrackIndex = i; format = f; break
                }
            }
            if (audioTrackIndex < 0 || format == null) return@withContext

            extractor.selectTrack(audioTrackIndex)
            val mime = format.getString(MediaFormat.KEY_MIME) ?: return@withContext
            val sampleRate = runCatching { format.getInteger(MediaFormat.KEY_SAMPLE_RATE) }.getOrDefault(44100)
            val channels = runCatching { format.getInteger(MediaFormat.KEY_CHANNEL_COUNT) }.getOrDefault(2).coerceAtLeast(1)

            val expectedSamples = (estDurationSec.coerceIn(30, 300)) * sampleRate
            nativeBeginTrackLoad(deckIndex, expectedSamples, sampleRate)

            codec = MediaCodec.createDecoderByType(mime)
            codec.configure(format, null, null, 0)
            codec.start()

            val chunkCap = sampleRate * 2 // 2-second progressive chunks for instant playback!
            val leftChunk = FloatArray(chunkCap)
            val rightChunk = FloatArray(chunkCap)
            var chunkCursor = 0
            var firstChunkStarted = false

            val bufferInfo = MediaCodec.BufferInfo()
            var isEOS = false
            var loops = 0

            while (coroutineContext.isActive && !isEOS && loops++ < 60000) {
                val inIdx = codec.dequeueInputBuffer(2000)
                if (inIdx >= 0) {
                    val inBuf = codec.getInputBuffer(inIdx)
                    if (inBuf != null) {
                        val read = extractor.readSampleData(inBuf, 0)
                        if (read < 0) {
                            codec.queueInputBuffer(inIdx, 0, 0, 0, MediaCodec.BUFFER_FLAG_END_OF_STREAM)
                            isEOS = true
                        } else {
                            codec.queueInputBuffer(inIdx, 0, read, extractor.sampleTime, 0)
                            extractor.advance()
                        }
                    }
                }

                var outIdx = codec.dequeueOutputBuffer(bufferInfo, 2000)
                while (outIdx >= 0) {
                    val outBuf = codec.getOutputBuffer(outIdx)
                    if (outBuf != null && bufferInfo.size > 0) {
                        outBuf.position(bufferInfo.offset)
                        outBuf.limit(bufferInfo.offset + bufferInfo.size)
                        outBuf.order(ByteOrder.nativeOrder())
                        val shortBuf = outBuf.asShortBuffer()
                        while (shortBuf.hasRemaining()) {
                            val sL = shortBuf.get() / 32768.0f
                            val sR = if (channels > 1 && shortBuf.hasRemaining()) shortBuf.get() / 32768.0f else sL
                            if (channels > 2) {
                                for (ex in 2 until channels) if (shortBuf.hasRemaining()) shortBuf.get()
                            }
                            leftChunk[chunkCursor] = sL
                            rightChunk[chunkCursor] = sR
                            chunkCursor++

                            if (chunkCursor >= chunkCap) {
                                nativeAppendPcmChunk(deckIndex, leftChunk, rightChunk, chunkCursor, sampleRate)
                                updateDeckWaveformFlow(deckIndex)
                                if (!firstChunkStarted && autoPlay) {
                                    firstChunkStarted = true
                                    nativeSetDeckPlayState(deckIndex, true)
                                }
                                chunkCursor = 0
                            }
                        }
                    }
                    codec.releaseOutputBuffer(outIdx, false)
                    if ((bufferInfo.flags and MediaCodec.BUFFER_FLAG_END_OF_STREAM) != 0) {
                        isEOS = true; break
                    }
                    outIdx = codec.dequeueOutputBuffer(bufferInfo, 0)
                }
            }

            if (chunkCursor > 0) {
                nativeAppendPcmChunk(deckIndex, leftChunk, rightChunk, chunkCursor, sampleRate)
                updateDeckWaveformFlow(deckIndex)
                if (!firstChunkStarted && autoPlay) {
                    nativeSetDeckPlayState(deckIndex, true)
                }
            }
        } finally {
            runCatching { codec?.stop() }
            runCatching { codec?.release() }
            runCatching { extractor.release() }
        }
    }

    private fun updateDeckWaveformFlow(deckIndex: Int) {
        val wave = nativeGetRgbWaveform(deckIndex)
        when (deckIndex) {
            0 -> _waveDeckA.value = wave
            1 -> _waveDeckB.value = wave
            2 -> _waveDeckBgm.value = wave
        }
    }

    fun toggleMasterWavRecording(): Pair<Boolean, String> {
        return if (nativeIsMasterRecording()) {
            nativeStopMasterRecording()
            false to "Recording Saved to /Music/ApexDJ_Recordings/"
        } else {
            val musicDir = File(Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_MUSIC), "ApexDJ_Recordings")
            if (!musicDir.exists()) musicDir.mkdirs()
            val stamp = SimpleDateFormat("yyyyMMdd_HHmmss", Locale.US).format(Date())
            val outWav = File(musicDir, "ApexDJ_Mix_$stamp.wav")
            val started = nativeStartMasterRecording(outWav.absolutePath)
            started to if (started) "REC LIVE: ${outWav.name}" else "Failed to start recorder"
        }
    }

    external fun nativeInitEngine()
    external fun nativeMountObbBrain(obbPath: String): Boolean
    external fun nativeIsObbBrainMounted(): Boolean
    external fun nativeBeginTrackLoad(deckIndex: Int, totalExpectedSamples: Int, sampleRate: Int)
    external fun nativeAppendPcmChunk(deckIndex: Int, leftChunk: FloatArray, rightChunk: FloatArray, chunkSamples: Int, sampleRate: Int)
    external fun nativeSetDeckPlayState(deck: Int, playing: Boolean)
    external fun nativeGetDeckPlayState(deck: Int): Boolean
    external fun nativeSetNeuralAutoMix(enabled: Boolean)
    external fun nativeGetNeuralAutoMix(): Boolean
    external fun nativeStartMasterRecording(wavPath: String): Boolean
    external fun nativeStopMasterRecording()
    external fun nativeIsMasterRecording(): Boolean
    external fun nativeTriggerHotCue(deck: Int, cueIndex: Int)
    external fun nativeToggleBeatLoop(deck: Int, beats: Double)
    external fun nativeSeekNormalized(deck: Int, normPos: Float)
    external fun nativeSetChannelParam(deck: Int, paramId: Int, value: Float)
    external fun nativeSetMasterParam(paramId: Int, value: Float)
    external fun nativeGetChannelVu(deck: Int): Float
    external fun nativeGetDetectedBpm(deck: Int): Float
    external fun nativeGetCamelotKey(deck: Int): Int
    external fun nativeGetPlayheadNormalized(deck: Int): Float
    external fun nativeGetRgbWaveform(deck: Int): FloatArray
    external fun nativeGetBrainTelemetry(): String
}
