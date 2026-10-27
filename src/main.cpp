// ========================================================================================
// RetroSaveConverter — Win32 GUI entry point
// Target: Windows 7 x64 and later; built with MinGW-w64 or MSVC.
// No external dependencies: user32, gdi32, comctl32, comdlg32, shell32, ole32.
// ========================================================================================
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef WINVER
#define WINVER 0x0601
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#ifndef _WIN32_IE
#define _WIN32_IE 0x0600
#endif

#include <windows.h>
#include <windowsx.h>   // ComboBox_* / Button_* / Edit_* control macros
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <uxtheme.h>

#include <algorithm>
#include <cwctype>
#include <iterator>
#include <string>
#include <vector>

#include "resource.h"
#include "converter.h"
#include "strings.h"

using namespace rsc;

// ========================================================================================
// Globals
// ========================================================================================
static HINSTANCE   g_hInst   = nullptr;
static HWND        g_main    = nullptr;
static HWND        g_tabs    = nullptr;
static HWND        g_placeholder = nullptr;   // SNES/N64 "coming soon" label
static HFONT       g_font    = nullptr;
static HFONT       g_fontStrong = nullptr;
static HMENU       g_menuBar = nullptr;
static HMENU       g_menuLanguage = nullptr;
static HMENU       g_menuView = nullptr;
static HMENU       g_menuHelp = nullptr;

// The three section panels ("Source", "Target", "Options") are NOT child windows.
// They used to be owner-drawn BS_GROUPBOX controls; an opaque owner-drawn group box
// that is repainted (after a combo drop-down closes, after a file dialog closes, ...)
// paints over its sibling controls and makes them "disappear". The panels are now
// painted by the main window itself (see PaintPanels) and are rebuilt by LayoutAll.
static const int kPanelCount = 3;
static RECT g_panelRc[kPanelCount];
static HWND g_lblSrcEmu, g_srcEmu;
static HWND g_lblSave,   g_srcFile, g_btnSrcBrowse;
static HWND g_lblRom,    g_romFile, g_btnRomBrowse;
static HWND g_lblType,   g_type;
static HWND g_lblTgt,    g_tgtEmu;
static HWND g_lblOutDir, g_outDir, g_btnOutDirBrowse, g_lblOutDirHint;
static HWND g_backup, g_convert, g_lblLog, g_log;

static rsc::Lang g_lang = rsc::LANG_EN;
static bool      g_darkTheme = false;
static HBRUSH    g_brushWindow = nullptr;
static HBRUSH    g_brushEdit = nullptr;
static HBRUSH    g_brushSurface = nullptr;
static float     g_scale = 1.0f;

static LRESULT CALLBACK TabSubclassProc(HWND hwnd, UINT msg, WPARAM wParam,
                                         LPARAM lParam, UINT_PTR subclassId, DWORD_PTR refData);

// A restrained modern palette. Custom-drawn controls use these colours in both
// themes; native edit/combo controls remain native for accessibility and keyboard support.
static COLORREF ThemeWindowColor()      { return g_darkTheme ? RGB(27, 30, 35) : RGB(246, 248, 251); }
static COLORREF ThemeSurfaceColor()     { return g_darkTheme ? RGB(36, 40, 46) : RGB(255, 255, 255); }
static COLORREF ThemeEditColor()        { return g_darkTheme ? RGB(29, 32, 37) : RGB(255, 255, 255); }
static COLORREF ThemeTextColor()        { return g_darkTheme ? RGB(233, 236, 241) : RGB(35, 42, 52); }
static COLORREF ThemeMutedTextColor()   { return g_darkTheme ? RGB(169, 176, 186) : RGB(100, 110, 123); }
static COLORREF ThemeBorderColor()      { return g_darkTheme ? RGB(65, 72, 82) : RGB(218, 224, 232); }
static COLORREF ThemeControlHoverColor(){ return g_darkTheme ? RGB(49, 54, 62) : RGB(239, 243, 249); }
static COLORREF ThemeControlDownColor() { return g_darkTheme ? RGB(57, 63, 72) : RGB(229, 235, 244); }
static COLORREF ThemeAccentColor()      { return g_darkTheme ? RGB(76, 133, 242) : RGB(45, 105, 230); }
static COLORREF ThemeAccentHoverColor() { return g_darkTheme ? RGB(94, 148, 250) : RGB(36, 91, 210); }
static COLORREF ThemeAccentDownColor()  { return g_darkTheme ? RGB(62, 115, 220) : RGB(29, 78, 190); }
static COLORREF ThemeAccentTextColor()  { return RGB(255, 255, 255); }

static void RecreateThemeBrushes() {
    if (g_brushWindow) { DeleteObject(g_brushWindow); g_brushWindow = nullptr; }
    if (g_brushEdit) { DeleteObject(g_brushEdit); g_brushEdit = nullptr; }
    if (g_brushSurface) { DeleteObject(g_brushSurface); g_brushSurface = nullptr; }
    // Keep app-owned brushes in light as well as dark mode so the UI never falls
    // back to the raised, system-grey Win9x button face between repaints.
    g_brushWindow = CreateSolidBrush(ThemeWindowColor());
    g_brushEdit = CreateSolidBrush(ThemeEditColor());
    g_brushSurface = CreateSolidBrush(ThemeSurfaceColor());
}

static HBRUSH ThemeWindowBrush() {
    return g_brushWindow ? g_brushWindow : GetSysColorBrush(COLOR_BTNFACE);
}
static HBRUSH ThemeSurfaceBrush() {
    return g_brushSurface ? g_brushSurface : GetSysColorBrush(COLOR_WINDOW);
}
static HBRUSH ThemeEditBrush() {
    return g_brushEdit ? g_brushEdit : GetSysColorBrush(COLOR_WINDOW);
}

static int SX(int x) { return static_cast<int>(x * g_scale + 0.5f); }

static void DrawRoundedSurface(HDC dc, const RECT& rc, COLORREF fillColor,
                               COLORREF borderColor, int radius) {
    HBRUSH brush = CreateSolidBrush(fillColor);
    HPEN pen = CreatePen(PS_SOLID, (SX(1) > 0 ? SX(1) : 1), borderColor);
    if (!brush || !pen) {
        if (brush) DeleteObject(brush);
        if (pen) DeleteObject(pen);
        return;
    }
    HGDIOBJ oldBrush = SelectObject(dc, brush);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    RoundRect(dc, rc.left, rc.top, rc.right, rc.bottom, SX(radius), SX(radius));
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);
}

// ========================================================================================
// Win32 implementation of the engine's filesystem interface
// ========================================================================================
struct WinEnv : rsc::IEnv {
    bool ReadFileBytes(const std::wstring& path, std::vector<uint8_t>& out) override {
        out.clear();
        HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return false;
        bool ok = false;
        LARGE_INTEGER sz = {};
        if (GetFileSizeEx(h, &sz) && sz.QuadPart >= 0 &&
            sz.QuadPart < 0x20000000 /* 512 MB sanity cap */) {
            try {
                out.resize(static_cast<size_t>(sz.QuadPart));
            } catch (...) {
                CloseHandle(h);
                out.clear();
                return false;
            }

            size_t offset = 0;
            ok = true; // an empty file is a successful read with zero bytes
            while (offset < out.size()) {
                const size_t remaining = out.size() - offset;
                const DWORD chunk = remaining > MAXDWORD ? MAXDWORD
                                                         : static_cast<DWORD>(remaining);
                DWORD rd = 0;
                if (!ReadFile(h, out.data() + offset, chunk, &rd, nullptr) || rd == 0) {
                    ok = false;
                    break;
                }
                offset += rd;
            }
            if (offset != out.size()) ok = false;
        }
        CloseHandle(h);
        if (!ok) out.clear();
        return ok;
    }

