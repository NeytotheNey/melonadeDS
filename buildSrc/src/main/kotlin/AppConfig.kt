object AppConfig {
    const val compileSdkVersion = 37
    const val targetSdkVersion = compileSdkVersion
    // 29 is the lowest level where the NDK uses native TLS instead of emulated TLS
    const val minSdkVersion = 29
    const val ndkVersion = "28.0.13004108"

    const val versionCode = 42
    const val versionName = "1.0.0"

    // The upstream melonDS-android release this fork is based on. Used for the network user agent (RetroAchievements)
    const val upstreamVersionName = "2.0.1"
}