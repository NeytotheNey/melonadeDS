package me.magnum.melonds.common.opengl

/**
 * debug.melonade.debandfold: whether a surface debands inside its screen shader, once per screen pixel, instead of in the pre-pass over every
 * frame texel. Only when the switch is on, the filter's screen shader can take it ([VideoFilter.canFoldDeband] and the variant built), no Lua
 * tilt-shift, bloom or vignette runs (their passes come after deband, so folding would change the order), and the screens draw fewer pixels
 * than the frame has texels (otherwise the pre-pass is cheaper, as with a frame upscaled less than the screens). Pure, so it can be asked on
 * every frame.
 *
 * @param drawnPixels the area of the screens' draw rects on the surface, summed
 * @param frameTexels the frame texture's width times its height
 */
fun shouldFoldDeband(
    isSwitchOn: Boolean,
    canFilterFold: Boolean,
    isLuaEffectActive: Boolean,
    drawnPixels: Long,
    frameTexels: Long,
): Boolean {
    return isSwitchOn && canFilterFold && !isLuaEffectActive && drawnPixels in 1 until frameTexels
}
