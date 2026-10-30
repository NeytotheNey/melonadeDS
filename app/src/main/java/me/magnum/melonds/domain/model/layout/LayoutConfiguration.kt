package me.magnum.melonds.domain.model.layout

import java.util.UUID

data class LayoutConfiguration(
    val id: UUID?,
    val name: String?,
    val type: LayoutType,
    val orientation: LayoutOrientation,
    val useCustomOpacity: Boolean,
    val opacity: Int,
    val layoutVariants: Map<UILayoutVariant, UILayout>,
) {

    // A wide layout has at least one saved variant with a ratio (UILayout.wideAspect). A game with widescreen on can pick it as its wide layout
    val isWide get() = layoutVariants.values.any { it.wideAspect != null }

    // The ratio all of its wide variants share, or null if it has none or they differ
    val commonWideAspect get() = layoutVariants.values.mapNotNull { it.wideAspect }.distinct().singleOrNull()

    companion object {
        val DEFAULT_ID = UUID(0, 0)

        fun newCustom(): LayoutConfiguration {
            return LayoutConfiguration(
                id = null,
                name = null,
                type = LayoutType.CUSTOM,
                orientation = LayoutOrientation.FOLLOW_SYSTEM,
                useCustomOpacity = false,
                opacity = 50,
                layoutVariants = emptyMap(),
            )
        }
    }

    // Empty constructor to include defaults that help in migrations
    constructor() : this(null, null, LayoutType.CUSTOM, LayoutOrientation.FOLLOW_SYSTEM, false, 50, emptyMap())

    enum class LayoutType {
        DEFAULT,
        CUSTOM
    }

    enum class LayoutOrientation {
        FOLLOW_SYSTEM,
        PORTRAIT,
        LANDSCAPE
    }
}