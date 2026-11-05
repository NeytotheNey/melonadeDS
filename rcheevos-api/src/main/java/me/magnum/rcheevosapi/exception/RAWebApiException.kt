package me.magnum.rcheevosapi.exception

/**
 * The ways a Web API call can fail. Request URLs carry the user's key, so these never hold a URL, a response body or a cause that might
 * (OkHttp's own exceptions are reduced to their type).
 */
sealed class RAWebApiException(message: String) : Exception(message) {
    class MissingKey : RAWebApiException("No Web API key is set")
    class InvalidKey : RAWebApiException("RetroAchievements rejected the Web API key")
    class RateLimited : RAWebApiException("RetroAchievements asked to slow down")
    class HttpError(val code: Int) : RAWebApiException("Request failed with HTTP $code")
    class NetworkError(errorType: String) : RAWebApiException("Network error: $errorType")
    /** A game's data couldn't be read, while the response itself was what it should be. */
    class MalformedResponse : RAWebApiException("The response could not be read")
    /** Whatever answered didn't send what was asked for at all, as with a maintenance or block page. */
    class UnexpectedResponse : RAWebApiException("The response was not what was asked for")
    class InvalidImagePath : RAWebApiException("The image path is not a RetroAchievements image")
    class ImageTooLarge : RAWebApiException("The image is too large")
}
