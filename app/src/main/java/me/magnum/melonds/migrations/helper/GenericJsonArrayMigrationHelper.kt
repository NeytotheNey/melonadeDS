package me.magnum.melonds.migrations.helper

import android.content.Context
import android.util.Log
import com.google.gson.Gson
import com.google.gson.reflect.TypeToken
import me.magnum.melonds.utils.sizeIfFileExists
import me.magnum.melonds.utils.writeFileAtomically
import java.io.File
import java.io.FileReader
import java.io.IOException

class GenericJsonArrayMigrationHelper(
    private val context: Context,
    private val gson: Gson,
    private val logFailure: (String, Throwable) -> Unit = { message, error -> Log.e(TAG, message, error) },
) {

    private companion object {
        const val TAG = "JsonArrayMigration"
    }

    // A later migration would read one of these in the wrong format, since it wasn't converted
    private val failedFiles = HashSet<String>()

    inline fun <reified T, U> migrateJsonArrayData(jsonFile: String, noinline dataMapper: (T) -> U?) {
        migrateJsonArrayData(jsonFile, T::class.java, dataMapper)
    }

    fun <T, U> migrateJsonArrayData(jsonFile: String, fromClass: Class<T>, dataMapper: (T) -> U?) {
        if (jsonFile in failedFiles) {
            return
        }

        val dataFile = File(context.filesDir, jsonFile)
        // Catches everything because migrations run during app start and last_version only moves on after they finish: an exception here would crash every start
        try {
            val newData = getOriginalData(dataFile, fromClass).mapNotNull { data ->
                dataMapper(data)
            }
            val newJson = gson.toJson(newData)

            writeFileAtomically(dataFile) {
                it.write(newJson.toByteArray(Charsets.UTF_8))
                true
            }
        } catch (e: Throwable) {
            failedFiles.add(jsonFile)
            logFailure("Failed to migrate $jsonFile. Leaving the file as it is", e)
        }
    }

    private fun <T> getOriginalData(dataFile: File, fromClass: Class<T>): List<T> {
        val size = sizeIfFileExists(dataFile)
        if (size == null || size == 0L) {
            return emptyList()
        }

        val dataListType = TypeToken.getParameterized(List::class.java, fromClass).type
        return FileReader(dataFile).use { gson.fromJson<List<T>?>(it, dataListType) } ?: throw IOException("${dataFile.name} has no data, but isn't empty")
    }
}
