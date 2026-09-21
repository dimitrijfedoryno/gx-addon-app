#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#include "ui.h"
#include "watch.h"
#include "app.h"
#include "update.h"
#include "version.h"
#include "..\res\resource.h"

#include <windowsx.h>
#include <commctrl.h>
#include <shellapi.h>
#include <dwmapi.h>
#include <shlwapi.h>
#include <uxtheme.h>
#include <algorithm>
#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <cstdio>
#include <objidl.h>
#include <gdiplus.h>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

// ---------------------------------------------------------------------------
// Palette (TradeSkillMaster-inspired dark graphite + gold)
// ---------------------------------------------------------------------------
static const COLORREF C_BG     = RGB(0x15, 0x18, 0x1E);
static const COLORREF C_PANEL  = RGB(0x1D, 0x22, 0x2B);
static const COLORREF C_EDGE   = RGB(0x2D, 0x35, 0x43);
static const COLORREF C_GOLD   = RGB(0xE8, 0xC0, 0x5F);
static const COLORREF C_TXT    = RGB(0xEA, 0xE8, 0xE3);
static const COLORREF C_DIM    = RGB(0x8C, 0x93, 0x9F);
static const COLORREF C_FAINT  = RGB(0x59, 0x60, 0x6E);
static const COLORREF C_BTN    = RGB(0x25, 0x2B, 0x36);
static const COLORREF C_BTNH   = RGB(0x31, 0x3A, 0x49);
static const COLORREF C_BTNP   = RGB(0x1E, 0x23, 0x2C);
static const COLORREF C_GRN    = RGB(0x4C, 0xD9, 0x64);
static const COLORREF C_AMB    = RGB(0xE8, 0xC0, 0x5F);
static const COLORREF C_RED    = RGB(0xE0, 0x5C, 0x5C);
static const COLORREF C_EDITBG = RGB(0x20, 0x24, 0x2E);
static const COLORREF C_GOLDMUTE = RGB(0xB0, 0x92, 0x54);
static const COLORREF C_KNOB   = RGB(0xF5, 0xF3, 0xEE);
static const COLORREF C_CAPHOV  = RGB(0x2A, 0x30, 0x3B);
static const COLORREF C_CAPICON = RGB(0x9A, 0xA1, 0xAC);

#define WM_APP_GOLD (WM_APP + 1)
#define WM_APP_ICON (WM_APP + 2)
#define WM_APP_UPDATE_PROGRESS (WM_APP + 3)
#define WM_APP_UPDATE_DONE (WM_APP + 4)

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
static HINSTANCE g_hinst = NULL;
static HWND g_hwnd = NULL;
static HANDLE g_singleMutex = NULL;
static Watcher g_watcher;
static Settings g_settings;
static std::wstring g_exeDir;

static std::mutex g_stateMu;
static int64_t g_total = -1;
static std::wstring g_formatted;
static std::wstring g_status;
static int g_statusKind = STATUS_WAITING;
static GoldBreakdown g_bd;

static bool g_paused = false;
static int g_hover = -1;
static int g_btnDown = -1;
static int g_dpi = 96;
static double g_sizeScale = 1.0;

// Custom title bar (no default Windows caption).
static int g_capH = 36;
static RECT g_capMinR{}, g_capCloseR{};
static int g_hoverCap = -1;
static int g_capDown = -1;

// Left icon nav rail (Home / Settings), full height below the caption.
static int g_sidebarW = 52;
static RECT g_navHomeR{}, g_navGearR{};
static int g_hoverNav = -1;
static int g_navDown = -1;

// In-app updater. The badge sits just under the caption row, right-aligned.
static std::mutex g_updateMu;
static bool g_updateAvailable = false;
static bool g_updating = false;
static bool g_updateFailed = false;
static UpdateInfo g_updateInfo;
static std::wstring g_updateStatus;
static RECT g_updateBadgeR{};
static bool g_hoverUpdate = false;
static bool g_updateDown = false;

static bool g_windowActive = true;

static HICON g_icon32 = NULL;
static HICON g_icon16 = NULL;
static HICON g_iconTray = NULL;

// Checkbox check-state for the (owner-drawn) settings checkboxes. Pure
// BS_OWNERDRAW buttons don't retain check state via BM_*, so track here.
static bool g_chkRaw = false, g_chkMin = false, g_chkRun = false;

static HBRUSH g_brDlg = NULL;
static HBRUSH g_brEdit = NULL;
static HFONT g_fntUi = NULL;

enum View { VIEW_DASHBOARD = 0, VIEW_SETTINGS = 1 };
static View g_view = VIEW_DASHBOARD;

// Dashboard bottom-row button actions. Settings now lives in the sidebar.
enum { ACT_NONE = -1, ACT_PAUSE = 0, ACT_OPEN = 1, ACT_REFRESH = 2 };

// Settings view child controls
enum {
    IDC_ED_FILE   = 2001, IDC_ED_ACCT = 2002, IDC_ED_OUT = 2003, IDC_ED_POLL = 2004,
    IDC_CHK_RAW   = 2005, IDC_CHK_MIN = 2006, IDC_CHK_RUN = 2007,
    IDC_BTN_BROWSE_GX = 2008, IDC_BTN_BROWSE_OUT = 2009, IDC_BTN_DETECT = 2010,
    IDC_BTN_SAVE  = 2011,
    IDC_LBL_FILE  = 2013, IDC_LBL_ACCT = 2014, IDC_LBL_OUT = 2015, IDC_LBL_POLL = 2016,
    IDC_ED_OUTPATH = 2017, IDC_BTN_COPY = 2018
};

// Forward declarations
static void ApplySettings();
static void UpdateOutputPathText();
static void ShowSettingsView();
static void HideSettingsView();
static void QuitApp();

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------
static int GetDpiOf(HWND h) {
    UINT d = GetDpiForWindow(h);
    return d ? (int)d : 96;
}

static void EnableDarkMode(HWND h) {
    BOOL on = TRUE;
    DwmSetWindowAttribute(h, 20, &on, sizeof(on)); // DWMWA_USE_IMMERSIVE_DARK_MODE
    DWORD corner = 2; // DWMWCP_ROUND
    DwmSetWindowAttribute(h, 33, &corner, sizeof(corner)); // DWMWA_WINDOW_CORNER_PREFERENCE
    DWORD ncrp = 1; // DWMNCRP_DISABLED
    DwmSetWindowAttribute(h, 2, &ncrp, sizeof(ncrp)); // DWMWA_NCRENDERING_POLICY
    MARGINS m{ 0, 0, 0, 1 };
    DwmExtendFrameIntoClientArea(h, &m); // keep a native drop shadow on the borderless window
}

// DWM's own 1px window-edge accent (independent of WM_NCPAINT/our custom
// caption) otherwise falls back to the system's default light/white
// inactive-window border, clashing with the dark theme. Drive it ourselves:
// gold while focused, hidden while not. Windows 11 only (22H2+); silently
// no-ops on older systems.
static void SetBorderActive(HWND h, bool active) {
    COLORREF col = active ? C_GOLD : (COLORREF)0xFFFFFFFE /* DWMWA_COLOR_NONE */;
    DwmSetWindowAttribute(h, 34, &col, sizeof(col)); // DWMWA_BORDER_COLOR
}

static bool IsWndMaximized(HWND h) { return IsZoomed(h) != FALSE; }

// Combined DPI + window-size scale factor: layout, fonts and paddings all
// shrink/grow together so the UI stays legible from the minimum window size
// up through large/high-DPI windows, not just across DPI changes.
static double ComputeSizeScale(HWND hwnd) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    double dpiS = GetDpiOf(hwnd) / 96.0;
    double designW = 520.0 * dpiS;
    double designH = 470.0 * dpiS;
    double w = (double)std::max(1L, rc.right);
    double h = (double)std::max(1L, rc.bottom);
    double f = std::min(w / designW, h / designH);
    return std::clamp(f, 0.72, 1.6);
}

static double UiScale(HWND hwnd) {
    return (GetDpiOf(hwnd) / 96.0) * ComputeSizeScale(hwnd);
}

// Caption bar height/buttons scale with DPI only (not window-size scale) so
// they stay a consistent, clickable physical size even when the window is
// shrunk down.
static void ComputeCaptionRects(HWND hwnd) {
    RECT full;
    GetClientRect(hwnd, &full);
    double s = GetDpiOf(hwnd) / 96.0;
    g_capH = (int)(36 * s);
    int btnW = (int)(46 * s);
    // No maximize button: the window can't be maximized/full-screened from
    // the custom title bar, so only minimize and close remain.
    g_capCloseR = { full.right - btnW, 0, full.right, g_capH };
    g_capMinR   = { g_capCloseR.left - btnW, 0, g_capCloseR.left, g_capH };

    // Update badge: a small pill just under the caption row, right-aligned.
    int upW = (int)(84 * s), upH = (int)(24 * s);
    int upTop = g_capH + (int)(8 * s);
    g_updateBadgeR = { full.right - (int)(12 * s) - upW, upTop, full.right - (int)(12 * s), upTop + upH };

    // Left icon nav rail: Home (dashboard) and Settings (gear), stacked near
    // the top, full-height sidebar drawn separately in DrawSidebar.
    g_sidebarW = (int)(52 * s);
    int navSz = (int)(40 * s);
    int navGap = (int)(6 * s);
    int navX = (g_sidebarW - navSz) / 2;
    int navY0 = g_capH + (int)(10 * s);
    g_navHomeR = { navX, navY0, navX + navSz, navY0 + navSz };
    g_navGearR = { navX, navY0 + navSz + navGap, navX + navSz, navY0 + 2 * navSz + navGap };
}

static int CapHitTest(int x, int y) {
    POINT p{ x, y };
    if (PtInRect(&g_capMinR, p)) return 0;
    if (PtInRect(&g_capCloseR, p)) return 2;
    return -1;
}

