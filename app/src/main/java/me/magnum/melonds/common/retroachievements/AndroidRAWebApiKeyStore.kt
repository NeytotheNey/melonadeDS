package me.magnum.melonds.common.retroachievements

import android.content.SharedPreferences
import androidx.core.content.edit
import me.magnum.rcheevosapi.RAWebApiKeyStore

/**
 * @param sharedPreferences The file named [PREFERENCES_FILE] and nothing else. The default preferences go to Android's backups and to settings
 * exports, and this file is excluded from both.
 */
class AndroidRAWebApiKeyStore(private val sharedPreferences: SharedPreferences) : RAWebApiKeyStore {

    companion object {
        // Excluded by name in res/xml/backup_rules.xml and res/xml/data_extraction_rules.xml, so change those with it
        const val PREFERENCES_FILE = "ra_web_api_key"
        private const val API_KEY_KEY = "ra_web_api_key"
    }

    override fun getApiKey(): String? {
        return sharedPreferences.getString(API_KEY_KEY, null)?.takeIf { it.isNotBlank() }
    }

    override fun storeApiKey(apiKey: String) {
        sharedPreferences.edit {
            putString(API_KEY_KEY, apiKey)
        }
    }

    override fun clearApiKey() {
        sharedPreferences.edit {
            remove(API_KEY_KEY)
        }
    }
}
