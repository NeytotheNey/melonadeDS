package me.magnum.rcheevosapi

/**
 * Where the user's own RetroAchievements Web API key is kept. The key is as sensitive as a password, so nothing may log it.
 */
interface RAWebApiKeyStore {
    fun getApiKey(): String?
    fun storeApiKey(apiKey: String)
    fun clearApiKey()
}
