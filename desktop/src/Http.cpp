#include "Http.h"

#include <windows.h>
#include <winhttp.h>

#include <chrono>
#include <cstdio>

// newer than some MinGW headers
#ifndef WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY
#define WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY 4
#endif
#ifndef WINHTTP_OPTION_DECOMPRESSION
#define WINHTTP_OPTION_DECOMPRESSION 118
#define WINHTTP_DECOMPRESSION_FLAG_GZIP 0x00000001
#define WINHTTP_DECOMPRESSION_FLAG_DEFLATE 0x00000002
#endif

namespace Frontend::Http
{

namespace
{

std::wstring Widen(const std::string& text)
{
    if (text.empty())
        return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, text.data(), (int)text.size(), nullptr, 0);
    std::wstring out(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), (int)text.size(), out.data(), n);
    return out;
}

std::string Narrow(const std::wstring& text)
{
    if (text.empty())
        return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), nullptr, 0, nullptr, nullptr);
    std::string out(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), out.data(), n, nullptr, nullptr);
    return out;
}

}

Client::Client(const std::string& userAgent)
{
    // the system's proxy settings (Windows 8.1 and up), else WinHTTP's own
    Session = WinHttpOpen(Widen(userAgent).c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!Session)
        Session = WinHttpOpen(Widen(userAgent).c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!Session)
        fprintf(stderr, "http: WinHttpOpen failed (%lu)\n", GetLastError());
}

Client::~Client()
{
    Cancel();
    if (Session)
        WinHttpCloseHandle(Session);
}

void Client::Cancel()
{
    std::lock_guard guard(Lock);
    Stopped = true;
    // closing a request's handle makes the call waiting on it return at once (ERROR_WINHTTP_OPERATION_CANCELLED); its owner sees it gone
    // from Active and doesn't close it again
    for (void* h : Active)
        WinHttpCloseHandle(h);
    Active.clear();
}

void Client::Resume()
{
    std::lock_guard guard(Lock);
    Stopped = false;
}