    bool WriteFileBytes(const std::wstring& path, const uint8_t* data, size_t size) override {
        if (size > 0 && data == nullptr) return false;

        // Write beside the destination, then replace it only after the complete
        // payload has been flushed. A failed/short write therefore cannot truncate
        // an existing save file.
        const size_t slash = path.find_last_of(L"\\/");
        const std::wstring directory =
            (slash == std::wstring::npos) ? L".\\" : path.substr(0, slash + 1);
        wchar_t tempPath[MAX_PATH + 1] = {0};
        if (!GetTempFileNameW(directory.c_str(), L"RSC", 0, tempPath)) return false;

        HANDLE h = CreateFileW(tempPath, GENERIC_WRITE, 0, nullptr, TRUNCATE_EXISTING,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) {
            DeleteFileW(tempPath);
            return false;
        }

        bool ok = true;
        size_t offset = 0;
        while (offset < size) {
            const size_t remaining = size - offset;
            const DWORD chunk = remaining > MAXDWORD ? MAXDWORD
                                                     : static_cast<DWORD>(remaining);
            DWORD written = 0;
            if (!WriteFile(h, data + offset, chunk, &written, nullptr) || written == 0) {
                ok = false;
                break;
            }
            offset += written;
        }
        if (ok && !FlushFileBuffers(h)) ok = false;
        if (!CloseHandle(h)) ok = false;
        if (ok && MoveFileExW(tempPath, path.c_str(),
                              MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            return true;

        DeleteFileW(tempPath);
        return false;
    }

    bool FileExists(const std::wstring& path) override {
        DWORD a = GetFileAttributesW(path.c_str());
        return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
    }
};

// ========================================================================================
// Small helpers
// ========================================================================================
static std::wstring ToLowerW(std::wstring s) {
    for (auto& c : s) c = static_cast<wchar_t>(towlower(c));
    return s;
}

static std::wstring GetExtOf(const std::wstring& path) {
    const size_t n = path.find_last_of(L"\\/");
    const std::wstring name =
        (n == std::wstring::npos) ? path : path.substr(n + 1);
    const size_t d = name.find_last_of(L'.');
    return (d == std::wstring::npos || d == 0) ? std::wstring()
                                               : ToLowerW(name.substr(d));
}

static std::wstring GetDirOf(const std::wstring& path) {
    const size_t n = path.find_last_of(L"\\/");
    return (n == std::wstring::npos) ? std::wstring() : path.substr(0, n + 1);
}

static std::wstring GetBaseNoExtOf(const std::wstring& path) {
    const size_t n = path.find_last_of(L"\\/");
    const std::wstring name =
        (n == std::wstring::npos) ? path : path.substr(n + 1);
    const size_t d = name.find_last_of(L'.');
    return (d == std::wstring::npos || d == 0) ? name : name.substr(0, d);
}

static bool SamePathW(const std::wstring& a, const std::wstring& b) {
    return ToLowerW(a) == ToLowerW(b);
}

static std::wstring GetEditText(HWND edit) {
    int len = GetWindowTextLengthW(edit);
    std::wstring s(static_cast<size_t>(len) + 1, L'\0');
    GetWindowTextW(edit, &s[0], len + 1);
    s.resize(static_cast<size_t>(len));
    // trim surrounding spaces
    size_t b = s.find_first_not_of(L" \t");
    size_t e = s.find_last_not_of(L" \t");
    if (b == std::wstring::npos) return std::wstring();
    return s.substr(b, e - b + 1);
}

static void LogLine(const std::wstring& line) {
    int len = GetWindowTextLengthW(g_log);
    SendMessageW(g_log, EM_SETSEL, len, len);
    std::wstring nl = line + L"\r\n";
    SendMessageW(g_log, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(nl.c_str()));
    SendMessageW(g_log, EM_SCROLLCARET, 0, 0);
}

static void LogIssue(const rsc::Issue& issue) {
    using rsc::Severity;
    const wchar_t* tag = (issue.severity == Severity::Error)   ? Str(S_LogError, g_lang)
                       : (issue.severity == Severity::Warning) ? Str(S_LogWarn, g_lang)
                                                               : Str(S_LogInfo, g_lang);
    LogLine(std::wstring(L"[") + tag + L"] " + rsc::FormatIssue(issue, g_lang));
}

static std::wstring ExeDirW() {
    wchar_t buf[MAX_PATH + 1] = {0};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring p = buf;
    const size_t n = p.find_last_of(L"\\/");
    return (n == std::wstring::npos) ? std::wstring(L".\\") : p.substr(0, n + 1);
}

static std::wstring UserConfigIniW() {
    wchar_t localAppData[MAX_PATH + 1] = {};
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA | CSIDL_FLAG_CREATE,
                                   nullptr, SHGFP_TYPE_CURRENT, localAppData))) {
        std::wstring dir = localAppData;
        if (!dir.empty() && dir.back() != L'\\' && dir.back() != L'/') dir += L'\\';
        dir += L"RetroSaveConverter";
        if (CreateDirectoryW(dir.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS)
            return dir + L"\\RetroSaveConverter.ini";
    }
    return ExeDirW() + L"RetroSaveConverter.ini";
}

static bool ReadIniValue(const std::wstring& ini, const wchar_t* key, wchar_t* value,
                         DWORD valueCount) {
    return GetPrivateProfileStringW(L"Settings", key, L"", value, valueCount,
                                    ini.c_str()) != 0;
}

static bool ReadSettingValue(const wchar_t* key, wchar_t* value, DWORD valueCount) {
    const std::wstring portableIni = ExeDirW() + L"RetroSaveConverter.ini";
    if (ReadIniValue(portableIni, key, value, valueCount)) return true;
    const std::wstring userIni = UserConfigIniW();
    return userIni != portableIni && ReadIniValue(userIni, key, value, valueCount);
}

static void SaveSettingValue(const wchar_t* key, const wchar_t* value) {
    const std::wstring portableIni = ExeDirW() + L"RetroSaveConverter.ini";
    if (WritePrivateProfileStringW(L"Settings", key, value, portableIni.c_str())) return;
    const std::wstring userIni = UserConfigIniW();
    if (userIni != portableIni)
        WritePrivateProfileStringW(L"Settings", key, value, userIni.c_str());
}

static void SaveLangIni() {
    SaveSettingValue(L"Language", (g_lang == rsc::LANG_RU) ? L"ru" : L"en");
}

static void LoadLangIni() {
    wchar_t value[16] = {};
    const bool found = ReadSettingValue(L"Language", value,
                                        static_cast<DWORD>(std::size(value)));
    const std::wstring saved = found ? ToLowerW(value) : L"en";
    // Accept textual values and legacy numeric values from older INI files.
    g_lang = (saved == L"ru" || saved == L"1") ? rsc::LANG_RU : rsc::LANG_EN;
}

static void SaveThemeIni() {
    SaveSettingValue(L"DarkTheme", g_darkTheme ? L"1" : L"0");
}

static void LoadThemeIni() {
    wchar_t value[16] = {};
    const bool found = ReadSettingValue(L"DarkTheme", value,
                                        static_cast<DWORD>(std::size(value)));
    const std::wstring saved = found ? ToLowerW(value) : L"0";
    g_darkTheme = (saved == L"1" || saved == L"true" || saved == L"dark");
}

// ========================================================================================
// Control factory helpers
// ========================================================================================
static HWND MkStatic(HWND parent, int id, DWORD style, int x, int y, int w, int h) {
    return CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | style,
                           x, y, w, h,
                           parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                           g_hInst, nullptr);
}

static HWND MkButton(HWND parent, int id, DWORD style, int x, int y, int w, int h) {
    // Custom-draw push buttons use the same flat, rounded theme.
    // Checkboxes stay native so their keyboard and accessibility behavior is retained.
    const DWORD type = style & BS_TYPEMASK;
    const bool pushButton = type == BS_PUSHBUTTON || type == BS_DEFPUSHBUTTON;
    if (pushButton) style = (style & ~BS_TYPEMASK) | BS_OWNERDRAW;
    return CreateWindowExW(0, L"BUTTON", L"",
                           WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP | style,
                           x, y, w, h, parent,
                           reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), g_hInst,
                           nullptr);
}

static bool IsOwnerDrawPushButton(HWND control) {
    return control && (control == g_btnSrcBrowse || control == g_btnRomBrowse ||
                       control == g_btnOutDirBrowse || control == g_convert);
}

static HWND MkEdit(HWND parent, int id, int x, int y, int w, int h) {
    return CreateWindowExW(0, L"EDIT", L"",
                           WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP | WS_BORDER |
                               ES_AUTOHSCROLL,
                           x, y, w, h, parent,
                           reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), g_hInst,
                           nullptr);
}

static HWND MkCombo(HWND parent, int id, int x, int y, int w, int h) {
    return CreateWindowExW(0, L"COMBOBOX", L"",
                           WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP |
                               CBS_DROPDOWNLIST | WS_VSCROLL,
                           x, y, w, h, parent,
                           reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), g_hInst,
                           nullptr);
}

static HWND* const kNesCtrls[] = {
    &g_lblSrcEmu, &g_srcEmu, &g_lblSave, &g_srcFile, &g_btnSrcBrowse,
    &g_lblRom, &g_romFile, &g_btnRomBrowse,
    &g_lblType, &g_type,
    &g_lblTgt, &g_tgtEmu,
    &g_lblOutDir, &g_outDir, &g_btnOutDirBrowse, &g_lblOutDirHint,
    &g_backup, &g_convert, &g_lblLog, &g_log,
};

static void ShowNesPage(bool show) {
    for (HWND* h : kNesCtrls)
        if (*h) ShowWindow(*h, show ? SW_SHOW : SW_HIDE);
}