static bool UpdateBadgeHit(int x, int y) {
    if (!g_updateAvailable || g_updating) return false;
    POINT p{ x, y };
    return PtInRect(&g_updateBadgeR, p) != FALSE;
}

static int NavHitTest(int x, int y) {
    POINT p{ x, y };
    if (PtInRect(&g_navHomeR, p)) return 0;
    if (PtInRect(&g_navGearR, p)) return 1;
    return -1;
}

// ---------------------------------------------------------------------------
// Fonts
// ---------------------------------------------------------------------------
static HFONT g_fCaption = NULL, g_fBig = NULL, g_fValue = NULL,
             g_fSub = NULL, g_fTiny = NULL, g_fBtn = NULL;
static int g_fontDpi = 0;

static HFONT MakeFontPt(int pt, int weight) {
    int px = (int)((pt * (double)g_dpi) / 72.0 * g_sizeScale + 0.5);
    return CreateFontW(-px, 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET,
                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}

static int g_fontScaleKey = -1;

static void EnsureFonts(HWND hwnd) {
    int dpi = GetDpiOf(hwnd);
    double sizeScale = ComputeSizeScale(hwnd);
    int key = dpi * 1000 + (int)(sizeScale * 100 + 0.5);
    if (g_fontScaleKey == key) return;
    g_dpi = dpi;
    g_sizeScale = sizeScale;
    if (g_fontScaleKey != -1) {
        DeleteObject(g_fCaption); DeleteObject(g_fBig);
        DeleteObject(g_fValue);  DeleteObject(g_fSub);
        DeleteObject(g_fTiny);   DeleteObject(g_fBtn);
    }
    g_fCaption = MakeFontPt(9, FW_SEMIBOLD);
    g_fBig     = MakeFontPt(30, FW_BOLD);
    g_fValue   = MakeFontPt(15, FW_SEMIBOLD);
    g_fSub     = MakeFontPt(8, FW_NORMAL);
    g_fTiny    = MakeFontPt(8, FW_NORMAL);
    g_fBtn     = MakeFontPt(9, FW_NORMAL);
    g_fontScaleKey = key;
    g_fontDpi = dpi;
}

static std::wstring TimeHMS() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t buf[32] = L"";
    swprintf(buf, 32, L"%02d:%02d:%02d", st.wHour, st.wMinute, st.wSecond);
    return buf;
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------
struct Layout {
    RECT bigCard, bigCap, bigVal, bigSub;
    RECT cardA, cardALbl, cardAVal, cardATiny;
    RECT cardB, cardBLbl, cardBVal, cardBTiny;
    RECT statusL, statusR;
    RECT btnPause, btnRefresh, btnOpen;
};
static Layout g_layout;

static void ComputeLayout(HWND hwnd) {
    ComputeCaptionRects(hwnd);
    RECT rc;
    GetClientRect(hwnd, &rc);
    double s = UiScale(hwnd);
    int W = rc.right, H = rc.bottom;
    int pad = (int)(14 * s);
    int capH = g_capH;
    int left = g_sidebarW + pad; // content starts right of the icon nav rail

    Layout L;
    L.bigCard = { left, capH + (int)(12 * s), W - pad, capH + (int)(128 * s) };
    L.bigCap  = { L.bigCard.left + (int)(20 * s), L.bigCard.top + (int)(12 * s),
                  L.bigCard.right - (int)(20 * s), L.bigCard.top + (int)(30 * s) };
    L.bigVal  = { L.bigCap.left, L.bigCard.top + (int)(28 * s),
                  L.bigCap.right, L.bigCard.top + (int)(86 * s) };
    L.bigSub  = { L.bigCap.left, L.bigCard.top + (int)(88 * s),
                  L.bigCap.right, L.bigCard.bottom - (int)(12 * s) };

    int gap = (int)(12 * s);
    int cw  = (W - left - pad - gap) / 2;
    int cy  = L.bigCard.bottom + (int)(12 * s);
    L.cardA = { left, cy, left + cw, cy + (int)(76 * s) };
    L.cardB = { left + cw + gap, cy, left + cw + gap + cw, cy + (int)(76 * s) };
    L.cardALbl  = { L.cardA.left + (int)(16 * s), L.cardA.top + (int)(10 * s),
                    L.cardA.right - (int)(12 * s), L.cardA.top + (int)(26 * s) };
    L.cardAVal  = { L.cardALbl.left, L.cardA.top + (int)(24 * s),
                    L.cardA.right - (int)(12 * s), L.cardA.top + (int)(56 * s) };
    L.cardATiny = { L.cardALbl.left, L.cardA.top + (int)(56 * s),
                    L.cardA.right - (int)(12 * s), L.cardA.bottom - (int)(6 * s) };
    L.cardBLbl  = { L.cardB.left + (int)(16 * s), L.cardB.top + (int)(10 * s),
                    L.cardB.right - (int)(12 * s), L.cardB.top + (int)(26 * s) };
    L.cardBVal  = { L.cardALbl.left + (L.cardB.left - L.cardA.left), L.cardB.top + (int)(24 * s),
                    L.cardB.right - (int)(12 * s), L.cardB.top + (int)(56 * s) };
    L.cardBTiny = { L.cardBVal.left, L.cardB.top + (int)(56 * s),
                    L.cardB.right - (int)(12 * s), L.cardB.bottom - (int)(6 * s) };

    int sy = L.cardB.bottom + (int)(12 * s);
    int mid = (left + W - pad) / 2;
    L.statusL = { left, sy + (int)(4 * s), mid, sy + (int)(24 * s) };
    L.statusR = { mid, L.statusL.top, W - pad, L.statusL.bottom };

    int by = H - (int)(12 * s) - (int)(30 * s);
    int bgap = (int)(8 * s);
    int bw = (W - left - pad - 2 * bgap) / 3;
    L.btnPause   = { left, by, left + bw, by + (int)(30 * s) };
    L.btnRefresh = { left + bw + bgap, by, left + 2 * bw + bgap, by + (int)(30 * s) };
    L.btnOpen    = { left + 2 * bw + 2 * bgap, by, left + 3 * bw + 2 * bgap, by + (int)(30 * s) };
    g_layout = L;
}

// ---------------------------------------------------------------------------
// Drawing helpers
// ---------------------------------------------------------------------------
static void DrawCard(HDC dc, const RECT& rc, COLORREF fill, COLORREF edge) {
    int r = std::max(6, std::min(16, (int)((rc.right - rc.left) * 0.06)));
    HRGN rg = CreateRoundRectRgn(rc.left, rc.top, rc.right, rc.bottom, r, r);
    HBRUSH fb = CreateSolidBrush(fill);
    FillRgn(dc, rg, fb);
    DeleteObject(fb);

    HPEN pen = CreatePen(PS_SOLID, 1, edge);
    HGDIOBJ op = SelectObject(dc, pen);
    HGDIOBJ ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
    RoundRect(dc, rc.left, rc.top, rc.right, rc.bottom, r, r);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(pen);
    DeleteObject(rg);
}

static void DrawButton(HDC dc, const RECT& rc, const wchar_t* text,
                       bool hover, bool pressed) {
    double s = (g_dpi / 96.0) * g_sizeScale;
    COLORREF bg = pressed ? C_BTNP : (hover ? C_BTNH : C_BTN);
    int r = std::max(6, (int)(10 * s));
    HRGN rg = CreateRoundRectRgn(rc.left, rc.top, rc.right, rc.bottom, r, r);
    HBRUSH fb = CreateSolidBrush(bg);
    FillRgn(dc, rg, fb);
    DeleteObject(fb);

    HPEN pen = CreatePen(PS_SOLID, 1, hover ? C_GOLD : C_GOLDMUTE);
    HGDIOBJ op = SelectObject(dc, pen);
    HGDIOBJ ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
    RoundRect(dc, rc.left, rc.top, rc.right, rc.bottom, r, r);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(pen);
    DeleteObject(rg);

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, hover ? C_GOLD : C_GOLDMUTE);
    HGDIOBJ of = SelectObject(dc, g_fBtn);
    RECT rr = rc;
    DrawTextW(dc, text, -1, &rr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(dc, of);
}

static void DrawTxt(HDC dc, RECT rc, const wchar_t* s, HFONT f,
                    COLORREF col, UINT fmt) {
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, col);
    HGDIOBJ of = SelectObject(dc, f);
    DrawTextW(dc, s, -1, &rc, fmt);
    SelectObject(dc, of);
}

