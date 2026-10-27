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

static void SaveLangIni() {
    const std::wstring ini = ExeDirW() + L"RetroSaveConverter.ini";
    WritePrivateProfileStringW(L"Settings", L"Language",
                               g_lang == rsc::LANG_RU ? L"ru" : L"en", ini.c_str());
}

static void LoadLangIni() {
    const std::wstring ini = ExeDirW() + L"RetroSaveConverter.ini";
    UINT v = GetPrivateProfileIntW(L"Settings", L"Language", 0, ini.c_str());
    g_lang = (v == 1) ? rsc::LANG_RU : rsc::LANG_EN;
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

static HWND* const kNesCtrls[] = {
    &g_grpSource, &g_grpTarget, &g_grpOptions,
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
    struct Cb { HWND h; const wchar_t* items[6]; int defSel; };
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

    // menu
    HMENU old = GetMenu(g_main);
    HMENU bar = CreateMenu();
    HMENU mLang = CreatePopupMenu();
    AppendMenuW(mLang, MF_STRING, IDM_LANG_EN, Str(S_MenuEnglish, lang));
    AppendMenuW(mLang, MF_STRING, IDM_LANG_RU, Str(S_MenuRussian, lang));
    HMENU mHelp = CreatePopupMenu();
    AppendMenuW(mHelp, MF_STRING, IDM_ABOUT, Str(S_MenuAbout, lang));
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(mLang),
                Str(S_MenuLanguage, lang));
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(mHelp), Str(S_MenuHelp, lang));
    CheckMenuRadioItem(mLang, IDM_LANG_EN, IDM_LANG_RU,
                       IDM_LANG_EN + static_cast<int>(lang), MF_BYCOMMAND);
    SetMenu(g_main, bar);
    if (old) DestroyMenu(old);
    DrawMenuBar(g_main);

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

            // DPI scale
            HDC dc = GetDC(hwnd);
            g_scale = GetDeviceCaps(dc, LOGPIXELSX) / 96.0f;
            ReleaseDC(hwnd, dc);
            if (g_scale < 1.0f) g_scale = 1.0f;

            // message font
            NONCLIENTMETRICSW ncm;
            ZeroMemory(&ncm, sizeof(ncm));
            ncm.cbSize = sizeof(ncm);
            SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
            g_font = CreateFontIndirectW(&ncm.lfMessageFont);

            // tabs
            g_tabs = CreateWindowExW(0, WC_TABCONTROLW, L"",
                                     WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS |
                                         TCS_TABS,
                                     0, 0, cs->cx, cs->cy, hwnd,
                                     reinterpret_cast<HMENU>(
                                         static_cast<INT_PTR>(IDC_TABS)),
                                     g_hInst, nullptr);

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
            ApplyLanguage(g_lang, false);
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

    const int W = SX(560), H = SX(590);
    RECT adj = {0, 0, W, H};
    AdjustWindowRect(&adj, WS_OVERLAPPEDWINDOW, TRUE);

    g_main = CreateWindowExW(0, L"RetroSaveConverterWnd", L"Retro Save Converter",
                             WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX, CW_USEDEFAULT,
                             CW_USEDEFAULT, adj.right - adj.left, adj.bottom - adj.top,
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