// ========================================================================================
// Layout
// ========================================================================================
static void LayoutAll() {
    RECT rc = {};
    if (!g_main || !g_tabs || !GetClientRect(g_main, &rc)) return;

    RECT disp = rc;
    SendMessageW(g_tabs, TCM_ADJUSTRECT, FALSE, reinterpret_cast<LPARAM>(&disp));
    // The tab control is only the strip with the three captions. It must NOT cover
    // the whole client area: a full-size tab control would sit underneath the panels
    // that the main window paints and hide them.
    MoveWindow(g_tabs, 0, 0, rc.right, (disp.top > SX(28)) ? disp.top : SX(28), TRUE);
    const int x0 = disp.left + SX(10);
    const int W  = disp.right - disp.left - SX(20);

    const int lblW  = SX(132);
    const int cx    = x0 + SX(140);
    const int cw    = W - SX(140) - SX(96);
    const int brw   = SX(88);
    const int brx   = x0 + W - SX(88);
    const int rowH  = SX(26);
    const int rowP  = SX(32);

    int y = disp.top + SX(8);

    // -- group: source ---------------------------------------------------------------
    const int srcRows = 4;
    const int comboH = SX(200);   // dropdown list height (closed height is font-based)
    SetRect(&g_panelRc[0], x0, y, x0 + W, y + SX(24) + srcRows * rowP + SX(8));
    y += SX(24);
    MoveWindow(g_lblSrcEmu, x0 + SX(10), y, lblW, rowH, TRUE);
    MoveWindow(g_srcEmu, cx, y - SX(3), cw + SX(96), comboH, TRUE);
    y += rowP;
    MoveWindow(g_lblSave, x0 + SX(10), y, lblW, rowH, TRUE);
    MoveWindow(g_srcFile, cx, y - SX(2), cw, rowH + SX(2), TRUE);
    MoveWindow(g_btnSrcBrowse, brx, y - SX(2), brw, rowH + SX(2), TRUE);
    y += rowP;
    MoveWindow(g_lblRom, x0 + SX(10), y, lblW, rowH, TRUE);
    MoveWindow(g_romFile, cx, y - SX(2), cw, rowH + SX(2), TRUE);
    MoveWindow(g_btnRomBrowse, brx, y - SX(2), brw, rowH + SX(2), TRUE);
    y += rowP;
    MoveWindow(g_lblType, x0 + SX(10), y, lblW, rowH, TRUE);
    MoveWindow(g_type, cx, y - SX(3), cw + SX(96), comboH, TRUE);
    y += rowP + SX(12);

    // -- group: target ---------------------------------------------------------------
    SetRect(&g_panelRc[1], x0, y, x0 + W,
            y + SX(24) + 2 * rowP + SX(2) + SX(14) + SX(10));
    y += SX(24);
    MoveWindow(g_lblTgt, x0 + SX(10), y, lblW, rowH, TRUE);
    MoveWindow(g_tgtEmu, cx, y - SX(3), cw + SX(96), comboH, TRUE);
    y += rowP;
    MoveWindow(g_lblOutDir, x0 + SX(10), y, lblW, rowH, TRUE);
    MoveWindow(g_outDir, cx, y - SX(2), cw, rowH + SX(2), TRUE);
    MoveWindow(g_btnOutDirBrowse, brx, y - SX(2), brw, rowH + SX(2), TRUE);
    y += rowP + SX(2);
    MoveWindow(g_lblOutDirHint, cx, y, W - SX(140), SX(14), TRUE);
    y += SX(16) + SX(10);

    // -- group: options --------------------------------------------------------------
    SetRect(&g_panelRc[2], x0, y, x0 + W, y + SX(24) + rowP + SX(6));
    y += SX(24);
    MoveWindow(g_backup, x0 + SX(10), y, W - SX(20), rowH + SX(2), TRUE);
    y += rowP + SX(6);

    // -- convert + log ---------------------------------------------------------------
    MoveWindow(g_convert, cx, y, SX(150), rowH + SX(8), TRUE);
    y += rowP + SX(12);
    MoveWindow(g_lblLog, x0, y, W, SX(16), TRUE);
    y += SX(20);
    const int logH = disp.bottom - y;
    MoveWindow(g_log, x0, y, W, (logH > SX(60)) ? logH : SX(60), TRUE);

    // placeholder label for SNES/N64 tabs (centered)
    MoveWindow(g_placeholder, disp.left + SX(10), disp.top + SX(40),
               disp.right - disp.left - SX(20), SX(60), TRUE);
}

// ========================================================================================
// Language handling
// ========================================================================================
static Emu GetComboEmu(HWND combo, Emu fallback) {
    if (!combo) return fallback;
    const LRESULT selection = SendMessageW(combo, CB_GETCURSEL, 0, 0);
    if (selection == CB_ERR) return fallback;
    const LRESULT data = SendMessageW(combo, CB_GETITEMDATA, static_cast<WPARAM>(selection), 0);
    if (data == CB_ERR) return fallback;
    return static_cast<Emu>(static_cast<int>(data));
}

static void SetComboEmu(HWND combo, Emu emu) {
    if (!combo) return;
    const LRESULT count = SendMessageW(combo, CB_GETCOUNT, 0, 0);
    for (LRESULT i = 0; i < count; ++i) {
        const LRESULT data = SendMessageW(combo, CB_GETITEMDATA, static_cast<WPARAM>(i), 0);
        if (data != CB_ERR && static_cast<Emu>(static_cast<int>(data)) == emu) {
            SendMessageW(combo, CB_SETCURSEL, static_cast<WPARAM>(i), 0);
            return;
        }
    }
}

static void AddEmuChoice(HWND combo, Emu emu, const wchar_t* name) {
    const LRESULT index = SendMessageW(combo, CB_ADDSTRING, 0,
                                       reinterpret_cast<LPARAM>(name));
    if (index != CB_ERR && index != CB_ERRSPACE)
        SendMessageW(combo, CB_SETITEMDATA, static_cast<WPARAM>(index),
                     static_cast<LPARAM>(static_cast<int>(emu)));
}

static void RebuildCombos() {
    // Auto-detect is a command rather than an emulator, so it remains pinned first.
    // Actual emulator entries are alphabetized identically in source and target lists.
    // Item data (not row number) carries the enum: changing presentation order can
    // therefore never change which emulator is selected for conversion.
    const Emu sourceSelection = GetComboEmu(g_srcEmu, Emu::Auto);
    const Emu targetSelection = GetComboEmu(g_tgtEmu, Emu::Nestopia);
    const int typeSelection = ComboBox_GetCurSel(g_type);

    ComboBox_ResetContent(g_srcEmu);
    AddEmuChoice(g_srcEmu, Emu::Auto, Str(S_AutoDetect, g_lang));
    AddEmuChoice(g_srcEmu, Emu::Ares, L"ares (NES)");
    AddEmuChoice(g_srcEmu, Emu::FCEUX, L"FCEUX");
    if (g_lang == LANG_EN) AddEmuChoice(g_srcEmu, Emu::Raw, Str(S_GenericSav, g_lang));
    AddEmuChoice(g_srcEmu, Emu::MesenCE, L"MesenCE");
    AddEmuChoice(g_srcEmu, Emu::Nestopia, L"Nestopia");
    AddEmuChoice(g_srcEmu, Emu::Nintendulator, L"Nintendulator");
    AddEmuChoice(g_srcEmu, Emu::PuNES, L"puNES");
    if (g_lang == LANG_RU) AddEmuChoice(g_srcEmu, Emu::Raw, Str(S_GenericSav, g_lang));
    SetComboEmu(g_srcEmu, sourceSelection);

    ComboBox_ResetContent(g_tgtEmu);
    AddEmuChoice(g_tgtEmu, Emu::Ares, L"ares (NES)");
    AddEmuChoice(g_tgtEmu, Emu::FCEUX, L"FCEUX");
    if (g_lang == LANG_EN) AddEmuChoice(g_tgtEmu, Emu::Raw, Str(S_GenericSav, g_lang));
    AddEmuChoice(g_tgtEmu, Emu::MesenCE, L"MesenCE");
    AddEmuChoice(g_tgtEmu, Emu::Nestopia, L"Nestopia");
    AddEmuChoice(g_tgtEmu, Emu::Nintendulator, L"Nintendulator");
    AddEmuChoice(g_tgtEmu, Emu::PuNES, L"puNES");
    if (g_lang == LANG_RU) AddEmuChoice(g_tgtEmu, Emu::Raw, Str(S_GenericSav, g_lang));
    SetComboEmu(g_tgtEmu, targetSelection);

    // Save-data kinds keep their semantic order (Auto, SRAM, EEPROM variants).
    const wchar_t* typeNames[5] = {
        Str(S_AutoDetect, g_lang), Str(S_TypeSram, g_lang), Str(S_Type24C01, g_lang),
        Str(S_Type24C02, g_lang), Str(S_TypeDatach, g_lang)};
    ComboBox_ResetContent(g_type);
    for (int i = 0; i < 5; ++i) ComboBox_AddString(g_type, typeNames[i]);
    ComboBox_SetCurSel(g_type, (typeSelection == CB_ERR || typeSelection < 0) ? 0 : typeSelection);
}

