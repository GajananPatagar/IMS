package com.apexdj.butter

import android.Manifest
import android.os.Build
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.core.app.ActivityCompat
import androidx.core.view.WindowCompat
import com.apexdj.butter.audio.OnlineAndLocalRepository
import com.apexdj.butter.ui.FlagshipMixerScreen

class MainActivity : ComponentActivity() {
    private lateinit var repository: OnlineAndLocalRepository

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        WindowCompat.setDecorFitsSystemWindows(window, false)

        val perms = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            arrayOf(Manifest.permission.READ_MEDIA_AUDIO, Manifest.permission.RECORD_AUDIO)
        } else {
            arrayOf(Manifest.permission.READ_EXTERNAL_STORAGE, Manifest.permission.WRITE_EXTERNAL_STORAGE, Manifest.permission.RECORD_AUDIO)
        }
        ActivityCompat.requestPermissions(this, perms, 101)

        repository = OnlineAndLocalRepository(applicationContext)
        setContent {
            FlagshipMixerScreen(repo = repository)
        }
    }
}
