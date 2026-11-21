// A small HTTP(S) client on WinHTTP (a system DLL), for the library's metadata and art (MetadataSync.h): GET only, synchronous, for worker
// threads only. It has timeouts, follows redirects when asked (never from HTTPS to HTTP, WinHTTP's default), caps the body's size, sends a
// generic User-Agent (melonadeDS-desktop/<version>, nothing about the user), and can be cancelled from another thread for shutdown or a game
// starting. URLs can hold a key (RetroAchievements' Web API key is a query parameter), so nothing here logs or returns them
#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace Frontend::Http
{

struct Request
{
    std::string Url;               // UTF-8, already encoded (UrlEncode for the parts that need it)
    bool FollowRedirects = true;   // false: a 3xx comes back as it is (the RetroAchievements Web API, whose URLs hold the key)
    size_t MaxBytes = 16u << 20;   // a longer body fails with TooLarge
    int TimeoutMs = 20000;         // connecting, sending and each wait for data
    int TotalTimeoutMs = 120000;   // the whole request, so a server sending a byte at a time can't hold it for ever
};

struct Response
{
    enum class Error { None, BadUrl, Network, Timeout, Cancelled, TooLarge };
    Error Err = Error::None;
    int Status = 0;                // the HTTP status, 0 without a response
    std::string ContentType;       // as sent ("image/png", "text/html; charset=utf-8")
    std::vector<uint8_t> Body;
    unsigned long SystemError = 0; // the WinHTTP error behind Network or Timeout, for the log
    bool Ok() const { return Err == Error::None && Status == 200; }
    std::string BodyText() const { return std::string(Body.begin(), Body.end()); }
};

class Client
{
public:
    explicit Client(const std::string& userAgent);
    ~Client();
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;

    Response Get(const Request& request);

    // Every request in flight fails at once with Cancelled (their handles closed), and so does every later one until Resume
    void Cancel();
    void Resume();
    bool Cancelled() const { return Stopped.load(); }

private:
    void* Session = nullptr; // HINTERNET
    std::atomic<bool> Stopped = false;
    std::mutex Lock;
    std::set<void*> Active;  // the requests in flight (HINTERNET), which Cancel closes
};

// [text] percent-encoded for a path segment or a query value: everything but A-Z a-z 0-9 - _ . ~ ("Mario Kart DS (USA)" ->
// "Mario%20Kart%20DS%20%28USA%29")
std::string UrlEncode(const std::string& text);
// "Network error 12007", "Timed out", "HTTP 404", ... for the log (never the URL)
std::string Describe(const Response& response);

}
