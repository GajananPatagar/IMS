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

data class SongTrack(
    val id: String,
    val title: String,
    val artist: String,
    val durationSec: Int,
    val streamUrl: String,
    val isLocal: Boolean = false,
    val localUri: Uri? = null
)

class OnlineAndLocalRepository(private val context: Context) {

    init {
        System.loadLibrary("apexdj_engine")
        nativeInitEngine()
    }

    private val httpClient = OkHttpClient.Builder()
        .connectTimeout(10, TimeUnit.SECONDS)
        .readTimeout(30, TimeUnit.SECONDS)
        .followRedirects(true)
        .build()

    fun ensureModelsDirectory(): String {
        val dir = File(context.filesDir, "models")
        if (!dir.exists()) dir.mkdirs()
        val stems = listOf("drums", "bass", "other", "vocals")
        for (stem in stems) {
            val fileName = "htdemucs_ft_$stem.onnx"
            val outFile = File(dir, fileName)
            if (!outFile.exists()) {
                runCatching {
                    context.assets.open("models/$fileName").use { input ->
                        FileOutputStream(outFile).use { output -> input.copyTo(output) }
                    }
                }
            }
        }
        return dir.absolutePath
    }

    suspend fun searchOnlineSongs(query: String): List<SongTrack> = withContext(Dispatchers.IO) {
        val results = mutableListOf<SongTrack>()
        val encoded = URLEncoder.encode(query, "UTF-8")

        runCatching {
            val url = "https://discoveryprovider.audius.co/v1/tracks/search?query=$encoded&app_name=ApexDJFlagship"
            val req = Request.Builder().url(url).build()
            httpClient.newCall(req).execute().use { resp ->
                if (resp.isSuccessful) {
                    val json = JSONObject(resp.body?.string() ?: "{}")
                    val data = json.optJSONArray("data")
                    if (data != null) {
                        for (i in 0 until minOf(data.length(), 15)) {
                            val item = data.getJSONObject(i)
                            val trackId = item.optString("id")
                            val title = item.optString("title", "Unknown Track")
                            val user = item.optJSONObject("user")
                            val artist = user?.optString("name", "Unknown Artist") ?: "Artist"
                            val duration = item.optInt("duration", 180)
                            val streamEndpoint = "https://discoveryprovider.audius.co/v1/tracks/$trackId/stream?app_name=ApexDJFlagship"
                            results.add(SongTrack(trackId, title, artist, duration, streamEndpoint, isLocal = false))
                        }
                    }
                }
            }
        }

        if (results.size < 5) {
            runCatching {
                val pipedUrl = "https://pipedapi.kavin.rocks/search?q=$encoded&filter=music_songs"
                val req = Request.Builder().url(pipedUrl).build()
                httpClient.newCall(req).execute().use { resp ->
                    if (resp.isSuccessful) {
                        val json = JSONObject(resp.body?.string() ?: "{}")
                        val items = json.optJSONArray("items")
                        if (items != null) {
                            for (i in 0 until minOf(items.length(), 10)) {
                                val item = items.getJSONObject(i)
                                val urlPath = item.optString("url", "")
                                val videoId = urlPath.substringAfter("v=", "")
                                if (videoId.isNotEmpty()) {
                                    results.add(
                                        SongTrack(
                                            id = videoId,
                                            title = item.optString("title", "Online Stream"),
                                            artist = item.optString("uploaderName", "Artist"),
                                            durationSec = item.optInt("duration", 200),
                                            streamUrl = "PIPED://$videoId",
                                            isLocal = false
                                        )
                                    )
                                }
                            }
                        }
                    }
                }
            }
        }
        results
    }

    suspend fun loadTrackIntoDeck(deckIndex: Int, track: SongTrack) = withContext(Dispatchers.IO) {
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
        val modelsDir = ensureModelsDirectory()
        nativeLoadPcmToDeck(deckIndex, leftPcm, rightPcm, sampleRate, modelsDir)
    }

    private fun decodeAudioFileToFloatStereo(filePath: String): Triple<FloatArray, FloatArray, Int> {
        val extractor = MediaExtractor()
        extractor.setDataSource(filePath)
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

        val maxSamples = sampleRate * 360
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

    external fun nativeInitEngine()
    external fun nativeLoadPcmToDeck(deckIndex: Int, leftPcm: FloatArray, rightPcm: FloatArray, sampleRate: Int, modelsDir: String)
    external fun nativeSetDeckPlayState(deck: Int, playing: Boolean)
    external fun nativeTriggerButterMorph(fromDeck: Int, toDeck: Int, seconds: Double)
    external fun nativeTriggerHotCue(deck: Int, cueIndex: Int)
    external fun nativeToggleBeatLoop(deck: Int, beats: Double)
    external fun nativeSetChannelParam(deck: Int, paramId: Int, value: Float)
    external fun nativeSetMasterParam(paramId: Int, value: Float)
    external fun nativeGetChannelVu(deck: Int): Float
    external fun nativeGetDetectedBpm(deck: Int): Float
}
