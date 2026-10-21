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

enum class AppThemeMode(val title: String) {
    DARK("DARK"),
    LIGHT("LIGHT"),
    GLASSMORPHISM("GLASS"),
    NEON_CYAN("NEON CYAN")
}

data class ApexThemePalette(
    val bgPrimary: Color,
    val cardSurface: Color,
    val cardBorder: Color,
    val textPrimary: Color,
    val textSecondary: Color,
    val accentA: Color,
    val accentB: Color,
    val accentBgm: Color,
    val knobSurface: Color
)

fun getThemePalette(mode: AppThemeMode): ApexThemePalette = when (mode) {
    AppThemeMode.LIGHT -> ApexThemePalette(
        bgPrimary = Color(0xFFF2F5F9),
        cardSurface = Color(0xFFFFFFFF),
        cardBorder = Color(0xFFD0D8E4),
        textPrimary = Color(0xFF0F172A),
        textSecondary = Color(0xFF475569),
        accentA = Color(0xFF0284C7),
        accentB = Color(0xFFEA580C),
        accentBgm = Color(0xFF7C3AED),
        knobSurface = Color(0xFFE2E8F0)
    )
    AppThemeMode.DARK -> ApexThemePalette(
        bgPrimary = Color(0xFF07090E),
        cardSurface = Color(0xFF10141E),
        cardBorder = Color(0xFF222A3A),
        textPrimary = Color(0xFFF0F6FC),
        textSecondary = Color(0xFF8B949E),
        accentA = Color(0xFF00E5FF),
        accentB = Color(0xFFFA903E),
        accentBgm = Color(0xFFC084FC),
        knobSurface = Color(0xFF090C12)
    )
    AppThemeMode.GLASSMORPHISM -> ApexThemePalette(
        bgPrimary = Color(0xFF0B1120),
        cardSurface = Color(0x991E293B),
        cardBorder = Color(0x8894A3B8),
        textPrimary = Color(0xFFF8FAFC),
        textSecondary = Color(0xFFCBD5E1),
        accentA = Color(0xFF38BDF8),
        accentB = Color(0xFFF472B6),
        accentBgm = Color(0xFF34D399),
        knobSurface = Color(0xAA0F172A)
    )
    AppThemeMode.NEON_CYAN -> ApexThemePalette(
        bgPrimary = Color(0xFF02080D),
        cardSurface = Color(0xFF05131E),
        cardBorder = Color(0xFF00E5FF),
        textPrimary = Color(0xFFE0FFFF),
        textSecondary = Color(0xFF00B8D4),
        accentA = Color(0xFF00E5FF),
        accentB = Color(0xFFFF007F),
        accentBgm = Color(0xFF00FF9D),
        knobSurface = Color(0xFF030B12)
    )
}

@Composable
fun RotaryKnob3D(
    label: String,
    value: Float,
    isBipolar: Boolean = true,
    accentColor: Color = Color(0xFF00E5FF),
    palette: ApexThemePalette,
    modifier: Modifier = Modifier,
    onChange: (Float) -> Unit
) {
    var knobVal by remember(value) { mutableFloatStateOf(value) }

    Column(modifier = modifier.padding(1.dp), horizontalAlignment = Alignment.CenterHorizontally) {
        Text(
            text = label,
            color = palette.textSecondary,
            fontSize = 7.sp,
            fontFamily = FontFamily.Monospace,
            fontWeight = FontWeight.Bold,
            maxLines = 1
        )
        Spacer(modifier = Modifier.height(1.dp))
        Box(
            modifier = Modifier
                .size(34.dp)
                .background(palette.knobSurface, CircleShape)
                .border(1.5.dp, accentColor.copy(alpha = 0.7f), CircleShape)
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
            Canvas(modifier = Modifier.size(28.dp)) {
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
                drawCircle(color = palette.textPrimary, radius = 3.0f, center = Offset(indicatorX, indicatorY))
            }
        }
    }
}

@Composable
fun RgbScrollingWaveformView(
    rgbFlatArray: FloatArray,
    playheadNormalized: Float,
    accentColor: Color,
    palette: ApexThemePalette,
    modifier: Modifier = Modifier,
    onSeek: (Float) -> Unit = {}
) {
    Box(
        modifier = modifier
            .background(palette.knobSurface, RoundedCornerShape(4.dp))
            .border(1.dp, accentColor.copy(alpha = 0.55f), RoundedCornerShape(4.dp))
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
                color = palette.textPrimary,
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
    vuBgm: Float,
    palette: ApexThemePalette,
    modifier: Modifier = Modifier
) {
    Box(
        modifier = modifier
            .background(palette.knobSurface, RoundedCornerShape(4.dp))
            .border(1.dp, palette.cardBorder, RoundedCornerShape(4.dp))
            .padding(3.dp)
    ) {
        Canvas(modifier = Modifier.fillMaxSize()) {
            val w = size.width
            val h = size.height
            val count = 28
            val barW = w / count.toFloat()
            val energy = ((vuA + vuB + vuBgm * 0.7f) * 0.55f).coerceIn(0.04f, 1.0f)

            for (i in 0 until count) {
                val shape = sin(PI * (i + 1) / (count + 1)).toFloat()
                val amp = (energy * (0.4f + 0.6f * shape)).coerceIn(0.04f, 1.0f)
                val barH = amp * h
                val barColor = when {
                    i < 9 -> palette.accentA
                    i < 19 -> palette.accentBgm
                    else -> palette.accentB
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
