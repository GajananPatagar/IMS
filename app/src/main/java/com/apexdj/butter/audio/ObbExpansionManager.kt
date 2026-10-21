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
        const val MIN_VALID_OBB_BYTES = 1_400_000_000L // 1.40 GB strict minimum
        const val EXPECTED_OBB_BYTES  = 1_610_612_736L // 1.50 GB (1,536 MiB)
        const val GITHUB_OBB_RELEASE_URL =
            "https://github.com/GajananPatagar/IMS/releases/download/final-obb-vault-v12/main.1.com.apexdj.butter.obb"
    }

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    private var downloadJob: Job? = null
    private val pauseRequested = AtomicBoolean(false)

    private val httpClient = OkHttpClient.Builder()
        .connectTimeout(15, TimeUnit.SECONDS)
        .readTimeout(60, TimeUnit.SECONDS)
        .followRedirects(true)
        .followSslRedirects(true)
        .build()

    private val _progressFlow = MutableStateFlow(
        ObbVaultProgress(
            state = ObbDownloadState.NOT_DOWNLOADED,
            downloadedBytes = 0L,
            totalBytes = EXPECTED_OBB_BYTES,
            percent = 0,
            speedMbps = 0f,
            obbFilePath = "",
            message = "Checking 1.50 GB Neural Brain OBB..."
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

        val validFound = candidates.firstOrNull { it.exists() && it.canRead() && it.length() >= MIN_VALID_OBB_BYTES }
        if (validFound != null) {
            if (validFound.absolutePath != official.absolutePath) {
                runCatching {
                    // Instant rename if on same mount, otherwise streamed buffered copy
                    if (!validFound.renameTo(official)) {
                        validFound.inputStream().use { input ->
                            official.outputStream().use { output ->
                                input.copyTo(output, 512 * 1024)
                            }
                        }
                    }
                }
            }
            val target = if (official.exists() && official.length() >= MIN_VALID_OBB_BYTES) official else validFound
            val mounted = onMountObbNative(target.absolutePath)
            if (mounted) {
                _progressFlow.value = ObbVaultProgress(
                    state = ObbDownloadState.ALREADY_DOWNLOADED,
                    downloadedBytes = target.length(),
                    totalBytes = target.length(),
                    percent = 100,
                    speedMbps = 0f,
                    obbFilePath = target.absolutePath,
                    message = "Already Downloaded (1.50 GB Neural Brain OBB Verified & Mounted)"
                )
                return@withContext true
            }
        }

        val part = getPartFile()
        val existingPartBytes = if (part.exists()) part.length() else 0L
        val pct = ((existingPartBytes * 100L) / EXPECTED_OBB_BYTES).toInt().coerceIn(0, 99)
        _progressFlow.value = ObbVaultProgress(
            state = if (existingPartBytes > 0L) ObbDownloadState.PAUSED else ObbDownloadState.NOT_DOWNLOADED,
            downloadedBytes = existingPartBytes,
            totalBytes = EXPECTED_OBB_BYTES,
            percent = pct,
            speedMbps = 0f,
            obbFilePath = official.absolutePath,
            message = if (existingPartBytes > 0L) {
                "Download Paused at $pct% (${existingPartBytes / (1024 * 1024)} MB / 1536 MB). Tap RESUME."
            } else {
                "1.50 GB Neural Brain OBB Required. App is locked until OBB is downloaded."
            }
        )
        false
    }

    fun startOrResumeDownload() {
        if (_progressFlow.value.state == ObbDownloadState.ALREADY_DOWNLOADED) return
        if (downloadJob?.isActive == true) return
        pauseRequested.set(false)

        downloadJob = scope.launch {
            val official = getOfficialObbFile()
            if (official.exists() && official.length() >= MIN_VALID_OBB_BYTES) {
                scanAndVerifyExistingObb()
                return@launch
            }

            val partFile = getPartFile()
            var downloaded = if (partFile.exists()) partFile.length() else 0L

            try {
                _progressFlow.value = _progressFlow.value.copy(
                    state = ObbDownloadState.DOWNLOADING,
                    message = "Connecting to 1.50 GB OBB Vault..."
                )

                val reqBuilder = Request.Builder().url(GITHUB_OBB_RELEASE_URL)
                if (downloaded > 0L) {
                    reqBuilder.header("Range", "bytes=$downloaded-")
                }

                httpClient.newCall(reqBuilder.build()).execute().use { resp ->
                    if (resp.code == 416) {
                        // Range already satisfied -> file is complete!
                        finalizePartFileInstant(partFile, official)
                        return@use
                    }
                    if (!resp.isSuccessful && resp.code != 206) {
                        error("Server returned HTTP ${resp.code}")
                    }

                    val isPartial = (resp.code == 206)
                    if (!isPartial) downloaded = 0L

                    val contentLen = resp.body?.contentLength() ?: EXPECTED_OBB_BYTES
                    val totalTarget = if (isPartial && contentLen > 0) downloaded + contentLen else maxOf(contentLen, EXPECTED_OBB_BYTES)

                    RandomAccessFile(partFile, "rw").use { raf ->
                        raf.seek(downloaded)
                        val bodyStream = resp.body?.byteStream() ?: error("Empty stream")
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
                                if (elapsedMs >= 250) {
                                    val mbps = (bytesSinceLast.toFloat() / (1024f * 1024f)) / (elapsedMs.toFloat() / 1000f)
                                    val pct = ((downloaded * 100L) / totalTarget).toInt().coerceIn(0, 99)
                                    _progressFlow.value = ObbVaultProgress(
                                        state = ObbDownloadState.DOWNLOADING,
                                        downloadedBytes = downloaded,
                                        totalBytes = totalTarget,
                                        percent = pct,
                                        speedMbps = mbps,
                                        obbFilePath = official.absolutePath,
                                        message = "Downloading 1.50 GB OBB: $pct% (${downloaded / (1024 * 1024)} / ${totalTarget / (1024 * 1024)} MB • %.1f MB/s)".format(mbps)
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
                            downloadedBytes = downloaded,
                            percent = pct,
                            speedMbps = 0f,
                            message = "Paused at $pct% (${downloaded / (1024 * 1024)} MB). Tap RESUME to continue."
                        )
                        return@launch
                    }

                    finalizePartFileInstant(partFile, official)
                }
            } catch (e: Throwable) {
                val pct = ((downloaded * 100L) / EXPECTED_OBB_BYTES).toInt().coerceIn(0, 99)
                _progressFlow.value = _progressFlow.value.copy(
                    state = ObbDownloadState.PAUSED,
                    downloadedBytes = downloaded,
                    percent = pct,
                    speedMbps = 0f,
                    message = "Network interrupted at $pct% (${e.localizedMessage ?: "Retry"}). Tap RESUME."
                )
            }
        }
    }

    fun pauseDownload() {
        pauseRequested.set(true)
    }

    // Fixes the 99% freeze bug by performing an instant 0ms filesystem renameTo() in the same directory!
    private fun finalizePartFileInstant(partFile: File, official: File) {
        if (official.exists()) official.delete()
        val renamed = partFile.renameTo(official)
        val finalFile = if (renamed) official else partFile
        val mounted = onMountObbNative(finalFile.absolutePath)
        if (mounted) {
            _progressFlow.value = ObbVaultProgress(
                state = ObbDownloadState.ALREADY_DOWNLOADED,
                downloadedBytes = finalFile.length(),
                totalBytes = finalFile.length(),
                percent = 100,
                speedMbps = 0f,
                obbFilePath = finalFile.absolutePath,
                message = "Already Downloaded (1.50 GB Neural Brain OBB Verified & Active)"
            )
        } else {
            _progressFlow.value = _progressFlow.value.copy(
                state = ObbDownloadState.ERROR,
                message = "OBB downloaded (${finalFile.length() / (1024 * 1024)} MB) but failed 1.40 GB verification."
            )
        }
    }
}