// ========================================================================================
// Native menu lifecycle
// ========================================================================================
static bool SetMenuLabel(HMENU menu, UINT item, bool byPosition, const wchar_t* text) {
    if (!menu || !text) return false;

    MENUITEMINFOW info = {};
    info.cbSize = sizeof(info);
    info.fMask = MIIM_STRING;
    info.dwTypeData = const_cast<LPWSTR>(text);
    info.cch = static_cast<UINT>(lstrlenW(text));
    return SetMenuItemInfoW(menu, item, byPosition ? TRUE : FALSE, &info) != FALSE;
}

static bool EnsureMainMenu() {
    if (!g_main || !IsWindow(g_main)) return false;
    if (g_menuBar && GetMenu(g_main) == g_menuBar && g_menuLanguage && g_menuView && g_menuHelp)
        return true;

    if (g_menuBar) {
        DestroyMenu(g_menuBar);
        g_menuBar = nullptr;
        g_menuLanguage = nullptr;
        g_menuView = nullptr;
        g_menuHelp = nullptr;
    }

    HMENU bar = CreateMenu();
    HMENU language = CreatePopupMenu();
    HMENU view = CreatePopupMenu();
    HMENU help = CreatePopupMenu();
    if (!bar || !language || !view || !help) {
        if (bar) DestroyMenu(bar);
        if (language) DestroyMenu(language);
        if (view) DestroyMenu(view);
        if (help) DestroyMenu(help);
        return false;
    }

    const bool languageItemsOk =
        AppendMenuW(language, MF_STRING, IDM_LANG_EN, L"English") != FALSE &&
        AppendMenuW(language, MF_STRING, IDM_LANG_RU, L"Русский") != FALSE;
    const bool viewItemsOk =
        AppendMenuW(view, MF_STRING, IDM_THEME_LIGHT, L"Light theme") != FALSE &&
        AppendMenuW(view, MF_STRING, IDM_THEME_DARK, L"Dark theme") != FALSE;
    const bool helpItemsOk = AppendMenuW(help, MF_STRING, IDM_ABOUT, L"About") != FALSE;

    bool languageAttached = false, viewAttached = false, helpAttached = false;
    if (languageItemsOk)
        languageAttached = AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(language),
                                       L"Language") != FALSE;
    if (viewItemsOk)
        viewAttached = AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(view), L"View") != FALSE;
    if (helpItemsOk)
        helpAttached = AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(help), L"Help") != FALSE;

    if (!languageItemsOk || !viewItemsOk || !helpItemsOk || !languageAttached ||
        !viewAttached || !helpAttached || !SetMenu(g_main, bar)) {
        DestroyMenu(bar); // frees popup menus already attached to it
        if (!languageAttached) DestroyMenu(language);
        if (!viewAttached) DestroyMenu(view);
        if (!helpAttached) DestroyMenu(help);
        return false;
    }

    g_menuBar = bar;
    g_menuLanguage = language;
    g_menuView = view;
    g_menuHelp = help;
    return true;
}

static bool UpdateMenuLanguage(rsc::Lang lang) {
    if (!EnsureMainMenu()) return false;

    bool labelsOk = true;
    labelsOk &= SetMenuLabel(g_menuBar, 0, true, Str(S_MenuLanguage, lang));
    labelsOk &= SetMenuLabel(g_menuBar, 1, true, Str(S_MenuView, lang));
    labelsOk &= SetMenuLabel(g_menuBar, 2, true, Str(S_MenuHelp, lang));
    labelsOk &= SetMenuLabel(g_menuLanguage, IDM_LANG_EN, false, Str(S_MenuEnglish, lang));
    labelsOk &= SetMenuLabel(g_menuLanguage, IDM_LANG_RU, false, Str(S_MenuRussian, lang));
    labelsOk &= SetMenuLabel(g_menuView, IDM_THEME_LIGHT, false, Str(S_MenuLightTheme, lang));
    labelsOk &= SetMenuLabel(g_menuView, IDM_THEME_DARK, false, Str(S_MenuDarkTheme, lang));
    labelsOk &= SetMenuLabel(g_menuHelp, IDM_ABOUT, false, Str(S_MenuAbout, lang));

    CheckMenuRadioItem(g_menuLanguage, IDM_LANG_EN, IDM_LANG_RU,
                       IDM_LANG_EN + static_cast<int>(lang), MF_BYCOMMAND);
    CheckMenuRadioItem(g_menuView, IDM_THEME_LIGHT, IDM_THEME_DARK,
                       g_darkTheme ? IDM_THEME_DARK : IDM_THEME_LIGHT, MF_BYCOMMAND);
    DrawMenuBar(g_main);
    return labelsOk;
}

static void DestroyMainMenu(HWND hwnd) {
    if (!g_menuBar) return;
    if (IsWindow(hwnd) && GetMenu(hwnd) == g_menuBar) SetMenu(hwnd, nullptr);
    DestroyMenu(g_menuBar);
    g_menuBar = nullptr;
    g_menuLanguage = nullptr;
    g_menuView = nullptr;
    g_menuHelp = nullptr;
}

