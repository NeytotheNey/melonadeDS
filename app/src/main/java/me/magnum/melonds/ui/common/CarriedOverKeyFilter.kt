package me.magnum.melonds.ui.common

/**
 * Spots key events of a press that began on the previous screen. Holding a button to open a screen keeps sending repeats after the screen has
 * opened, and those look like a fresh press to whatever has the focus there, so letting go would click it.
 */
class CarriedOverKeyFilter {

    private val pressedHere = mutableSetOf<Int>()
    private val carriedOver = mutableSetOf<Int>()

    /**
     * @return True if the event should be ignored
     */
    fun shouldIgnore(keyCode: Int, isDown: Boolean, repeatCount: Int): Boolean {
        if (isDown) {
            if (repeatCount == 0) {
                carriedOver.remove(keyCode)
                pressedHere.add(keyCode)
                return false
            }
            if (keyCode in pressedHere) {
                return false
            }
            carriedOver.add(keyCode)
            return true
        }

        pressedHere.remove(keyCode)
        return carriedOver.remove(keyCode)
    }
}
