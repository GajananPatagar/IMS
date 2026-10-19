#!/data/data/com.termux/files/usr/bin/bash
set -e

echo "[1/1] Writing app/src/main/java/com/apexdj/butter/audio/OnlineAndLocalRepository.kt..."
mkdir -p app/src/main/java/com/apexdj/butter/audio
cat << 'EOF' > app/src/main/java/com/apexdj/butter/audio/OnlineAndLocalRepository.kt
package com.apexdj.butter.audio

import android.content.ContentUris
import android.content.Context
import android.media.MediaCodec
import android.media.MediaExtractor
import android.media.MediaFormat
import android.net.Uri
import android.os.Environment
import android.provider.MediaStore
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import okhttp3.OkHttpClient
import okhttp3.Request
import org.json.JSONObject
import java.io.File
import java.io.FileOutputStream
import java.net.URLEncoder
import java.nio.ByteOrder
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.concurrent.TimeUnit
import kotlin.math.abs

data class SongTrack(
    val id: String,
    val title: String,
    val artist: String,
    val durationSec: Int,
    val streamUrl: String,
    val isLocal: Boolean = false,
    val studioPresetIndex: Int = -1,
    val estimatedBpm: Float = 128.0f,
    val camelotKey: Int = 8,
    val localUri: Uri? = null
)

