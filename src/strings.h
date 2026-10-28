// ========================================================================================
// RetroSaveConverter — UI string tables (English default + Russian)
// ========================================================================================
#pragma once

#include "converter.h"

#include <string>
#include <vector>

namespace rsc {

enum Lang { LANG_EN = 0, LANG_RU = 1 };

// ---------------------------------------------------------------------------------------
// UI string ids
// ---------------------------------------------------------------------------------------
enum StrId {
    S_AppTitle,
    S_TabNes, S_TabSnes, S_TabN64,
    S_GrpSource, S_GrpTarget, S_GrpOptions,
    S_SrcEmu, S_SaveFile, S_Browse, S_RomFile, S_ContentType,
    S_TargetEmu, S_OutDir, S_OutDirHint, S_Backup, S_Convert, S_Log,
    S_ComingSoonSnes, S_ComingSoonN64,
    S_MenuLanguage, S_MenuView, S_MenuLightTheme, S_MenuDarkTheme,
    S_MenuEnglish, S_MenuRussian, S_MenuAbout, S_MenuHelp,
    S_AutoDetect, S_TypeSram, S_Type24C01, S_Type24C02, S_TypeDatach,
    S_GenericSav,
    S_PickSaveTitle, S_PickRomTitle, S_PickFolderTitle,
    S_SaveFilter, S_RomFilter,
    S_ErrNoSource, S_ErrReadSave, S_ErrReadRom, S_ErrTarget,
    S_AskOverwrite, S_AskInPlace, S_BackupNote, S_NoBackupNote,
    S_Cancelled, S_Done, S_DoneWarn, S_Failed,
    S_DroppedSave, S_DroppedRom, S_DroppedUnknown,
    S_LogInfo, S_LogWarn, S_LogError,
    S_AboutText,
    S_COUNT
};

inline const wchar_t* Str(StrId id, Lang lang) {
    static const wchar_t* const kTable[2][S_COUNT] = {
        // ------------------------------------------------------------------ English
        {
            L"Retro Save Converter",
            L"NES / FC", L"SNES / SFC", L"N64",
            L"Source save", L"Target format", L"Options",
            L"Source emulator:", L"Save file:", L"Browse...", L"ROM file (optional):",
            L"Content type:",
            L"Target emulator:", L"Output folder:", L"(empty = same folder as the save file)",
            L"Back up replaced files (.bak)", L"Convert", L"Log:",
            L"SNES/SFC save conversion will be added in a future version.",
            L"N64 save conversion will be added in a future version.",
            L"&Language", L"&View", L"&Light theme", L"&Dark theme",
            L"&English", L"&Russian", L"&About...", L"&Help",
            L"Auto-detect",
            L"Battery SRAM (regular save)",
            L"EEPROM 24C01 — 128 bytes",
            L"EEPROM 24C02 — 256 bytes",
            L"EEPROM 24C02+24C01 — Datach, 384 bytes",
            L"Generic (.sav)",
            L"Select the save file",
            L"Select the ROM file (iNES .nes)",
            L"Select the output folder",
            L"Save files (*.sav;*.srm;*.prb;*.ram;*.eeprom;*.eeprom128;*.eeprom256)\0*.sav;*.srm;*.prb;*.ram;*.eeprom;*.eeprom128;*.eeprom256\0All files (*.*)\0*.*\0",
            L"ROM images (*.nes;*.unf)\0*.nes;*.unf\0All files (*.*)\0*.*\0",
            L"Select a save file first.",
            L"Could not read the save file:\n%1",
            L"Could not read the ROM file:\n%1",
            L"Select a target emulator.",
            L"The following files already exist and will be overwritten:\n\n%1\n\nContinue?",
            L"The source file itself will be replaced%1. Continue?",
            L" (a .bak backup will be created)",
            L" (NO backup will be created!)",
            L"Conversion cancelled.",
            L"Conversion finished.",
            L"Conversion finished with warnings.",
            L"Conversion failed.",
            L"Save file set: %1",
            L"ROM file set: %1",
            L"Unsupported file type: %1",
            L"INFO", L"WARNING", L"ERROR",
            L"Retro Save Converter 1.0\n\n"
            L"Converts in-game battery saves (NOT save states) between NES emulators:\n"
            L"MesenCE, Nestopia, Nintendulator, FCEUX, puNES and ares (NES).\n\n"
            L"Based on the save-format research of each emulator's source code.\n"
            L"MIT License. See LICENSE for details.",
        },
        // ------------------------------------------------------------------ Русский
        {
            L"Конвертер сохранений Retro",
            L"NES / FC", L"SNES / SFC", L"N64",
            L"Исходное сохранение", L"Целевой формат", L"Параметры",
            L"Эмулятор-источник:", L"Файл сохранения:", L"Обзор...", L"Файл ROM (необязательно):",
            L"Тип содержимого:",
            L"Целевой эмулятор:", L"Папка для результата:", L"(пусто = папка файла сохранения)",
            L"Создавать резервные копии (.bak)", L"Конвертировать", L"Журнал:",
            L"Конвертер сохранений SNES/SFC появится в следующей версии.",
            L"Конвертер сохранений N64 появится в следующей версии.",
            L"&Язык", L"&Вид", L"Светлая тема", L"Тёмная тема",
            L"&Английский", L"&Русский", L"&О программе...", L"&Справка",
            L"Автоопределение",
            L"Батарейный SRAM (обычное сохранение)",
            L"EEPROM 24C01 — 128 байт",
            L"EEPROM 24C02 — 256 байт",
            L"EEPROM 24C02+24C01 — Datach, 384 байта",
            L"Универсальный (.sav)",
            L"Выберите файл сохранения",
            L"Выберите файл ROM (iNES .nes)",
            L"Выберите папку для результата",
            L"Файлы сохранений (*.sav;*.srm;*.prb;*.ram;*.eeprom;*.eeprom128;*.eeprom256)\0*.sav;*.srm;*.prb;*.ram;*.eeprom;*.eeprom128;*.eeprom256\0Все файлы (*.*)\0*.*\0",
            L"Образы ROM (*.nes;*.unf)\0*.nes;*.unf\0Все файлы (*.*)\0*.*\0",
            L"Сначала выберите файл сохранения.",
            L"Не удалось прочитать файл сохранения:\n%1",
            L"Не удалось прочитать файл ROM:\n%1",
            L"Выберите целевой эмулятор.",
            L"Следующие файлы уже существуют и будут перезаписаны:\n\n%1\n\nПродолжить?",
            L"Исходный файл будет заменён%1. Продолжить?",
            L" (будет создана резервная копия .bak)",
            L" (резервная копия НЕ будет создана!)",
            L"Конвертация отменена.",
            L"Конвертация завершена.",
            L"Конвертация завершена с предупреждениями.",
            L"Конвертация не удалась.",
            L"Файл сохранения выбран: %1",
            L"Файл ROM выбран: %1",
            L"Неподдерживаемый тип файла: %1",
            L"ИНФО", L"ВНИМАНИЕ", L"ОШИБКА",
            L"Конвертер сохранений Retro 1.0\n\n"
            L"Конвертирует игровые сохранения (НЕ сейв-стейты) между эмуляторами NES:\n"
            L"MesenCE, Nestopia, Nintendulator, FCEUX, puNES и ares (NES).\n\n"
            L"Форматы сохранений изучены по исходному коду каждого эмулятора.\n"
            L"Лицензия MIT — см. файл LICENSE.",
        },
    };
    return kTable[lang][id];
}

// ---------------------------------------------------------------------------------------
// Core-engine message templates, per language. %1..%3 are replaced with params.
// ---------------------------------------------------------------------------------------
inline const wchar_t* MsgTemplate(Msg m, Lang lang) {
    static const wchar_t* const kMsg[2][23] = {
        // -------------------------------------------------------------- English
        {
            /* DetectedSource     */ L"Detected: source %1, type %2, size %3 bytes.",
            /* RomInfo            */ L"ROM header: mapper %1 (%2, battery: %3).",
            /* SiblingMerged      */ L"Merged complementary EEPROM file: %1",
            /* Normalized         */ L"Size normalized from %1 to %2 bytes.",
            /* OutputWritten      */ L"Written: %1 (%2 bytes)",
            /* BackupCreated      */ L"Backup created: %1",
            /* NoAction           */ L"%1 is already in the target format — left unchanged.",
            /* WarnRomTypeConflict*/ L"File looks like %1, but the ROM header suggests %2 — using %2.",
            /* WarnMesenSramUnused*/ L"This game uses an EEPROM (mapper 16/157/159). MesenCE keeps the real data in <game>.eeprom128/.eeprom256 — the selected .sav only holds unused SRAM.",
            /* WarnNoSibling      */ L"Complementary EEPROM file %1 not found — the missing chip data will be filled with 0xFF.",
            /* WarnPadding        */ L"File is smaller than the EEPROM size — padded up to %1 bytes with 0xFF.",
            /* WarnTruncating     */ L"File is larger than the EEPROM size — truncated to %1 bytes.",
            /* WarnOverwrite      */ L"Overwriting existing file: %1",
            /* ErrEmptyFile       */ L"The save file is empty.",
            /* ErrTooSmall        */ L"File is too small to be a battery save (%1 bytes).",
            /* ErrTypeMismatch    */ L"Incompatible save types: %1 -> %2.",
            /* ErrDatachUnsupported*/ L"Datach (mapper 157) saves are not supported by Nintendulator.",
            /* ErrReadFile        */ L"Could not read file: %1",
            /* ErrWriteFile       */ L"Could not write file: %1",
            /* ErrDatachUnsupportedByTarget */ L"The Datach save cannot be represented by %1 for the selected ROM because its external 24C01 storage is unavailable or too small.",
            /* WarnAresRamUnused  */ L"This ROM uses EEPROM (mapper 16/157/159). In ares, the actual game EEPROM is stored in save.eeprom and, for Datach, external.eeprom; save.ram is a separate RAM region.",
            /* ErrPunesCompositeNvram */ L"This puNES .prb contains %1 bytes of PRG NVRAM plus %2 bytes of additional NVRAM. Converting it to this target would discard unsupported data; choose puNES as the target to preserve the complete payload.",
            /* ErrPunesNvramTooSmall */ L"This ROM declares only %2 bytes of PRG NVRAM, but the selected EEPROM requires at least %1 bytes. Choose a compatible ROM/header or another target.",
        },
        // -------------------------------------------------------------- Русский
        {
            /* DetectedSource     */ L"Определено: источник %1, тип %2, размер %3 байт.",
            /* RomInfo            */ L"Заголовок ROM: маппер %1 (%2, батарея: %3).",
            /* SiblingMerged      */ L"Присоединён дополнительный файл EEPROM: %1",
            /* Normalized         */ L"Размер нормализован с %1 до %2 байт.",
            /* OutputWritten      */ L"Записан файл: %1 (%2 байт)",
            /* BackupCreated      */ L"Создана резервная копия: %1",
            /* NoAction           */ L"%1 уже в целевом формате — оставлен без изменений.",
            /* WarnRomTypeConflict*/ L"Файл похож на %1, но заголовок ROM указывает на %2 — используется %2.",
            /* WarnMesenSramUnused*/ L"Эта игра использует EEPROM (маппер 16/157/159). MesenCE хранит реальные данные в <игра>.eeprom128/.eeprom256 — выбранный .sav содержит только неиспользуемый SRAM.",
            /* WarnNoSibling      */ L"Дополнительный файл EEPROM %1 не найден — данные отсутствующего чипа будут заполнены 0xFF.",
            /* WarnPadding        */ L"Файл меньше размера EEPROM — дополнен до %1 байт значением 0xFF.",
            /* WarnTruncating     */ L"Файл больше размера EEPROM — усечён до %1 байт.",
            /* WarnOverwrite      */ L"Перезапись существующего файла: %1",
            /* ErrEmptyFile       */ L"Файл сохранения пуст.",
            /* ErrTooSmall        */ L"Файл слишком мал для игрового сохранения (%1 байт).",
            /* ErrTypeMismatch    */ L"Несовместимые типы сохранений: %1 -> %2.",
            /* ErrDatachUnsupported*/ L"Сохранения Datach (маппер 157) не поддерживаются Nintendulator.",
            /* ErrReadFile        */ L"Не удалось прочитать файл: %1",
            /* ErrWriteFile       */ L"Не удалось записать файл: %1",
            /* ErrDatachUnsupportedByTarget */ L"Сохранение Datach нельзя представить в формате %1 для выбранного ROM: внешняя EEPROM 24C01 отсутствует в конфигурации или для неё недостаточно памяти.",
            /* WarnAresRamUnused  */ L"Эта игра использует EEPROM (маппер 16/157/159). В ares настоящая EEPROM игры хранится в save.eeprom, а у Datach также в external.eeprom; save.ram — отдельная область RAM.",
            /* ErrPunesCompositeNvram */ L"Файл puNES .prb содержит %1 байт PRG NVRAM и ещё %2 байт дополнительных данных NVRAM. При конвертации в выбранный формат эти неподдерживаемые данные будут потеряны; выберите puNES, чтобы сохранить полный файл.",
            /* ErrPunesNvramTooSmall */ L"В заголовке выбранного ROM указано только %2 байт PRG NVRAM, а для выбранной EEPROM нужно не менее %1 байт. Выберите совместимый ROM/заголовок или другой целевой эмулятор.",
        },
    };
    // keep the enum order of Msg in sync with the tables above
    static_assert(static_cast<int>(Msg::ErrDatachUnsupportedByTarget) == 19, "Msg table order");
    static_assert(static_cast<int>(Msg::ErrPunesCompositeNvram) == 21, "Msg table size/order");
    static_assert(static_cast<int>(Msg::ErrPunesNvramTooSmall) == 22, "Msg table size/order");
    return kMsg[lang][static_cast<int>(m)];
}

// Expand %1..%3 in a template.
//
// Single left-to-right pass over the TEMPLATE only: substituted text is never rescanned.
// (The earlier replace-in-a-loop version looped forever -- and kept growing the string --
// when a parameter such as a file name contained its own token, e.g. "Game%1.sav", and it
// also let a "%2" inside one parameter be overwritten by the next parameter.)
// A token whose parameter is missing is left in the output unchanged.
inline std::wstring Expand(const std::wstring& tmpl, const std::vector<std::wstring>& params) {
    std::wstring out;
    out.reserve(tmpl.size() + 64);
    for (size_t i = 0; i < tmpl.size(); ++i) {
        const wchar_t c = tmpl[i];
        if (c == L'%' && i + 1 < tmpl.size()) {
            const wchar_t d = tmpl[i + 1];
            if (d >= L'1' && d <= L'3') {
                const size_t index = static_cast<size_t>(d - L'1');
                if (index < params.size()) {
                    out += params[index];
                    ++i;  // consume the digit as well
                    continue;
                }
            }
        }
        out += c;
    }
    return out;
}

inline std::wstring FormatIssue(const Issue& issue, Lang lang) {
    return Expand(MsgTemplate(issue.code, lang), issue.params);
}

}  // namespace rsc
