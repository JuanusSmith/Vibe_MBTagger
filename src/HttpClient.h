// HttpClient.h - WinHTTP-based HTTPS client for API calls
#pragma once
#include "pch.h"

struct HttpResponse {
    int         statusCode = 0;
    std::string body;
    bool        ok() const { return statusCode >= 200 && statusCode < 300; }
};

class HttpClient {
public:
    // Synchronous GET request. Returns body as UTF-8.
    static HttpResponse Get(const std::wstring& url,
                            const std::vector<std::wstring>& extraHeaders = {})
    {
        HttpResponse resp;

        // Parse URL
        URL_COMPONENTSW uc = {};
        uc.dwStructSize = sizeof(uc);
        wchar_t scheme[16]{}, host[256]{}, path[2048]{};
        uc.lpszScheme   = scheme; uc.dwSchemeLength   = 16;
        uc.lpszHostName = host;   uc.dwHostNameLength = 256;
        uc.lpszUrlPath  = path;   uc.dwUrlPathLength  = 2048;
        if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) return resp;

        HINTERNET hSession = WinHttpOpen(
            L"MBTagger/2.0",
            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            WINHTTP_NO_PROXY_NAME,
            WINHTTP_NO_PROXY_BYPASS,
            0);
        if (!hSession) return resp;

        HINTERNET hConn = WinHttpConnect(hSession, host, uc.nPort, 0);
        if (!hConn) { WinHttpCloseHandle(hSession); return resp; }

        DWORD flags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
        HINTERNET hReq = WinHttpOpenRequest(
            hConn, L"GET", path,
            nullptr, WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES,
            flags);

        if (!hReq) {
            WinHttpCloseHandle(hConn);
            WinHttpCloseHandle(hSession);
            return resp;
        }

        // Add extra headers
        for (const auto& h : extraHeaders)
            WinHttpAddRequestHeaders(hReq, h.c_str(), DWORD(-1), WINHTTP_ADDREQ_FLAG_ADD);

        if (WinHttpSendRequest(hReq, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                               WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
            WinHttpReceiveResponse(hReq, nullptr))
        {
            DWORD statusDW = 0;
            DWORD statusLen = sizeof(statusDW);
            WinHttpQueryHeaders(hReq,
                WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                WINHTTP_HEADER_NAME_BY_INDEX,
                &statusDW, &statusLen, WINHTTP_NO_HEADER_INDEX);
            resp.statusCode = static_cast<int>(statusDW);

            DWORD bytesAvail = 0;
            while (WinHttpQueryDataAvailable(hReq, &bytesAvail) && bytesAvail > 0) {
                std::string chunk(bytesAvail, '\0');
                DWORD bytesRead = 0;
                WinHttpReadData(hReq, chunk.data(), bytesAvail, &bytesRead);
                resp.body.append(chunk.data(), bytesRead);
            }
        }

        WinHttpCloseHandle(hReq);
        WinHttpCloseHandle(hConn);
        WinHttpCloseHandle(hSession);
        return resp;
    }

    // URL-encode a string (UTF-8 percent-encoding)
    static std::wstring UrlEncode(const std::wstring& s) {
        // Convert to UTF-8 first
        int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, nullptr, 0, nullptr, nullptr);
        std::string utf8(n - 1, '\0');
        WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, utf8.data(), n, nullptr, nullptr);

        std::wstring out;
        out.reserve(utf8.size() * 3);
        for (unsigned char c : utf8) {
            if (isalnum(c) || c=='-'||c=='_'||c=='.'||c=='~') {
                out += wchar_t(c);
            } else {
                wchar_t buf[8];
                swprintf_s(buf, L"%%%02X", c);
                out += buf;
            }
        }
        return out;
    }
};
