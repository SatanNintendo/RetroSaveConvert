// ========================================================================================
// RetroSaveConverter — Win32 GUI entry point
// Target: Windows 7 x64 and later; built with MinGW-w64 or MSVC.
// No external dependencies: user32, gdi32, comctl32, comdlg32, shell32, ole32, advapi32.
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
#include <uxtheme.h>    // SetWindowTheme (DarkMode_Explorer)

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cwctype>
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

static HWND g_grpSource, g_grpTarget, g_grpOptions;
static HWND g_lblSrcEmu, g_srcEmu;
static HWND g_lblSave,   g_srcFile, g_btnSrcBrowse;
static HWND g_lblRom,    g_romFile, g_btnRomBrowse;
static HWND g_lblType,   g_type;
static HWND g_lblTgt,    g_tgtEmu;
static HWND g_lblOutDir, g_outDir, g_btnOutDirBrowse, g_lblOutDirHint;
static HWND g_backup, g_convert, g_lblLog, g_log;

static rsc::Lang g_lang = rsc::LANG_EN;
static float     g_scale = 1.0f;

// ---- dark theme support ---------------------------------------------------------------
static bool   g_dark = false;        // текущая тема (следует за системной)
static HFONT  g_menuFont = nullptr;  // шрифт для owner-draw меню
static HBRUSH g_brWin = nullptr;     // фон окна и меню (тёмный режим)
static HBRUSH g_brCtrl = nullptr;    // фон полей ввода и списков (тёмный режим)
static HBRUSH g_brMenuSel = nullptr; // выделение пункта всплывающего меню
static HBRUSH g_brBarSel = nullptr;  // выделение пункта строки меню

static const COLORREF kDarkBg    = RGB(32, 32, 32);    // фон окна
static const COLORREF kDarkCtrl  = RGB(43, 43, 43);    // фон контролов
static const COLORREF kDarkText  = RGB(240, 240, 240); // основной текст
static const COLORREF kDarkSel   = RGB(0, 120, 215);   // выделение (акцент)

