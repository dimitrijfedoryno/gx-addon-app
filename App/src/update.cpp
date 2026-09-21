#include "update.h"
#include "version.h"

#include <winhttp.h>
#include <shellapi.h>
#include <vector>
#include <algorithm>
#include <cstdlib>

#pragma comment(lib, "winhttp.lib")

static const wchar_t* kUserAgent = L"GXMonitor-Updater/1.0";

// ---------------------------------------------------------------------------
// Small string helpers
// ---------------------------------------------------------------------------
static std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) return std::string();
    int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), NULL, 0, NULL, NULL);
    std::string s(len, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], len, NULL, NULL);
    return s;
}

static std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return std::wstring();
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), NULL, 0);
    std::wstring w(len, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], len);
    return w;
}

static std::string WideToAnsi(const std::wstring& w) {
    if (w.empty()) return std::string();
    int len = WideCharToMultiByte(CP_ACP, 0, w.c_str(), (int)w.size(), NULL, 0, NULL, NULL);
    std::string s(len, 0);
    WideCharToMultiByte(CP_ACP, 0, w.c_str(), (int)w.size(), &s[0], len, NULL, NULL);
    return s;
}

// Extracts the string value of `"key":"value"` from raw JSON text, starting
// the search at `fromPos`. Handles the couple of escape sequences GitHub's
// API actually emits (\" \\ \/). Good enough for our narrow, controlled
// schema - not a general JSON parser.
static bool ExtractJsonString(const std::string& json, const std::string& key,
                              std::string& out, size_t fromPos = 0) {
    std::string needle = "\"" + key + "\"";
    size_t pos = json.find(needle, fromPos);
    if (pos == std::string::npos) return false;
    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) return false;
    pos = json.find('"', pos);
    if (pos == std::string::npos) return false;
    size_t i = pos + 1;
    std::string result;
    while (i < json.size() && json[i] != '"') {
        if (json[i] == '\\' && i + 1 < json.size()) {
            result += json[i + 1];
            i += 2;
            continue;
        }
        result += json[i];
        i++;
    }
    if (i >= json.size()) return false;
    out = result;
    return true;
}

// ---------------------------------------------------------------------------
// Version comparison
// ---------------------------------------------------------------------------
static std::vector<int> SplitVersion(const std::wstring& v) {
    std::vector<int> parts;
    std::wstring cur;
    for (wchar_t c : v) {
        if (c == L'.') {
            parts.push_back(cur.empty() ? 0 : _wtoi(cur.c_str()));
            cur.clear();
        } else if (iswdigit(c)) {
            cur += c;
        } else {
            break; // stop at any non-numeric suffix (e.g. "-beta")
        }
    }
    parts.push_back(cur.empty() ? 0 : _wtoi(cur.c_str()));
    return parts;
}

bool is_newer_version(const std::wstring& a, const std::wstring& b) {
    std::vector<int> pa = SplitVersion(a), pb = SplitVersion(b);
    size_t n = std::max(pa.size(), pb.size());
    for (size_t i = 0; i < n; i++) {
        int va = i < pa.size() ? pa[i] : 0;
        int vb = i < pb.size() ? pb[i] : 0;
        if (va != vb) return va > vb;
    }
    return false;
}

// ---------------------------------------------------------------------------
// WinHTTP plumbing
// ---------------------------------------------------------------------------
static bool CrackUrl(const std::wstring& url, std::wstring& host, std::wstring& path,
                     INTERNET_PORT& port, bool& secure) {
    wchar_t hostBuf[256]{}, pathBuf[2048]{}, extraBuf[1024]{};
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);
    uc.lpszHostName = hostBuf; uc.dwHostNameLength = 256;
    uc.lpszUrlPath = pathBuf; uc.dwUrlPathLength = 2048;
    uc.lpszExtraInfo = extraBuf; uc.dwExtraInfoLength = 1024;
    if (!WinHttpCrackUrl(url.c_str(), (DWORD)url.size(), 0, &uc)) return false;
    host = hostBuf;
    path = std::wstring(pathBuf) + extraBuf;
    port = uc.nPort;
    secure = (uc.nScheme == INTERNET_SCHEME_HTTPS);
    return true;
}

// GET a small text response (used for the GitHub API call).
static std::string HttpsGetText(const std::wstring& host, const std::wstring& path,
                                DWORD& outStatus, const wchar_t* extraHeaders) {
    std::string body;
    outStatus = 0;

    HINTERNET hSession = WinHttpOpen(kUserAgent, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                     WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return body;
    HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return body; }
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path.c_str(), NULL,
                                            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                            WINHTTP_FLAG_SECURE);
    if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return body; }

    if (extraHeaders)
        WinHttpAddRequestHeaders(hRequest, extraHeaders, (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD);

    BOOL ok = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                 WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    if (ok) ok = WinHttpReceiveResponse(hRequest, NULL);
    if (ok) {
        DWORD statusCode = 0, sz = sizeof(statusCode);
        WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &sz, WINHTTP_NO_HEADER_INDEX);
        outStatus = statusCode;

        DWORD avail = 0;
        do {
            avail = 0;
            if (!WinHttpQueryDataAvailable(hRequest, &avail) || avail == 0) break;
            std::vector<char> buf(avail);
            DWORD read = 0;
            if (!WinHttpReadData(hRequest, buf.data(), avail, &read)) break;
            body.append(buf.data(), read);
        } while (avail > 0);
    }
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return body;
}

