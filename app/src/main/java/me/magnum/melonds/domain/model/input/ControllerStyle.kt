package me.magnum.melonds.domain.model.input

enum class ControllerStyle {
    CLASSIC,
    DS_LITE,
    CHARCOAL,
    SUPER_COLOURS,
    INDIGO,
    OUTLINE,
    SOFT_TINT,
    PIXEL_8,
    PIXEL_16;

    /**
     * The lower-case name, which installs already have saved, so entries can't be renamed
     */
    fun toPreferenceValue(): String = name.lowercase()

    companion object {
        val DEFAULT = CLASSIC

        /**
         * A missing or unknown value gives [DEFAULT]
         */
        fun fromPreferenceValue(value: String?): ControllerStyle {
            return entries.firstOrNull { it.name.equals(value, ignoreCase = true) } ?: DEFAULT
        }
    }
}
