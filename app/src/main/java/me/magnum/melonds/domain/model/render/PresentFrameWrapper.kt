package me.magnum.melonds.domain.model.render

data class PresentFrameWrapper(
    var isValidFrame: Boolean = false,
    var textureId: Int = 0,
    // Real size of the frame texture, which grows with the internal resolution scaling of the OpenGL and Compute renderers
    var textureWidth: Int = 0,
    var textureHeight: Int = 0,
    // Sequence number of the emulator frame. The same value means the same frame is being presented again. 0 if the frame is not valid
    var frameSequence: Long = 0,
)