package com.apexdj.butter

import android.Manifest
import android.os.Build
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.core.view.WindowCompat
import androidx.lifecycle.lifecycleScope
import com.apexdj.butter.audio.OnlineAndLocalRepository
import com.apexdj.butter.ui.FlagshipMixerScreen
import kotlinx.coroutines.launch

class MainActivity : ComponentActivity() {
    private lateinit var repository: OnlineAndLocalRepository

    private val permissionLauncher = registerForActivityResult(
        ActivityResultContracts.RequestMultiplePermissions()
    ) {
        lifecycleScope.launch {
            repository.obbManager.scanAndVerifyExistingObb()
            repository.refreshInAppLocalSongs()
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        WindowCompat.setDecorFitsSystemWindows(window, true)

        repository = OnlineAndLocalRepository(applicationContext)

        val perms = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            arrayOf(Manifest.permission.READ_MEDIA_AUDIO)
        } else {
            arrayOf(Manifest.permission.READ_EXTERNAL_STORAGE, Manifest.permission.WRITE_EXTERNAL_STORAGE)
        }
        permissionLauncher.launch(perms)

        setContent {
            FlagshipMixerScreen(repo = repository)
        }
    }
}