static void DrawCheckbox(HDC dc, const RECT& rc, const wchar_t* text, bool checked) {
    HBRUSH bg = CreateSolidBrush(C_BG);
    FillRect(dc, &rc, bg);
    DeleteObject(bg);

    double s = (g_dpi / 96.0) * g_sizeScale;

    // Toggle-switch track, right-aligned within the row. Drawn with GDI+
    // (antialiased) instead of plain GDI RoundRect/Ellipse, which leaves
    // visibly jagged/pixelated edges on curves at this small a size.
    int trackW = (int)(34 * s);
    int trackH = (int)(18 * s);
    int vy     = rc.top + (rc.bottom - rc.top - trackH) / 2;
    RECT track = { rc.right - trackW, vy, rc.right, vy + trackH };

    COLORREF trackCol = checked ? C_GOLD : C_BTN;
    COLORREF edgeCol  = checked ? C_GOLD : C_EDGE;
    int knobD = trackH - (int)(4 * s);
    int knobY = track.top + (trackH - knobD) / 2;
    int knobX = checked ? (track.right - knobD - (int)(2 * s)) : (track.left + (int)(2 * s));

    {
        Gdiplus::Graphics g(dc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

        int d = trackH; // corner arc diameter -> full pill/stadium shape
        float tx = (float)track.left, ty = (float)track.top;
        float tw = (float)(track.right - track.left), th = (float)(track.bottom - track.top);
        Gdiplus::GraphicsPath path;
        path.AddArc(tx, ty, (float)d, (float)d, 180, 90);
        path.AddArc(tx + tw - d, ty, (float)d, (float)d, 270, 90);
        path.AddArc(tx + tw - d, ty + th - d, (float)d, (float)d, 0, 90);
        path.AddArc(tx, ty + th - d, (float)d, (float)d, 90, 90);
        path.CloseFigure();

        Gdiplus::SolidBrush fillBrush(Gdiplus::Color(255,
            GetRValue(trackCol), GetGValue(trackCol), GetBValue(trackCol)));
        g.FillPath(&fillBrush, &path);
        Gdiplus::Pen edgePen(Gdiplus::Color(255,
            GetRValue(edgeCol), GetGValue(edgeCol), GetBValue(edgeCol)), 1.0f);
        g.DrawPath(&edgePen, &path);

        Gdiplus::SolidBrush knobBrush(Gdiplus::Color(255,
            GetRValue(C_KNOB), GetGValue(C_KNOB), GetBValue(C_KNOB)));
        g.FillEllipse(&knobBrush, (float)knobX, (float)knobY, (float)knobD, (float)knobD);
    }

    // Label text (left side, leaving room for the switch)
    RECT trc = { rc.left, rc.top, track.left - (int)(10 * s), rc.bottom };
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, C_DIM);
    HGDIOBJ of = SelectObject(dc, g_fBtn);
    DrawTextW(dc, text, -1, &trc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    SelectObject(dc, of);
}

static void DrawCapButton(HDC dc, const RECT& rc, int kind, bool hover) {
    COLORREF bg = hover ? (kind == 2 ? C_RED : C_CAPHOV) : C_BG;
    HBRUSH bgb = CreateSolidBrush(bg);
    FillRect(dc, &rc, bgb);
    DeleteObject(bgb);

    double s = g_dpi / 96.0;
    int cx = (rc.left + rc.right) / 2;
    int cy = (rc.top + rc.bottom) / 2;
    int half = (int)(5 * s);
    COLORREF col = hover ? C_TXT : C_CAPICON;
    int penW = std::max(1, (int)(1.2 * s));
    HPEN pen = CreatePen(PS_SOLID, penW, col);
    HGDIOBJ op = SelectObject(dc, pen);
    HGDIOBJ ob = SelectObject(dc, GetStockObject(NULL_BRUSH));

    if (kind == 0) { // minimize
        MoveToEx(dc, cx - half, cy, NULL);
        LineTo(dc, cx + half, cy);
    } else { // close
        MoveToEx(dc, cx - half, cy - half, NULL); LineTo(dc, cx + half, cy + half);
        MoveToEx(dc, cx + half, cy - half, NULL); LineTo(dc, cx - half, cy + half);
    }

    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(pen);
}

static void DrawCaption(HDC dc, HWND hwnd) {
    RECT full;
    GetClientRect(hwnd, &full);
    RECT cap = { 0, 0, full.right, g_capH };
    HBRUSH b = CreateSolidBrush(C_BG);
    FillRect(dc, &cap, b);
    DeleteObject(b);

    double s = g_dpi / 96.0;
    int iconSz = (int)(18 * s);
    int padL = (int)(12 * s);
    int iy = (g_capH - iconSz) / 2;
    if (g_icon16) DrawIconEx(dc, padL, iy, g_icon16, iconSz, iconSz, 0, NULL, DI_NORMAL);

    wchar_t title[128] = L"";
    GetWindowTextW(hwnd, title, 128);
    RECT titleR = { padL + iconSz + (int)(8 * s), 0, g_capMinR.left - (int)(8 * s), g_capH };
    DrawTxt(dc, titleR, title, g_fCaption, C_TXT, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    HGDIOBJ prevF = SelectObject(dc, g_fCaption);
    SIZE titleSz{};
    GetTextExtentPoint32W(dc, title, (int)wcslen(title), &titleSz);
    SelectObject(dc, prevF);

    RECT verR = titleR;
    verR.left = std::min(titleR.left + titleSz.cx + (int)(6 * s), titleR.right);
    DrawTxt(dc, verR, L"v" GX_APP_VERSION, g_fTiny, C_FAINT, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    DrawCapButton(dc, g_capMinR, 0, g_hoverCap == 0);
    DrawCapButton(dc, g_capCloseR, 2, g_hoverCap == 2);
}

static void DrawUpdateBadge(HDC dc) {
    int r = (g_updateBadgeR.bottom - g_updateBadgeR.top) / 2;
    HRGN rg = CreateRoundRectRgn(g_updateBadgeR.left, g_updateBadgeR.top,
                                 g_updateBadgeR.right, g_updateBadgeR.bottom, r, r);
    HBRUSH fb = CreateSolidBrush(g_hoverUpdate ? C_GOLD : C_GOLDMUTE);
    FillRgn(dc, rg, fb);
    DeleteObject(fb);
    DeleteObject(rg);

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(0x1A, 0x1D, 0x24));
    HGDIOBJ of = SelectObject(dc, g_fBtn);
    RECT tr = g_updateBadgeR;
    const wchar_t* label = g_updating ? L"Updating\x2026" : g_updateFailed ? L"Retry" : L"Update";
    DrawTextW(dc, label, -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(dc, of);
}

static void DrawHomeIcon(Gdiplus::Graphics& g, float cx, float cy, float sz, Gdiplus::Color col) {
    Gdiplus::Pen pen(col, sz * 0.16f);
    pen.SetLineJoin(Gdiplus::LineJoinRound);
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    Gdiplus::PointF roof[3] = {
        Gdiplus::PointF(cx - sz, cy + sz * 0.05f),
        Gdiplus::PointF(cx, cy - sz),
        Gdiplus::PointF(cx + sz, cy + sz * 0.05f)
    };
    g.DrawLines(&pen, roof, 3);
    float bx = sz * 0.68f, bt = cy - sz * 0.05f, bb = cy + sz * 0.85f;
    g.DrawLine(&pen, cx - bx, bt, cx - bx, bb);
    g.DrawLine(&pen, cx + bx, bt, cx + bx, bb);
    g.DrawLine(&pen, cx - bx, bb, cx + bx, bb);
    Gdiplus::SolidBrush doorBrush(col);
    float dw = sz * 0.36f, dh = sz * 0.52f;
    g.FillRectangle(&doorBrush, cx - dw / 2, bb - dh, dw, dh);
}

static void DrawGearIcon(Gdiplus::Graphics& g, float cx, float cy, float sz,
                         Gdiplus::Color col, Gdiplus::Color holeCol) {
    Gdiplus::SolidBrush brush(col);
    const int teeth = 8;
    float outerR = sz, bodyR = sz * 0.62f, toothW = sz * 0.36f, toothLen = sz * 0.36f;
    Gdiplus::Matrix identity;
    for (int i = 0; i < teeth; i++) {
        Gdiplus::Matrix m;
        m.RotateAt(360.0f * i / teeth, Gdiplus::PointF(cx, cy));
        g.SetTransform(&m);
        g.FillRectangle(&brush, cx - toothW / 2, cy - outerR, toothW, toothLen);
        g.SetTransform(&identity);
    }
    g.FillEllipse(&brush, cx - bodyR, cy - bodyR, bodyR * 2, bodyR * 2);
    Gdiplus::SolidBrush hole(holeCol);
    float holeR = sz * 0.3f;
    g.FillEllipse(&hole, cx - holeR, cy - holeR, holeR * 2, holeR * 2);
}

// A rectangle rounded only on its right side -- used for the active nav
// tab, which sits flush against the sidebar's own left edge and blends into
// the content area on the right.
static void FillRightRoundedRect(Gdiplus::Graphics& g, Gdiplus::Brush& brush,
                                 float x, float y, float w, float h, float r) {
    Gdiplus::GraphicsPath path;
    path.AddLine(x, y, x + w - r, y);
    path.AddArc(x + w - 2 * r, y, 2 * r, 2 * r, 270, 90);
    path.AddLine(x + w, y + r, x + w, y + h - r);
    path.AddArc(x + w - 2 * r, y + h - 2 * r, 2 * r, 2 * r, 0, 90);
    path.AddLine(x + w - r, y + h, x, y + h);
    path.CloseFigure();
    g.FillPath(&brush, &path);
}

static void DrawNavIcon(HDC dc, const RECT& rc, int kind, bool active, bool hover) {
    COLORREF bgCol = active ? C_GOLD : (hover ? C_CAPHOV : C_BG);
    Gdiplus::Graphics g(dc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

    if (active) {
        Gdiplus::SolidBrush brush(Gdiplus::Color(255,
            GetRValue(C_GOLD), GetGValue(C_GOLD), GetBValue(C_GOLD)));
        float r = (float)(rc.right - rc.left) * 0.28f;
        FillRightRoundedRect(g, brush, 0.0f, (float)rc.top,
                             (float)g_sidebarW, (float)(rc.bottom - rc.top), r);
    } else if (hover) {
        int r = (int)((rc.right - rc.left) * 0.28);
        HRGN rg = CreateRoundRectRgn(rc.left, rc.top, rc.right, rc.bottom, r, r);
        HBRUSH b = CreateSolidBrush(bgCol);
        FillRgn(dc, rg, b);
        DeleteObject(b);
        DeleteObject(rg);
    }

    COLORREF iconCol = active ? RGB(0xFF, 0xFF, 0xFF) : (hover ? C_TXT : C_CAPICON);
    Gdiplus::Color col(255, GetRValue(iconCol), GetGValue(iconCol), GetBValue(iconCol));
    Gdiplus::Color bg(255, GetRValue(bgCol), GetGValue(bgCol), GetBValue(bgCol));
    float cx = (rc.left + rc.right) / 2.0f;
    float cy = (rc.top + rc.bottom) / 2.0f;
    float sz = (rc.right - rc.left) * 0.24f;
    if (kind == 0) DrawHomeIcon(g, cx, cy, sz, col);
    else DrawGearIcon(g, cx, cy, sz, col, bg);
}

static void DrawSidebar(HDC dc, HWND hwnd) {
    RECT full;
    GetClientRect(hwnd, &full);
    RECT sidebar = { 0, g_capH, g_sidebarW, full.bottom };
    HBRUSH b = CreateSolidBrush(C_BG);
    FillRect(dc, &sidebar, b);
    DeleteObject(b);

    HPEN pen = CreatePen(PS_SOLID, 1, C_EDGE);
    HGDIOBJ op = SelectObject(dc, pen);
    MoveToEx(dc, g_sidebarW, g_capH, NULL);
    LineTo(dc, g_sidebarW, full.bottom);
    SelectObject(dc, op);
    DeleteObject(pen);

    DrawNavIcon(dc, g_navHomeR, 0, g_view == VIEW_DASHBOARD, g_hoverNav == 0);
    DrawNavIcon(dc, g_navGearR, 1, g_view == VIEW_SETTINGS, g_hoverNav == 1);
}

// The window has no native frame at all (WS_POPUP, no WS_CAPTION) so DWM
// never draws a border of its own -- draw the focused-state accent
// ourselves, right on the outer edge of the client area, and simply skip it
// while unfocused.
static void DrawActiveBorder(HDC dc, HWND hwnd) {
    if (!g_windowActive) return;
    RECT rc;
    GetClientRect(hwnd, &rc);
    int w = std::max(1, (int)(1.5 * (g_dpi / 96.0) + 0.5));
    HPEN pen = CreatePen(PS_SOLID, w, C_GOLD);
    HGDIOBJ op = SelectObject(dc, pen);
    HGDIOBJ ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
    Rectangle(dc, 0, 0, rc.right, rc.bottom);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(pen);
}

// ---------------------------------------------------------------------------
// Main window painting
// ---------------------------------------------------------------------------
static void PaintMain(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);
    int W = rc.right, H = rc.bottom;

    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, W, H);
    HGDIOBJ oldB = SelectObject(mem, bmp);

    EnsureFonts(hwnd);
    ComputeLayout(hwnd);

    HBRUSH bg = CreateSolidBrush(C_BG);
    FillRect(mem, &rc, bg);
    DeleteObject(bg);

    GoldBreakdown bd;
    std::wstring formatted, status;
    int statusKind = STATUS_WAITING;
    bool paused = false;
    {
        std::lock_guard<std::mutex> lk(g_stateMu);
        bd = g_bd;
        formatted = g_formatted;
        status = g_status;
        statusKind = g_statusKind;
        paused = g_paused;
    }

    // Big card
    DrawCard(mem, g_layout.bigCard, C_PANEL, C_EDGE);
    DrawTxt(mem, g_layout.bigCap, L"ACCOUNT GOLD TOTAL", g_fCaption, C_DIM,
            DT_LEFT | DT_SINGLELINE);
    std::wstring big = formatted.empty() ? L"\x2014" : formatted;
    DrawTxt(mem, g_layout.bigVal, big.c_str(), g_fBig, C_GOLD,
            DT_LEFT | DT_SINGLELINE);
    std::wstring sub;
    if (formatted.empty()) {
        sub = L"waiting for SavedVariables \x2026";
    } else {
        sub = L"raw: " + format_num(bd.total()) + L" copper    \x00b7    "
            + std::to_wstring(bd.chars.size())
            + (bd.chars.size() == 1 ? L" character" : L" characters");
    }
    DrawTxt(mem, g_layout.bigSub, sub.c_str(), g_fSub, C_FAINT,
            DT_LEFT | DT_SINGLELINE);

    // Characters card
    DrawCard(mem, g_layout.cardA, C_PANEL, C_EDGE);
    DrawTxt(mem, g_layout.cardALbl, L"CHARACTERS", g_fCaption, C_DIM,
            DT_LEFT | DT_SINGLELINE);
    std::wstring va = format_gold(bd.characters);
    if (va.empty()) va = L"0c";
    DrawTxt(mem, g_layout.cardAVal, va.c_str(), g_fValue, C_TXT,
            DT_LEFT | DT_SINGLELINE);
    std::wstring ta = std::to_wstring(bd.chars.size()) + L" tracked";
    DrawTxt(mem, g_layout.cardATiny, ta.c_str(), g_fTiny, C_FAINT,
            DT_LEFT | DT_SINGLELINE);

    // Warband card
    DrawCard(mem, g_layout.cardB, C_PANEL, C_EDGE);
    DrawTxt(mem, g_layout.cardBLbl, L"WARBAND BANK", g_fCaption, C_DIM,
            DT_LEFT | DT_SINGLELINE);
    std::wstring vb = format_gold(bd.warband);
    if (vb.empty()) vb = L"0c";
    DrawTxt(mem, g_layout.cardBVal, vb.c_str(), g_fValue, C_TXT,
            DT_LEFT | DT_SINGLELINE);
    DrawTxt(mem, g_layout.cardBTiny, L"shared account bank", g_fTiny, C_FAINT,
            DT_LEFT | DT_SINGLELINE);

    // Status row
    bool updateFailed;
    { std::lock_guard<std::mutex> lk(g_updateMu); updateFailed = g_updateFailed; }
    COLORREF dotCol = updateFailed ? C_RED
                     : paused ? C_AMB
                     : (statusKind == STATUS_ERROR ? C_RED
                        : statusKind == STATUS_WAITING ? C_AMB : C_GOLD);
    int dotSize = (int)(8.0 * g_dpi / 96.0 * g_sizeScale);
    int midY = (g_layout.statusL.top + g_layout.statusL.bottom) / 2;
    HBRUSH dot = CreateSolidBrush(dotCol);
    HGDIOBJ op = SelectObject(mem, GetStockObject(NULL_PEN));
    HGDIOBJ obr = SelectObject(mem, dot);
    Ellipse(mem, g_layout.statusL.left, midY - dotSize / 2,
            g_layout.statusL.left + dotSize, midY + dotSize / 2);
    SelectObject(mem, obr);
    SelectObject(mem, op);
    DeleteObject(dot);

    RECT statusText = g_layout.statusL;
    statusText.left += dotSize + (int)(8.0 * g_dpi / 96.0 * g_sizeScale);
    std::wstring updateStatus;
    { std::lock_guard<std::mutex> lk(g_updateMu); if (g_updating || g_updateFailed) updateStatus = g_updateStatus; }
    std::wstring statusLine = !updateStatus.empty() ? updateStatus
                             : paused ? L"Paused" : (status.empty() ? L"Starting\x2026" : status);
    DrawTxt(mem, statusText, statusLine.c_str(), g_fSub, dotCol, DT_LEFT | DT_SINGLELINE);

    std::wstring right;
    if (!formatted.empty())
        right = L"updated " + TimeHMS();
    DrawTxt(mem, g_layout.statusR, right.c_str(), g_fSub, C_FAINT,
            DT_RIGHT | DT_SINGLELINE);

    // Buttons
    DrawButton(mem, g_layout.btnPause, paused ? L"Resume" : L"Pause",
               g_hover == ACT_PAUSE, g_btnDown == ACT_PAUSE);
    DrawButton(mem, g_layout.btnRefresh, L"Refresh", g_hover == ACT_REFRESH, g_btnDown == ACT_REFRESH);
    DrawButton(mem, g_layout.btnOpen, L"Open output", g_hover == ACT_OPEN, g_btnDown == ACT_OPEN);

    DrawCaption(mem, hwnd);
    DrawSidebar(mem, hwnd);
    if (g_updateAvailable) DrawUpdateBadge(mem);
    DrawActiveBorder(mem, hwnd);

    BitBlt(dc, 0, 0, W, H, mem, 0, 0, SRCCOPY);
    SelectObject(mem, oldB);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

static void PaintSettingsBg(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);
    int W = rc.right, H = rc.bottom;

    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, W, H);
    HGDIOBJ oldB = SelectObject(mem, bmp);

    EnsureFonts(hwnd);
    ComputeCaptionRects(hwnd);
    FillRect(mem, &rc, g_brDlg);

    double s = UiScale(hwnd);
    RECT header = { g_sidebarW + (int)(14 * s), g_capH + (int)(12 * s),
                    rc.right - (int)(14 * s), g_capH + (int)(12 * s) + (int)(24 * s) };
    DrawTxt(mem, header, L"Settings", g_fValue, C_TXT, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    DrawCaption(mem, hwnd);
    DrawSidebar(mem, hwnd);
    if (g_updateAvailable) DrawUpdateBadge(mem);
    DrawActiveBorder(mem, hwnd);

    BitBlt(dc, 0, 0, W, H, mem, 0, 0, SRCCOPY);
    SelectObject(mem, oldB);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------
static int HitTest(int x, int y) {
    POINT p = { x, y };
    if (PtInRect(&g_layout.btnPause, p)) return ACT_PAUSE;
    if (PtInRect(&g_layout.btnRefresh, p)) return ACT_REFRESH;
    if (PtInRect(&g_layout.btnOpen, p)) return ACT_OPEN;
    return ACT_NONE;
}

static void TogglePause() {
    g_paused = !g_paused;
    g_watcher.setPaused(g_paused);
    InvalidateRect(g_hwnd, NULL, FALSE);
}

static void DoRefresh() {
    {
        std::lock_guard<std::mutex> lk(g_stateMu);
        g_status = L"Refreshing\x2026";
        g_statusKind = STATUS_WATCHING;
    }
    InvalidateRect(g_hwnd, NULL, FALSE);
    g_watcher.refresh();
}

static void OpenOutputFile() {
    std::wstring path = g_settings.wc.output;
    if (path.empty()) path = default_output_file();
    std::wstring args = L"/select,\"" + path + L"\"";
    ShellExecuteW(g_hwnd, L"open", L"explorer.exe", args.c_str(), NULL, SW_SHOWNORMAL);
}

static void QuitApp() {
    g_watcher.stop();
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(NOTIFYICONDATAW);
    nid.hWnd = g_hwnd;
    nid.uID = 1;
    Shell_NotifyIconW(NIM_DELETE, &nid);
    DestroyWindow(g_hwnd);
    PostQuitMessage(0);
}

// ---------------------------------------------------------------------------
// In-app updater
// ---------------------------------------------------------------------------
static void StartUpdateDownload() {
    UpdateInfo info;
    {
        std::lock_guard<std::mutex> lk(g_updateMu);
        if (g_updating || !g_updateAvailable) return;
        g_updating = true;
        g_updateFailed = false;
        g_updateStatus = L"Downloading update\x2026";
        info = g_updateInfo;
    }
    InvalidateRect(g_hwnd, NULL, FALSE);

    std::thread([info]() {
        wchar_t tempDir[MAX_PATH]{};
        GetTempPathW(MAX_PATH, tempDir);
        std::wstring dest = std::wstring(tempDir) + L"GXMonitor_update_" + info.version + L".exe";

        bool ok = download_update(info.exeUrl, dest, [](long long got, long long total) {
            std::lock_guard<std::mutex> lk(g_updateMu);
            if (total > 0) {
                int pct = (int)(got * 100 / total);
                g_updateStatus = L"Downloading update\x2026 " + std::to_wstring(pct) + L"%";
            } else {
                g_updateStatus = L"Downloading update\x2026";
            }
            if (g_hwnd) PostMessageW(g_hwnd, WM_APP_UPDATE_PROGRESS, 0, 0);
        });

        if (ok) {
            {
                std::lock_guard<std::mutex> lk(g_updateMu);
                g_updateStatus = L"Installing update, restarting\x2026";
            }
            if (g_hwnd) PostMessageW(g_hwnd, WM_APP_UPDATE_PROGRESS, 0, 0);
            ok = apply_update_and_restart(dest);
        }

        if (ok) {
            if (g_hwnd) PostMessageW(g_hwnd, WM_APP_UPDATE_DONE, 0, 0);
        } else {
            std::lock_guard<std::mutex> lk(g_updateMu);
            g_updating = false;
            g_updateFailed = true;
            g_updateStatus = L"Update failed \x2014 see gx_update.log, click Update to retry";
            if (g_hwnd) PostMessageW(g_hwnd, WM_APP_UPDATE_PROGRESS, 0, 0);
        }
    }).detach();
}

static void UpdateCheckThreadFn() {
    std::this_thread::sleep_for(std::chrono::seconds(3));
    for (;;) {
        UpdateInfo info;
        if (check_latest_release(info)) {
            std::lock_guard<std::mutex> lk(g_updateMu);
            if (!g_updating) {
                g_updateAvailable = true;
                g_updateInfo = info;
            }
        }
        if (g_hwnd) PostMessageW(g_hwnd, WM_APP_UPDATE_PROGRESS, 0, 0);
        std::this_thread::sleep_for(std::chrono::hours(6));
    }
}

// ---------------------------------------------------------------------------
// Tray
// ---------------------------------------------------------------------------
static void AddTrayIcon() {
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(NOTIFYICONDATAW);
    nid.hWnd = g_hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_APP_ICON;
    nid.hIcon = g_iconTray;
    wcscpy_s(nid.szTip, L"GX Gold Monitor");
    Shell_NotifyIconW(NIM_ADD, &nid);
}

static void UpdateTrayTip() {
    std::wstring tip;
    {
        std::lock_guard<std::mutex> lk(g_stateMu);
        if (g_paused)
            tip = L"GX Gold Monitor \x2014 paused";
        else if (g_formatted.empty())
            tip = L"GX Gold Monitor \x2014 waiting for SavedVariables";
        else
            tip = L"GX \x2014 " + g_formatted;
    }
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(NOTIFYICONDATAW);
    nid.hWnd = g_hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_TIP;
    wcscpy_s(nid.szTip, tip.c_str());
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

static void ShowTrayMenu() {
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, 1, L"Open GX Monitor");
    AppendMenuW(m, MF_STRING, 2, g_paused ? L"Resume" : L"Pause");
    AppendMenuW(m, MF_STRING, 3, L"Settings\x2026");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, 4, L"Exit");

    SetForegroundWindow(g_hwnd);
    POINT pt;
    GetCursorPos(&pt);
    int id = TrackPopupMenu(m, TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY,
                            pt.x, pt.y, 0, g_hwnd, NULL);
    DestroyMenu(m);

    switch (id) {
        case 1:
            ShowWindow(g_hwnd, SW_RESTORE);
            SetForegroundWindow(g_hwnd);
            break;
        case 2: TogglePause(); UpdateTrayTip(); break;
        case 3:
            ShowWindow(g_hwnd, SW_RESTORE);
            SetForegroundWindow(g_hwnd);
            ShowSettingsView();
            break;
        case 4: QuitApp(); break;
    }
}

static void OnTrayIcon(WPARAM, LPARAM lp) {
    switch (LOWORD(lp)) {
        case WM_LBUTTONDBLCLK:
        case WM_LBUTTONUP:
            ShowWindow(g_hwnd, SW_RESTORE);
            SetForegroundWindow(g_hwnd);
            break;
        case WM_RBUTTONUP:
            ShowTrayMenu();
            break;
    }
}

// ---------------------------------------------------------------------------
// Watcher -> UI callback
// ---------------------------------------------------------------------------
static void WatcherCallback(int64_t total, const std::wstring& formatted,
                            const std::wstring& status, int statusKind,
                            const GoldBreakdown& bd) {
    {
        std::lock_guard<std::mutex> lk(g_stateMu);
        if (total >= 0) {
            g_total = total;
            g_formatted = formatted;
            g_bd = bd;
        }
        g_status = status;
        g_statusKind = statusKind;
    }
    if (g_hwnd) PostMessageW(g_hwnd, WM_APP_GOLD, 0, 0);
}

// ---------------------------------------------------------------------------
// Settings view (embedded, responsive)
// ---------------------------------------------------------------------------
static HWND MakeChild(HWND parent, const wchar_t* cls, const wchar_t* text,
                      DWORD style, DWORD exstyle, int id, LPRECT r, HFONT font,
                      bool visible) {
    DWORD st = style | WS_CHILD | (visible ? WS_VISIBLE : 0);
    HWND h = CreateWindowExW(exstyle, cls, text ? text : L"", st,
                             r->left, r->top, r->right - r->left, r->bottom - r->top,
                             parent, (HMENU)(INT_PTR)id, g_hinst, NULL);
    if (h) {
        SendMessageW(h, WM_SETFONT, (WPARAM)font, TRUE);
        if (id) SetWindowLongPtrW(h, GWLP_ID, id);
    }
    return h;
}

static std::wstring BrowseOpen(HWND owner, bool mustExist) {
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    wchar_t file[MAX_PATH] = L"";
    ofn.lpstrFilter = L"All files\0*.*\0Lua files (*.lua)\0*.lua\0\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = mustExist ? L"Select GX.lua" : L"Output text file (for OBS)";
    ofn.Flags = OFN_EXPLORER | OFN_HIDEREADONLY |
                (mustExist ? (OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST) : 0);
    if (GetOpenFileNameW(&ofn)) return std::wstring(file);
    return std::wstring();
}

static void PopulateSettings() {
    HWND h = g_hwnd;
    wchar_t buf[1024] = L"";
    SetWindowTextW(GetDlgItem(h, IDC_ED_FILE), g_settings.wc.file.c_str());
    SetWindowTextW(GetDlgItem(h, IDC_ED_OUT), g_settings.wc.output.c_str());
    SetWindowTextW(GetDlgItem(h, IDC_ED_ACCT), g_settings.wc.account.c_str());
    swprintf(buf, 1024, L"%g", g_settings.wc.pollSeconds);
    SetWindowTextW(GetDlgItem(h, IDC_ED_POLL), buf);
    g_chkRaw = g_settings.wc.raw;
    g_chkMin = g_settings.startMinimized;
    g_chkRun = run_at_startup_enabled();
    InvalidateRect(g_hwnd, NULL, FALSE);
}

static void ApplySettings() {
    HWND h = g_hwnd;
    wchar_t buf[1024] = L"";
    WatchConfig wc = g_settings.wc;

    GetWindowTextW(GetDlgItem(h, IDC_ED_FILE), buf, 1024);  wc.file = buf;
    GetWindowTextW(GetDlgItem(h, IDC_ED_OUT), buf, 1024);   wc.output = buf;
    GetWindowTextW(GetDlgItem(h, IDC_ED_ACCT), buf, 1024);  wc.account = buf;
    GetWindowTextW(GetDlgItem(h, IDC_ED_POLL), buf, 1024);  wc.pollSeconds = wcstod(buf, NULL);
    if (!(wc.pollSeconds >= 0.5)) wc.pollSeconds = 2.0;
    wc.raw = g_chkRaw;
    g_settings.startMinimized = g_chkMin;
    bool run = g_chkRun;

    if (wc.output.empty()) wc.output = default_output_file();
    g_settings.wc = wc;

    g_watcher.setConfig(wc);
    save_settings(g_settings);
    run_at_startup(run);

    UpdateOutputPathText();
    InvalidateRect(g_hwnd, NULL, TRUE);
    UpdateTrayTip();
}

static void PositionSettingsControls(HWND hwnd) {
    ComputeCaptionRects(hwnd);
    RECT rc;
    GetClientRect(hwnd, &rc);
    double s = UiScale(hwnd);
    int W = rc.right, H = rc.bottom;
    int pad = (int)(14 * s);
    int capH = g_capH;
    int left = g_sidebarW + pad;
    int rowW = W - left - pad;

    double fy = std::clamp((double)(H - capH - 40 * s) / (320 * s), 0.7, 1.0);
    int rowH = (int)((14 + 4 + 24 + 6) * s * fy);

    // "Settings" header occupies the first row's worth of space; fields
    // start right below it (no more separate Back button -- the sidebar's
    // Home icon returns to the dashboard).
    int headerH = (int)(24 * s);
    int y = capH + (int)(12 * s) + headerH + (int)(10 * s);

    auto Row = [&](int lbl, int ed, int btn, int btnW) {
        MoveWindow(GetDlgItem(hwnd, lbl), left, y, rowW, (int)(14 * s), TRUE);
        int ey = y + (int)(18 * s);
        int ew = rowW;
        if (btn) ew = rowW - btnW - (int)(10 * s);
        MoveWindow(GetDlgItem(hwnd, ed), left, ey, ew, (int)(24 * s), TRUE);
        if (btn) MoveWindow(GetDlgItem(hwnd, btn), left + ew + (int)(10 * s),
                            ey, btnW, (int)(24 * s), TRUE);
        y += rowH;
    };

    Row(IDC_LBL_FILE, IDC_ED_FILE, IDC_BTN_BROWSE_GX, (int)(88 * s));
    Row(IDC_LBL_ACCT, IDC_ED_ACCT, IDC_BTN_DETECT, (int)(110 * s));
    Row(IDC_LBL_OUT, IDC_ED_OUT, IDC_BTN_BROWSE_OUT, (int)(88 * s));
    Row(IDC_LBL_POLL, IDC_ED_POLL, 0, 0);

    MoveWindow(GetDlgItem(hwnd, IDC_CHK_RAW), left, y, rowW, (int)(22 * s), TRUE);
    y += (int)(26 * s);
    MoveWindow(GetDlgItem(hwnd, IDC_CHK_MIN), left, y, rowW, (int)(22 * s), TRUE);
    y += (int)(26 * s);
    MoveWindow(GetDlgItem(hwnd, IDC_CHK_RUN), left, y, rowW, (int)(22 * s), TRUE);
    y += (int)(30 * s);

    int bw = (int)(110 * s), bh = (int)(30 * s);
    MoveWindow(GetDlgItem(hwnd, IDC_BTN_SAVE), W - pad - bw, H - pad - bh, bw, bh, TRUE);
}

// ---------------------------------------------------------------------------
// OBS output path row (dashboard)
// ---------------------------------------------------------------------------
static std::wstring GetOutputPath() {
    std::wstring p = g_settings.wc.output;
    if (p.empty()) p = default_output_file();
    return p;
}

static void UpdateOutputPathText() {
    if (!g_hwnd) return;
    HWND hEd = GetDlgItem(g_hwnd, IDC_ED_OUTPATH);
    if (hEd) SetWindowTextW(hEd, GetOutputPath().c_str());
}

static void CopyOutputPath() {
    std::wstring path = GetOutputPath();
    if (path.empty()) return;
    if (!OpenClipboard(g_hwnd)) return;
    EmptyClipboard();
    size_t bytes = (path.size() + 1) * sizeof(wchar_t);
    HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (hg) {
        wchar_t* p = (wchar_t*)GlobalLock(hg);
        wcscpy_s(p, path.size() + 1, path.c_str());
        GlobalUnlock(hg);
        SetClipboardData(CF_UNICODETEXT, hg);
    }
    CloseClipboard();
}

static void PositionOutputPath(HWND hwnd) {
    ComputeCaptionRects(hwnd);
    RECT rc;
    GetClientRect(hwnd, &rc);
    double s = UiScale(hwnd);
    int W = rc.right, H = rc.bottom;
    int pad = (int)(14 * s);
    int left = g_sidebarW + pad;

    int btnW = (int)(70 * s);
    int hh   = (int)(24 * s);
    int by   = H - (int)(12 * s) - (int)(30 * s);   // button row top
    int y    = by - (int)(8 * s) - hh;

    HWND hEd  = GetDlgItem(hwnd, IDC_ED_OUTPATH);
    HWND hBtn = GetDlgItem(hwnd, IDC_BTN_COPY);
    if (hEd)  MoveWindow(hEd,  left, y, W - left - pad - btnW - (int)(8 * s), hh, TRUE);
    if (hBtn) MoveWindow(hBtn, W - pad - btnW, y, btnW, hh, TRUE);
}

static void ShowSettingsChildren(int show) {
    const int ids[] = {
        IDC_LBL_FILE, IDC_ED_FILE, IDC_BTN_BROWSE_GX,
        IDC_LBL_ACCT, IDC_ED_ACCT, IDC_BTN_DETECT,
        IDC_LBL_OUT, IDC_ED_OUT, IDC_BTN_BROWSE_OUT,
        IDC_LBL_POLL, IDC_ED_POLL,
        IDC_CHK_RAW, IDC_CHK_MIN, IDC_CHK_RUN, IDC_BTN_SAVE
    };
    for (int id : ids)
        ShowWindow(GetDlgItem(g_hwnd, id), show ? SW_SHOW : SW_HIDE);
}

static void ShowSettingsView() {
    if (g_view == VIEW_SETTINGS) return;
    g_view = VIEW_SETTINGS;
    PopulateSettings();
    PositionSettingsControls(g_hwnd);
    ShowSettingsChildren(TRUE);
    ShowWindow(GetDlgItem(g_hwnd, IDC_ED_OUTPATH), SW_HIDE);
    ShowWindow(GetDlgItem(g_hwnd, IDC_BTN_COPY), SW_HIDE);
    SendMessageW(g_hwnd, DM_SETDEFID, IDC_BTN_SAVE, 0);
    g_hover = g_btnDown = ACT_NONE;
    SetFocus(GetDlgItem(g_hwnd, IDC_ED_FILE));
    InvalidateRect(g_hwnd, NULL, TRUE);
}

static void HideSettingsView() {
    if (g_view != VIEW_SETTINGS) return;
    g_view = VIEW_DASHBOARD;
    ShowSettingsChildren(FALSE);
    ShowWindow(GetDlgItem(g_hwnd, IDC_ED_OUTPATH), SW_SHOW);
    ShowWindow(GetDlgItem(g_hwnd, IDC_BTN_COPY), SW_SHOW);
    UpdateOutputPathText();
    PositionOutputPath(g_hwnd);
    g_hover = g_btnDown = ACT_NONE;
    SetFocus(g_hwnd);
    InvalidateRect(g_hwnd, NULL, TRUE);
}

// ---------------------------------------------------------------------------
// Main window proc
// ---------------------------------------------------------------------------
static void OnGoldUpdate() {
    UpdateTrayTip();
    InvalidateRect(g_hwnd, NULL, FALSE);
}

static LRESULT CALLBACK GoldWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        g_hwnd = hwnd;
        EnableDarkMode(hwnd);
        SetBorderActive(hwnd, true); // newly created window starts focused
        SendMessageW(hwnd, WM_SETICON, ICON_BIG, (LPARAM)g_icon32);
        SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)g_icon16);
        AddTrayIcon();

        RECT r{ 0, 0, 100, 24 };
        MakeChild(hwnd, L"STATIC",
                  L"SAVED VARIABLES \x2014 GX.lua  (empty = auto-detect)",
                  SS_LEFT, 0, IDC_LBL_FILE, &r, g_fntUi, false);
        HWND         hEd = MakeChild(hwnd, L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, 0,
                  IDC_ED_FILE, &r, g_fntUi, false);
        if (hEd) SetWindowTheme(hEd, L"DarkMode_Explorer", NULL);
        MakeChild(hwnd, L"BUTTON", L"Browse", WS_TABSTOP | BS_OWNERDRAW, 0,
                  IDC_BTN_BROWSE_GX, &r, g_fntUi, false);
        MakeChild(hwnd, L"STATIC",
                  L"ACCOUNT FILTER  (optional, e.g. 410566417#1)",
                  SS_LEFT, 0, IDC_LBL_ACCT, &r, g_fntUi, false);
        hEd = MakeChild(hwnd, L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, 0,
                  IDC_ED_ACCT, &r, g_fntUi, false);
        if (hEd) SetWindowTheme(hEd, L"DarkMode_Explorer", NULL);
        MakeChild(hwnd, L"BUTTON", L"Auto-detect", WS_TABSTOP | BS_OWNERDRAW, 0,
                  IDC_BTN_DETECT, &r, g_fntUi, false);
        MakeChild(hwnd, L"STATIC",
                  L"OUTPUT TEXT FILE  (OBS reads this)",
                  SS_LEFT, 0, IDC_LBL_OUT, &r, g_fntUi, false);
        hEd = MakeChild(hwnd, L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, 0,
                  IDC_ED_OUT, &r, g_fntUi, false);
        if (hEd) SetWindowTheme(hEd, L"DarkMode_Explorer", NULL);
        MakeChild(hwnd, L"BUTTON", L"Browse", WS_TABSTOP | BS_OWNERDRAW, 0,
                  IDC_BTN_BROWSE_OUT, &r, g_fntUi, false);
        MakeChild(hwnd, L"STATIC",
                  L"POLL INTERVAL SECONDS  (default 2, min 0.5)",
                  SS_LEFT, 0, IDC_LBL_POLL, &r, g_fntUi, false);
        hEd = MakeChild(hwnd, L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, 0,
                  IDC_ED_POLL, &r, g_fntUi, false);
        if (hEd) SetWindowTheme(hEd, L"DarkMode_Explorer", NULL);
        MakeChild(hwnd, L"BUTTON", L"Raw numeric output (bare copper number)",
                  WS_TABSTOP | BS_OWNERDRAW, 0, IDC_CHK_RAW, &r, g_fntUi, false);
        MakeChild(hwnd, L"BUTTON", L"Start minimized to tray",
                  WS_TABSTOP | BS_OWNERDRAW, 0, IDC_CHK_MIN, &r, g_fntUi, false);
        MakeChild(hwnd, L"BUTTON", L"Run at Windows startup",
                  WS_TABSTOP | BS_OWNERDRAW, 0, IDC_CHK_RUN, &r, g_fntUi, false);
        MakeChild(hwnd, L"BUTTON", L"Save && Apply", WS_TABSTOP | BS_OWNERDRAW, 0,
                  IDC_BTN_SAVE, &r, g_fntUi, false);

        hEd = MakeChild(hwnd, L"EDIT", L"", ES_READONLY | ES_AUTOHSCROLL, 0,
                  IDC_ED_OUTPATH, &r, g_fntUi, true);
        if (hEd) {
            SetWindowTheme(hEd, L"DarkMode_Explorer", NULL);
            SendMessageW(hEd, EM_SETREADONLY, TRUE, 0);
        }
        MakeChild(hwnd, L"BUTTON", L"Copy", WS_TABSTOP | BS_OWNERDRAW, 0,
                  IDC_BTN_COPY, &r, g_fntUi, true);
        UpdateOutputPathText();

        ComputeLayout(hwnd);
        return 0;
    }

    case WM_PAINT:
        if (g_view == VIEW_SETTINGS) PaintSettingsBg(hwnd);
        else PaintMain(hwnd);
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_CTLCOLOREDIT:
        SetTextColor((HDC)wp, C_TXT);
        SetBkColor((HDC)wp, C_EDITBG);
        return (LRESULT)g_brEdit;

    case WM_CTLCOLORSTATIC:
        SetTextColor((HDC)wp, C_DIM);
        SetBkColor((HDC)wp, C_BG);
        return (LRESULT)g_brDlg;

    case WM_DRAWITEM: {
        DRAWITEMSTRUCT* ds = (DRAWITEMSTRUCT*)lp;
        if (ds->CtlType != ODT_BUTTON) break;
        int id = (int)ds->CtlID;
        wchar_t txt[64] = L"";
        GetWindowTextW(ds->hwndItem, txt, 64);
        bool isChk = (id == IDC_CHK_RAW || id == IDC_CHK_MIN || id == IDC_CHK_RUN);
        if (isChk) {
            bool checked = id == IDC_CHK_RAW ? g_chkRaw :
                           id == IDC_CHK_MIN ? g_chkMin : g_chkRun;
            DrawCheckbox(ds->hDC, ds->rcItem, txt, checked);
        } else {
            HBRUSH bb = CreateSolidBrush(C_BG);
            FillRect(ds->hDC, &ds->rcItem, bb);
            DeleteObject(bb);
            bool pressed = (ds->itemState & ODS_SELECTED) != 0;
            DrawButton(ds->hDC, ds->rcItem, txt, pressed, pressed);
        }
        return TRUE;
    }

    case WM_SIZE:
        ComputeLayout(hwnd);
        PositionSettingsControls(hwnd);
        PositionOutputPath(hwnd);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_GETMINMAXINFO: {
        MINMAXINFO* mm = (MINMAXINFO*)lp;
        int dpi = GetDpiOf(hwnd);
        mm->ptMinTrackSize.x = (LONG)(380.0 * dpi / 96.0);
        mm->ptMinTrackSize.y = (LONG)(420.0 * dpi / 96.0);
        return 0;
    }

    case WM_COMMAND: {
        int id = LOWORD(wp);
        if (HIWORD(wp) != BN_CLICKED) break;
        switch (id) {
        case IDC_BTN_SAVE:
            ApplySettings();
            HideSettingsView();
            break;
        case IDC_BTN_BROWSE_GX: {
            std::wstring f = BrowseOpen(g_hwnd, true);
            if (!f.empty()) SetWindowTextW(GetDlgItem(g_hwnd, IDC_ED_FILE), f.c_str());
            break;
        }
        case IDC_BTN_DETECT: {
            wchar_t acct[1024] = L"", out[1024] = L"";
            GetWindowTextW(GetDlgItem(g_hwnd, IDC_ED_ACCT), acct, 1024);
            std::wstring f = auto_discover(g_exeDir, L"", acct);
            SetWindowTextW(GetDlgItem(g_hwnd, IDC_ED_FILE), f.c_str());
            GetWindowTextW(GetDlgItem(g_hwnd, IDC_ED_OUT), out, 1024);
            if (out[0] == L'\0') {
                SetWindowTextW(GetDlgItem(g_hwnd, IDC_ED_OUT), default_output_file().c_str());
            }
            break;
        }
        case IDC_BTN_BROWSE_OUT: {
            std::wstring f = BrowseOpen(g_hwnd, false);
            if (!f.empty()) SetWindowTextW(GetDlgItem(g_hwnd, IDC_ED_OUT), f.c_str());
            break;
        }
        case IDC_BTN_COPY:
            CopyOutputPath();
            break;
        case IDC_CHK_RAW:
        case IDC_CHK_MIN:
        case IDC_CHK_RUN: {
            bool* p = id == IDC_CHK_RAW ? &g_chkRaw :
                      id == IDC_CHK_MIN ? &g_chkMin : &g_chkRun;
            *p = !*p;
            InvalidateRect(GetDlgItem(hwnd, id), NULL, FALSE);
            break;
        }
        }
        return 0;
    }

    case WM_MOUSEMOVE: {
        int mx = GET_X_LPARAM(lp), my = GET_Y_LPARAM(lp);
        bool changed = false;
        int ch = CapHitTest(mx, my);
        if (ch != g_hoverCap) { g_hoverCap = ch; changed = true; }
        bool uh = UpdateBadgeHit(mx, my);
        if (uh != g_hoverUpdate) { g_hoverUpdate = uh; changed = true; }
        int nh = NavHitTest(mx, my);
        if (nh != g_hoverNav) { g_hoverNav = nh; changed = true; }
        if (g_view != VIEW_SETTINGS) {
            int h = HitTest(mx, my);
            if (h != g_hover) { g_hover = h; changed = true; }
        }
        if (changed) InvalidateRect(hwnd, NULL, FALSE);
        TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, hwnd, 0 };
        TrackMouseEvent(&tme);
        return 0;
    }

    case WM_MOUSELEAVE:
        if (g_hover != -1 || g_btnDown != -1 || g_hoverCap != -1 || g_hoverUpdate || g_hoverNav != -1) {
            g_hover = -1;
            g_btnDown = -1;
            g_hoverCap = -1;
            g_hoverUpdate = false;
            g_hoverNav = -1;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;

    case WM_LBUTTONDOWN: {
        int mx = GET_X_LPARAM(lp), my = GET_Y_LPARAM(lp);
        if (UpdateBadgeHit(mx, my)) {
            g_updateDown = true;
            SetCapture(hwnd);
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        int ch = CapHitTest(mx, my);
        if (ch != -1) {
            g_capDown = ch;
            SetCapture(hwnd);
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        int nh = NavHitTest(mx, my);
        if (nh != -1) {
            g_navDown = nh;
            SetCapture(hwnd);
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        if (g_view == VIEW_SETTINGS) return 0;
        int h = HitTest(mx, my);
        if (h != ACT_NONE) {
            g_btnDown = h;
            SetCapture(hwnd);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_LBUTTONUP: {
        int mx = GET_X_LPARAM(lp), my = GET_Y_LPARAM(lp);
        if (g_updateDown) {
            g_updateDown = false;
            ReleaseCapture();
            if (UpdateBadgeHit(mx, my)) StartUpdateDownload();
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        if (g_capDown != -1) {
            int down = g_capDown;
            g_capDown = -1;
            ReleaseCapture();
            if (CapHitTest(mx, my) == down) {
                switch (down) {
                case 0: ShowWindow(hwnd, SW_MINIMIZE); break;
                case 2: PostMessageW(hwnd, WM_CLOSE, 0, 0); break;
                }
            }
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        if (g_navDown != -1) {
            int down = g_navDown;
            g_navDown = -1;
            ReleaseCapture();
            if (NavHitTest(mx, my) == down) {
                if (down == 0) HideSettingsView();
                else ShowSettingsView();
            }
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        if (g_view == VIEW_SETTINGS) return 0;
        if (g_btnDown != ACT_NONE) {
            int down = g_btnDown;
            g_btnDown = ACT_NONE;
            ReleaseCapture();
            if (HitTest(mx, my) == down) {
                switch (down) {
                case ACT_PAUSE: TogglePause(); UpdateTrayTip(); break;
                case ACT_REFRESH: DoRefresh(); break;
                case ACT_OPEN: OpenOutputFile(); break;
                }
            }
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_CAPTURECHANGED:
        g_btnDown = ACT_NONE;
        g_capDown = -1;
        g_updateDown = false;
        g_navDown = -1;
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_NCCALCSIZE: {
        if (wp) {
            NCCALCSIZE_PARAMS* params = (NCCALCSIZE_PARAMS*)lp;
            if (IsWndMaximized(hwnd)) {
                HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
                MONITORINFO mi{ sizeof(mi) };
                if (mon && GetMonitorInfoW(mon, &mi)) {
                    params->rgrc[0] = mi.rcWork;
                }
            }
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    case WM_NCHITTEST: {
        POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        ScreenToClient(hwnd, &pt);
        RECT rc;
        GetClientRect(hwnd, &rc);
        double s = GetDpiOf(hwnd) / 96.0;
        int border = (int)(6 * s);
        if (!IsWndMaximized(hwnd)) {
            bool left = pt.x < border;
            bool right = pt.x >= rc.right - border;
            bool top = pt.y < border;
            bool bottom = pt.y >= rc.bottom - border;
            if (top && left) return HTTOPLEFT;
            if (top && right) return HTTOPRIGHT;
            if (bottom && left) return HTBOTTOMLEFT;
            if (bottom && right) return HTBOTTOMRIGHT;
            if (left) return HTLEFT;
            if (right) return HTRIGHT;
            if (top && pt.x < g_capMinR.left) return HTTOP;
            if (bottom) return HTBOTTOM;
        }
        if (pt.y >= 0 && pt.y < g_capH && pt.x < g_capMinR.left) return HTCAPTION;
        return HTCLIENT;
    }

    case WM_NCACTIVATE:
        SetBorderActive(hwnd, wp != FALSE);
        return DefWindowProcW(hwnd, msg, wp, -1);

    case WM_ACTIVATE: {
        bool active = LOWORD(wp) != WA_INACTIVE;
        g_windowActive = active;
        SetBorderActive(hwnd, active);
        InvalidateRect(hwnd, NULL, FALSE);
        break;
    }

    // WM_ACTIVATE can be unreliable for a WS_POPUP window with no caption;
    // focus changes are the more robust signal for this style.
    case WM_SETFOCUS:
        g_windowActive = true;
        SetBorderActive(hwnd, true);
        InvalidateRect(hwnd, NULL, FALSE);
        break;

    case WM_KILLFOCUS: {
        // wp is the window RECEIVING focus. Settings-view fields live as
        // child controls of this same window (e.g. ShowSettingsView calls
        // SetFocus on the file-path edit box) -- that's focus moving within
        // our own app, not the app losing focus, so only treat it as
        // deactivation when focus genuinely leaves our window hierarchy.
        HWND next = (HWND)wp;
        if (!next || !(next == hwnd || IsChild(hwnd, next))) {
            g_windowActive = false;
            SetBorderActive(hwnd, false);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        break;
    }

    case WM_NCPAINT:
        // The window keeps WS_CAPTION (for DWM's shadow/rounded corners and
        // Aero-snap), so without this DWM still paints its own native
        // caption strip on top, above our custom-drawn one.
        return 0;

    case WM_APP_GOLD:
        OnGoldUpdate();
        return 0;

    case WM_APP_ICON:
        OnTrayIcon(wp, lp);
        return 0;

    case WM_APP_UPDATE_PROGRESS:
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_APP_UPDATE_DONE:
        QuitApp();
        return 0;

    case WM_DPICHANGED: {
        RECT* pr = (RECT*)lp;
        SetWindowPos(hwnd, NULL, pr->left, pr->top,
                     pr->right - pr->left, pr->bottom - pr->top,
                     SWP_NOACTIVATE | SWP_NOZORDER);

        // Moving to a monitor with a different DPI only auto-rescales the
        // window's own custom-drawn content (refreshed lazily by
        // EnsureFonts on next paint). The settings-view child controls
        // (static labels, edit boxes) keep whatever font g_fntUi was built
        // with at creation time unless we rebuild and rebind it here --
        // otherwise their old-DPI-sized text overflows the newly-resized
        // (new-DPI-sized) control rects and leaves ghosted remnants.
        EnsureFonts(hwnd);
        HFONT oldUi = g_fntUi;
        g_fntUi = MakeFontPt(9, FW_NORMAL);
        const int uiFontIds[] = {
            IDC_LBL_FILE, IDC_ED_FILE, IDC_BTN_BROWSE_GX,
            IDC_LBL_ACCT, IDC_ED_ACCT, IDC_BTN_DETECT,
            IDC_LBL_OUT, IDC_ED_OUT, IDC_BTN_BROWSE_OUT,
            IDC_LBL_POLL, IDC_ED_POLL,
            IDC_CHK_RAW, IDC_CHK_MIN, IDC_CHK_RUN, IDC_BTN_SAVE,
            IDC_ED_OUTPATH, IDC_BTN_COPY
        };
        for (int id : uiFontIds) {
            HWND c = GetDlgItem(hwnd, id);
            if (c) SendMessageW(c, WM_SETFONT, (WPARAM)g_fntUi, TRUE);
        }
        if (oldUi) DeleteObject(oldUi);

        ComputeLayout(hwnd);
        PositionSettingsControls(hwnd);
        PositionOutputPath(hwnd);
        RedrawWindow(hwnd, NULL, NULL,
                     RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
        return 0;
    }

    case WM_CLOSE:
        ShowWindow(hwnd, SW_HIDE);
        return 0;

    case WM_DESTROY:
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ---------------------------------------------------------------------------
// Entry
// ---------------------------------------------------------------------------
int RunApp(HINSTANCE hinst) {
    g_hinst = hinst;

    // Prevent multiple instances: a named mutex is held for the whole process.
    // If another instance already owns it, focus that window and exit.
    g_singleMutex = CreateMutexW(NULL, FALSE, L"Local\\GXGoldMonitorMutex");
    if (!g_singleMutex || GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND existing = FindWindowW(L"GXGoldWnd", NULL);
        if (existing) {
            ShowWindow(existing, SW_RESTORE);
            SetForegroundWindow(existing);
        }
        if (g_singleMutex) {
            CloseHandle(g_singleMutex);
            g_singleMutex = NULL;
        }
        return 0;
    }

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    ULONG_PTR gdipToken = 0;
    Gdiplus::GdiplusStartupInput gdipInput;
    Gdiplus::GdiplusStartup(&gdipToken, &gdipInput, NULL);

    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);

    g_exeDir = get_exe_dir();
    load_settings(g_settings);

    if (g_settings.wc.file.empty()) {
        std::wstring f = auto_discover(g_exeDir, g_settings.wc.wowRoot, g_settings.wc.account);
        if (!f.empty()) g_settings.wc.file = f;
    }

    g_dpi = GetDpiForSystem();
    g_icon32 = (HICON)LoadImageW(hinst, MAKEINTRESOURCE(IDI_APP), IMAGE_ICON,
                                 32, 32, LR_DEFAULTCOLOR);
    g_icon16 = (HICON)LoadImageW(hinst, MAKEINTRESOURCE(IDI_APP), IMAGE_ICON,
                                 16, 16, LR_DEFAULTCOLOR);
    g_iconTray = g_icon16;

    g_brDlg = CreateSolidBrush(C_BG);
    g_brEdit = CreateSolidBrush(C_EDITBG);
    g_fntUi = MakeFontPt(9, FW_NORMAL);

    WNDCLASSW wc{};
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = GoldWndProc;
    wc.hInstance = hinst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = g_icon32;
    wc.lpszClassName = L"GXGoldWnd";
    RegisterClassW(&wc);

    double s = g_dpi / 96.0;
    // WS_POPUP instead of WS_OVERLAPPEDWINDOW: no WS_CAPTION at all, so
    // there's no native title bar for DWM to ever fall back to drawing
    // (it otherwise still painted its own default light caption -- with a
    // maximize button we don't even have -- whenever the window lost focus,
    // no matter how much WM_NCPAINT/WM_NCACTIVATE suppression was added).
    // WS_THICKFRAME keeps resizing (driven by our own WM_NCHITTEST), and
    // WS_SYSMENU + WS_MINIMIZEBOX keep the taskbar/Alt+Tab/system-menu
    // behavior working. No WS_MAXIMIZEBOX: the window can't be
    // full-screened (no maximize button, no double-click-caption maximize,
    // no Win+Up / drag-to-top snap).
    // WS_CLIPCHILDREN: without it, the parent's own paint DC isn't clipped
    // around the settings-view child controls (edit boxes, buttons), so
    // every background repaint (e.g. from a hover state change while the
    // mouse sits over the sidebar) draws straight over them; they then
    // redraw themselves a frame later, which reads as a visible flicker.
    g_hwnd = CreateWindowExW(WS_EX_APPWINDOW, L"GXGoldWnd", L"GX Gold Export",
                             WS_POPUP | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN,
                             CW_USEDEFAULT, CW_USEDEFAULT,
                             (int)(520 * s), (int)(470 * s),
                             NULL, NULL, hinst, NULL);
    if (!g_hwnd) return 1;

    g_watcher.start(g_exeDir, g_settings.wc, WatcherCallback);
    std::thread(UpdateCheckThreadFn).detach();

    if (g_settings.startMinimized) {
        ShowWindow(g_hwnd, SW_HIDE);
    } else {
        ShowWindow(g_hwnd, SW_SHOW);
        UpdateWindow(g_hwnd);
    }

    MSG m;
    while (GetMessageW(&m, NULL, 0, 0) > 0) {
        if (g_view == VIEW_SETTINGS && IsDialogMessageW(g_hwnd, &m))
            continue;
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }

    g_watcher.stop();
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(NOTIFYICONDATAW);
    nid.hWnd = g_hwnd;
    nid.uID = 1;
    Shell_NotifyIconW(NIM_DELETE, &nid);

    if (g_icon32) DestroyIcon(g_icon32);
    if (g_icon16) DestroyIcon(g_icon16);
    if (g_brDlg) DeleteObject(g_brDlg);
    if (g_brEdit) DeleteObject(g_brEdit);
    if (g_fntUi) DeleteObject(g_fntUi);
    if (g_fontDpi) {
        DeleteObject(g_fCaption); DeleteObject(g_fBig);
        DeleteObject(g_fValue); DeleteObject(g_fSub);
        DeleteObject(g_fTiny); DeleteObject(g_fBtn);
    }
    if (g_singleMutex) {
        CloseHandle(g_singleMutex);
        g_singleMutex = NULL;
    }
    if (gdipToken) Gdiplus::GdiplusShutdown(gdipToken);
    return (int)m.wParam;
}