static void RefreshWindowPainting(HWND hwnd) {
    if (!IsWindow(hwnd)) return;
    DrawMenuBar(hwnd);
    RedrawWindow(hwnd, nullptr, nullptr,
                 RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
}

static void ApplyControlTheme(HWND control) {
    if (!control || !IsWindow(control)) return;
    // Native edits and combo boxes retain their OS interaction/keyboard behavior;
    // owner-drawn controls and the parent use the app palette for a consistent look.
    SetWindowTheme(control, g_darkTheme ? L"" : nullptr, g_darkTheme ? L"" : nullptr);
    InvalidateRect(control, nullptr, TRUE);
}

static void ApplyTheme(bool dark, bool saveIni) {
    g_darkTheme = dark;
    RecreateThemeBrushes();

    if (g_tabs) ApplyControlTheme(g_tabs);
    for (HWND* control : kNesCtrls) if (*control) ApplyControlTheme(*control);
    if (g_placeholder) ApplyControlTheme(g_placeholder);

    if (g_menuView) {
        CheckMenuRadioItem(g_menuView, IDM_THEME_LIGHT, IDM_THEME_DARK,
                           g_darkTheme ? IDM_THEME_DARK : IDM_THEME_LIGHT, MF_BYCOMMAND);
        if (g_main) DrawMenuBar(g_main);
    }
    if (saveIni) SaveThemeIni();
    if (g_main) RefreshWindowPainting(g_main);
}

static void ApplyLanguage(rsc::Lang lang, bool saveIni) {
    g_lang = lang;

    SetWindowTextW(g_main, Str(S_AppTitle, lang));

    SetWindowTextW(g_lblSrcEmu, Str(S_SrcEmu, lang));
    SetWindowTextW(g_lblSave, Str(S_SaveFile, lang));
    SetWindowTextW(g_lblRom, Str(S_RomFile, lang));
    SetWindowTextW(g_lblType, Str(S_ContentType, lang));
    SetWindowTextW(g_lblTgt, Str(S_TargetEmu, lang));
    SetWindowTextW(g_lblOutDir, Str(S_OutDir, lang));
    SetWindowTextW(g_lblOutDirHint, Str(S_OutDirHint, lang));
    SetWindowTextW(g_lblLog, Str(S_Log, lang));

    SetWindowTextW(g_btnSrcBrowse, Str(S_Browse, lang));
    SetWindowTextW(g_btnRomBrowse, Str(S_Browse, lang));
    SetWindowTextW(g_btnOutDirBrowse, Str(S_Browse, lang));
    SetWindowTextW(g_backup, Str(S_Backup, lang));
    SetWindowTextW(g_convert, Str(S_Convert, lang));

    RebuildCombos();

    // tab captions
    TCITEMW ti;
    ZeroMemory(&ti, sizeof(ti));
    ti.mask = TCIF_TEXT;
    ti.pszText = const_cast<LPWSTR>(Str(S_TabNes, lang));
    TabCtrl_SetItem(g_tabs, 0, &ti);
    ti.pszText = const_cast<LPWSTR>(Str(S_TabSnes, lang));
    TabCtrl_SetItem(g_tabs, 1, &ti);
    ti.pszText = const_cast<LPWSTR>(Str(S_TabN64, lang));
    TabCtrl_SetItem(g_tabs, 2, &ti);

    const int cur = TabCtrl_GetCurSel(g_tabs);
    SetWindowTextW(g_placeholder, (cur == 2) ? Str(S_ComingSoonN64, lang)
                                             : Str(S_ComingSoonSnes, lang));

    // Keep native menu handles stable. Recreating a menu on every language
    // change creates unnecessary ownership risk and can leave a stale menu
    // attached if SetMenu fails.
    UpdateMenuLanguage(lang);

    if (g_main) {
        RedrawWindow(g_main, nullptr, nullptr,
                     RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
    }
    if (saveIni) SaveLangIni();
}

// ========================================================================================
// File pickers
// ========================================================================================
// Keep file selection on the standard Win32 dialog APIs. They run with the
// existing main-window message loop and do not require a second COM modal UI path.
// Do not force a full parent/child redraw when the modal dialog closes: native
// ComboBox list windows and the app's owner-drawn buttons repaint themselves.
static std::wstring PickOpenFile(HWND owner, const wchar_t* title, const wchar_t* filter) {
    std::vector<wchar_t> buffer(32768, L'\0');
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = buffer.data();
    ofn.nMaxFile = static_cast<DWORD>(buffer.size());
    ofn.lpstrTitle = title;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY |
                OFN_EXPLORER | OFN_NOCHANGEDIR;
    ofn.nFilterIndex = 1;

    if (GetOpenFileNameW(&ofn)) return std::wstring(buffer.data());
    const DWORD error = CommDlgExtendedError();
    if (error != 0) {
        wchar_t message[192] = {};
        if (g_lang == LANG_RU) {
            wsprintfW(message, L"Не удалось открыть диалог выбора файла (код 0x%08lX).", error);
        } else {
            wsprintfW(message, L"Could not open the file picker (error 0x%08lX).", error);
        }
        MessageBoxW(owner, message, Str(S_AppTitle, g_lang), MB_OK | MB_ICONWARNING);
    }
    return std::wstring(); // Cancel is normal; leave the form untouched.
}

static std::wstring PickFolder(HWND owner, const wchar_t* title) {
    BROWSEINFOW bi = {};
    bi.hwndOwner = owner;
    bi.lpszTitle = title;
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE | BIF_USENEWUI;

    std::wstring result;
    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (pidl) {
        wchar_t path[MAX_PATH + 1] = {};
        if (SHGetPathFromIDListW(pidl, path)) result = path;
        CoTaskMemFree(pidl);
    }
    if (!result.empty() && result.back() != L'\\' && result.back() != L'/')
        result += L'\\';
    return result;
}

// after a save file is selected, try to auto-fill the ROM field with the sibling .nes
static void TryAutoRom(const std::wstring& savePath) {
    std::wstring rom = GetEditText(g_romFile);
    if (!rom.empty()) return;
    WinEnv env;

    std::wstring saveName = savePath;
    const size_t slash = saveName.find_last_of(L"\\/");
    if (slash != std::wstring::npos) saveName = saveName.substr(slash + 1);
    saveName = ToLowerW(saveName);

    std::wstring guess;
    if (saveName == L"save.ram" || saveName == L"save.eeprom" ||
        saveName == L"external.eeprom") {
        // ares' fixed save files live in <game>.pak; the ROM is normally next
        // to that package and named after the package, not after "save".
        std::wstring packageDir = GetDirOf(savePath);
        while (!packageDir.empty() &&
               (packageDir.back() == L'\\' || packageDir.back() == L'/'))
            packageDir.pop_back();
        if (GetExtOf(packageDir) == L".pak")
            guess = GetDirOf(packageDir) + GetBaseNoExtOf(packageDir) + L".nes";
    }
    if (guess.empty())
        guess = GetDirOf(savePath) + GetBaseNoExtOf(savePath) + L".nes";

    if (env.FileExists(guess)) {
        SetWindowTextW(g_romFile, guess.c_str());
        LogLine(rsc::Expand(Str(S_DroppedRom, g_lang), {guess}));
    }
}


// ========================================================================================
// Conversion
// ========================================================================================
static void OnConvert() {
    using namespace rsc;

    const std::wstring srcPath = GetEditText(g_srcFile);
    if (srcPath.empty()) {
        MessageBoxW(g_main, Str(S_ErrNoSource, g_lang), Str(S_AppTitle, g_lang),
                    MB_OK | MB_ICONWARNING);
        return;
    }

    WinEnv env;
    ConvertRequest req;
    req.sourcePath = srcPath;
    if (!env.ReadFileBytes(srcPath, req.sourceBytes)) {
        MessageBoxW(g_main, Expand(Str(S_ErrReadSave, g_lang), {srcPath}).c_str(),
                    Str(S_AppTitle, g_lang), MB_OK | MB_ICONERROR);
        return;
    }

    const std::wstring romPath = GetEditText(g_romFile);
    if (!romPath.empty()) {
        std::vector<uint8_t> romBytes;
        if (!env.ReadFileBytes(romPath, romBytes)) {
            MessageBoxW(g_main, Expand(Str(S_ErrReadRom, g_lang), {romPath}).c_str(),
                        Str(S_AppTitle, g_lang), MB_OK | MB_ICONERROR);
            return;
        }
        req.rom = ParseNesHeader(romBytes.data(), romBytes.size());
    }

    req.sourceFormat = GetComboEmu(g_srcEmu, Emu::Auto);
    const int typeSel = ComboBox_GetCurSel(g_type);
    req.saveType = (typeSel == CB_ERR) ? SaveType::Auto : static_cast<SaveType>(typeSel);
    req.target = GetComboEmu(g_tgtEmu, Emu::Nestopia);
    req.outputDir = GetEditText(g_outDir);
    req.backup = (SendMessageW(g_backup, BM_GETCHECK, 0, 0) == BST_CHECKED);

    LogLine(L"");
    LogLine(std::wstring(L"=== ") + Str(S_Convert, g_lang) + L" ===");

    ConvertResult res = Convert(req, env);

    // ---- confirm destructive writes --------------------------------------------------
    std::wstring overList;
    bool replaceSourceChanged = false;
    for (const auto& o : res.outputs) {
        if (SamePathW(o.path, srcPath)) {
            const bool identical = o.bytes.size() == req.sourceBytes.size() &&
                                   std::equal(o.bytes.begin(), o.bytes.end(),
                                              req.sourceBytes.begin());
            if (!identical) replaceSourceChanged = true;
        } else if (env.FileExists(o.path)) {
            overList += o.path + L"\n";
        }
    }
    if (!overList.empty()) {
        const std::wstring msg = Expand(Str(S_AskOverwrite, g_lang), {overList});
        if (MessageBoxW(g_main, msg.c_str(), Str(S_AppTitle, g_lang),
                        MB_YESNO | MB_ICONQUESTION) != IDYES) {
            for (const auto& i : res.issues) LogIssue(i);
            LogLine(Str(S_Cancelled, g_lang));
            return;
        }
    }
    if (replaceSourceChanged) {
        const std::wstring msg =
            Expand(Str(S_AskInPlace, g_lang),
                   {req.backup ? Str(S_BackupNote, g_lang) : Str(S_NoBackupNote, g_lang)});
        if (MessageBoxW(g_main, msg.c_str(), Str(S_AppTitle, g_lang),
                        MB_YESNO | MB_ICONWARNING) != IDYES) {
            for (const auto& i : res.issues) LogIssue(i);
            LogLine(Str(S_Cancelled, g_lang));
            return;
        }
    }

    WriteOutputs(res, req, env);
    for (const auto& i : res.issues) LogIssue(i);
    LogLine(res.ok ? (res.HasWarnings() ? Str(S_DoneWarn, g_lang)
                                         : Str(S_Done, g_lang))
                   : Str(S_Failed, g_lang));
}

// ========================================================================================
// Drag & drop
// ========================================================================================
static void OnDropFiles(HDROP hd) {
    const UINT count = DragQueryFileW(hd, 0xFFFFFFFF, nullptr, 0);
    if (count == 0) {
        DragFinish(hd);
        return;
    }

    const UINT pathLength = DragQueryFileW(hd, 0, nullptr, 0);
    std::wstring path(static_cast<size_t>(pathLength) + 1, L'\0');
    const UINT copied = DragQueryFileW(hd, 0, &path[0], pathLength + 1);
    DragFinish(hd);
    if (copied == 0) return;
    path.resize(copied);
    const std::wstring ext = GetExtOf(path);
    if (ext == L".nes" || ext == L".unf") {
        SetWindowTextW(g_romFile, path.c_str());
        LogLine(rsc::Expand(Str(S_DroppedRom, g_lang), {path}));
    } else if (ext == L".sav" || ext == L".srm" || ext == L".prb" ||
               ext == L".ram" || ext == L".eeprom" || ext == L".eeprom128" ||
               ext == L".eeprom256") {
        SetWindowTextW(g_srcFile, path.c_str());
        LogLine(rsc::Expand(Str(S_DroppedSave, g_lang), {path}));
        TryAutoRom(path);
    } else {
        LogLine(rsc::Expand(Str(S_DroppedUnknown, g_lang), {path}));
    }
}

static LRESULT CALLBACK TabSubclassProc(HWND hwnd, UINT msg, WPARAM wParam,
                                         LPARAM lParam, UINT_PTR, DWORD_PTR) {
    if (msg == WM_ERASEBKGND) {
        RECT rc = {};
        GetClientRect(hwnd, &rc);
        FillRect(reinterpret_cast<HDC>(wParam), &rc, ThemeWindowBrush());
        return 1;
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

// Draws the rounded "Source / Target / Options" panels in main-window client
// coordinates. Called from WM_ERASEBKGND, which Windows also uses to obtain the
// parent background for themed child controls (DrawThemeParentBackground) -- so
// native checkboxes get the panel colour behind them too.
static void PaintPanels(HDC dc) {
    if (!g_tabs || TabCtrl_GetCurSel(g_tabs) != 0) return;   // panels belong to the NES page
    const StrId captions[kPanelCount] = {S_GrpSource, S_GrpTarget, S_GrpOptions};
    for (int i = 0; i < kPanelCount; ++i) {
        const RECT& r = g_panelRc[i];
        if (r.right <= r.left || r.bottom <= r.top) continue;

        RECT panel = r;
        InflateRect(&panel, -SX(1), -SX(1));
        DrawRoundedSurface(dc, panel, ThemeSurfaceColor(), ThemeBorderColor(), 8);

        const wchar_t* caption = Str(captions[i], g_lang);
        RECT textRc = panel;
        textRc.left += SX(12);
        textRc.top += SX(4);
        textRc.right -= SX(10);
        textRc.bottom = textRc.top + SX(20);
        const int oldMode = SetBkMode(dc, TRANSPARENT);
        const COLORREF oldColor = SetTextColor(dc, ThemeTextColor());
        HGDIOBJ oldFont = g_fontStrong ? SelectObject(dc, g_fontStrong) : nullptr;
        DrawTextW(dc, caption, -1, &textRc,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        if (oldFont) SelectObject(dc, oldFont);
        SetTextColor(dc, oldColor);
        SetBkMode(dc, oldMode);
    }
}

// The main window has WS_CLIPCHILDREN, so this erase can never paint over a child
// control; children repaint themselves (labels and buttons are opaque and use the
// same colours as the panel they sit on).
static void EraseMainBackground(HWND hwnd, HDC dc) {
    RECT client = {};
    GetClientRect(hwnd, &client);
    FillRect(dc, &client, ThemeWindowBrush());
    PaintPanels(dc);
}

static void FillSolid(HDC dc, const RECT& rc, COLORREF color) {
    const COLORREF old = SetBkColor(dc, color);
    ExtTextOutW(dc, 0, 0, ETO_OPAQUE, &rc, nullptr, 0, nullptr);
    SetBkColor(dc, old);
}

// ========================================================================================
// Window procedure
// ========================================================================================
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_NCCREATE:
            // WM_CREATE is sent from inside CreateWindowExW, before that function
            // returns. Set the global handle here so initial menu/layout work uses
            // the real window rather than nullptr.
            g_main = hwnd;
            return DefWindowProcW(hwnd, msg, wParam, lParam);
        case WM_CREATE: {
            CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);

            // DPI scale
            HDC dc = GetDC(hwnd);
            if (dc) {
                const int dpi = GetDeviceCaps(dc, LOGPIXELSX);
                if (dpi > 0) g_scale = dpi / 96.0f;
                ReleaseDC(hwnd, dc);
            }
            if (g_scale < 1.0f) g_scale = 1.0f;

            // message font
            NONCLIENTMETRICSW ncm;
            ZeroMemory(&ncm, sizeof(ncm));
            ncm.cbSize = sizeof(ncm);
            SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
            // Prefer Segoe UI for a current Windows look; keep system sizing and fall
            // back naturally to the OS message-font metrics if font creation fails.
            lstrcpynW(ncm.lfMessageFont.lfFaceName, L"Segoe UI", LF_FACESIZE);
            ncm.lfMessageFont.lfQuality = CLEARTYPE_QUALITY;
            g_font = CreateFontIndirectW(&ncm.lfMessageFont);
            LOGFONTW strong = ncm.lfMessageFont;
            strong.lfWeight = FW_SEMIBOLD;
            g_fontStrong = CreateFontIndirectW(&strong);
            if (!g_fontStrong) g_fontStrong = g_font;

            // Tabs are owner-drawn so their background and captions can follow the
            // selected theme instead of leaving a light strip in dark mode.
            g_tabs = CreateWindowExW(0, WC_TABCONTROLW, L"",
                                     WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS |
                                         TCS_TABS | TCS_FIXEDWIDTH | TCS_OWNERDRAWFIXED,
                                     0, 0, cs->cx, cs->cy, hwnd,
                                     reinterpret_cast<HMENU>(
                                         static_cast<INT_PTR>(IDC_TABS)),
                                     g_hInst, nullptr);
            if (g_tabs) {
                TabCtrl_SetItemSize(g_tabs, SX(100), SX(26));
                SetWindowSubclass(g_tabs, TabSubclassProc, 1, 0);
            }

            const int lx = SX(140), lh = SX(26);
            // NES page controls (positions set in LayoutAll)
            g_lblSrcEmu = MkStatic(hwnd, IDC_LBL_SRC_EMU, 0, 0, 0, lx, lh);
            g_srcEmu    = MkCombo(hwnd, IDC_SRC_EMU, 0, 0, lx + SX(120), lh);
            g_lblSave   = MkStatic(hwnd, IDC_LBL_SAVE, 0, 0, 0, lx, lh);
            g_srcFile   = MkEdit(hwnd, IDC_SRC_FILE, 0, 0, lx, lh);
            g_btnSrcBrowse = MkButton(hwnd, IDC_SRC_BROWSE, BS_PUSHBUTTON, 0, 0, lx, lh);
            g_lblRom    = MkStatic(hwnd, IDC_LBL_ROM, 0, 0, 0, lx, lh);
            g_romFile   = MkEdit(hwnd, IDC_ROM_FILE, 0, 0, lx, lh);
            g_btnRomBrowse = MkButton(hwnd, IDC_ROM_BROWSE, BS_PUSHBUTTON, 0, 0, lx, lh);
            g_lblType   = MkStatic(hwnd, IDC_LBL_TYPE, 0, 0, 0, lx, lh);
            g_type      = MkCombo(hwnd, IDC_TYPE, 0, 0, lx + SX(120), lh);
            g_lblTgt    = MkStatic(hwnd, IDC_LBL_TGT_EMU, 0, 0, 0, lx, lh);
            g_tgtEmu    = MkCombo(hwnd, IDC_TGT_EMU, 0, 0, lx + SX(120), lh);
            g_lblOutDir = MkStatic(hwnd, IDC_LBL_OUTDIR, 0, 0, 0, lx, lh);
            g_outDir    = MkEdit(hwnd, IDC_OUTDIR, 0, 0, lx, lh);
            g_btnOutDirBrowse =
                MkButton(hwnd, IDC_OUTDIR_BROWSE, BS_PUSHBUTTON, 0, 0, lx, lh);
            g_lblOutDirHint =
                MkStatic(hwnd, IDC_LBL_OUTDIR_HINT, 0, 0, 0, lx, SX(14));
            g_backup   = MkButton(hwnd, IDC_BACKUP, BS_AUTOCHECKBOX, 0, 0, lx, lh);
            g_convert  = MkButton(hwnd, IDC_CONVERT,
                                  BS_DEFPUSHBUTTON | BS_PUSHBUTTON, 0, 0, lx, lh + SX(8));
            g_lblLog   = MkStatic(hwnd, IDC_LBL_LOG, 0, 0, 0, lx, SX(16));
            g_log      = CreateWindowExW(
                0, L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP | WS_BORDER | WS_VSCROLL |
                    ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
                0, 0, lx, lh, hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_LOG)), g_hInst, nullptr);

            g_placeholder = MkStatic(hwnd, IDC_PLACEHOLDER, SS_CENTER, 0, 0, 10, 10);

            // Insert the three pages before ApplyLanguage sets localized captions.
            TCITEMW ti = {};
            ti.mask = TCIF_TEXT;
            ti.pszText = const_cast<LPWSTR>(L"NES / FC");
            TabCtrl_InsertItem(g_tabs, 0, &ti);
            ti.pszText = const_cast<LPWSTR>(L"SNES / SFC");
            TabCtrl_InsertItem(g_tabs, 1, &ti);
            ti.pszText = const_cast<LPWSTR>(L"N64");
            TabCtrl_InsertItem(g_tabs, 2, &ti);

            // default states
            SendMessageW(g_backup, BM_SETCHECK, BST_CHECKED, 0);

            // fonts for everything
            SendMessageW(g_tabs, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
            for (HWND* h : kNesCtrls)
                if (*h) SendMessageW(*h, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
            SendMessageW(g_placeholder, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);

            LoadLangIni();
            LoadThemeIni();
            ApplyLanguage(g_lang, false);
            ApplyTheme(g_darkTheme, false);
            LayoutAll();
            ShowNesPage(true);
            ShowWindow(g_placeholder, SW_HIDE);
            RedrawWindow(hwnd, nullptr, nullptr,
                         RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);

            DragAcceptFiles(hwnd, TRUE);
            return 0;
        }

        case WM_ERASEBKGND: {
            EraseMainBackground(hwnd, reinterpret_cast<HDC>(wParam));
            return 1;
        }

        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN:
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX: {
            HDC dc = reinterpret_cast<HDC>(wParam);
            HWND control = reinterpret_cast<HWND>(lParam);
            // The read-only log is an EDIT, but Windows reports read-only edits via
            // WM_CTLCOLORSTATIC; paint it like every other edit.
            if (msg == WM_CTLCOLOREDIT || msg == WM_CTLCOLORLISTBOX || control == g_log) {
                SetTextColor(dc, ThemeTextColor());
                SetBkMode(dc, OPAQUE);
                SetBkColor(dc, ThemeEditColor());
                return reinterpret_cast<INT_PTR>(ThemeEditBrush());
            }
            // Labels and the checkbox sit on a panel (surface colour); the log caption
            // and the "coming soon" placeholder sit directly on the window colour.
            const bool onWindow = control == g_lblLog || control == g_placeholder;
            const bool muted = control == g_lblOutDirHint || control == g_lblLog;
            const COLORREF bg = onWindow ? ThemeWindowColor() : ThemeSurfaceColor();
            SetTextColor(dc, muted ? ThemeMutedTextColor() : ThemeTextColor());
            SetBkMode(dc, OPAQUE);
            SetBkColor(dc, bg);
            return reinterpret_cast<INT_PTR>(onWindow ? ThemeWindowBrush() : ThemeSurfaceBrush());
        }

        case WM_THEMECHANGED:
        case WM_SYSCOLORCHANGE:
        case WM_SETTINGCHANGE: {
            // Let Windows update themed controls first, then repaint the complete
            // child tree. This avoids stale/light fragments after an OS theme or
            // system-colour change while the application is running.
            const LRESULT result = DefWindowProcW(hwnd, msg, wParam, lParam);
            if (hwnd == g_main) RefreshWindowPainting(hwnd);
            return result;
        }

        case WM_SIZE:
            // Layout first, then invalidate the child tree. Do not rely on a mouse
            // hover or a theme switch to trigger repainting of native combo/edit
            // controls after the parent has erased its background during live resize.
            LayoutAll();
            if (wParam != SIZE_MINIMIZED) {
                RedrawWindow(hwnd, nullptr, nullptr,
                             RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
            }
            return 0;

        case WM_EXITSIZEMOVE:
            // Final synchronous repaint after interactive resizing. This also
            // repairs any stale owner-drawn tabs/buttons left by intermediate
            // WM_SIZE paints, in either theme.
            RefreshWindowPainting(hwnd);
            return 0;

        case WM_GETMINMAXINFO: {
            MINMAXINFO* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
            mmi->ptMinTrackSize.x = SX(560);
            mmi->ptMinTrackSize.y = SX(560);
            return 0;
        }

        case WM_DRAWITEM: {
            const DRAWITEMSTRUCT* dis = reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
            if (!dis) return DefWindowProcW(hwnd, msg, wParam, lParam);

            if (dis->hwndItem == g_tabs &&
                dis->itemID < static_cast<UINT>(TabCtrl_GetItemCount(g_tabs))) {
                const bool selected =
                    static_cast<int>(dis->itemID) == TabCtrl_GetCurSel(g_tabs);
                FillRect(dis->hDC, &dis->rcItem, ThemeWindowBrush());
                RECT pill = dis->rcItem;
                InflateRect(&pill, -SX(2), -SX(1));
                DrawRoundedSurface(dis->hDC, pill,
                                   selected ? ThemeAccentColor() : ThemeSurfaceColor(),
                                   selected ? ThemeAccentColor() : ThemeBorderColor(), 6);

                wchar_t caption[128] = {};
                TCITEMW ti = {};
                ti.mask = TCIF_TEXT;
                ti.pszText = caption;
                ti.cchTextMax = static_cast<int>(std::size(caption));
                TabCtrl_GetItem(g_tabs, static_cast<int>(dis->itemID), &ti);
                SetBkMode(dis->hDC, TRANSPARENT);
                SetTextColor(dis->hDC, selected ? ThemeAccentTextColor() : ThemeTextColor());
                RECT textRc = pill;
                InflateRect(&textRc, -SX(4), -SX(1));
                DrawTextW(dis->hDC, caption, -1, &textRc,
                          DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
                return TRUE;
            }

            if (dis->CtlType == ODT_BUTTON && IsOwnerDrawPushButton(dis->hwndItem)) {
                const bool disabled = (dis->itemState & ODS_DISABLED) != 0;
                const bool pressed = (dis->itemState & ODS_SELECTED) != 0;
                const bool hot = (dis->itemState & ODS_HOTLIGHT) != 0;
                const bool focused = (dis->itemState & ODS_FOCUS) != 0;
                const bool primary = dis->hwndItem == g_convert;
                RECT button = dis->rcItem;
                InflateRect(&button, -SX(1), -SX(1));
                // Corners outside the rounded button show whatever the button sits on:
                // a panel (Browse buttons) or the plain window (Convert button).
                const COLORREF surround = primary ? ThemeWindowColor() : ThemeSurfaceColor();
                FillSolid(dis->hDC, dis->rcItem, surround);

                COLORREF fill = ThemeSurfaceColor();
                COLORREF border = ThemeBorderColor();
                COLORREF text = ThemeTextColor();
                if (primary) {
                    fill = pressed ? ThemeAccentDownColor()
                                   : (hot ? ThemeAccentHoverColor() : ThemeAccentColor());
                    border = fill;
                    text = ThemeAccentTextColor();
                } else {
                    if (pressed) fill = ThemeControlDownColor();
                    else if (hot) fill = ThemeControlHoverColor();
                    if (focused) border = ThemeAccentColor();
                }
                if (disabled) {
                    fill = surround;
                    border = ThemeBorderColor();
                    text = ThemeMutedTextColor();
                }
                DrawRoundedSurface(dis->hDC, button, fill, border, 6);

                wchar_t caption[128] = {};
                GetWindowTextW(dis->hwndItem, caption, static_cast<int>(std::size(caption)));
                RECT textRc = button;
                InflateRect(&textRc, -SX(7), -SX(3));
                if (pressed) OffsetRect(&textRc, SX(1), SX(1));
                SetBkMode(dis->hDC, TRANSPARENT);
                SetTextColor(dis->hDC, text);
                DrawTextW(dis->hDC, caption, -1, &textRc,
                          DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
                if (focused && !primary) {
                    RECT focusRc = button;
                    InflateRect(&focusRc, -SX(4), -SX(4));
                    DrawFocusRect(dis->hDC, &focusRc);
                }
                return TRUE;
            }
            return DefWindowProcW(hwnd, msg, wParam, lParam);
        }

        case WM_NOTIFY: {
            const NMHDR* nm = reinterpret_cast<const NMHDR*>(lParam);
            if (nm->idFrom == IDC_TABS && nm->code == TCN_SELCHANGE) {
                const int sel = TabCtrl_GetCurSel(g_tabs);
                ShowNesPage(sel == 0);
                ShowWindow(g_placeholder, sel != 0 ? SW_SHOW : SW_HIDE);
                SetWindowTextW(g_placeholder,
                               (sel == 2) ? Str(S_ComingSoonN64, g_lang)
                                          : Str(S_ComingSoonSnes, g_lang));
                // Panels are painted by the main window: repaint so they appear/disappear
                // together with the controls of the NES page.
                RedrawWindow(g_main, nullptr, nullptr,
                             RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
            }
            return 0;
        }

        case WM_DROPFILES:
            OnDropFiles(reinterpret_cast<HDROP>(wParam));
            return 0;

        case WM_COMMAND: {
            const int id = LOWORD(wParam);
            const int code = HIWORD(wParam);
            switch (id) {
                case IDC_SRC_EMU:
                    if (reinterpret_cast<HWND>(lParam) == g_srcEmu &&
                        (code == CBN_SELCHANGE || code == CBN_SELENDOK ||
                         code == CBN_SELENDCANCEL)) return 0;
                    break;
                case IDC_TYPE:
                    if (reinterpret_cast<HWND>(lParam) == g_type &&
                        (code == CBN_SELCHANGE || code == CBN_SELENDOK ||
                         code == CBN_SELENDCANCEL)) return 0;
                    break;
                case IDC_TGT_EMU:
                    if (reinterpret_cast<HWND>(lParam) == g_tgtEmu &&
                        (code == CBN_SELCHANGE || code == CBN_SELENDOK ||
                         code == CBN_SELENDCANCEL)) return 0;
                    break;
                case IDC_SRC_BROWSE: {
                    if (reinterpret_cast<HWND>(lParam) != g_btnSrcBrowse || code != BN_CLICKED)
                        break;
                    std::wstring p = PickOpenFile(g_main, Str(S_PickSaveTitle, g_lang),
                                                  Str(S_SaveFilter, g_lang));
                    if (!p.empty()) {
                        SetWindowTextW(g_srcFile, p.c_str());
                        LogLine(rsc::Expand(Str(S_DroppedSave, g_lang), {p}));
                        TryAutoRom(p);
                    }
                    return 0;
                }
                case IDC_ROM_BROWSE: {
                    if (reinterpret_cast<HWND>(lParam) != g_btnRomBrowse || code != BN_CLICKED)
                        break;
                    std::wstring p = PickOpenFile(g_main, Str(S_PickRomTitle, g_lang),
                                                  Str(S_RomFilter, g_lang));
                    if (!p.empty()) SetWindowTextW(g_romFile, p.c_str());
                    return 0;
                }
                case IDC_OUTDIR_BROWSE: {
                    if (reinterpret_cast<HWND>(lParam) != g_btnOutDirBrowse || code != BN_CLICKED)
                        break;
                    std::wstring p = PickFolder(g_main, Str(S_PickFolderTitle, g_lang));
                    if (!p.empty()) SetWindowTextW(g_outDir, p.c_str());
                    return 0;
                }
                case IDC_CONVERT:
                    if (code == BN_CLICKED) {
                        OnConvert();
                        return 0;
                    }
                    break;
                case IDOK:  // Enter key via IsDialogMessage — same as Convert
                    OnConvert();
                    return 0;
                case IDM_LANG_EN:
                    ApplyLanguage(rsc::LANG_EN, true);
                    return 0;
                case IDM_LANG_RU:
                    ApplyLanguage(rsc::LANG_RU, true);
                    return 0;
                case IDM_THEME_LIGHT:
                    ApplyTheme(false, true);
                    return 0;
                case IDM_THEME_DARK:
                    ApplyTheme(true, true);
                    return 0;
                case IDM_ABOUT:
                    MessageBoxW(g_main, Str(S_AboutText, g_lang), Str(S_AppTitle, g_lang),
                                MB_OK | MB_ICONINFORMATION);
                    return 0;
                default:
                    break;
            }
            return DefWindowProcW(hwnd, msg, wParam, lParam);
        }

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;

        case WM_NCDESTROY: {
            if (hwnd == g_main) DestroyMainMenu(hwnd);
            const LRESULT result = DefWindowProcW(hwnd, msg, wParam, lParam);
            if (g_fontStrong && g_fontStrong != g_font) DeleteObject(g_fontStrong);
            g_fontStrong = nullptr;
            if (g_font) {
                DeleteObject(g_font);
                g_font = nullptr;
            }
            if (g_brushWindow) { DeleteObject(g_brushWindow); g_brushWindow = nullptr; }
            if (g_brushEdit) { DeleteObject(g_brushEdit); g_brushEdit = nullptr; }
            if (g_brushSurface) { DeleteObject(g_brushSurface); g_brushSurface = nullptr; }
            if (g_main == hwnd) g_main = nullptr;
            return result;
        }

        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

static bool IsComboDropDownOpen() {
    return (g_srcEmu && SendMessageW(g_srcEmu, CB_GETDROPPEDSTATE, 0, 0)) ||
           (g_type && SendMessageW(g_type, CB_GETDROPPEDSTATE, 0, 0)) ||
           (g_tgtEmu && SendMessageW(g_tgtEmu, CB_GETDROPPEDSTATE, 0, 0));
}

// ========================================================================================
// Entry point
// ========================================================================================
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow) {
    g_hInst = hInstance;

    // BIF_NEWDIALOGSTYLE (resizable folder picker) requires COM/OLE to be initialised
    // on the UI thread, otherwise the "Browse" button for the output folder misbehaves.
    const HRESULT oleHr = OleInitialize(nullptr);
    const bool oleReady = SUCCEEDED(oleHr);

    INITCOMMONCONTROLSEX icc;
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_WIN95_CLASSES | ICC_STANDARD_CLASSES;
    if (!InitCommonControlsEx(&icc)) {
        if (oleReady) OleUninitialize();
        return 1;
    }

    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(2));
    wc.hIconSm = static_cast<HICON>(LoadImageW(hInstance, MAKEINTRESOURCEW(2), IMAGE_ICON,
                                               GetSystemMetrics(SM_CXSMICON),
                                               GetSystemMetrics(SM_CYSMICON),
                                               LR_DEFAULTCOLOR));
    wc.hbrBackground = nullptr;
    wc.lpszClassName = L"RetroSaveConverterWnd";

    if (!RegisterClassExW(&wc)) {
        if (wc.hIconSm) DestroyIcon(wc.hIconSm);
        if (oleReady) OleUninitialize();
        return 1;
    }

    // Establish scale before calculating the initial outer window size. With
    // DPI awareness enabled in the manifest, initializing it only in WM_CREATE
    // makes the first window too small on high-DPI displays.
    HDC desktopDc = GetDC(nullptr);
    if (desktopDc) {
        const int dpi = GetDeviceCaps(desktopDc, LOGPIXELSX);
        if (dpi > 0) g_scale = dpi / 96.0f;
        ReleaseDC(nullptr, desktopDc);
    }
    if (g_scale < 1.0f) g_scale = 1.0f;

    const int W = SX(560), H = SX(590);
    RECT adj = {0, 0, W, H};
    AdjustWindowRect(&adj, WS_OVERLAPPEDWINDOW, TRUE);

    // WS_CLIPCHILDREN: the parent's background/panel painting must never draw over
    // child controls. Every child is opaque (see WM_CTLCOLOR*), so nothing depends on
    // the parent background showing through.
    const DWORD mainStyle =
        (WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX) | WS_CLIPCHILDREN;
    g_main = CreateWindowExW(0, L"RetroSaveConverterWnd", L"Retro Save Converter",
                             mainStyle, CW_USEDEFAULT, CW_USEDEFAULT,
                             adj.right - adj.left, adj.bottom - adj.top,
                             nullptr, nullptr, hInstance, nullptr);
    if (!g_main) {
        UnregisterClassW(wc.lpszClassName, hInstance);
        if (wc.hIconSm) DestroyIcon(wc.hIconSm);
        if (oleReady) OleUninitialize();
        return 1;
    }

    ShowWindow(g_main, nCmdShow);
    UpdateWindow(g_main);
    // UpdateWindow paints only the top-level client area. Force a synchronous
    // invalidation/paint of child controls too, so combo captions and labels are
    // visible immediately rather than only after the first mouse movement.
    RefreshWindowPainting(g_main);

    MSG msg = {};
    BOOL messageResult = 0;
    while ((messageResult = GetMessageW(&msg, nullptr, 0, 0)) > 0) {
        if (IsDialogMessageW(g_main, &msg)) continue;

        // Owner-drawn buttons no longer carry the BS_DEFPUSHBUTTON type flag.
        // Preserve the app's existing Enter-to-convert behaviour when a combo's
        // dropdown is not open; leave keyboard navigation and menu activation to
        // IsDialogMessage above.
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_RETURN &&
            !IsComboDropDownOpen() && GetFocus() != g_btnSrcBrowse &&
            GetFocus() != g_btnRomBrowse && GetFocus() != g_btnOutDirBrowse) {
            SendMessageW(g_main, WM_COMMAND, MAKEWPARAM(IDC_CONVERT, BN_CLICKED), 0);
            continue;
        }

        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    const int exitCode = (messageResult == -1) ? 1 : static_cast<int>(msg.wParam);
    UnregisterClassW(wc.lpszClassName, hInstance);
    if (wc.hIconSm) DestroyIcon(wc.hIconSm);
    if (oleReady) OleUninitialize();
    return exitCode;
}
