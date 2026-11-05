package me.magnum.rcheevosapi

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.cancelAndJoin
import kotlinx.coroutines.launch
import kotlinx.coroutines.runBlocking
import kotlinx.serialization.json.Json
import me.magnum.rcheevosapi.exception.RAWebApiException
import me.magnum.rcheevosapi.model.RAGameDetails
import me.magnum.rcheevosapi.model.RAGameId
import okhttp3.Interceptor
import okhttp3.MediaType.Companion.toMediaType
import okhttp3.OkHttpClient
import okhttp3.Protocol
import okhttp3.Request
import okhttp3.Response
import okhttp3.ResponseBody.Companion.toResponseBody
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.IOException
import java.io.PrintWriter
import java.io.StringWriter
import java.util.concurrent.CountDownLatch

/**
 * Nothing here goes near a network: an interceptor answers every request before OkHttp would connect.
 */
class RAWebApiTest {

    private companion object {
        const val API_KEY = "s3cr3t-web-api-key"
    }

    private val json = Json {
        ignoreUnknownKeys = true
        explicitNulls = false
    }

    private class FakeKeyStore(private var apiKey: String? = API_KEY) : RAWebApiKeyStore {
        override fun getApiKey() = apiKey
        override fun storeApiKey(apiKey: String) { this.apiKey = apiKey }
        override fun clearApiKey() { apiKey = null }
    }

    private var lastRequest: Request? = null

    private fun api(keyStore: RAWebApiKeyStore = FakeKeyStore(), respond: (Interceptor.Chain) -> Response): RAWebApi {
        val client = OkHttpClient.Builder()
            .addInterceptor(Interceptor { chain ->
                lastRequest = chain.request()
                respond(chain)
            })
            .build()
        return RAWebApi(client, json, keyStore)
    }

    private fun respondWith(code: Int, body: String = "", contentType: String = "application/json"): (Interceptor.Chain) -> Response = { chain ->
        Response.Builder()
            .request(chain.request())
            .protocol(Protocol.HTTP_1_1)
            .code(code)
            .message("Status $code")
            .body(body.toResponseBody(contentType.toMediaType()))
            .build()
    }

    private fun getGame(code: Int, body: String = ""): Result<RAGameDetails?> = runBlocking {
        api(respond = respondWith(code, body)).getGame(RAGameId(42))
    }

    @Test
    fun callsGetGameWithTheGameIdAndKeyInTheUrl() {
        getGame(200, "{}")

        val url = lastRequest!!.url
        assertEquals("https", url.scheme)
        assertEquals("retroachievements.org", url.host)
        assertEquals("/API/API_GetGame.php", url.encodedPath)
        assertEquals("42", url.queryParameter("i"))
        assertEquals(API_KEY, url.queryParameter("y"))
    }

    @Test
    fun parsesACompleteGame() {
        val game = getGame(
            200,
            """
            {
              "Title": "Super Plumber", "GameTitle": "Super Plumber", "ConsoleID": 18, "ForumTopicID": 123,
              "Publisher": "Nontendo", "Developer": "Nontendo EAD", "Genre": "Platformer",
              "Released": "2006-05-15", "ReleasedAtGranularity": "day",
              "ImageIcon": "/Images/000100.png", "ImageTitle": "/Images/000101.png", "ImageIngame": "/Images/000102.png", "ImageBoxArt": "/Images/000103.png"
            }
            """,
        ).getOrThrow()!!

        assertEquals(RAGameId(42), game.id)
        assertEquals("Super Plumber", game.title)
        assertEquals("Nontendo", game.publisher)
        assertEquals("Nontendo EAD", game.developer)
        assertEquals("Platformer", game.genre)
        assertEquals("2006-05-15", game.released)
        assertEquals("day", game.releasedGranularity)
        assertEquals("/Images/000100.png", game.imageIcon)
        assertEquals("/Images/000101.png", game.imageTitle)
        assertEquals("/Images/000102.png", game.imageIngame)
        assertEquals("/Images/000103.png", game.imageBoxArt)
    }

    @Test
    fun blankNullAndMissingFieldsBecomeNull() {
        val game = getGame(
            200,
            """{"Title": "Game", "Publisher": "", "Developer": "   ", "Genre": null, "Released": null, "ImageBoxArt": ""}""",
        ).getOrThrow()!!

        assertNull(game.publisher)
        assertNull(game.developer)
        assertNull(game.genre)
        assertNull(game.released)
        assertNull(game.releasedGranularity)
        assertNull(game.imageBoxArt)
        assertNull(game.imageTitle)
        assertNull(game.imageIcon)
    }

