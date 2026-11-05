package me.magnum.rcheevosapi

import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.suspendCancellableCoroutine
import kotlinx.coroutines.withContext
import kotlinx.serialization.SerializationException
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonArray
import kotlinx.serialization.json.JsonObject
import me.magnum.rcheevosapi.dto.GameDetailsDto
import me.magnum.rcheevosapi.exception.RAWebApiException
import me.magnum.rcheevosapi.model.RAGameDetails
import me.magnum.rcheevosapi.model.RAGameId
import okhttp3.Call
import okhttp3.Callback
import okhttp3.HttpUrl
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.Response
import java.io.ByteArrayOutputStream
import java.io.IOException
import kotlin.coroutines.resume
import kotlin.coroutines.resumeWithException

/**
 * The RetroAchievements Web API (`/API/`), which unlike the connect API in [RAApi] has no `Success` envelope and is authenticated with the
 * user's own key in the URL. That URL must never reach a log or an exception, so don't log [Request]s, [Response]s or [Call]s here, and
 * don't add a logging interceptor to the client this gets.
 */
class RAWebApi(
    okHttpClient: OkHttpClient,
    private val json: Json,
    private val keyStore: RAWebApiKeyStore,
) {

    companion object {
        private const val API_HOST = "retroachievements.org"
        private const val MEDIA_HOST = "media.retroachievements.org"

        private const val PARAMETER_API_KEY = "y"
        private const val PARAMETER_GAME_ID = "i"

        private const val TITLE_FIELD = "Title"

        private const val MAX_IMAGE_BYTES = 16 * 1024 * 1024

        // A game that exists, so being told it doesn't means the answer can't be trusted
        private val CONTROL_GAME_ID = RAGameId(1)

        // What RetroAchievements returns for a game with no icon or no other image
        private val PLACEHOLDER_IMAGES = setOf("/Images/000001.png", "/Images/000002.png")
        private val IMAGE_PATH = Regex("/Images/([A-Za-z0-9_-]+\\.(?:png|jpe?g|gif|webp))", RegexOption.IGNORE_CASE)

        /**
         * The URL of the image at [imagePath] on the media host, or null if the path is anything but a plain image name. The path comes from
         * the API, so it's never appended to the host as text: "@evil.example/x.png" or "/../x" could then point somewhere else.
         */
        fun imageUrl(imagePath: String): HttpUrl? {
            val imageName = IMAGE_PATH.matchEntire(imagePath)?.groupValues?.get(1) ?: return null
            return HttpUrl.Builder()
                .scheme("https")
                .host(MEDIA_HOST)
                .addPathSegment("Images")
                .addPathSegment(imageName)
                .build()
        }
    }

    // Following a redirect could take the key or an image request to another host
    private val client = okHttpClient.newBuilder()
        .followRedirects(false)
        .followSslRedirects(false)
        .build()

    /**
     * Calls `API_GetGame` with the stored key. The result is null when RetroAchievements says it doesn't know the game, which is a 200 with nothing in
     * it rather than a 404, and a failure is always a [RAWebApiException].
     */
    suspend fun getGame(gameId: RAGameId): Result<RAGameDetails?> {
        val apiKey = keyStore.getApiKey() ?: return Result.failure(RAWebApiException.MissingKey())
        return getGame(gameId, apiKey)
    }

    /**
     * Asks for a game that exists with the stored key, to tell whether "not found" answers can be believed.
     */
    suspend fun getControlGame(): Result<RAGameDetails?> = getGame(CONTROL_GAME_ID)

    /**
     * Tries [apiKey] with one `API_GetGame` call. Fails with [RAWebApiException.InvalidKey] if RetroAchievements rejects it.
     */
    suspend fun testApiKey(apiKey: String): Result<Unit> {
        return getGame(CONTROL_GAME_ID, apiKey).mapCatching { game ->
            if (game == null) {
                throw RAWebApiException.UnexpectedResponse()
            }
        }
    }

    /**
     * Downloads an image from the media host. [imagePath] is the value of one of the API's image fields.
     */
    suspend fun downloadImage(imagePath: String): Result<ByteArray> {
        val url = imageUrl(imagePath) ?: return Result.failure(RAWebApiException.InvalidImagePath())
        return request(Request.Builder().get().url(url).build()) { response ->
            if (response.code != 200) {
                throw RAWebApiException.HttpError(response.code)
            }
            // A page instead of an image says something about the host, as a block or a challenge page would, and nothing about the image
            if (response.body.contentType()?.type == "text") {
                throw RAWebApiException.UnexpectedResponse()
            }
            readLimited(response)
        }
    }

    private suspend fun getGame(gameId: RAGameId, apiKey: String): Result<RAGameDetails?> {
        val url = HttpUrl.Builder()
            .scheme("https")
            .host(API_HOST)
            .addPathSegment("API")
            .addPathSegment("API_GetGame.php")
            .addQueryParameter(PARAMETER_GAME_ID, gameId.id.toString())
            .addQueryParameter(PARAMETER_API_KEY, apiKey)
            .build()

        return request(Request.Builder().get().url(url).build()) { response ->
            when (response.code) {
                200 -> parseGame(gameId, response.body.string())
                401, 403 -> throw RAWebApiException.InvalidKey()
                429 -> throw RAWebApiException.RateLimited()
                else -> throw RAWebApiException.HttpError(response.code)
            }
        }
    }

    private suspend fun <T> request(request: Request, handleResponse: (Response) -> T): Result<T> = withContext(Dispatchers.IO) {
        try {
            Result.success(execute(request).use(handleResponse))
        } catch (e: CancellationException) {
            throw e
        } catch (e: RAWebApiException) {
            Result.failure(e)
        } catch (e: Exception) {
            // Only the type, since OkHttp's messages aren't ours to promise anything about
            Result.failure(RAWebApiException.NetworkError(e.javaClass.simpleName))
        }
    }

    private suspend fun execute(request: Request): Response = suspendCancellableCoroutine { continuation ->
        val call = client.newCall(request)
        call.enqueue(object : Callback {
            override fun onFailure(call: Call, e: IOException) {
                continuation.resumeWithException(RAWebApiException.NetworkError(e.javaClass.simpleName))
            }

            override fun onResponse(call: Call, response: Response) {
                // If the coroutine was cancelled in the meantime nobody will read this response, and it would leak a connection
                continuation.resume(response) { _, _, _ -> response.close() }
            }
        })

        continuation.invokeOnCancellation {
            call.cancel()
        }
    }

    private fun parseGame(gameId: RAGameId, body: String): RAGameDetails? {
        val element = try {
            json.parseToJsonElement(body)
        } catch (_: SerializationException) {
            throw RAWebApiException.UnexpectedResponse()
        }
        // Only an empty answer means an unknown game. The parser takes a lone unquoted word as a value, so garbage can get this far, and it mustn't pass
        // for "not found" because that's never retried
        if ((element is JsonArray && element.isEmpty()) || (element is JsonObject && element.isEmpty())) {
            return null
        }
        // An object about something else, such as an error message, isn't an answer about the game at all. One with an empty title is, so that's handled below
        if (element !is JsonObject || TITLE_FIELD !in element) {
            throw RAWebApiException.UnexpectedResponse()
        }

        val dto = try {
            json.decodeFromJsonElement(GameDetailsDto.serializer(), element)
        } catch (_: SerializationException) {
            throw RAWebApiException.MalformedResponse()
        }
        val title = dto.title.orNullIfBlank() ?: return null

        return RAGameDetails(
            id = gameId,
            title = title,
            publisher = dto.publisher.orNullIfBlank(),
            developer = dto.developer.orNullIfBlank(),
            genre = dto.genre.orNullIfBlank(),
            released = dto.released.orNullIfBlank(),
            releasedGranularity = dto.releasedGranularity.orNullIfBlank(),
            imageIcon = dto.imageIcon.toImagePath(),
            imageTitle = dto.imageTitle.toImagePath(),
            imageIngame = dto.imageIngame.toImagePath(),
            imageBoxArt = dto.imageBoxArt.toImagePath(),
        )
    }

    private fun readLimited(response: Response): ByteArray {
        val output = ByteArrayOutputStream()
        val buffer = ByteArray(16 * 1024)
        response.body.byteStream().use { input ->
            while (true) {
                val read = input.read(buffer)
                if (read < 0) {
                    return output.toByteArray()
                }
                output.write(buffer, 0, read)
                if (output.size() > MAX_IMAGE_BYTES) {
                    throw RAWebApiException.ImageTooLarge()
                }
            }
        }
    }

    private fun String?.orNullIfBlank(): String? = this?.trim()?.takeIf { it.isNotEmpty() }

    private fun String?.toImagePath(): String? = orNullIfBlank()?.takeIf { it !in PLACEHOLDER_IMAGES }
}
