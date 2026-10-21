package com.apexdj.butter.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.gestures.detectTapGestures
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
    value: Float,
    isBipolar: Boolean = true,
    accentColor: Color = Color(0xFF00E5FF),
    modifier: Modifier = Modifier,
    onChange: (Float) -> Unit
) {
    var knobVal by remember(value) { mutableFloatStateOf(value) }

    Column(modifier = modifier.padding(1.dp), horizontalAlignment = Alignment.CenterHorizontally) {
        Text(
            text = label,
            color = Color(0xFF9EABB8),
            fontSize = 7.sp,
            fontFamily = FontFamily.Monospace,
            fontWeight = FontWeight.Bold,
            maxLines = 1
        )
        Spacer(modifier = Modifier.height(2.dp))
        Box(
            modifier = Modifier
                .size(36.dp)
                .background(Color(0xFF0B0E16), CircleShape)
                .border(1.5.dp, accentColor.copy(alpha = 0.65f), CircleShape)
                .pointerInput(isBipolar) {
                    detectTapGestures(
                        onDoubleTap = {
                            val reset = if (isBipolar) 0f else 0.5f
                            knobVal = reset
                            onChange(reset)
                        }
                    )
                }
                .pointerInput(isBipolar) {
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
            Canvas(modifier = Modifier.size(30.dp)) {
                val radius = size.minDimension / 2.2f
                val center = Offset(size.width / 2f, size.height / 2f)
                val angleRange = 240f
                val startAngle = 150f
                val norm = if (isBipolar) ((knobVal + 1f) * 0.5f).coerceIn(0f, 1f) else knobVal.coerceIn(0f, 1f)
                val currentAngle = startAngle + norm * angleRange

                drawArc(
                    color = accentColor.copy(alpha = 0.22f),
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
                val indicatorX = center.x + (radius * 0.72f) * cos(rad).toFloat()
                val indicatorY = center.y + (radius * 0.72f) * sin(rad).toFloat()
                drawCircle(color = Color.White, radius = 3.2f, center = Offset(indicatorX, indicatorY))
            }
        }
    }
}

@Composable
fun RgbScrollingWaveformView(
    rgbFlatArray: FloatArray,
    playheadNormalized: Float,
    accentColor: Color,
    modifier: Modifier = Modifier,
    onSeek: (Float) -> Unit = {}
) {
    Box(
        modifier = modifier
            .background(Color(0xFF05070B), RoundedCornerShape(4.dp))
            .border(1.dp, accentColor.copy(alpha = 0.45f), RoundedCornerShape(4.dp))
            .pointerInput(Unit) {
                detectTapGestures { offset ->
                    if (size.width > 0) {
                        onSeek((offset.x / size.width.toFloat()).coerceIn(0f, 1f))
                    }
                }
            }
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
                drawLine(
                    color = Color(0xFFFF5252),
                    start = Offset(x, midY - low * (h * 0.46f)),
                    end = Offset(x, midY + low * (h * 0.46f)),
                    strokeWidth = binWidth.coerceAtLeast(1.2f)
                )
                drawLine(
                    color = Color(0xFF69F0AE),
                    start = Offset(x, midY - mid * (h * 0.32f)),
                    end = Offset(x, midY + mid * (h * 0.32f)),
                    strokeWidth = binWidth.coerceAtLeast(1.2f)
                )
                drawLine(
                    color = Color(0xFF40C4FF),
                    start = Offset(x, midY - high * (h * 0.18f)),
                    end = Offset(x, midY + high * (h * 0.18f)),
                    strokeWidth = binWidth.coerceAtLeast(1.2f)
                )
            }

            val playX = (playheadNormalized.coerceIn(0f, 1f)) * w
            drawLine(
                color = Color.White,
                start = Offset(playX, 0f),
                end = Offset(playX, h),
                strokeWidth = 2.5f
            )
        }
    }
}

@Composable
fun MasterSpectrumAnalyzer(
    vuA: Float,
    vuB: Float,
    modifier: Modifier = Modifier
) {
    Box(
        modifier = modifier
            .background(Color(0xFF05070B), RoundedCornerShape(4.dp))
            .border(1.dp, Color(0xFF21262D), RoundedCornerShape(4.dp))
            .padding(3.dp)
    ) {
        Canvas(modifier = Modifier.fillMaxSize()) {
            val w = size.width
            val h = size.height
            val count = 24
            val barW = w / count.toFloat()
            val energy = ((vuA + vuB) * 0.65f).coerceIn(0.05f, 1.0f)

            for (i in 0 until count) {
                val shape = sin(PI * (i + 1) / (count + 1)).toFloat()
                val amp = (energy * (0.45f + 0.55f * shape)).coerceIn(0.05f, 1.0f)
                val barH = amp * h
                val barColor = when {
                    i < 8 -> Color(0xFF00E5FF)
                    i < 16 -> Color(0xFF57AB5A)
                    else -> Color(0xFFFCC934)
                }
                drawRect(
                    color = barColor,
                    topLeft = Offset(i * barW + 1f, h - barH),
                    size = Size((barW - 2f).coerceAtLeast(1f), barH)
                )
            }
        }
    }
}
