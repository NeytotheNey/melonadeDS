package me.magnum.rcheevosapi.dto

import kotlinx.serialization.SerialName
import kotlinx.serialization.Serializable

@Serializable
internal data class GameDetailsDto(
    @SerialName("Title")
    val title: String? = null,
    @SerialName("Publisher")
    val publisher: String? = null,
    @SerialName("Developer")
    val developer: String? = null,
    @SerialName("Genre")
    val genre: String? = null,
    @SerialName("Released")
    val released: String? = null,
    @SerialName("ReleasedAtGranularity")
    val releasedGranularity: String? = null,
    @SerialName("ImageIcon")
    val imageIcon: String? = null,
    @SerialName("ImageTitle")
    val imageTitle: String? = null,
    @SerialName("ImageIngame")
    val imageIngame: String? = null,
    @SerialName("ImageBoxArt")
    val imageBoxArt: String? = null,
)