static int SX(int x) { return static_cast<int>(x * g_scale + 0.5f); }

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
        LARGE_INTEGER sz;
        if (GetFileSizeEx(h, &sz) && sz.QuadPart >= 0 &&
            sz.QuadPart < 0x20000000 /* 512 MB sanity cap */) {
            out.resize(static_cast<size_t>(sz.QuadPart));
            DWORD rd = 0;
            if (out.empty()) {
                ok = true;  // empty file reads as success with zero bytes
            } else if (ReadFile(h, out.data(), static_cast<DWORD>(out.size()), &rd,
                                nullptr) &&
                       rd == out.size()) {
                ok = true;
            }
        }
        CloseHandle(h);
        if (!ok) out.clear();
        return ok;
    }

    bool WriteFileBytes(const std::wstring& path, const uint8_t* data, size_t size) override {
        HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return false;
        DWORD wr = 0;
        bool ok = true;
        if (size) {
            if (!WriteFile(h, data, static_cast<DWORD>(size), &wr, nullptr) ||
                wr != static_cast<DWORD>(size))
                ok = false;
        }
        CloseHandle(h);
        return ok;
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

// Если рядом с exe писать нельзя (Program Files и т.п.), настройки живут в %APPDATA%.
static std::wstring AppDataIniW() {
    wchar_t buf[MAX_PATH + 1] = {0};
    const DWORD n = GetEnvironmentVariableW(L"APPDATA", buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return std::wstring();
    const std::wstring dir = std::wstring(buf) + L"\\RetroSaveConverter";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir + L"\\RetroSaveConverter.ini";
}

// FIX (баг №4): раньше в INI записывалась строка ("ru"/"en"), а читалось значение
// как ЧИСЛО (GetPrivateProfileIntW), из-за чего чтение всегда давало 0 и язык
// сбрасывался на английский. Теперь и запись, и чтение работают со строками
// (дополнительно понимаются старые числовые значения "1"/"0").
static void SaveLangIni() {
    const wchar_t* val = (g_lang == rsc::LANG_RU) ? L"ru" : L"en";
    const std::wstring ini = ExeDirW() + L"RetroSaveConverter.ini";
    if (WritePrivateProfileStringW(L"Settings", L"Language", val, ini.c_str()))
        return;  // сохранилось рядом с программой
    const std::wstring ini2 = AppDataIniW();
    if (!ini2.empty())
        WritePrivateProfileStringW(L"Settings", L"Language", val, ini2.c_str());
}

static rsc::Lang ParseLangToken(std::wstring v) {
    for (auto& c : v) c = static_cast<wchar_t>(towlower(c));
    if (v == L"ru" || v == L"1" || v == L"russian") return rsc::LANG_RU;
    return rsc::LANG_EN;
}

static void LoadLangIni() {
    wchar_t buf[32] = {0};
    // 1) INI рядом с программой
    GetPrivateProfileStringW(L"Settings", L"Language", L"", buf, 32,
                             (ExeDirW() + L"RetroSaveConverter.ini").c_str());
    std::wstring v = buf;
    // 2) резервное расположение в %APPDATA%
    if (v.empty()) {
        const std::wstring ini2 = AppDataIniW();
        if (!ini2.empty())
            GetPrivateProfileStringW(L"Settings", L"Language", L"", buf, 32, ini2.c_str());
        v = buf;
    }
    g_lang = ParseLangToken(v);
}

// ========================================================================================
// Dark theme (Windows 10 1809+ / Windows 11)
// ========================================================================================

// список всех контролов страницы NES (нужен и для перекраски темы)
static HWND* const kNesCtrls[] = {
    &g_grpSource, &g_grpTarget, &g_grpOptions,
    &g_lblSrcEmu, &g_srcEmu, &g_lblSave, &g_srcFile, &g_btnSrcBrowse,
    &g_lblRom, &g_romFile, &g_btnRomBrowse,
    &g_lblType, &g_type,
    &g_lblTgt, &g_tgtEmu,
    &g_lblOutDir, &g_outDir, &g_btnOutDirBrowse, &g_lblOutDirHint,
    &g_backup, &g_convert, &g_lblLog, &g_log,
};

static bool QueryRegDword(HKEY root, const wchar_t* subkey, const wchar_t* value,
                          DWORD* out) {
    HKEY k = nullptr;
    if (RegOpenKeyExW(root, subkey, 0, KEY_READ, &k) != ERROR_SUCCESS) return false;
    DWORD type = 0, v = 0, size = sizeof(DWORD);
    const bool ok =
        RegQueryValueExW(k, value, nullptr, &type, reinterpret_cast<LPBYTE>(&v), &size) ==
            ERROR_SUCCESS &&
        type == REG_DWORD;
    RegCloseKey(k);
    if (ok) *out = v;
    return ok;
}

// Системная тема приложений: AppsUseLightTheme == 0 => тёмная.
static bool SystemDarkMode() {
    DWORD light = 1;
    if (QueryRegDword(HKEY_CURRENT_USER,
                      L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                      L"AppsUseLightTheme", &light))
        return light == 0;
    return false;
}

// Тёмный/светлый заголовок окна (DWMWA_USE_IMMERSIVE_DARK_MODE).
// На Windows 7/8 и сборках ниже 1809 вызов просто не сработает — останется стандартный
// заголовок, это ожидаемое поведение.
static void ApplyTitleBarTheme() {
    if (!g_main) return;
    static HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
    if (!dwm) return;
    typedef HRESULT(WINAPI * FnDswa)(HWND, DWORD, const void*, DWORD);
    union {                             // корректное приведение FARPROC -> FnDswa
        FARPROC p;
        FnDswa f;
    } cast;
    cast.p = GetProcAddress(dwm, "DwmSetWindowAttribute");
    if (!cast.f) return;
    BOOL v = g_dark ? TRUE : FALSE;
    if (FAILED(cast.f(g_main, 20, &v, sizeof(v))))   // DWMWA_USE_IMMERSIVE_DARK_MODE
        cast.f(g_main, 19, &v, sizeof(v));           // то же для 1809/1903
}

// Тёмная тема контролов. Помимо класса "DarkMode_Explorer" (кнопки, стрелки
// комбобоксов, полосы прокрутки — поддерживается Windows 10 1809+; на старых
// системах и в Wine просто не сработает), в тёмном режиме включается собственная
// отрисовка вкладок, кнопок, групповых рамок и комбобоксов — так тёмный вид
// остаётся полным и согласованным на любой системе.
static void SetStyleBit(HWND h, LONG_PTR bit, bool on) {
    const LONG_PTR st = GetWindowLongW(h, GWL_STYLE);
    const LONG_PTR ns = on ? (st | bit) : (st & ~bit);
    if (ns != st) SetWindowLongW(h, GWL_STYLE, ns);
}

static void ApplyControlThemes() {
    if (!g_tabs) return;
    const wchar_t* cls = g_dark ? L"DarkMode_Explorer" : nullptr;
    SetWindowTheme(g_tabs, cls, nullptr);
    for (HWND* h : kNesCtrls) {
        if (!*h) continue;
        const HWND c = *h;
        const LONG_PTR st = GetWindowLongW(c, GWL_STYLE);
        // тип кнопки хранится в младшем полубайте (BS_PUSHBUTTON == 0!)
        const LONG_PTR btnType = (st & 0x0F);
        const bool isPushBtn = btnType == BS_PUSHBUTTON || btnType == BS_DEFPUSHBUTTON ||
                               btnType == BS_GROUPBOX;
        if (isPushBtn) SetStyleBit(c, BS_OWNERDRAW, g_dark);
        SetWindowTheme(c, cls, nullptr);
        InvalidateRect(c, nullptr, TRUE);
    }
    if (g_placeholder) SetWindowTheme(g_placeholder, cls, nullptr);
    InvalidateRect(g_tabs, nullptr, TRUE);
}

// ---- собственная отрисовка вкладок в тёмной теме ----------------------------------------
// ВАЖНО: данные вкладок собираются ДО BeginPaint — запросы TCM_* во время
// отрисовки (реентерабельные SendMessage самому себе) нарушают очередь
// перерисовки соседних контролов (комбобоксы перестают рисоваться).
struct DarkTabItem {
    RECT rc;
    wchar_t text[64];
};

static void DrawDarkTabs(HWND, HDC hdc, const DarkTabItem* items, int n, int sel,
                         const RECT& rcClient, int headerBottom) {
    FillRect(hdc, &rcClient, g_brWin);  // панель вкладок

    HGDIOBJ oldF = SelectObject(hdc, g_font);
    SetBkMode(hdc, TRANSPARENT);
    for (int i = 0; i < n; ++i) {
        const RECT& it = items[i].rc;
        const bool selected = (i == sel);
        FillRect(hdc, &it, selected ? g_brCtrl : g_brWin);
        if (selected) {  // синяя полоска сверху активной вкладки
            RECT top = {it.left, it.top, it.right, it.top + 2};
            FillRect(hdc, &top, g_brMenuSel);
        }
        SetTextColor(hdc, selected ? kDarkText : RGB(176, 176, 176));
        RECT tr = it;
        tr.left += SX(10);
        DrawTextW(hdc, items[i].text, -1, &tr, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
    }
    if (headerBottom) {  // линия под заголовками вкладок
        HPEN pen = CreatePen(PS_SOLID, 1, RGB(70, 70, 70));
        HGDIOBJ oldP = SelectObject(hdc, pen);
        MoveToEx(hdc, rcClient.left, headerBottom, nullptr);
        LineTo(hdc, rcClient.right, headerBottom);
        SelectObject(hdc, oldP);
        DeleteObject(pen);
    }
    SelectObject(hdc, oldF);
}

static LRESULT CALLBACK TabSubclassProc(HWND h, UINT msg, WPARAM wp, LPARAM lp,
                                        UINT_PTR, DWORD_PTR) {
    if (g_dark) {
        if (msg == WM_PAINT) {
            // 1) сбор данных — до BeginPaint, без реентерабельных вызовов в отрисовке
            DarkTabItem items[8];
            const int n = std::min(TabCtrl_GetItemCount(h), 8);
            const int sel = TabCtrl_GetCurSel(h);
            int headerBottom = 0;
            RECT rcClient;
            GetClientRect(h, &rcClient);
            for (int i = 0; i < n; ++i) {
                ZeroMemory(&items[i], sizeof(items[i]));
                RECT it = {0, 0, 0, 0};
                if (TabCtrl_GetItemRect(h, i, &it)) items[i].rc = it;
                if (items[i].rc.bottom > headerBottom) headerBottom = items[i].rc.bottom;
                TCITEMW ti;
                ZeroMemory(&ti, sizeof(ti));
                ti.mask = TCIF_TEXT;
                ti.pszText = items[i].text;
                ti.cchTextMax = 64;
                TabCtrl_GetItem(h, i, &ti);
            }
            // 2) отрисовка
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(h, &ps);
            DrawDarkTabs(h, hdc, items, n, sel, rcClient, headerBottom);
            EndPaint(h, &ps);
            return 0;
        }
        if (msg == WM_ERASEBKGND) return 1;  // фон рисуем в WM_PAINT
    }
    return DefSubclassProc(h, msg, wp, lp);
}

// ---- собственная отрисовка кнопок и групповых рамок в тёмной теме -----------------------
static void DrawDarkButton(DRAWITEMSTRUCT* dis) {
    const bool pressed = (dis->itemState & ODS_SELECTED) != 0;
    const bool def = (GetWindowLongW(dis->hwndItem, GWL_STYLE) & BS_DEFPUSHBUTTON) != 0;
    const RECT r = dis->rcItem;
    FillRect(dis->hDC, &r, pressed ? g_brBarSel : g_brCtrl);
    HPEN pen = CreatePen(PS_SOLID, 1, def ? kDarkSel : RGB(104, 104, 104));
    HGDIOBJ oldP = SelectObject(dis->hDC, pen);
    HGDIOBJ oldB = SelectObject(dis->hDC, GetStockObject(NULL_BRUSH));
    Rectangle(dis->hDC, r.left, r.top, r.right, r.bottom);
    SelectObject(dis->hDC, oldB);
    SelectObject(dis->hDC, oldP);
    DeleteObject(pen);

    wchar_t txt[128] = {0};
    GetWindowTextW(dis->hwndItem, txt, 128);
    SetBkMode(dis->hDC, TRANSPARENT);
    SetTextColor(dis->hDC, kDarkText);
    HGDIOBJ oldF = SelectObject(dis->hDC, g_font);
    RECT tr = r;
    tr.left += 4;
    tr.right -= 4;
    DrawTextW(dis->hDC, txt, -1, &tr, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
    SelectObject(dis->hDC, oldF);
}

static void DrawDarkGroupBox(DRAWITEMSTRUCT* dis) {
    const RECT r = dis->rcItem;
    wchar_t txt[128] = {0};
    GetWindowTextW(dis->hwndItem, txt, 128);

    HGDIOBJ oldF = SelectObject(dis->hDC, g_font);
    SIZE sz = {0, 0};
    if (txt[0]) GetTextExtentPoint32W(dis->hDC, txt, lstrlenW(txt), &sz);

    // рамка (проходит за заголовком)
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(84, 84, 84));
    HGDIOBJ oldP = SelectObject(dis->hDC, pen);
    HGDIOBJ oldB = SelectObject(dis->hDC, GetStockObject(NULL_BRUSH));
    Rectangle(dis->hDC, r.left, r.top + (sz.cy ? sz.cy / 2 : 5), r.right, r.bottom);
    SelectObject(dis->hDC, oldB);
    SelectObject(dis->hDC, oldP);
    DeleteObject(pen);

    if (txt[0]) {  // заголовок: разрыв в рамке + светлый текст
        RECT tr = {r.left + SX(9), r.top, r.left + SX(9) + sz.cx + SX(8),
                   r.top + sz.cy + 1};
        FillRect(dis->hDC, &tr, g_brWin);
        SetBkMode(dis->hDC, TRANSPARENT);
        SetTextColor(dis->hDC, kDarkText);
        DrawTextW(dis->hDC, txt, -1, &tr, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
    }
    SelectObject(dis->hDC, oldF);
}

// ---- собственная отрисовка комбобоксов в тёмной теме -----------------------------------
// Комбобоксы остаются стандартными: фон и цвет текста задаёт WM_CTLCOLORLISTBOX,
// а чтобы текст выбранного пункта гарантированно был светлым (в Wine и на части
// сборок Windows он рисуется системным тёмным цветом), он дорисовывается поверх
// стандартной отрисовки в субклассе ComboSubclassProc.
static std::vector<std::wstring> g_srcEmuItems, g_tgtEmuItems, g_typeItems;

static LRESULT CALLBACK ComboSubclassProc(HWND h, UINT msg, WPARAM wp, LPARAM lp,
                                          UINT_PTR, DWORD_PTR) {
    const LRESULT res = DefSubclassProc(h, msg, wp, lp);
    if (g_dark && msg == WM_PAINT && !ComboBox_GetDroppedState(h)) {
        const std::vector<std::wstring>* items = nullptr;
        if (h == g_srcEmu)
            items = &g_srcEmuItems;
        else if (h == g_type)
            items = &g_typeItems;
        else if (h == g_tgtEmu)
            items = &g_tgtEmuItems;
        const int sel = ComboBox_GetCurSel(h);
        const wchar_t* txt =
            (items && sel != CB_ERR && (size_t)sel < items->size())
                ? (*items)[(size_t)sel].c_str()
                : L"";
        RECT rc;
        GetClientRect(h, &rc);
        RECT tr = rc;
        tr.left += SX(6);
        tr.right -= GetSystemMetrics(SM_CXVSCROLL) + SX(8);  // не залезаем на стрелку
        if (tr.right > tr.left) {
            HDC dc = GetDC(h);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, kDarkText);
            HGDIOBJ oldF = SelectObject(dc, g_font);
            DrawTextW(dc, txt, -1, &tr,
                      DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX |
                          DT_END_ELLIPSIS);
            SelectObject(dc, oldF);
            ReleaseDC(h, dc);
        }
    }
    return res;
}

// ---- тёмное меню (owner-draw) ----------------------------------------------------------
struct OdItem {
    const wchar_t* text;  // указатель на статическую строку из таблицы
    bool isBar;           // пункт строки меню (true) или всплывающего меню (false)
};
static OdItem g_od[] = {
    {nullptr, true },  // 0: меню "Язык" (bar)
    {nullptr, true },  // 1: меню "Справка" (bar)
    {nullptr, false},  // 2: English
    {nullptr, false},  // 3: Русский
    {nullptr, false},  // 4: О программе
};

static void AddMenuItem(HMENU menu, OdItem* od, UINT_PTR idOrSubmenu, bool isPopup) {
    MENUITEMINFOW mii;
    ZeroMemory(&mii, sizeof(mii));
    mii.cbSize = sizeof(mii);
    if (g_dark) {
        // owner-draw: рисуем сами (тёмный фон, светлый текст); строка всё равно
        // хранится в пункте — продолжают работать мнемоники (Alt+буква)
        mii.fMask = MIIM_ID | MIIM_FTYPE | MIIM_DATA | MIIM_STRING;
        mii.fType = MFT_OWNERDRAW;
        mii.dwItemData = reinterpret_cast<ULONG_PTR>(od);
    } else {
        mii.fMask = MIIM_ID | MIIM_FTYPE | MIIM_STRING;
        mii.fType = MFT_STRING;
    }
    mii.wID = static_cast<UINT>(idOrSubmenu);
    mii.dwTypeData = const_cast<LPWSTR>(od->text);
    if (isPopup) {
        mii.fMask |= MIIM_SUBMENU;
        mii.hSubMenu = reinterpret_cast<HMENU>(idOrSubmenu);
    }
    InsertMenuItemW(menu, UINT_MAX, TRUE, &mii);
}

// Создаёт строку меню заново с учётом текущего языка и темы.
// FIX (баг №2): раньше меню собиралось в ApplyLanguage, который вызывался из WM_CREATE,
// где глобальное g_main ещё не было присвоено — SetMenu(g_main, ...) получал nullptr,
// и меню не появлялось до первой смены языка. Теперь меню строится отдельно
// и g_main назначается в самом начале WM_CREATE.
static void RebuildMenu() {
    if (!g_main) return;

    g_od[0].text = Str(S_MenuLanguage, g_lang);
    g_od[1].text = Str(S_MenuHelp, g_lang);
    g_od[2].text = Str(S_MenuEnglish, g_lang);
    g_od[3].text = Str(S_MenuRussian, g_lang);
    g_od[4].text = Str(S_MenuAbout, g_lang);

    HMENU old = GetMenu(g_main);
    HMENU bar = CreateMenu();
    HMENU mLang = CreatePopupMenu();
    HMENU mHelp = CreatePopupMenu();

    AddMenuItem(mLang, &g_od[2], IDM_LANG_EN, false);
    AddMenuItem(mLang, &g_od[3], IDM_LANG_RU, false);
    AddMenuItem(mHelp, &g_od[4], IDM_ABOUT, false);
    AddMenuItem(bar, &g_od[0], reinterpret_cast<UINT_PTR>(mLang), true);
    AddMenuItem(bar, &g_od[1], reinterpret_cast<UINT_PTR>(mHelp), true);

    CheckMenuRadioItem(mLang, IDM_LANG_EN, IDM_LANG_RU,
                       IDM_LANG_EN + static_cast<int>(g_lang), MF_BYCOMMAND);

    if (g_dark) {
        // тёмная подложка строки меню и всплывающих меню
        MENUINFO mi;
        ZeroMemory(&mi, sizeof(mi));
        mi.cbSize = sizeof(mi);
        mi.fMask = MIM_BACKGROUND | MIM_APPLYTOSUBMENUS;
        mi.hbrBack = g_brWin;
        SetMenuInfo(bar, &mi);
        SetMenuInfo(mLang, &mi);
        SetMenuInfo(mHelp, &mi);
    }

    SetMenu(g_main, bar);
    if (old) DestroyMenu(old);
    DrawMenuBar(g_main);
}

// Перечитывает системную тему и перекрашивает всё живьём — без перезапуска.
// FIX (баг №3): раньше тёмная тема нигде не отслеживалась и применялась только
// при следующем запуске. Теперь WM_SETTINGCHANGE (смена темы Windows) обновляет
// окно сразу: заголовок, фон, контролы и меню.
static void ApplyThemeAll() {
    const bool dark = SystemDarkMode();
    if (dark == g_dark && g_main && GetMenu(g_main)) return;  // тема не менялась
    g_dark = dark;
    ApplyTitleBarTheme();
    ApplyControlThemes();
    RebuildMenu();
    RedrawWindow(g_main, nullptr, nullptr,
                 RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN | RDW_UPDATENOW);
}

// ========================================================================================
// Control factory helpers
// ========================================================================================
static HWND MkStatic(HWND parent, int id, DWORD style, int x, int y, int w, int h) {
    return CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | style, x, y, w, h,
                           parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                           g_hInst, nullptr);
}

static HWND MkButton(HWND parent, int id, DWORD style, int x, int y, int w, int h) {
    return CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | style,
                           x, y, w, h, parent,
                           reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), g_hInst,
                           nullptr);
}

