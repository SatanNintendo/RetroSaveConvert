// ========================================================================================
// RetroSaveConverter — battery-save converter for NES emulators
// Core conversion engine (platform-independent, no Win32 code here)
//
// Supported emulators (NES/FC):
//   MesenCE       — SRAM: <rom>.sav | EEPROM: <rom>.eeprom128 / <rom>.eeprom256
//   Nestopia      — everything in <rom>.sav (EEPROM boards: raw chip data,
//                   Datach = 24C02(256) + 24C01(128) concatenated = 384 bytes)
//   Nintendulator — everything in <rom>.sav (SRAM area; EEPROM mappers 16/159
//                   store raw chip data of 256/128 bytes; mapper 157 unsupported)
//   Raw           — generic .sav passthrough (works with most other emulators)
//
// Bit order of EEPROM data bytes is IDENTICAL in all three emulators
// (24C01: LSB-first, 24C02: MSB-first), so no bit-reversal is ever required —
// verified against the sources of each emulator (see README.md, "Research").
// ========================================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace rsc {

// ---------------------------------------------------------------------------------------
// Emulator formats
// ---------------------------------------------------------------------------------------
enum class Emu {
    Auto,          // source only: detect from file
    MesenCE,
    Nestopia,
    Nintendulator,
    Raw            // generic .sav
};

// ---------------------------------------------------------------------------------------
// Kind of battery-backed content inside the file
// ---------------------------------------------------------------------------------------
enum class SaveType {
    Auto,          // detect
    SRAM,          // regular battery-backed work RAM (Zelda, Final Fantasy, ...)
    Eeprom24C01,   // 128-byte Bandai serial EEPROM
    Eeprom24C02,   // 256-byte Bandai serial EEPROM
    EepromDatach   // Datach (mapper 157): 24C02 256 B + 24C01 128 B
};

// ---------------------------------------------------------------------------------------
// Parsed iNES / NES 2.0 ROM header (the ROM file is OPTIONAL — used to refine
// auto-detection and to normalize output sizes)
// ---------------------------------------------------------------------------------------
struct RomInfo {
    bool        valid = false;
    bool        isNes20 = false;
    int         mapper = -1;
    int         subMapper = -1;
    bool        battery = false;
    uint32_t    batteryRamSize = 0;   // bytes; NES 2.0 only, 0 = unspecified

    bool Parse(const uint8_t* data, size_t size);  // false if not an iNES file
};

RomInfo ParseNesHeader(const uint8_t* data, size_t size);

// Expected save type for a given (valid) ROM header
SaveType ExpectedSaveTypeForRom(const RomInfo& rom);

// ---------------------------------------------------------------------------------------
// Result reporting: the engine emits structured issues; the UI translates them
// into the selected language
// ---------------------------------------------------------------------------------------
enum class Severity { Info, Warning, Error };

enum class Msg {
    // info
    DetectedSource,     // p1=format name, p2=type name, p3=size
    RomInfo,            // p1=mapper, p2=n/s2.0, p3=battery
    SiblingMerged,      // p1=file name
    Normalized,         // p1=from size, p2=to size
    OutputWritten,      // p1=file name, p2=bytes
    BackupCreated,      // p1=file name
    NoAction,           // source==target, file copied unchanged
    // warnings
    WarnRomTypeConflict,// p1=detected, p2=rom-expected
    WarnMesenSramUnused,// Mesen .sav of an EEPROM board carries no real data
    WarnNoSibling,      // p1=missing file name
    WarnPadding,        // p1=size
    WarnTruncating,     // p1=size
    WarnOverwrite,      // p1=file name
    // errors
    ErrEmptyFile,
    ErrTooSmall,        // p1=size
    ErrTypeMismatch,    // p1=src type, p2=dst type
    ErrDatachUnsupported,   // target Nintendulator
    ErrReadFile,        // p1=name
    ErrWriteFile,       // p1=name
};

struct Issue {
    Severity    severity;
    Msg         code;
    std::vector<std::wstring> params;
};

// ---------------------------------------------------------------------------------------
// Filesystem abstraction (keeps the core testable on Linux / in-memory)
// ---------------------------------------------------------------------------------------
struct IEnv {
    virtual ~IEnv() = default;
    // read whole file; false if it does not exist / cannot be read
    virtual bool ReadFileBytes(const std::wstring& path, std::vector<uint8_t>& out) = 0;
    // write whole file, creating or overwriting
    virtual bool WriteFileBytes(const std::wstring& path, const uint8_t* data, size_t size) = 0;
    virtual bool FileExists(const std::wstring& path) = 0;
};

// ---------------------------------------------------------------------------------------
// Conversion request / result
// ---------------------------------------------------------------------------------------
struct ConvertRequest {
    // source
    std::wstring            sourcePath;     // full path of the picked save file
    std::vector<uint8_t>    sourceBytes;    // its contents (UI reads it, passes here)
    Emu                     sourceFormat = Emu::Auto;
    SaveType                saveType = SaveType::Auto;

    // optional ROM (iNES) for detection/normalization
    RomInfo                 rom;            // rom.valid == false when not supplied

    // target
    Emu                     target = Emu::Nestopia;
    std::wstring            outputDir;      // empty = directory of the source file
    bool                    backup = true;  // create .bak next to overwritten source
};

struct OutputFile {
    std::wstring            path;
    std::vector<uint8_t>    bytes;
};

struct ConvertResult {
    bool                    ok = false;
    std::vector<OutputFile> outputs;
    std::vector<Issue>      issues;

    bool HasErrors() const;
    bool HasWarnings() const;
};

// Run the conversion. Files listed in result.outputs are NOT written by the core —
// call WriteOutputs() to persist them (keeps UI in control of overwrite prompts).
ConvertResult Convert(const ConvertRequest& req, IEnv& env);

// Persist conversion outputs (with optional .bak of a file being replaced).
// Returns false if any write failed (issues appended).
bool WriteOutputs(ConvertResult& result, const ConvertRequest& req, IEnv& env);

// ---------------------------------------------------------------------------------------
// Helpers shared with the UI
// ---------------------------------------------------------------------------------------
const wchar_t* EmuToName(Emu e);
const wchar_t* SaveTypeToName(SaveType t);

} // namespace rsc