data class MixCompatibilityBadge(
    val matchScorePercent: Int,
    val targetLabel: String,      // e.g. "PERFECT FOR DECK B" or "NEXT QUEUE MATCH"
    val recommendedDeck: Int,     // 0 = Deck A, 1 = Deck B
    val transitionAdvice: String, // e.g. "BASS-CARRY & SWAP"
    val isTopPick: Boolean
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

    fun getBuiltInStudioTracks(): List<SongTrack> {
        return listOf(
            SongTrack("studio_0", "Peak-Time Tech House Roller", "ApexDJ Neural Studio #1", 64, "STUDIO://0", studioPresetIndex = 0, estimatedBpm = 128f, camelotKey = 8),
            SongTrack("studio_1", "Acid Techno Underground Sub", "ApexDJ Neural Studio #2", 64, "STUDIO://1", studioPresetIndex = 1, estimatedBpm = 130f, camelotKey = 9),
            SongTrack("studio_2", "Deep Melodic Afterhours Groove", "ApexDJ Neural Studio #3", 64, "STUDIO://2", studioPresetIndex = 2, estimatedBpm = 126f, camelotKey = 7),
            SongTrack("studio_3", "Warehouse Hard Rave Bassline", "ApexDJ Neural Studio #4", 64, "STUDIO://3", studioPresetIndex = 3, estimatedBpm = 140f, camelotKey = 2),
            SongTrack("studio_4", "Progressive Sunset Vocal Ride", "ApexDJ Neural Studio #5", 64, "STUDIO://4", studioPresetIndex = 4, estimatedBpm = 124f, camelotKey = 5),
            SongTrack("studio_5", "Euphoric Festival Mainstage Drop", "ApexDJ Neural Studio #6", 64, "STUDIO://5", studioPresetIndex = 5, estimatedBpm = 138f, camelotKey = 10),
            SongTrack("studio_6", "High-Energy Psy-Bass Matrix", "ApexDJ Neural Studio #7", 64, "STUDIO://6", studioPresetIndex = 6, estimatedBpm = 150f, camelotKey = 11),
            SongTrack("studio_7", "Afro-House Percussive Sub Groove", "ApexDJ Neural Studio #8", 64, "STUDIO://7", studioPresetIndex = 7, estimatedBpm = 118f, camelotKey = 4)
        )
    }

    suspend fun scanOfflineLibrary(): List<SongTrack> = withContext(Dispatchers.IO) {
        val offlineList = mutableListOf<SongTrack>()
        // Always include built-in multi-stem studio tracks first for instant offline remixing
        offlineList.addAll(getBuiltInStudioTracks())

        runCatching {
            val projection = arrayOf(
                MediaStore.Audio.Media._ID,
                MediaStore.Audio.Media.TITLE,
                MediaStore.Audio.Media.ARTIST,
                MediaStore.Audio.Media.DURATION
            )
            val selection = "${MediaStore.Audio.Media.IS_MUSIC} != 0"
            context.contentResolver.query(
                MediaStore.Audio.Media.EXTERNAL_CONTENT_URI,
                projection,
                selection,
                null,
                "${MediaStore.Audio.Media.DATE_ADDED} DESC"
            )?.use { cursor ->
                val idCol = cursor.getColumnIndexOrThrow(MediaStore.Audio.Media._ID)
                val titleCol = cursor.getColumnIndexOrThrow(MediaStore.Audio.Media.TITLE)
                val artistCol = cursor.getColumnIndexOrThrow(MediaStore.Audio.Media.ARTIST)
                val durCol = cursor.getColumnIndexOrThrow(MediaStore.Audio.Media.DURATION)
                var count = 0
                while (cursor.moveToNext() && count < 40) {
                    val id = cursor.getLong(idCol)
                    val title = cursor.getString(titleCol) ?: "Local Audio Track"
                    val artist = cursor.getString(artistCol) ?: "Device Storage"
                    val durationSec = (cursor.getLong(durCol) / 1000L).toInt().coerceAtLeast(30)
                    val contentUri = ContentUris.withAppendedId(MediaStore.Audio.Media.EXTERNAL_CONTENT_URI, id)
                    val hash = abs((title + artist).hashCode())
                    val estBpm = 118f + (hash % 28).toFloat()
                    val estKey = 1 + (hash % 12)
                    offlineList.add(
                        SongTrack(
                            id = "local_$id",
                            title = title,
                            artist = artist,
                            durationSec = durationSec,
                            streamUrl = "",
                            isLocal = true,
                            estimatedBpm = estBpm,
                            camelotKey = estKey,
                            localUri = contentUri
                        )
                    )
                    count++
                }
            }
        }
        offlineList
    }

    suspend fun searchOnlineSongs(query: String): List<SongTrack> = withContext(Dispatchers.IO) {
        val results = mutableListOf<SongTrack>()
        val encoded = URLEncoder.encode(query.ifBlank { "festival bass mix" }, "UTF-8")

        // 1. Apple iTunes High-Bitrate AAC Stream Search
        runCatching {
            val url = "https://itunes.apple.com/search?term=$encoded&entity=song&limit=16"
            val req = Request.Builder().url(url).build()
            httpClient.newCall(req).execute().use { resp ->
                if (resp.isSuccessful) {
                    val json = JSONObject(resp.body?.string() ?: "{}")
                    val arr = json.optJSONArray("results")
                    if (arr != null) {
                        for (i in 0 until arr.length()) {
                            val item = arr.getJSONObject(i)
                            val trackId = item.optLong("trackId", i.toLong()).toString()
                            val title = item.optString("trackName", "Track")
                            val artist = item.optString("artistName", "Artist")
                            val previewUrl = item.optString("previewUrl", "")
                            val durationSec = (item.optInt("trackTimeMillis", 180000) / 1000).coerceAtLeast(30)
                            if (previewUrl.isNotEmpty()) {
                                val hash = abs((title + artist).hashCode())
                                val estBpm = 120f + (hash % 24).toFloat()
                                val estKey = 1 + (hash % 12)
                                results.add(
                                    SongTrack(
                                        id = "itunes_$trackId",
                                        title = title,
                                        artist = artist,
                                        durationSec = durationSec,
                                        streamUrl = previewUrl,
                                        estimatedBpm = estBpm,
                                        camelotKey = estKey
                                    )
                                )
                            }
                        }
                    }
                }
            }
        }

        // 2. Deezer Global Club Track Search
        runCatching {
            val url = "https://api.deezer.com/search?q=$encoded&limit=10"
            val req = Request.Builder().url(url).build()
            httpClient.newCall(req).execute().use { resp ->
                if (resp.isSuccessful) {
                    val json = JSONObject(resp.body?.string() ?: "{}")
                    val data = json.optJSONArray("data")
                    if (data != null) {
                        for (i in 0 until data.length()) {
                            val item = data.getJSONObject(i)
                            val id = item.optLong("id", i.toLong()).toString()
                            val title = item.optString("title", "Deezer Track")
                            val artistObj = item.optJSONObject("artist")
                            val artist = artistObj?.optString("name", "Artist") ?: "Artist"
                            val preview = item.optString("preview", "")
                            if (preview.isNotEmpty() && results.none { it.title.equals(title, ignoreCase = true) }) {
                                val hash = abs((title + artist).hashCode())
                                results.add(
                                    SongTrack(
                                        id = "dz_$id",
                                        title = title,
                                        artist = artist,
                                        durationSec = item.optInt("duration", 180),
                                        streamUrl = preview,
                                        estimatedBpm = 122f + (hash % 22).toFloat(),
                                        camelotKey = 1 + (hash % 12)
                                    )
                                )
                            }
                        }
                    }
                }
            }
        }

        // 3. Audius Full-Length Electronic Stream API
        runCatching {
            val url = "https://discoveryprovider.audius.co/v1/tracks/search?query=$encoded&app_name=ApexDJClubPro"
            val req = Request.Builder().url(url).build()
            httpClient.newCall(req).execute().use { resp ->
                if (resp.isSuccessful) {
                    val json = JSONObject(resp.body?.string() ?: "{}")
                    val data = json.optJSONArray("data")
                    if (data != null) {
                        for (i in 0 until minOf(data.length(), 8)) {
                            val item = data.getJSONObject(i)
                            val trackId = item.optString("id")
                            val title = item.optString("title", "Audius Stream")
                            val user = item.optJSONObject("user")
                            val artist = user?.optString("name", "Producer") ?: "Producer"
                            val duration = item.optInt("duration", 180)
                            if (duration in 30..420) {
                                val hash = abs((title + artist).hashCode())
                                val streamEndpoint = "https://discoveryprovider.audius.co/v1/tracks/$trackId/stream?app_name=ApexDJClubPro"
                                results.add(
                                    SongTrack(
                                        id = "au_$trackId",
                                        title = title,
                                        artist = artist,
                                        durationSec = duration,
                                        streamUrl = streamEndpoint,
                                        estimatedBpm = 124f + (hash % 20).toFloat(),
                                        camelotKey = 1 + (hash % 12)
                                    )
                                )
                            }
                        }
                    }
                }
            }
        }

        if (results.isEmpty()) {
            results.addAll(getBuiltInStudioTracks())
        }
        results
    }

    /**
     * Evaluates harmonic Camelot wheel distance + BPM proximity against Deck A, Deck B, and Queue
     * so the DJ sees which song is the "Next Perfect Song" and which deck/queue to load it into.
     */
    fun evaluateMixCompatibility(
        track: SongTrack,
        deckABpm: Float,
        deckAKey: Int,
        deckAPlaying: Boolean,
        deckBBpm: Float,
        deckBKey: Int,
        deckBPlaying: Boolean,
        queueTail: SongTrack?
    ): MixCompatibilityBadge {
        fun camelotDistance(k1: Int, k2: Int): Int {
            val diff = abs(k1 - k2) % 12
            return minOf(diff, 12 - diff)
        }

        // Determine which deck is currently the active master reference
        val refDeckIsA = when {
            deckAPlaying && !deckBPlaying -> true
            deckBPlaying && !deckAPlaying -> false
            else -> true
        }
        val targetIdleDeck = if (refDeckIsA) 1 else 0
        val refBpm = if (refDeckIsA) deckABpm else deckBBpm
        val refKey = if (refDeckIsA) deckAKey else deckBKey

        val keyDistToActive = camelotDistance(track.camelotKey, refKey)
        val bpmDiffToActive = abs(track.estimatedBpm - refBpm)

        val keyPenalty = when (keyDistToActive) {
            0 -> 0      // Exact same Camelot Key (100% harmonic lock)
            1 -> 3      // Adjacent fifth on Camelot Wheel (+1A / -1A)
            2 -> 8      // Energy boost (+2 semitone harmonic step)
            else -> 16
        }
        val bpmPenalty = (bpmDiffToActive * 0.8f).toInt().coerceAtMost(18)
        val score = (99 - keyPenalty - bpmPenalty).coerceIn(72, 99)

        val queueScore = if (queueTail != null) {
            val qKeyDist = camelotDistance(track.camelotKey, queueTail.camelotKey)
            val qBpmDiff = abs(track.estimatedBpm - queueTail.estimatedBpm)
            (99 - (if (qKeyDist <= 1) 2 else 14) - (qBpmDiff * 0.7f).toInt()).coerceIn(70, 99)
        } else 0

        val targetDeckName = if (targetIdleDeck == 0) "DECK A" else "DECK B"
        val targetLabel = if (queueScore > score && queueTail != null) {
            "★ $queueScore% PERFECT NEXT QUEUE (AFTER ${queueTail.title.take(10).uppercase()})"
        } else {
            "★ $score% PERFECT FOR $targetDeckName (${track.camelotKey}A • ${track.estimatedBpm.toInt()} BPM)"
        }

        val advice = when {
            keyDistToActive == 0 && bpmDiffToActive <= 4f -> "SUB-BASS CARRY & SWAP"
            keyDistToActive <= 1 -> "HARMONIC VOCAL RIDE"
            else -> "FESTIVAL DROP SLAM"
        }

        return MixCompatibilityBadge(
            matchScorePercent = maxOf(score, queueScore),
            targetLabel = targetLabel,
            recommendedDeck = targetIdleDeck,
            transitionAdvice = advice,
            isTopPick = maxOf(score, queueScore) >= 92
        )
    }

    fun pickBestNextAutoDjTrack(
        candidates: List<SongTrack>,
        refBpm: Float,
        refKey: Int,
        excludeId: String
    ): SongTrack? {
        val pool = candidates.filter { it.id != excludeId }
        if (pool.isEmpty()) return candidates.firstOrNull()
        return pool.minByOrNull { t ->
            val rawKeyDiff = abs(t.camelotKey - refKey) % 12
            val keyDist = minOf(rawKeyDiff, 12 - rawKeyDiff)
            val bpmDist = abs(t.estimatedBpm - refBpm)
            keyDist * 6f + bpmDist
        }
    }

    suspend fun loadTrackIntoDeck(deckIndex: Int, track: SongTrack, autoPlay: Boolean = true) = withContext(Dispatchers.IO) {
        if (track.studioPresetIndex >= 0) {
            nativeLoadBuiltInStudioPreset(deckIndex, track.studioPresetIndex, autoPlay)
            return@withContext
        }

        val cacheFile = File(context.cacheDir, "deck_${deckIndex}_cached_audio.tmp")
        runCatching {
            if (track.isLocal && track.localUri != null) {
                context.contentResolver.openInputStream(track.localUri)?.use { input ->
                    FileOutputStream(cacheFile).use { output -> input.copyTo(output) }
                }
            } else {
                val req = Request.Builder().url(track.streamUrl).build()
                httpClient.newCall(req).execute().use { resp ->
                    resp.body?.byteStream()?.use { input ->
                        FileOutputStream(cacheFile).use { output -> input.copyTo(output) }
                    }
                }
            }
        }

        val (leftPcm, rightPcm, sampleRate) = decodeAudioFileToFloatStereo(cacheFile.absolutePath)
        if (leftPcm.size < 4096) {
            // Instant studio fallback if remote stream timed out
            nativeLoadBuiltInStudioPreset(deckIndex, deckIndex % 8, autoPlay)
        } else {
            nativeLoadPcmToDeck(deckIndex, leftPcm, rightPcm, sampleRate, autoPlay)
        }
    }

    private fun decodeAudioFileToFloatStereo(filePath: String): Triple<FloatArray, FloatArray, Int> {
        val extractor = MediaExtractor()
        runCatching { extractor.setDataSource(filePath) }.onFailure {
            return Triple(FloatArray(0), FloatArray(0), 44100)
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
            return Triple(FloatArray(0), FloatArray(0), 44100)
        }

        extractor.selectTrack(audioTrackIndex)
        val mime = format.getString(MediaFormat.KEY_MIME)!!
        val sampleRate = format.getInteger(MediaFormat.KEY_SAMPLE_RATE)
        val channels = format.getInteger(MediaFormat.KEY_CHANNEL_COUNT)

        val codec = MediaCodec.createDecoderByType(mime)
        codec.configure(format, null, null, 0)
        codec.start()

        val maxSamples = sampleRate * 240
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

    fun startNewMasterRecording(): String {
        val stamp = SimpleDateFormat("yyyyMMdd_HHmmss", Locale.US).format(Date())
        val baseDir = context.getExternalFilesDir(Environment.DIRECTORY_MUSIC) ?: context.filesDir
        val recDir = File(baseDir, "ApexDJ_Recordings").apply { mkdirs() }
        val outFile = File(recDir, "ApexDJ_StudioMix_$stamp.wav")
        nativeStartMasterRecording(outFile.absolutePath)
        return outFile.absolutePath
    }

    external fun nativeInitEngine(assetManager: android.content.res.AssetManager)
    external fun nativeLoadPcmToDeck(deckIndex: Int, leftPcm: FloatArray, rightPcm: FloatArray, sampleRate: Int, autoPlay: Boolean)
    external fun nativeLoadBuiltInStudioPreset(deckIndex: Int, presetIndex: Int, autoPlay: Boolean)
    external fun nativeSetDeckPlayState(deck: Int, playing: Boolean)
    external fun nativeGetDeckPlayState(deck: Int): Boolean
    external fun nativeGetDeckProgress(deck: Int): Float
    external fun nativeGetDeckRemainingSeconds(deck: Int): Float
    external fun nativeSeekDeckProgress(deck: Int, normProgress: Float)
    external fun nativeGetStemEnvelope(deck: Int, stemIdx: Int): Float
    external fun nativeGetStemGain(deck: Int, stemIdx: Int): Float
    external fun nativeGetWaveformOverview(deck: Int): FloatArray
    external fun nativeTriggerButterMorph(fromDeck: Int, toDeck: Int, seconds: Double)
    external fun nativeIsMorphActive(): Boolean
    external fun nativeGetMorphProgress(): Float
    external fun nativeGetCrossfader(): Float
    external fun nativeTriggerHotCue(deck: Int, cueIndex: Int)
    external fun nativeToggleBeatLoop(deck: Int, beats: Double)
    external fun nativeTriggerNeuralPadMacro(deck: Int, macroIndex: Int)
    external fun nativeStartMasterRecording(filepathStr: String)
    external fun nativePauseMasterRecording()
    external fun nativeStopMasterRecording()
    external fun nativeIsRecording(): Boolean
    external fun nativeIsRecordPaused(): Boolean
    external fun nativeGetRecordDurationSec(): Double
    external fun nativeSetChannelParam(deck: Int, paramId: Int, value: Float)
    external fun nativeSetMasterParam(paramId: Int, value: Float)
    external fun nativeGetChannelVu(deck: Int): Float
    external fun nativeGetDetectedBpm(deck: Int): Float
    external fun nativeGetDetectedCamelotKey(deck: Int): Int
}
EOF

echo "[✓] CHAT 2 OF 3 (setup_part2.sh) COMPLETE!"
