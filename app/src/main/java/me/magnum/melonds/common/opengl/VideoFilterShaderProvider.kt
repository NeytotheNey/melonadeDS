package me.magnum.melonds.common.opengl

import me.magnum.melonds.domain.model.VideoFiltering

object VideoFilterShaderProvider {
    private val NO_FILTER = VideoFilter(ShaderProgramSource.NoFilterShader)

    private val FILTER_MAP = mapOf(
        VideoFiltering.NONE to NO_FILTER,
        VideoFiltering.LINEAR to VideoFilter(ShaderProgramSource.LinearShader),
        VideoFiltering.XBR2 to VideoFilter(ShaderProgramSource.XbrShader),
        VideoFiltering.HQ2X to VideoFilter(ShaderProgramSource.Hq2xShader),
        VideoFiltering.HQ4X to VideoFilter(ShaderProgramSource.Hq4xShader),
        VideoFiltering.QUILEZ to VideoFilter(ShaderProgramSource.QuilezShader),
        VideoFiltering.LCD to VideoFilter(ShaderProgramSource.LcdShader),
        VideoFiltering.SCANLINES to VideoFilter(ShaderProgramSource.ScanlinesShader),
        VideoFiltering.SCALE2X to VideoFilter(ShaderProgramSource.Scale2xShader),
        // FXAA runs once per frame texel as a pre-pass, then Quilez scales the result up smoothly
        VideoFiltering.FXAA to VideoFilter(ShaderProgramSource.QuilezShader, listOf(FilterPrePass(ShaderProgramSource.FxaaPrePassShader))),
        VideoFiltering.SHARP to VideoFilter(ShaderProgramSource.SharpShader),
        // MMPX doubles the frame texture, then Sharp scales that to the screen with every pixel the same size (just Sharp when 3D is upscaled)
        VideoFiltering.MMPX to VideoFilter(
            ShaderProgramSource.SharpShader,
            listOf(FilterPrePass(ShaderProgramSource.MmpxPrePassShader, scale = 2, nativeResolutionOnly = true)),
        ),
    )

    fun getFilter(filtering: VideoFiltering): VideoFilter = FILTER_MAP[filtering] ?: NO_FILTER
}