bool check_latest_release(UpdateInfo& out) {
    DWORD status = 0;
    std::wstring path = std::wstring(L"/repos/") + GX_UPDATE_OWNER + L"/" + GX_UPDATE_REPO + L"/releases/latest";
    std::string body = HttpsGetText(L"api.github.com", path, status,
                                    L"Accept: application/vnd.github+json\r\n");
    if (status != 200 || body.empty()) return false;

    std::string tag;
    if (!ExtractJsonString(body, "tag_name", tag)) return false;
    size_t skip = (!tag.empty() && (tag[0] == 'v' || tag[0] == 'V')) ? 1 : 0;
    std::wstring latest = Utf8ToWide(tag.substr(skip));

    if (!is_newer_version(latest, GX_APP_VERSION)) return false;

    std::string assetNameNeedle = "\"" + WideToUtf8(std::wstring(GX_UPDATE_ASSET)) + "\"";
    size_t namePos = body.find(assetNameNeedle);
    if (namePos == std::string::npos) return false;

    std::string url;
    if (!ExtractJsonString(body, "browser_download_url", url, namePos)) return false;

    out.version = latest;
    out.exeUrl = Utf8ToWide(url);
    return true;
}

bool download_update(const std::wstring& url, const std::wstring& destPath,
                     const std::function<void(long long, long long)>& onProgress) {
    std::wstring host, path; INTERNET_PORT port; bool secure;
    if (!CrackUrl(url, host, path, port, secure)) return false;

    HINTERNET hSession = WinHttpOpen(kUserAgent, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                     WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return false;
    HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), port, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return false; }
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path.c_str(), NULL,
                                            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                            secure ? WINHTTP_FLAG_SECURE : 0);
    if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return false; }

    bool ok = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                 WINHTTP_NO_REQUEST_DATA, 0, 0, 0) != FALSE;
    if (ok) ok = WinHttpReceiveResponse(hRequest, NULL) != FALSE;

    bool success = false;
    if (ok) {
        DWORD statusCode = 0, sz = sizeof(statusCode);
        WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &sz, WINHTTP_NO_HEADER_INDEX);
        if (statusCode == 200) {
            long long total = -1;
            wchar_t lenBuf[32]; DWORD lenBufSz = sizeof(lenBuf);
            if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX,
                                    lenBuf, &lenBufSz, WINHTTP_NO_HEADER_INDEX)) {
                total = _wtoi64(lenBuf);
            }
            HANDLE hFile = CreateFileW(destPath.c_str(), GENERIC_WRITE, 0, NULL,
                                       CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
            if (hFile != INVALID_HANDLE_VALUE) {
                long long got = 0;
                std::vector<char> buf(65536);
                DWORD avail = 0;
                bool ioOk = true;
                do {
                    avail = 0;
                    if (!WinHttpQueryDataAvailable(hRequest, &avail)) { ioOk = false; break; }
                    if (avail == 0) break;
                    DWORD toRead = (std::min)(avail, (DWORD)buf.size());
                    DWORD read = 0;
                    if (!WinHttpReadData(hRequest, buf.data(), toRead, &read)) { ioOk = false; break; }
                    if (read == 0) break;
                    DWORD written = 0;
                    if (!WriteFile(hFile, buf.data(), read, &written, NULL) || written != read) {
                        ioOk = false; break;
                    }
                    got += read;
                    if (onProgress) onProgress(got, total);
                } while (avail > 0);
                CloseHandle(hFile);
                success = ioOk && got > 0;
            }
        }
    }
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    if (!success) DeleteFileW(destPath.c_str());
    return success;
}

bool apply_update_and_restart(const std::wstring& newExePath) {
    wchar_t exePathBuf[MAX_PATH]{};
    if (!GetModuleFileNameW(NULL, exePathBuf, MAX_PATH)) return false;
    std::wstring currentExe = exePathBuf;

    wchar_t tempDir[MAX_PATH]{};
    GetTempPathW(MAX_PATH, tempDir);
    std::wstring batPath = std::wstring(tempDir) + L"gx_update.bat";

    // The running exe can't overwrite itself, so a tiny detached helper
    // script waits for this process to release the file (retrying the move
    // until it succeeds), swaps the new build into place, relaunches it and
    // deletes itself.
    std::wstring script =
        L"@echo off\r\n"
        L":retry\r\n"
        L"move /Y \"" + newExePath + L"\" \"" + currentExe + L"\" >nul 2>nul\r\n"
        L"if errorlevel 1 (\r\n"
        L"  timeout /t 1 /nobreak >nul\r\n"
        L"  goto retry\r\n"
        L")\r\n"
        L"start \"\" \"" + currentExe + L"\"\r\n"
        L"del \"%~f0\"\r\n";

    HANDLE hFile = CreateFileW(batPath.c_str(), GENERIC_WRITE, 0, NULL,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return false;
    std::string ansi = WideToAnsi(script);
    DWORD written = 0;
    bool wrote = WriteFile(hFile, ansi.data(), (DWORD)ansi.size(), &written, NULL) != FALSE;
    CloseHandle(hFile);
    if (!wrote) return false;

    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = L"open";
    sei.lpFile = L"cmd.exe";
    std::wstring args = L"/c \"" + batPath + L"\"";
    sei.lpParameters = args.c_str();
    sei.nShow = SW_HIDE;
    if (!ShellExecuteExW(&sei)) return false;
    if (sei.hProcess) CloseHandle(sei.hProcess);
    return true;
}