    @Test
    fun placeholderImagesCountAsNoImage() {
        val game = getGame(
            200,
            """{"Title": "Game", "ImageIcon": "/Images/000001.png", "ImageTitle": "/Images/000002.png", "ImageIngame": "/Images/000002.png", "ImageBoxArt": "/Images/000002.png"}""",
        ).getOrThrow()!!

        assertNull(game.imageIcon)
        assertNull(game.imageTitle)
        assertNull(game.imageIngame)
        assertNull(game.imageBoxArt)
    }

    @Test
    fun anEmptyAnswerOrAnEmptyTitleMeansNotFound() {
        listOf("{}", "[]", """{"Title": ""}""", """{"Title": "  "}""", """{"Title": null}""").forEach { body ->
            assertTrue(body, getGame(200, body).isSuccess)
            assertNull(body, getGame(200, body).getOrThrow())
        }
    }

    @Test
    fun anAnswerAboutSomethingElseIsNotNotFound() {
        listOf("""{"message": "Service unavailable"}""", """{"Publisher": "Nontendo"}""", "null").forEach { body ->
            assertTrue(body, getGame(200, body).exceptionOrNull() is RAWebApiException.UnexpectedResponse)
        }
    }

    @Test
    fun a404IsNotNotFound() {
        assertEquals(404, (getGame(404).exceptionOrNull() as RAWebApiException.HttpError).code)
    }

    @Test
    fun errorsMapToTheirOwnExceptions() {
        assertTrue(getGame(401).exceptionOrNull() is RAWebApiException.InvalidKey)
        assertTrue(getGame(403).exceptionOrNull() is RAWebApiException.InvalidKey)
        assertTrue(getGame(429).exceptionOrNull() is RAWebApiException.RateLimited)
        assertEquals(503, (getGame(503).exceptionOrNull() as RAWebApiException.HttpError).code)
        assertTrue(getGame(200, "<html>not json").exceptionOrNull() is RAWebApiException.UnexpectedResponse)
        assertTrue(getGame(200, "").exceptionOrNull() is RAWebApiException.UnexpectedResponse)
        assertTrue(getGame(200, """{"Title": {"nested": true}}""").exceptionOrNull() is RAWebApiException.MalformedResponse)
    }

    @Test
    fun garbageThatParsesIsNotMistakenForAnUnknownGame() {
        listOf("<html>", "maintenance", "42", "[1]").forEach { body ->
            assertTrue(body, getGame(200, body).exceptionOrNull() is RAWebApiException.UnexpectedResponse)
        }
    }

    @Test
    fun noKeyMeansNoRequest() {
        val result = runBlocking {
            api(FakeKeyStore(apiKey = null), respondWith(200, "{}")).getGame(RAGameId(42))
        }

        assertTrue(result.exceptionOrNull() is RAWebApiException.MissingKey)
        assertNull(lastRequest)
    }

    @Test
    fun theControlCallAsksForTheKnownGameWithTheStoredKey() {
        val game = runBlocking { api(respond = respondWith(200, """{"Title": "Game"}""")).getControlGame() }.getOrThrow()

        assertEquals("Game", game!!.title)
        assertEquals("1", lastRequest!!.url.queryParameter("i"))
        assertEquals(API_KEY, lastRequest!!.url.queryParameter("y"))
    }

    @Test
    fun theKeyTestUsesTheGivenKeyAndNeedsARealGameBack() {
        val keyStore = FakeKeyStore(apiKey = null)

        assertTrue(runBlocking { api(keyStore, respondWith(200, """{"Title": "Game"}""")).testApiKey("candidate") }.isSuccess)
        assertEquals("candidate", lastRequest!!.url.queryParameter("y"))
        assertTrue(runBlocking { api(keyStore, respondWith(401)).testApiKey("candidate") }.exceptionOrNull() is RAWebApiException.InvalidKey)
    }

    @Test
    fun theKeyTestDoesNotCallAnAnswerWithNoGameAWorkingKey() {
        val keyStore = FakeKeyStore(apiKey = null)

        listOf(respondWith(200, "{}"), respondWith(200, "[]"), respondWith(404), respondWith(200, """{"message": "?"}""")).forEach { respond ->
            val result = runBlocking { api(keyStore, respond).testApiKey("candidate") }

            assertTrue(result.isFailure)
            assertFalse(result.exceptionOrNull() is RAWebApiException.InvalidKey)
        }
    }

