package me.magnum.melonds.ui.common.view

import android.content.Context
import android.graphics.Matrix
import android.graphics.Path
import android.graphics.RectF
import me.magnum.melonds.domain.model.Input

/**
 * Builders for the raised on-screen buttons. Button positions match the hit areas used by the input handlers (512 grid for the D-pad and ABXY).
 */
object RaisedButtons {

    fun dpad(context: Context): RaisedButtonView {
        // Centre square at (256, 246) so that the cross plus its lip is centred in the view
        val corner = 16f
        val parts = listOf(
            RaisedButtonView.Part(roundRect(176f, 166f, 336f, 326f, corner), pressable = false),
            RaisedButtonView.Part(roundRect(176f, 14f, 336f, 160f, corner), Input.UP, glyph = triangle(256f, 87f, 0f, -1f)),
            RaisedButtonView.Part(roundRect(176f, 332f, 336f, 478f, corner), Input.DOWN, glyph = triangle(256f, 405f, 0f, 1f)),
            RaisedButtonView.Part(roundRect(24f, 166f, 170f, 326f, corner), Input.LEFT, glyph = triangle(97f, 246f, -1f, 0f)),
            RaisedButtonView.Part(roundRect(342f, 166f, 488f, 326f, corner), Input.RIGHT, glyph = triangle(415f, 246f, 1f, 0f)),
        )
        return RaisedButtonView(context, 512f, 20f, 16f, parts)
    }

    fun abxy(context: Context): RaisedButtonView {
        val parts = listOf(
            circleButton(256f, 94f, 78f, Input.X, "X", 64f),
            circleButton(96f, 254f, 78f, Input.Y, "Y", 64f),
            circleButton(416f, 254f, 78f, Input.A, "A", 64f),
            circleButton(256f, 414f, 78f, Input.B, "B", 64f),
        )
        return RaisedButtonView(context, 512f, 20f, 16f, parts)
    }

    fun shoulderL(context: Context): RaisedButtonView {
        return RaisedButtonView(context, 128f, 10f, 8f, listOf(RaisedButtonView.Part(shoulderShape(false), label = "L", labelX = 40f, labelY = 59f, labelSize = 60f)))
    }

    fun shoulderR(context: Context): RaisedButtonView {
        return RaisedButtonView(context, 128f, 10f, 8f, listOf(RaisedButtonView.Part(shoulderShape(true), label = "R", labelX = 88f, labelY = 59f, labelSize = 60f)))
    }

    fun start(context: Context): RaisedButtonView {
        return RaisedButtonView(context, 64f, 7f, 5.6f, listOf(circleButton(32f, 28.5f, 28f, null, "START", 12f)))
    }

    fun select(context: Context): RaisedButtonView {
        return RaisedButtonView(context, 64f, 7f, 5.6f, listOf(circleButton(32f, 28.5f, 28f, null, "SELECT", 12f)))
    }

    private fun circleButton(cx: Float, cy: Float, radius: Float, input: Input?, label: String, labelSize: Float): RaisedButtonView.Part {
        val shape = Path().apply { addCircle(cx, cy, radius, Path.Direction.CW) }
        return RaisedButtonView.Part(shape, input, label = label, labelX = cx, labelY = cy, labelSize = labelSize)
    }

    private fun roundRect(left: Float, top: Float, right: Float, bottom: Float, radius: Float): Path {
        return Path().apply { addRoundRect(RectF(left, top, right, bottom), radius, radius, Path.Direction.CW) }
    }

    private fun triangle(cx: Float, cy: Float, dx: Float, dy: Float): Path {
        val halfLength = 20f
        val halfWidth = 26f
        return Path().apply {
            moveTo(cx + dx * halfLength, cy + dy * halfLength)
            lineTo(cx - dx * halfLength - dy * halfWidth, cy - dy * halfLength + dx * halfWidth)
            lineTo(cx - dx * halfLength + dy * halfWidth, cy - dy * halfLength - dx * halfWidth)
            close()
        }
    }

    // L-shaped tab in a 128 grid: a full-width bar on top with a stem going down on the outer side. The face ends at 118 to leave room for the lip.
    private fun shoulderShape(mirrored: Boolean): Path {
        val bar = Path().apply {
            addRoundRect(RectF(0f, 0f, 128f, 70f), floatArrayOf(12f, 12f, 12f, 12f, 8f, 8f, 8f, 8f), Path.Direction.CW)
        }
        val stem = Path().apply {
            addRoundRect(RectF(0f, 0f, 80f, 118f), floatArrayOf(12f, 12f, 0f, 0f, 8f, 8f, 12f, 12f), Path.Direction.CW)
        }
        bar.op(stem, Path.Op.UNION)
        if (mirrored) {
            bar.transform(Matrix().apply { setScale(-1f, 1f, 64f, 0f) })
        }
        return bar
    }
}