Response Client::Get(const Request& request)
{
    Response r;
    if (Stopped)
    {
        r.Err = Response::Error::Cancelled;
        return r;
    }
    if (!Session)
    {
        r.Err = Response::Error::Network;
        return r;
    }

    std::wstring url = Widen(request.Url);
    URL_COMPONENTS parts {};
    parts.dwStructSize = sizeof(parts);
    wchar_t host[256], path[4096], extra[4096];
    parts.lpszHostName = host;
    parts.dwHostNameLength = 256;
    parts.lpszUrlPath = path;
    parts.dwUrlPathLength = 4096;
    parts.lpszExtraInfo = extra;
    parts.dwExtraInfoLength = 4096;
    if (url.empty() || !WinHttpCrackUrl(url.c_str(), (DWORD)url.size(), 0, &parts)
        || (parts.nScheme != INTERNET_SCHEME_HTTP && parts.nScheme != INTERNET_SCHEME_HTTPS))
    {
        r.Err = Response::Error::BadUrl;
        return r;
    }
    bool secure = parts.nScheme == INTERNET_SCHEME_HTTPS;
    std::wstring object = std::wstring(path, parts.dwUrlPathLength) + std::wstring(extra, parts.dwExtraInfoLength);
    if (object.empty())
        object = L"/";

    HINTERNET connect = WinHttpConnect(Session, std::wstring(host, parts.dwHostNameLength).c_str(), parts.nPort, 0);
    if (!connect)
    {
        r.Err = Response::Error::Network;
        r.SystemError = GetLastError();
        return r;
    }
    HINTERNET req = WinHttpOpenRequest(connect, L"GET", object.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
        secure ? WINHTTP_FLAG_SECURE : 0);
    if (!req)
    {
        r.Err = Response::Error::Network;
        r.SystemError = GetLastError();
        WinHttpCloseHandle(connect);
        return r;
    }
    {
        std::lock_guard guard(Lock);
        if (Stopped)
        {
            WinHttpCloseHandle(req);
            WinHttpCloseHandle(connect);
            r.Err = Response::Error::Cancelled;
            return r;
        }
        Active.insert(req);
    }
    int t = request.TimeoutMs;
    WinHttpSetTimeouts(req, t, t, t, t);
    if (!request.FollowRedirects)
    {
        DWORD off = WINHTTP_DISABLE_REDIRECTS;
        WinHttpSetOption(req, WINHTTP_OPTION_DISABLE_FEATURE, &off, sizeof(off));
    }
    // (Windows 8.1 and up; an older one just gets the body as it is)
    DWORD decompress = WINHTTP_DECOMPRESSION_FLAG_GZIP | WINHTTP_DECOMPRESSION_FLAG_DEFLATE;
    WinHttpSetOption(req, WINHTTP_OPTION_DECOMPRESSION, &decompress, sizeof(decompress));

    auto fail = [&](Response::Error error)
    {
        DWORD code = GetLastError();
        r.Err = Stopped || code == ERROR_WINHTTP_OPERATION_CANCELLED ? Response::Error::Cancelled
            : code == ERROR_WINHTTP_TIMEOUT ? Response::Error::Timeout : error;
        r.SystemError = code;
    };
    bool ok = WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) && WinHttpReceiveResponse(req, nullptr);
    if (!ok)
        fail(Response::Error::Network);
    if (ok)
    {
        DWORD status = 0, size = sizeof(status);
        if (WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size,
                WINHTTP_NO_HEADER_INDEX))
            r.Status = (int)status;
        wchar_t type[256];
        size = sizeof(type);
        if (WinHttpQueryHeaders(req, WINHTTP_QUERY_CONTENT_TYPE, WINHTTP_HEADER_NAME_BY_INDEX, type, &size, WINHTTP_NO_HEADER_INDEX))
            r.ContentType = Narrow(std::wstring(type, size / sizeof(wchar_t)));
        // the body, up to the cap and the deadline
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(request.TotalTimeoutMs);
        for (;;)
        {
            if (std::chrono::steady_clock::now() > deadline)
            {
                r.Err = Response::Error::Timeout;
                break;
            }
            DWORD available = 0;
            if (!WinHttpQueryDataAvailable(req, &available))
            {
                fail(Response::Error::Network);
                break;
            }
            if (available == 0)
                break;
            if (r.Body.size() + available > request.MaxBytes)
            {
                r.Err = Response::Error::TooLarge;
                break;
            }
            size_t at = r.Body.size();
            r.Body.resize(at + available);
            DWORD read = 0;
            if (!WinHttpReadData(req, r.Body.data() + at, available, &read))
            {
                r.Body.resize(at);
                fail(Response::Error::Network);
                break;
            }
            r.Body.resize(at + read);
            if (read == 0)
                break;
        }
    }
    {
        std::lock_guard guard(Lock);
        // still ours unless Cancel closed it
        if (Active.erase(req))
            WinHttpCloseHandle(req);
        else if (r.Err == Response::Error::None)
            r.Err = Response::Error::Cancelled;
    }
    WinHttpCloseHandle(connect);
    if (r.Err != Response::Error::None)
        r.Body.clear();
    return r;
}

std::string UrlEncode(const std::string& text)
{
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : text)
    {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~')
            out += (char)c;
        else
        {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}

std::string Describe(const Response& r)
{
    char text[64];
    switch (r.Err)
    {
    case Response::Error::None: snprintf(text, sizeof(text), "HTTP %d", r.Status); break;
    case Response::Error::BadUrl: return "a bad URL";
    case Response::Error::Network: snprintf(text, sizeof(text), "network error %lu", r.SystemError); break;
    case Response::Error::Timeout: return "timed out";
    case Response::Error::Cancelled: return "cancelled";
    case Response::Error::TooLarge: return "too large";
    }
    return text;
}

}