    @Test
    fun aFailedRequestNeverCarriesTheKey() {
        // As bad as it could get: the underlying error quotes the whole URL
        val failures = listOf<(Interceptor.Chain) -> Response>(
            { chain -> throw IOException("Failed to connect to ${chain.request().url}") },
            respondWith(500, "error for $API_KEY"),
            respondWith(200, "<html>$API_KEY"),
            respondWith(200, """{"Title": {"key": "$API_KEY"}}"""),
        )

        failures.forEach { respond ->
            val exception = runBlocking { api(respond = respond).getGame(RAGameId(42)) }.exceptionOrNull()
            assertNotNull(exception)

            val printed = StringWriter().also { exception!!.printStackTrace(PrintWriter(it)) }.toString()
            assertFalse(printed, printed.contains(API_KEY))
            assertNull(exception!!.cause)
        }
    }

    @Test
    fun aCancelledCallEndsAsACancellationRatherThanAFailure() {
        val requestStarted = CountDownLatch(1)
        val releaseRequest = CountDownLatch(1)
        var result: Result<RAGameDetails?>? = null

        runBlocking(Dispatchers.Default) {
            val call = launch {
                result = api(respond = { chain ->
                    requestStarted.countDown()
                    releaseRequest.await()
                    respondWith(200, "{}")(chain)
                }).getGame(RAGameId(42))
            }

            requestStarted.await()
            call.cancelAndJoin()
            releaseRequest.countDown()

            assertTrue(call.isCancelled)
        }

        // A failure here would make the sync mark the game or back off because it was told to stop
        assertNull(result)
    }

    @Test
    fun imageUrlsAlwaysPointAtTheMediaHost() {
        assertEquals("https://media.retroachievements.org/Images/012345.png", RAWebApi.imageUrl("/Images/012345.png").toString())
        assertEquals("https://media.retroachievements.org/Images/a_b-c.JPG", RAWebApi.imageUrl("/Images/a_b-c.JPG").toString())
    }

    @Test
    fun imagePathsThatCouldLeaveTheMediaHostOrImagesFolderAreRefused() {
        listOf(
            "",
            "012345.png",
            "@evil.example/Images/1.png",
            "//evil.example/Images/1.png",
            "https://evil.example/Images/1.png",
            "/Images/../secret.png",
            "/Images/..%2Fsecret.png",
            "/Images/sub/1.png",
            "/Images/1.png?x=1",
            "/Images/1.png#x",
            "/Images/1.svg",
            "/Other/1.png",
        ).forEach { path ->
            assertNull(path, RAWebApi.imageUrl(path))
        }
    }

    @Test
    fun imageDownloadsNeverAskForARefusedPathOrSendTheKey() {
        val refused = runBlocking { api(respond = respondWith(200, "x")).downloadImage("@evil.example/1.png") }
        assertTrue(refused.exceptionOrNull() is RAWebApiException.InvalidImagePath)
        assertNull(lastRequest)

        val downloaded = runBlocking { api(respond = respondWith(200, "image")).downloadImage("/Images/1.png") }
        assertEquals("image", String(downloaded.getOrThrow()))
        assertEquals("media.retroachievements.org", lastRequest!!.url.host)
        assertFalse(lastRequest!!.url.toString().contains(API_KEY))
    }

    @Test
    fun aPageInsteadOfAnImageIsAHostProblem() {
        listOf("text/html", "text/plain; charset=utf-8", "TEXT/HTML").forEach { contentType ->
            val result = runBlocking { api(respond = respondWith(200, "<html>Checking your browser", contentType)).downloadImage("/Images/1.png") }

            assertTrue(contentType, result.exceptionOrNull() is RAWebApiException.UnexpectedResponse)
        }
    }

    @Test
    fun otherContentTypesAreLeftForTheImageCheck() {
        listOf("image/png", "binary/octet-stream").forEach { contentType ->
            val result = runBlocking { api(respond = respondWith(200, "bytes", contentType)).downloadImage("/Images/1.png") }

            assertEquals(contentType, "bytes", String(result.getOrThrow()))
        }
    }

    @Test
    fun aFailedImageDownloadIsAFailure() {
        val result = runBlocking { api(respond = respondWith(404)).downloadImage("/Images/1.png") }
        assertEquals(404, (result.exceptionOrNull() as RAWebApiException.HttpError).code)
    }
}
