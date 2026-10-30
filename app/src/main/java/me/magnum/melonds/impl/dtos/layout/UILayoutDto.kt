package me.magnum.melonds.impl.dtos.layout

import com.google.gson.annotations.SerializedName
import me.magnum.melonds.domain.model.layout.UILayout
import me.magnum.melonds.domain.model.layout.WidescreenAspect

data class UILayoutDto(
    @SerializedName("mainScreenLayoutDto")
    val mainScreenLayout: ScreenLayoutDto,
    @SerializedName("secondaryScreenLayoutDto")
    val secondaryScreenLayout: ScreenLayoutDto,
    // WidescreenAspect.key. Missing in variants saved before wide layouts, which Gson leaves null, so they load as normal variants
    @SerializedName("wideAspect")
    val wideAspect: String? = null,
) {

    companion object {
        fun fromModel(uiLayout: UILayout): UILayoutDto {
            return UILayoutDto(
                mainScreenLayout = ScreenLayoutDto.fromModel(uiLayout.mainScreenLayout),
                secondaryScreenLayout = ScreenLayoutDto.fromModel(uiLayout.secondaryScreenLayout),
                wideAspect = uiLayout.wideAspect?.key,
            )
        }
    }

    fun toModel(): UILayout {
        return UILayout(
            mainScreenLayout = mainScreenLayout.toModel(),
            secondaryScreenLayout = secondaryScreenLayout.toModel(),
            // A ratio this version doesn't know counts as a normal variant
            wideAspect = WidescreenAspect.fromKey(wideAspect),
        )
    }
}