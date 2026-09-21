#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string>
#include <functional>

struct UpdateInfo {
    std::wstring version;   // e.g. L"1.2.0" (no leading 'v')
    std::wstring exeUrl;    // direct download URL of the GXMonitor.exe asset
};

// true if `a` is a newer semantic version than `b` (numeric, dot-separated).
bool is_newer_version(const std::wstring& a, const std::wstring& b);

// Hits the GitHub "latest release" API. Returns true and fills `out` only
// when a release newer than GX_APP_VERSION is found with a GXMonitor.exe
// asset attached. Safe to call with no releases published yet (just returns
// false). Blocking - call from a background thread.
bool check_latest_release(UpdateInfo& out);

// Downloads `url` to `destPath`, overwriting it. `onProgress(got, total)` is
// called from the calling thread as bytes arrive; `total` is -1 if unknown.
// Blocking - call from a background thread.
bool download_update(const std::wstring& url, const std::wstring& destPath,
                      const std::function<void(long long got, long long total)>& onProgress);

// Schedules the currently-running exe to be replaced by `newExePath` and
// relaunched (via a small detached helper script that waits for this
// process to exit, since a running exe can't overwrite itself), then
// returns. The caller must exit the process right after this returns true.
bool apply_update_and_restart(const std::wstring& newExePath);
