package com.apexdj.butter.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlin.math.*

@Composable
fun RotaryKnob3D(
    label: String,
    value: Float, // -1.0 to 1.0 or 0.0 to 1.0
    isBipolar: Boolean = true,
    accentColor: Color = Color(0xFF00E5FF),
    modifier: Modifier = Modifier,
    onChange: (Float) -> Unit
) {
    var knobVal by remember { mutableFloatStateOf(value) }
    
    Column(modifier = modifier.padding(2.dp), horizontalAlignment = Alignment.CenterHorizontally) {
        Text(label, color = Color(0xFF8B949E), fontSize = 7.sp, fontFamily = FontFamily.Monospace, fontWeight = FontWeight.Bold)
        Spacer(modifier = Modifier.height(2.dp))
        Box(
            modifier = Modifier
                .size(38.dp)
                .background(Color(0xFF0C0F17), CircleShape)
                .border(1.5.dp, accentColor.copy(alpha = 0.7f), CircleShape)
                .pointerInput(Unit) {
                    detectDragGestures { _, dragAmount ->
                        val delta = -dragAmount.y * 0.012f
                        knobVal = if (isBipolar) {
                            (knobVal + delta).coerceIn(-1.0f, 1.0f)
                        } else {
                            (knobVal + delta).coerceIn(0.0f, 1.0f)
                        }
                        onChange(knobVal)
                    }
                },
            contentAlignment = Alignment.Center
        ) {
            Canvas(modifier = Modifier.size(32.dp)) {
                val radius = size.minDimension / 2.2f
                val center = Offset(size.width / 2f, size.height / 2f)
                val angleRange = 240f
                val startAngle = 150f
                val norm = if (isBipolar) (knobVal + 1f) / 2f else knobVal
                val currentAngle = startAngle + norm * angleRange

                drawArc(
                    color = accentColor.copy(alpha = 0.3f),
                    startAngle = startAngle,
                    sweepAngle = angleRange,
                    useCenter = false,
                    style = Stroke(width = 3f)
                )
                drawArc(
                    color = accentColor,
                    startAngle = startAngle,
                    sweepAngle = norm * angleRange,
                    useCenter = false,
                    style = Stroke(width = 3f)
                )

                val rad = Math.toRadians(currentAngle.toDouble())
                val indicatorX = center.x + (radius * 0.7f) * cos(rad).toFloat()
                val indicatorY = center.y + (radius * 0.7f) * sin(rad).toFloat()
                drawCircle(color = Color.White, radius = 3.5f, center = Offset(indicatorX, indicatorY))
            }
        }
    }
}

@Composable
fun RgbScrollingWaveformView(
    rgbFlatArray: FloatArray,
    playheadNormalized: Float,
    accentColor: Color,
    modifier: Modifier = Modifier
) {
    Box(
        modifier = modifier
            .background(Color(0xFF06080D), RoundedCornerShape(4.dp))
            .border(1.dp, accentColor.copy(alpha = 0.4f), RoundedCornerShape(4.dp))
    ) {
        Canvas(modifier = Modifier.fillMaxSize()) {
            val w = size.width
            val h = size.height
            val midY = h / 2f
            val bins = rgbFlatArray.size / 3
            if (bins <= 0) return@Canvas

            val binWidth = w / bins.toFloat()

            for (i in 0 until bins) {
                val low = rgbFlatArray[i * 3].coerceIn(0f, 1f)
                val mid = rgbFlatArray[i * 3 + 1].coerceIn(0f, 1f)
                val high = rgbFlatArray[i * 3 + 2].coerceIn(0f, 1f)

                val x = i * binWidth
                // Low = Red, Mid = Green, High = Blue (Standard CDJ-3000 RGB Waveform Spectrum)
                drawLine(Color(0xFFFF5252), Offset(x, midY - low * (h * 0.45f)), Offset(x, midY + low * (h * 0.45f)), strokeWidth = binWidth.coerceAtLeast(1f))
                drawLine(Color(0xFF69F0AE), Offset(x, midY - mid * (h * 0.30f)), Offset(x, midY + mid * (h * 0.30f)), strokeWidth = binWidth.coerceAtLeast(1f))
                drawLine(Color(0xFF40C4FF), Offset(x, midY - high * (h * 0.15f)), Offset(x, midY + high * (h * 0.15f)), strokeWidth = binWidth.coerceAtLeast(1f))
            }

            // Playhead Cursor
            val playX = (playheadNormalized.coerceIn(0f, 1f)) * w
            drawLine(Color.White, Offset(playX, 0f), Offset(playX, h), strokeWidth = 2.5f)
        }
    }
}

@Composable
fun MasterSpectrumAnalyzer(spectrumArray: Array<Float>, modifier: Modifier = Modifier) {
    Box(
        modifier = modifier
            .background(Color(0xFF04060A), RoundedCornerShape(4.dp))
            .border(1.dp, Color(0xFF21262D), RoundedCornerShape(4.dp))
            .padding(4.dp)
    ) {
        Canvas(modifier = Modifier.fillMaxSize()) {
            val w = size.width
            val h = size.height
            val count = spectrumArray.size
            val barW = w / count.toFloat()

            for (i in 0 until count) {
                val amp = spectrumArray[i].coerceIn(0f, 1f)
                val barH = amp * h
                drawRect(
                    color = if (i < count / 3) Color(0xFF00E5FF) else if (i < 2 * count / 3) Color(0xFF57AB5A) else Color(0xFFF85149),
                    topLeft = Offset(i * barW + 1f, h - barH),
                    size = Size(barW - 1.5f, barH)
                )
            }
        }
    }
}
