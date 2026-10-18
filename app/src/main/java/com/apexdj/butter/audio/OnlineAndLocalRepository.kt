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
