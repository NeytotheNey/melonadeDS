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
        // FXAA has to run once per frame texel, so it's a pre-pass. Quilez then scales the result up smoothly, like DraStic does
        VideoFiltering.FXAA to VideoFilter(ShaderProgramSource.QuilezShader, listOf(ShaderProgramSource.FxaaPrePassShader)),
    )

    fun getFilter(filtering: VideoFiltering): VideoFilter = FILTER_MAP[filtering] ?: NO_FILTER
}
