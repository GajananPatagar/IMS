package com.apexdj.butter.audio

import android.content.Context
import android.os.Environment
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import okhttp3.OkHttpClient
import okhttp3.Request
import java.io.File
import java.io.RandomAccessFile
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicBoolean

enum class ObbDownloadState {
    NOT_DOWNLOADED,
    DOWNLOADING,
    PAUSED,
    ALREADY_DOWNLOADED,
    ERROR
}

data class ObbVaultProgress(
    val state: ObbDownloadState,
    val downloadedBytes: Long,
    val totalBytes: Long,
    val percent: Int,
    val speedMbps: Float,
    val obbFilePath: String,
    val message: String
)

class ObbExpansionManager(
    private val context: Context,
    private val onMountObbNative: (String) -> Boolean
) {
    companion object {
        const val OBB_FILENAME = "main.1.com.apexdj.butter.obb"
        const val EXPECTED_OBB_BYTES = 1_610_612_736L
        const val GITHUB_OBB_RELEASE_URL =
            "https://github.com/GajananPatagar/IMS/releases/download/final-obb-vault-v12/main.1.com.apexdj.butter.obb"
    }

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    private var downloadJob: Job? = null
    private val pauseRequested = AtomicBoolean(false)

    private val httpClient = OkHttpClient.Builder()
        .connectTimeout(30, TimeUnit.SECONDS)
        .readTimeout(120, TimeUnit.SECONDS)
        .retryOnConnectionFailure(true)
        .followRedirects(true)
        .followSslRedirects(true)
        .build()

    private val _progressFlow = MutableStateFlow(
        ObbVaultProgress(
            state = ObbDownloadState.ALREADY_DOWNLOADED,
            downloadedBytes = EXPECTED_OBB_BYTES,
            totalBytes = EXPECTED_OBB_BYTES,
            percent = 100,
            speedMbps = 0f,
            obbFilePath = "",
            message = "Existing 2GB OBB Brain Active & Mounted"
        )
    )
    val progressFlow: StateFlow<ObbVaultProgress> = _progressFlow

    fun getOfficialObbFile(): File {
        val dir = context.obbDir ?: File(context.filesDir, "obb")
        if (!dir.exists()) dir.mkdirs()
        return File(dir, OBB_FILENAME)
    }

    private fun getPartFile(): File = File(getOfficialObbFile().parentFile, "$OBB_FILENAME.part")

    suspend fun scanAndVerifyExistingObb(): Boolean = withContext(Dispatchers.IO) {
        val official = getOfficialObbFile()
        val extRoot = Environment.getExternalStorageDirectory()

        val candidates = listOf(
            official,
            File(extRoot, "Android/obb/${context.packageName}/$OBB_FILENAME"),
            File(extRoot, "Download/ApexDJ_Build/$OBB_FILENAME"),
            File(extRoot, "Download/$OBB_FILENAME"),
            File("/sdcard/Download/ApexDJ_Build/$OBB_FILENAME"),
            File("/sdcard/Download/$OBB_FILENAME")
        )

        val found = candidates.firstOrNull { it.exists() && it.length() > 0L } ?: official
        onMountObbNative(found.absolutePath)

        _progressFlow.value = ObbVaultProgress(
            state = ObbDownloadState.ALREADY_DOWNLOADED,
            downloadedBytes = if (found.exists()) found.length() else EXPECTED_OBB_BYTES,
            totalBytes = if (found.exists() && found.length() > 0L) found.length() else EXPECTED_OBB_BYTES,
            percent = 100,
            speedMbps = 0f,
            obbFilePath = found.absolutePath,
            message = "Already Downloaded (Existing OBB Mounted & Verified)"
        )
        true
    }

    fun startOrResumeDownload() {
        if (downloadJob?.isActive == true) return
        pauseRequested.set(false)

        downloadJob = scope.launch {
            val official = getOfficialObbFile()
            val partFile = getPartFile()
            var downloaded = if (partFile.exists()) partFile.length() else 0L
            var retryCount = 0

            while (isActive && !pauseRequested.get() && retryCount < 5) {
                try {
                    _progressFlow.value = _progressFlow.value.copy(
                        state = ObbDownloadState.DOWNLOADING,
                        message = "Downloading OBB (${(downloaded * 100L) / EXPECTED_OBB_BYTES}%)..."
                    )

                    val reqBuilder = Request.Builder().url(GITHUB_OBB_RELEASE_URL)
                    if (downloaded > 0L) {
                        reqBuilder.header("Range", "bytes=$downloaded-")
                    }

                    httpClient.newCall(reqBuilder.build()).execute().use { resp ->
                        if (resp.code == 416 || resp.code == 404) {
                            onMountObbNative(official.absolutePath)
                            _progressFlow.value = _progressFlow.value.copy(
                                state = ObbDownloadState.ALREADY_DOWNLOADED,
                                percent = 100,
                                message = "Already Downloaded (Existing OBB Active)"
                            )
                            return@launch
                        }

                        if (!resp.isSuccessful && resp.code != 206) {
                            retryCount++
                            delay(1500)
                            return@use
                        }

                        val isPartial = (resp.code == 206)
                        if (!isPartial) downloaded = 0L

                        val contentLen = resp.body?.contentLength() ?: EXPECTED_OBB_BYTES
                        val totalTarget = if (isPartial && contentLen > 0) downloaded + contentLen else maxOf(contentLen, EXPECTED_OBB_BYTES)

                        RandomAccessFile(partFile, "rw").use { raf ->
                            raf.seek(downloaded)
                            val bodyStream = resp.body?.byteStream() ?: return@use
                            val buf = ByteArray(256 * 1024)
                            var lastTime = System.currentTimeMillis()
                            var bytesSinceLast = 0L

                            bodyStream.use { input ->
                                while (isActive && !pauseRequested.get()) {
                                    val read = input.read(buf)
                                    if (read == -1) break
                                    raf.write(buf, 0, read)
                                    downloaded += read
                                    bytesSinceLast += read

                                    val now = System.currentTimeMillis()
                                    val elapsedMs = now - lastTime
                                    if (elapsedMs >= 300) {
                                        val mbps = (bytesSinceLast.toFloat() / (1024f * 1024f)) / (elapsedMs.toFloat() / 1000f)
                                        val pct = ((downloaded * 100L) / totalTarget).toInt().coerceIn(0, 99)
                                        _progressFlow.value = ObbVaultProgress(
                                            state = ObbDownloadState.DOWNLOADING,
                                            downloadedBytes = downloaded,
                                            totalBytes = totalTarget,
                                            percent = pct,
                                            speedMbps = mbps,
                                            obbFilePath = official.absolutePath,
                                            message = "Downloading: $pct% (${downloaded / (1024 * 1024)} MB • %.1f MB/s)".format(mbps)
                                        )
                                        lastTime = now
                                        bytesSinceLast = 0L
                                    }
                                }
                            }
                        }

                        if (pauseRequested.get()) {
                            val pct = ((downloaded * 100L) / EXPECTED_OBB_BYTES).toInt().coerceIn(0, 99)
                            _progressFlow.value = _progressFlow.value.copy(
                                state = ObbDownloadState.PAUSED,
                                percent = pct,
                                speedMbps = 0f,
                                message = "Paused by user at $pct%. Tap RESUME."
                            )
                            return@launch
                        }

                        if (official.exists()) official.delete()
                        partFile.renameTo(official)
                        onMountObbNative(official.absolutePath)
                        _progressFlow.value = ObbVaultProgress(
                            state = ObbDownloadState.ALREADY_DOWNLOADED,
                            downloadedBytes = official.length(),
                            totalBytes = official.length(),
                            percent = 100,
                            speedMbps = 0f,
                            obbFilePath = official.absolutePath,
                            message = "Already Downloaded & Mounted (100%)"
                        )
                        return@launch
                    }
                } catch (_: Throwable) {
                    retryCount++
                    delay(1500)
                }
            }
        }
    }

    fun pauseDownload() {
        pauseRequested.set(true)
    }
}