static HWND MkEdit(HWND parent, int id, int x, int y, int w, int h) {
    return CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                           WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, x, y, w,
                           h, parent,
                           reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), g_hInst,
                           nullptr);
}

static HWND MkCombo(HWND parent, int id, int x, int y, int w, int h) {
    return CreateWindowExW(0, L"COMBOBOX", L"",
                           WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST |
                               WS_VSCROLL,
                           x, y, w, h, parent,
                           reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), g_hInst,
                           nullptr);
}

static void ShowNesPage(bool show) {
    for (HWND* h : kNesCtrls)
        if (*h) ShowWindow(*h, show ? SW_SHOW : SW_HIDE);
}

// ========================================================================================
// Layout
// ========================================================================================
static void LayoutAll() {
    RECT rc;
    GetClientRect(g_main, &rc);
    MoveWindow(g_tabs, 0, 0, rc.right, rc.bottom, TRUE);

    RECT disp = rc;
    SendMessageW(g_tabs, TCM_ADJUSTRECT, FALSE, reinterpret_cast<LPARAM>(&disp));
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
    MoveWindow(g_grpSource, x0, y, W, SX(24) + srcRows * rowP + SX(8), TRUE);
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
    MoveWindow(g_grpTarget, x0, y, W, SX(24) + 2 * rowP + SX(2) + SX(14) + SX(10),
               TRUE);
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
    MoveWindow(g_grpOptions, x0, y, W, SX(24) + rowP + SX(6), TRUE);
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
static void RebuildCombos() {
    // source emulator combo: index matches rsc::Emu exactly
    // (Auto, MesenCE, Nestopia, Nintendulator, Raw)
    const wchar_t* emuNames[5] = {Str(S_AutoDetect, g_lang), L"MesenCE", L"Nestopia",
                                  L"Nintendulator", Str(S_GenericSav, g_lang)};
    // target emulator combo: MesenCE, Nestopia, Nintendulator, Raw
    const wchar_t* tgtNames[4] = {L"MesenCE", L"Nestopia", L"Nintendulator",
                                  Str(S_GenericSav, g_lang)};
    // content type combo: index matches rsc::SaveType exactly
    const wchar_t* typeNames[5] = {
        Str(S_AutoDetect, g_lang), Str(S_TypeSram, g_lang), Str(S_Type24C01, g_lang),
        Str(S_Type24C02, g_lang), Str(S_TypeDatach, g_lang)};

    // копии строк для дорисовки текста в тёмной теме — см. ComboSubclassProc
    g_srcEmuItems.assign(emuNames, emuNames + 5);
    g_tgtEmuItems.assign(tgtNames, tgtNames + 4);
    g_typeItems.assign(typeNames, typeNames + 5);

    int sel;
    sel = ComboBox_GetCurSel(g_srcEmu);
    ComboBox_ResetContent(g_srcEmu);
    for (int i = 0; i < 5; ++i) ComboBox_AddString(g_srcEmu, emuNames[i]);
    ComboBox_SetCurSel(g_srcEmu, (sel == CB_ERR || sel < 0) ? 0 : sel);

    sel = ComboBox_GetCurSel(g_tgtEmu);
    ComboBox_ResetContent(g_tgtEmu);
    for (int i = 0; i < 4; ++i) ComboBox_AddString(g_tgtEmu, tgtNames[i]);
    ComboBox_SetCurSel(g_tgtEmu, (sel == CB_ERR || sel < 0) ? 1 : sel);

    sel = ComboBox_GetCurSel(g_type);
    ComboBox_ResetContent(g_type);
    for (int i = 0; i < 5; ++i) ComboBox_AddString(g_type, typeNames[i]);
    ComboBox_SetCurSel(g_type, (sel == CB_ERR || sel < 0) ? 0 : sel);
}

static void ApplyLanguage(rsc::Lang lang, bool saveIni) {
    g_lang = lang;

    SetWindowTextW(g_main, Str(S_AppTitle, lang));

    SetWindowTextW(g_grpSource, Str(S_GrpSource, lang));
    SetWindowTextW(g_grpTarget, Str(S_GrpTarget, lang));
    SetWindowTextW(g_grpOptions, Str(S_GrpOptions, lang));

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

    // меню строится отдельно (RebuildMenu) с учётом языка и текущей темы
    RebuildMenu();

    if (saveIni) SaveLangIni();
}

// ========================================================================================
// File pickers
// ========================================================================================
static std::wstring PickOpenFile(HWND owner, const wchar_t* title, const wchar_t* filter) {
    wchar_t buf[1024] = {0};
    OPENFILENAMEW ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = buf;
    ofn.nMaxFile = 1024;
    ofn.lpstrTitle = title;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY | OFN_EXPLORER;
    ofn.nFilterIndex = 1;
    if (GetOpenFileNameW(&ofn)) return buf;
    return std::wstring();
}

static std::wstring PickFolder(HWND owner, const wchar_t* title) {
    std::wstring result;
    BROWSEINFOW bi;
    ZeroMemory(&bi, sizeof(bi));
    bi.hwndOwner = owner;
    bi.lpszTitle = title;
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE | BIF_USENEWUI;
    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (pidl) {
        wchar_t path[MAX_PATH + 1] = {0};
        if (SHGetPathFromIDListW(pidl, path)) {
            result = path;
            if (!result.empty() && result.back() != L'\\' && result.back() != L'/')
                result += L'\\';
        }
        CoTaskMemFree(pidl);
    }
    return result;
}

// after a save file is selected, try to auto-fill the ROM field with the sibling .nes
static void TryAutoRom(const std::wstring& savePath) {
    std::wstring rom = GetEditText(g_romFile);
    if (!rom.empty()) return;
    WinEnv env;
    const std::wstring guess = GetDirOf(savePath) + GetBaseNoExtOf(savePath) + L".nes";
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

    int sel = ComboBox_GetCurSel(g_srcEmu);
    req.sourceFormat = (sel == CB_ERR) ? Emu::Auto : static_cast<Emu>(sel);
    sel = ComboBox_GetCurSel(g_type);
    req.saveType = (sel == CB_ERR) ? SaveType::Auto : static_cast<SaveType>(sel);
    sel = ComboBox_GetCurSel(g_tgtEmu);
    static const Emu tgtMap[4] = {Emu::MesenCE, Emu::Nestopia, Emu::Nintendulator,
                                  Emu::Raw};
    req.target = (sel == CB_ERR) ? Emu::Nestopia : tgtMap[sel];
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
    wchar_t buf[1024] = {0};
    const UINT n = DragQueryFileW(hd, 0xFFFFFFFF, nullptr, 0);
    if (n == 0) {
        DragFinish(hd);
        return;
    }
    DragQueryFileW(hd, 0, buf, 1024);
    DragFinish(hd);

    const std::wstring path = buf;
    const std::wstring ext = GetExtOf(path);
    if (ext == L".nes" || ext == L".unf") {
        SetWindowTextW(g_romFile, path.c_str());
        LogLine(rsc::Expand(Str(S_DroppedRom, g_lang), {path}));
    } else if (ext == L".sav" || ext == L".srm" || ext == L".eeprom128" ||
               ext == L".eeprom256") {
        SetWindowTextW(g_srcFile, path.c_str());
        LogLine(rsc::Expand(Str(S_DroppedSave, g_lang), {path}));
        TryAutoRom(path);
    } else {
        LogLine(rsc::Expand(Str(S_DroppedUnknown, g_lang), {path}));
    }
}

// ========================================================================================
// Window procedure
// ========================================================================================
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);

            // FIX (баг №2): g_main должен быть валиден до вызова ApplyLanguage —
            // раньше он присваивался только после возврата CreateWindowExW, поэтому
            // SetMenu(g_main, ...) внутри WM_CREATE получал nullptr, и меню
            // отсутствовало до первой смены языка.
            g_main = hwnd;

            // тема узнаётся ДО создания контролов: комбобоксы в тёмной теме
            // создаются сразу с owner-draw стилем (сменить его потом нельзя)
            g_dark = SystemDarkMode();

            // message font + menu font (для owner-draw меню)
            NONCLIENTMETRICSW ncm;
            ZeroMemory(&ncm, sizeof(ncm));
            ncm.cbSize = sizeof(ncm);
            SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
            g_font = CreateFontIndirectW(&ncm.lfMessageFont);
            g_menuFont = CreateFontIndirectW(&ncm.lfMenuFont);

            // кисти тёмной темы
            g_brWin = CreateSolidBrush(kDarkBg);
            g_brCtrl = CreateSolidBrush(kDarkCtrl);
            g_brMenuSel = CreateSolidBrush(kDarkSel);
            g_brBarSel = CreateSolidBrush(RGB(64, 64, 64));

            // tabs
            g_tabs = CreateWindowExW(0, WC_TABCONTROLW, L"",
                                     WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS |
                                         TCS_TABS,
                                     0, 0, cs->cx, cs->cy, hwnd,
                                     reinterpret_cast<HMENU>(
                                         static_cast<INT_PTR>(IDC_TABS)),
                                     g_hInst, nullptr);
            // в тёмной теме вкладки рисуем сами (согласованный тёмный вид)
            SetWindowSubclass(g_tabs, TabSubclassProc, 1, 0);

            const int lx = SX(140), lh = SX(26);
            // NES page controls (positions set in LayoutAll)
            g_grpSource =
                MkButton(hwnd, IDC_GRP_SOURCE, BS_GROUPBOX, 0, 0, 10, 10);
            g_grpTarget =
                MkButton(hwnd, IDC_GRP_TARGET, BS_GROUPBOX, 0, 0, 10, 10);
            g_grpOptions =
                MkButton(hwnd, IDC_GRP_OPTIONS, BS_GROUPBOX, 0, 0, 10, 10);
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
                WS_EX_CLIENTEDGE, L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | ES_MULTILINE |
                    ES_AUTOVSCROLL | ES_READONLY,
                0, 0, lx, lh, hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_LOG)), g_hInst, nullptr);

            g_placeholder = MkStatic(hwnd, IDC_PLACEHOLDER, SS_CENTER | WS_BORDER, 0, 0,
                                     10, 10);

            // дорисовка светлого текста в комбобоксах (тёмная тема)
            SetWindowSubclass(g_srcEmu, ComboSubclassProc, 1, 0);
            SetWindowSubclass(g_type, ComboSubclassProc, 1, 0);
            SetWindowSubclass(g_tgtEmu, ComboSubclassProc, 1, 0);

    // tab captions are set by ApplyLanguage; insert the three pages first
    TCITEMW ti;
    ZeroMemory(&ti, sizeof(ti));
    ti.mask = TCIF_TEXT;
    ti.pszText = const_cast<LPWSTR>(L"NES / FC");
    TabCtrl_InsertItem(g_tabs, 0, &ti);
    ti.pszText = const_cast<LPWSTR>(L"SNES / SFC");
    TabCtrl_InsertItem(g_tabs, 1, &ti);
    ti.pszText = const_cast<LPWSTR>(L"N64");
    TabCtrl_InsertItem(g_tabs, 2, &ti);

            // default states
            SendMessageW(g_backup, BM_SETCHECK, BST_CHECKED, 0);
            ComboBox_SetCurSel(g_tgtEmu, 1);

            // fonts for everything
            SendMessageW(g_tabs, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
            for (HWND* h : kNesCtrls)
                if (*h) SendMessageW(*h, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
            SendMessageW(g_placeholder, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);

            LoadLangIni();
            ApplyLanguage(g_lang, false);  // тексты, комбобоксы, меню
            ApplyTitleBarTheme();          // тёмный заголовок (DWM)
            ApplyControlThemes();          // тёмные контролы (DarkMode_Explorer)
            LayoutAll();
            ShowNesPage(true);
            ShowWindow(g_placeholder, SW_HIDE);

            DragAcceptFiles(hwnd, TRUE);
            return 0;
        }

        case WM_SIZE:
            LayoutAll();
            return 0;

        case WM_GETMINMAXINFO: {
            MINMAXINFO* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
            mmi->ptMinTrackSize.x = SX(560);
            mmi->ptMinTrackSize.y = SX(560);
            return 0;
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
            }
            return 0;
        }

        case WM_DROPFILES:
            OnDropFiles(reinterpret_cast<HDROP>(wParam));
            return 0;

        // ---- тёмная тема: фон окна -------------------------------------------
        case WM_ERASEBKGND: {
            HDC dc = reinterpret_cast<HDC>(wParam);
            RECT rc;
            GetClientRect(hwnd, &rc);
            FillRect(dc, &rc, g_dark ? g_brWin : GetSysColorBrush(COLOR_BTNFACE));
            return 1;
        }

        // ---- тёмная тема: цвета контролов ------------------------------------
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN: {
            if (!g_dark)
                return DefWindowProcW(hwnd, msg, wParam, lParam);
            HDC dc = reinterpret_cast<HDC>(wParam);
            const HWND ctrl = reinterpret_cast<HWND>(lParam);
            if (ctrl == g_log) {
                // журнал — только читается: рисуем как тёмное поле ввода
                SetBkColor(dc, kDarkCtrl);
                SetTextColor(dc, kDarkText);
                return reinterpret_cast<LRESULT>(g_brCtrl);
            }
            // метки, групповые рамки, чекбокс, панель вкладок
            SetBkColor(dc, kDarkBg);
            SetTextColor(dc, kDarkText);
            return reinterpret_cast<LRESULT>(g_brWin);
        }

        case WM_CTLCOLOREDIT: {
            if (!g_dark) return DefWindowProcW(hwnd, msg, wParam, lParam);
            HDC dc = reinterpret_cast<HDC>(wParam);
            SetBkColor(dc, kDarkCtrl);
            SetTextColor(dc, kDarkText);
            return reinterpret_cast<LRESULT>(g_brCtrl);
        }

        case WM_CTLCOLORLISTBOX: {
            if (!g_dark) return DefWindowProcW(hwnd, msg, wParam, lParam);
            HDC dc = reinterpret_cast<HDC>(wParam);
            SetBkColor(dc, kDarkCtrl);
            SetTextColor(dc, kDarkText);
            return reinterpret_cast<LRESULT>(g_brCtrl);
        }

        // ---- owner-draw: меню и кнопки (тёмная тема) -----------------------------
        case WM_MEASUREITEM: {
            MEASUREITEMSTRUCT* mis = reinterpret_cast<MEASUREITEMSTRUCT*>(lParam);
            if (mis->CtlType != ODT_MENU)
                return DefWindowProcW(hwnd, msg, wParam, lParam);
            const OdItem* od = reinterpret_cast<const OdItem*>(mis->itemData);
            if (!od || !od->text) return DefWindowProcW(hwnd, msg, wParam, lParam);
            HDC dc = GetDC(hwnd);
            HGDIOBJ oldF = SelectObject(dc, g_menuFont);
            SIZE sz = {0, 0};
            GetTextExtentPoint32W(dc, od->text, lstrlenW(od->text), &sz);
            SelectObject(dc, oldF);
            ReleaseDC(hwnd, dc);
            if (od->isBar) {
                mis->itemWidth = sz.cx + SX(16);
                mis->itemHeight = GetSystemMetrics(SM_CYMENU);
            } else {
                mis->itemWidth = sz.cx + SX(38);   // текст + маркер выбора + отступы
                mis->itemHeight = std::max<int>(sz.cy, GetSystemMetrics(SM_CYMENU) - 4) +
                                  SX(6);
            }
            return TRUE;
        }

        case WM_DRAWITEM: {
            DRAWITEMSTRUCT* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
            if (dis->CtlType == ODT_MENU) {
                const OdItem* od = reinterpret_cast<const OdItem*>(dis->itemData);
                if (!od || !od->text) return DefWindowProcW(hwnd, msg, wParam, lParam);
                const bool sel = (dis->itemState & ODS_SELECTED) != 0;
                const int saveDC = SaveDC(dis->hDC);
                if (od->isBar) {
                    FillRect(dis->hDC, &dis->rcItem, sel ? g_brBarSel : g_brWin);
                    SetTextColor(dis->hDC, kDarkText);
                } else {
                    FillRect(dis->hDC, &dis->rcItem, sel ? g_brMenuSel : g_brWin);
                    SetTextColor(dis->hDC, sel ? RGB(255, 255, 255) : kDarkText);
                }
                SetBkMode(dis->hDC, TRANSPARENT);
                HGDIOBJ oldF = SelectObject(dis->hDC, g_menuFont);
                RECT rc = dis->rcItem;
                rc.left += od->isBar ? SX(8) : SX(26);   // место под маркер выбора
                // без DT_NOPREFIX: DrawTextW сам скроет & и подчеркнёт букву-мнемонику
                DrawTextW(dis->hDC, od->text, -1, &rc,
                          DT_SINGLELINE | DT_VCENTER | DT_LEFT);
                SelectObject(dis->hDC, oldF);
                if (!od->isBar && (dis->itemState & ODS_CHECKED)) {
                    // маркер-точка активного языка (радиокнопка)
                    const int cy = (dis->rcItem.top + dis->rcItem.bottom) / 2;
                    const int cx = dis->rcItem.left + SX(12);
                    const COLORREF mark = sel ? RGB(255, 255, 255) : kDarkText;
                    HPEN pen = CreatePen(PS_SOLID, 1, mark);
                    HBRUSH br = CreateSolidBrush(mark);
                    HGDIOBJ oldP = SelectObject(dis->hDC, pen);
                    HGDIOBJ oldB = SelectObject(dis->hDC, br);
                    Ellipse(dis->hDC, cx - SX(4), cy - SX(4), cx + SX(4), cy + SX(4));
                    SelectObject(dis->hDC, oldB);
                    SelectObject(dis->hDC, oldP);
                    DeleteObject(pen);
                    DeleteObject(br);
                }
                RestoreDC(dis->hDC, saveDC);
                return TRUE;
            }
            if (dis->CtlType == ODT_BUTTON) {
                if (GetWindowLongW(dis->hwndItem, GWL_STYLE) & BS_GROUPBOX)
                    DrawDarkGroupBox(dis);
                else
                    DrawDarkButton(dis);
                return TRUE;
            }
            return DefWindowProcW(hwnd, msg, wParam, lParam);
        }

        // ---- живая смена темы Windows (без перезапуска) -----------------------
        case WM_SETTINGCHANGE:
            if (wParam == 0 && lParam != 0 &&
                wcscmp(reinterpret_cast<const wchar_t*>(lParam), L"ImmersiveColorSet") == 0)
                ApplyThemeAll();
            return DefWindowProcW(hwnd, msg, wParam, lParam);

        case WM_THEMECHANGED:
            // смена визуального стиля сбрасывает темы контролов — применяем заново
            ApplyControlThemes();
            return DefWindowProcW(hwnd, msg, wParam, lParam);

        case WM_COMMAND: {
            const int id = LOWORD(wParam);
            const int code = HIWORD(wParam);
            switch (id) {
                case IDC_SRC_BROWSE: {
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
                    std::wstring p = PickOpenFile(g_main, Str(S_PickRomTitle, g_lang),
                                                  Str(S_RomFilter, g_lang));
                    if (!p.empty()) SetWindowTextW(g_romFile, p.c_str());
                    return 0;
                }
                case IDC_OUTDIR_BROWSE: {
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
                case IDM_ABOUT:
                    MessageBoxW(g_main, Str(S_AboutText, g_lang), Str(S_AppTitle, g_lang),
                                MB_OK | MB_ICONINFORMATION);
                    return 0;
                default:
                    break;
            }
            return 0;
        }

        case WM_DESTROY:
            if (g_font) DeleteObject(g_font);
            if (g_menuFont) DeleteObject(g_menuFont);
            if (g_brWin) DeleteObject(g_brWin);
            if (g_brCtrl) DeleteObject(g_brCtrl);
            if (g_brMenuSel) DeleteObject(g_brMenuSel);
            if (g_brBarSel) DeleteObject(g_brBarSel);
            PostQuitMessage(0);
            return 0;

        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

// ========================================================================================
// Entry point
// ========================================================================================
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow) {
    g_hInst = hInstance;

    // FIX (отображение при высоком DPI): масштаб вычисляется ДО создания окна.
    // Раньше он считался только в WM_CREATE — то есть уже после того, как размер
    // окна рассчитан по SX() с ещё не обновлённым g_scale (всегда 1.0), из-за чего
    // при масштабе 125–200% контролы вылезали за пределы окна.
    HDC dc0 = GetDC(nullptr);
    g_scale = GetDeviceCaps(dc0, LOGPIXELSX) / 96.0f;
    ReleaseDC(nullptr, dc0);
    if (g_scale < 1.0f) g_scale = 1.0f;

    INITCOMMONCONTROLSEX icc;
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_WIN95_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(2));
    wc.hIconSm = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(2), IMAGE_ICON,
                                   GetSystemMetrics(SM_CXSMICON),
                                   GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"RetroSaveConverterWnd";
    RegisterClassExW(&wc);

    const DWORD style = WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX;
    const int W = SX(560), H = SX(590);
    RECT adj = {0, 0, W, H};
    AdjustWindowRect(&adj, style, TRUE);

    g_main = CreateWindowExW(0, L"RetroSaveConverterWnd", L"Retro Save Converter",
                             style, CW_USEDEFAULT, CW_USEDEFAULT,
                             adj.right - adj.left, adj.bottom - adj.top,
                             nullptr, nullptr, hInstance, nullptr);
    if (!g_main) return 1;

    ShowWindow(g_main, nCmdShow);
    UpdateWindow(g_main);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (!IsDialogMessageW(g_main, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    CoUninitialize();
    return static_cast<int>(msg.wParam);
}
