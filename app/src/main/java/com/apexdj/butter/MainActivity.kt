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
