// ========================================================================================
// RetroSaveConverter — core conversion engine implementation
// ========================================================================================
#include "converter.h"

#include <algorithm>
#include <utility>

namespace rsc {

// ---------------------------------------------------------------------------------------
// Small path helpers (both '\\' and '/' are treated as separators so the core
// can be exercised by unit tests on Linux)
// ---------------------------------------------------------------------------------------
namespace {

std::wstring ToLower(std::wstring s) {
    for (auto& c : s) {
        if (c >= L'A' && c <= L'Z') c = static_cast<wchar_t>(c + 32);
    }
    return s;
}

std::wstring GetDir(const std::wstring& p) {
    const size_t n = p.find_last_of(L"\\/");
    return (n == std::wstring::npos) ? std::wstring() : p.substr(0, n + 1);
}

std::wstring GetName(const std::wstring& p) {  // file name incl. extension
    const size_t n = p.find_last_of(L"\\/");
    return (n == std::wstring::npos) ? p : p.substr(n + 1);
}

std::wstring GetBaseNoExt(const std::wstring& p) {  // file name w/o extension
    const std::wstring name = GetName(p);
    const size_t n = name.find_last_of(L'.');
    return (n == std::wstring::npos || n == 0) ? name : name.substr(0, n);
}

std::wstring GetExtLower(const std::wstring& p) {
    const std::wstring name = GetName(p);
    const size_t n = name.find_last_of(L'.');
    return (n == std::wstring::npos || n == 0) ? std::wstring() : ToLower(name.substr(n));
}

bool SamePath(const std::wstring& a, const std::wstring& b) {
    return ToLower(a) == ToLower(b);
}

std::wstring Num(size_t v) {
    if (v == 0) return L"0";
    wchar_t buf[24];
    size_t i = 24;
    while (v > 0) {
        buf[--i] = static_cast<wchar_t>(L'0' + (v % 10));
        v /= 10;
    }
    return std::wstring(&buf[i], 24 - i);
}

void AddIssue(std::vector<Issue>& out, Severity s, Msg m,
              std::vector<std::wstring> params = {}) {
    Issue i{s, m, std::move(params)};
    out.push_back(std::move(i));
}

constexpr size_t kSize24C01 = 128;
constexpr size_t kSize24C02 = 256;
constexpr size_t kSizeDatach = 384;  // 24C02 (256) + 24C01 (128), Nestopia layout
constexpr size_t kMinFileSize = 16;  // smaller than this = definitely not a save

}  // namespace

// ---------------------------------------------------------------------------------------
// iNES / NES 2.0 header
// ---------------------------------------------------------------------------------------
bool RomInfo::Parse(const uint8_t* d, size_t n) {
    *this = RomInfo{};
    if (d == nullptr || n < 16) return false;
    if (d[0] != 'N' || d[1] != 'E' || d[2] != 'S' || d[3] != 0x1A) return false;

    mapper = (d[6] >> 4) | (((d[7] & 0xF0) >> 4) << 4);  // lo nibble | hi nibble<<4
    isNes20 = ((d[7] & 0x0C) == 0x08);
    battery = (d[6] & 0x02) != 0;

    if (isNes20) {
        if (n >= 12) subMapper = (d[11] >> 4) & 0x0F;
        const uint8_t nvramNibble = (n >= 11) ? ((d[10] >> 4) & 0x0F) : 0;
        if (nvramNibble > 0 && nvramNibble < 0x0F)
            batteryRamSize = 64u << nvramNibble;  // Nintendulator: 64 << n
    }

    valid = true;
    return true;
}

RomInfo ParseNesHeader(const uint8_t* data, size_t size) {
    RomInfo r;
    r.Parse(data, size);
    return r;
}

SaveType ExpectedSaveTypeForRom(const RomInfo& rom) {
    if (!rom.valid || !rom.battery) return SaveType::Auto;
    switch (rom.mapper) {
        case 159: return SaveType::Eeprom24C01;
        case 157: return SaveType::EepromDatach;
        case 16:
            // MesenCE logic: submapper 4 = plain SRAM board; submapper 5 with
            // 256-byte NVRAM = 24C02; plain iNES mapper 16 = 24C02 (all three
            // emulators simplify it this way).
            if (rom.isNes20 && rom.subMapper == 4) return SaveType::SRAM;
            if (rom.isNes20 && rom.subMapper == 5)
                return (rom.batteryRamSize == kSize24C02) ? SaveType::Eeprom24C02
                                                          : SaveType::SRAM;
            return SaveType::Eeprom24C02;
        default:
            return SaveType::SRAM;
    }
}

// ---------------------------------------------------------------------------------------
// Names (English; the UI localizes these labels itself)
// ---------------------------------------------------------------------------------------
const wchar_t* EmuToName(Emu e) {
    switch (e) {
        case Emu::MesenCE:      return L"MesenCE";
        case Emu::Nestopia:     return L"Nestopia";
        case Emu::Nintendulator:return L"Nintendulator";
        case Emu::Raw:          return L"Generic (.sav)";
        case Emu::Auto:         return L"Auto";
    }
    return L"?";
}

const wchar_t* SaveTypeToName(SaveType t) {
    switch (t) {
        case SaveType::SRAM:         return L"Battery SRAM";
        case SaveType::Eeprom24C01:  return L"EEPROM 24C01 (128 B)";
        case SaveType::Eeprom24C02:  return L"EEPROM 24C02 (256 B)";
        case SaveType::EepromDatach: return L"EEPROM 24C02+24C01 (Datach)";
        case SaveType::Auto:         return L"Auto";
    }
    return L"?";
}

// ---------------------------------------------------------------------------------------
// Result helpers
// ---------------------------------------------------------------------------------------
bool ConvertResult::HasErrors() const {
    for (const auto& i : issues)
        if (i.severity == Severity::Error) return true;
    return false;
}

bool ConvertResult::HasWarnings() const {
    for (const auto& i : issues)
        if (i.severity == Severity::Warning) return true;
    return false;
}

// ---------------------------------------------------------------------------------------
// EEPROM payload normalization: chip dumps must be exactly chip-sized
// ---------------------------------------------------------------------------------------
namespace {

std::vector<uint8_t> NormalizeChip(const std::vector<uint8_t>& src, size_t chipSize,
                                   std::vector<Issue>& issues) {
    std::vector<uint8_t> out = src;
    if (out.size() < chipSize) {
        AddIssue(issues, Severity::Warning, Msg::WarnPadding, {Num(chipSize)});
        out.resize(chipSize, 0xFF);  // erased EEPROM state
    } else if (out.size() > chipSize) {
        AddIssue(issues, Severity::Warning, Msg::WarnTruncating, {Num(chipSize)});
        out.resize(chipSize);
    }
    return out;
}

}  // namespace

// ---------------------------------------------------------------------------------------
// The conversion itself
// ---------------------------------------------------------------------------------------
ConvertResult Convert(const ConvertRequest& req, IEnv& env) {
    ConvertResult res;

    const std::vector<uint8_t>& src = req.sourceBytes;
    const size_t size = src.size();
    const std::wstring ext = GetExtLower(req.sourcePath);

    if (size == 0) {
        AddIssue(res.issues, Severity::Error, Msg::ErrEmptyFile);
        return res;
    }
    if (size < kMinFileSize) {
        AddIssue(res.issues, Severity::Error, Msg::ErrTooSmall, {Num(size)});
        return res;
    }

    // ---- resolve the effective source format (for reporting) -------------------------
    Emu srcFmt = req.sourceFormat;
    if (srcFmt == Emu::Auto) {
        srcFmt = (ext == L".eeprom128" || ext == L".eeprom256") ? Emu::MesenCE : Emu::Raw;
    }

    // ---- resolve the save type -------------------------------------------------------
    const bool manual = (req.saveType != SaveType::Auto);
    const SaveType romType = ExpectedSaveTypeForRom(req.rom);

    // hard hint from the file extension (MesenCE EEPROM files)
    SaveType extType = SaveType::Auto;
    if (ext == L".eeprom128") extType = SaveType::Eeprom24C01;
    else if (ext == L".eeprom256") extType = SaveType::Eeprom24C02;

    // heuristic from the file size
    SaveType sizeType = SaveType::SRAM;
    if (size == kSize24C01) sizeType = SaveType::Eeprom24C01;
    else if (size == kSize24C02) sizeType = SaveType::Eeprom24C02;
    else if (size == kSizeDatach) sizeType = SaveType::EepromDatach;

    // complementary EEPROM file next to the source (MesenCE Datach pair)
    const std::wstring dir = GetDir(req.sourcePath);
    const std::wstring base = GetBaseNoExt(req.sourcePath);
    auto siblingOf = [&](const wchar_t* e) -> std::wstring { return dir + base + e; };
    std::wstring siblingPath;
    bool siblingFound = false;
    if (extType == SaveType::Eeprom24C02 || extType == SaveType::Eeprom24C01) {
        const wchar_t* other = (extType == SaveType::Eeprom24C02) ? L".eeprom128" : L".eeprom256";
        siblingPath = siblingOf(other);
        std::vector<uint8_t> tmp;
        siblingFound = env.ReadFileBytes(siblingPath, tmp);
    }

    SaveType type;
    if (manual) {
        type = req.saveType;
        if (romType != SaveType::Auto && romType != type)
            AddIssue(res.issues, Severity::Warning, Msg::WarnRomTypeConflict,
                     {SaveTypeToName(type), SaveTypeToName(romType)});
    } else if (extType != SaveType::Auto) {
        type = extType;
        // A MesenCE EEPROM file may belong to a Datach (mapper 157) game that uses
        // BOTH chips; merge the sibling file when it exists (or the ROM says so).
        const bool wantDatach = (romType == SaveType::EepromDatach) ||
                                (!req.rom.valid && siblingFound);
        if (wantDatach && (type == SaveType::Eeprom24C01 || type == SaveType::Eeprom24C02)) {
            type = SaveType::EepromDatach;
        } else if (romType != SaveType::Auto && romType != type &&
                   romType != SaveType::EepromDatach) {
            AddIssue(res.issues, Severity::Warning, Msg::WarnRomTypeConflict,
                     {SaveTypeToName(type), SaveTypeToName(romType)});
        }
    } else if (req.rom.valid && req.rom.battery && romType != SaveType::Auto) {
        // MesenCE also writes a mostly-unused 8 KB .sav for EEPROM boards — if the
        // user picked that one, tell them where the real data lives.
        const bool mesenGarbageSram =
            (romType == SaveType::Eeprom24C01 || romType == SaveType::Eeprom24C02 ||
             romType == SaveType::EepromDatach) &&
            sizeType == SaveType::SRAM && size >= 512;
        if (mesenGarbageSram) {
            type = SaveType::SRAM;
            AddIssue(res.issues, Severity::Warning, Msg::WarnMesenSramUnused);
        } else {
            type = romType;
            if (sizeType != romType)
                AddIssue(res.issues, Severity::Warning, Msg::WarnRomTypeConflict,
                         {SaveTypeToName(sizeType), SaveTypeToName(romType)});
        }
    } else {
        type = sizeType;
    }

    AddIssue(res.issues, Severity::Info, Msg::DetectedSource,
             {EmuToName(srcFmt), SaveTypeToName(type), Num(size)});
    if (req.rom.valid) {
        AddIssue(res.issues, Severity::Info, Msg::RomInfo,
                 {Num(static_cast<size_t>(req.rom.mapper)),
                  req.rom.isNes20 ? L"NES 2.0" : L"iNES",
                  req.rom.battery ? L"yes" : L"no"});
    }

    // ---- build the canonical payload --------------------------------------------------
    std::vector<uint8_t> canonical;

    switch (type) {
        case SaveType::SRAM: {
            canonical = src;  // copy as-is; SRAM sizes legitimately vary
            break;
        }
        case SaveType::Eeprom24C01: {
            canonical = NormalizeChip(src, kSize24C01, res.issues);
            break;
        }
        case SaveType::Eeprom24C02: {
            canonical = NormalizeChip(src, kSize24C02, res.issues);
            break;
        }
        case SaveType::EepromDatach: {
            if (size == kSizeDatach) {
                canonical = src;  // Nestopia layout already: [24C02 | 24C01]
            } else {
                std::vector<uint8_t> part24c02(kSize24C02, 0xFF);
                std::vector<uint8_t> part24c01(kSize24C01, 0xFF);
                if (size >= kSize24C02) {
                    // the picked file is (or starts with) the 24C02 part
                    part24c02.assign(src.begin(), src.begin() + kSize24C02);
                } else {
                    // the picked file is the external 24C01 part
                    part24c01.assign(src.begin(), src.end());
                }
                // try to fill the other half from the sibling file
                if (siblingFound) {
                    std::vector<uint8_t> sib;
                    env.ReadFileBytes(siblingPath, sib);
                    if (siblingPath.find(L".eeprom256") != std::wstring::npos) {
                        part24c02 = NormalizeChip(sib, kSize24C02, res.issues);
                    } else {
                        part24c01 = NormalizeChip(sib, kSize24C01, res.issues);
                    }
                    AddIssue(res.issues, Severity::Info, Msg::SiblingMerged,
                             {GetName(siblingPath)});
                } else if (size >= kSize24C02) {
                    AddIssue(res.issues, Severity::Warning, Msg::WarnNoSibling,
                             {base + L".eeprom128"});
                } else {
                    AddIssue(res.issues, Severity::Warning, Msg::WarnNoSibling,
                             {base + L".eeprom256"});
                }
                canonical = part24c02;
                canonical.insert(canonical.end(), part24c01.begin(), part24c01.end());
            }
            break;
        }
        default: {
            AddIssue(res.issues, Severity::Error, Msg::ErrEmptyFile);
            return res;
        }
    }

    // ---- emit outputs ------------------------------------------------------------------
    std::wstring outDir = req.outputDir.empty() ? GetDir(req.sourcePath) : req.outputDir;
    if (!outDir.empty() && outDir.back() != L'\\' && outDir.back() != L'/')
        outDir += L'\\';  // tolerate a manually typed folder without a trailing slash
    const std::wstring outBase = outDir.empty() ? base : (outDir + base);
    auto addOut = [&](const wchar_t* extW, std::vector<uint8_t> bytes) {
        OutputFile o{outBase + extW, std::move(bytes)};
        res.outputs.push_back(std::move(o));
    };

    switch (type) {
        case SaveType::SRAM: {
            // Nintendulator is the only target that expects an exact size (it
            // prints a "file length mismatch" warning otherwise); normalize when
            // the ROM header tells us the size. Nestopia and MesenCE load
            // min(fileSize, expected) silently.
            std::vector<uint8_t> bytes = canonical;
            if (req.target == Emu::Nintendulator && req.rom.valid && req.rom.battery) {
                size_t expected = 0;
                if (req.rom.isNes20)
                    expected = req.rom.batteryRamSize;  // 0 = unspecified
                else
                    expected = 8192;  // Nintendulator d_iNES.cpp default
                if (expected > 0 && bytes.size() != expected) {
                    if (bytes.size() < expected) {
                        bytes.resize(expected, 0x00);
                    } else {
                        bytes.resize(expected);
                    }
                    AddIssue(res.issues, Severity::Info, Msg::Normalized,
                             {Num(canonical.size()), Num(expected)});
                }
            }
            addOut(L".sav", std::move(bytes));
            break;
        }
        case SaveType::Eeprom24C01: {
            if (req.target == Emu::MesenCE)
                addOut(L".eeprom128", canonical);
            else
                addOut(L".sav", canonical);
            break;
        }
        case SaveType::Eeprom24C02: {
            if (req.target == Emu::MesenCE)
                addOut(L".eeprom256", canonical);
            else
                addOut(L".sav", canonical);
            break;
        }
        case SaveType::EepromDatach: {
            if (req.target == Emu::Nintendulator) {
                AddIssue(res.issues, Severity::Error, Msg::ErrDatachUnsupported);
                return res;  // mapper 157 is not supported by Nintendulator at all
            }
            if (req.target == Emu::MesenCE) {
                std::vector<uint8_t> p24c02(canonical.begin(), canonical.begin() + kSize24C02);
                std::vector<uint8_t> p24c01(canonical.begin() + kSize24C02, canonical.end());
                addOut(L".eeprom256", std::move(p24c02));
                addOut(L".eeprom128", std::move(p24c01));
            } else {
                addOut(L".sav", canonical);  // 384 bytes, Nestopia block order
            }
            break;
        }
        default:
            break;
    }

    res.ok = !res.HasErrors();
    return res;
}

// ---------------------------------------------------------------------------------------
// Writing the outputs (the UI asks the user about overwrites before calling this)
// ---------------------------------------------------------------------------------------
bool WriteOutputs(ConvertResult& result, const ConvertRequest& req, IEnv& env) {
    bool allOk = true;

    for (const auto& out : result.outputs) {
        const bool sameAsSource = SamePath(out.path, req.sourcePath);
        const bool identical =
            sameAsSource && out.bytes.size() == req.sourceBytes.size() &&
            std::equal(out.bytes.begin(), out.bytes.end(), req.sourceBytes.begin());

        if (identical) {
            AddIssue(result.issues, Severity::Info, Msg::NoAction, {GetName(out.path)});
            continue;  // nothing to do — source is already in the target format
        }

        if (env.FileExists(out.path)) {
            if (req.backup) {
                // Never overwrite an older .bak silently. Keep the familiar .bak
                // name for the first backup and use numbered siblings thereafter.
                std::wstring bak = out.path + L".bak";
                if (env.FileExists(bak)) {
                    size_t suffix = 1;
                    do {
                        bak = out.path + L".bak." + Num(suffix++);
                    } while (env.FileExists(bak) && suffix <= 1000000);
                    if (env.FileExists(bak)) {
                        AddIssue(result.issues, Severity::Error, Msg::ErrWriteFile,
                                 {GetName(bak)});
                        allOk = false;
                        continue;
                    }
                }

                std::vector<uint8_t> orig;
                if (!env.ReadFileBytes(out.path, orig)) {
                    AddIssue(result.issues, Severity::Error, Msg::ErrReadFile,
                             {GetName(out.path)});
                    allOk = false;
                    continue; // Do not replace a file if its backup cannot be read.
                }
                if (!env.WriteFileBytes(bak, orig.data(), orig.size())) {
                    AddIssue(result.issues, Severity::Error, Msg::ErrWriteFile,
                             {GetName(bak)});
                    allOk = false;
                    continue; // Preserve the original unless its backup is secured.
                }
                AddIssue(result.issues, Severity::Info, Msg::BackupCreated,
                         {GetName(bak)});
            }
            AddIssue(result.issues, Severity::Warning, Msg::WarnOverwrite,
                     {GetName(out.path)});
        }

        if (env.WriteFileBytes(out.path, out.bytes.data(), out.bytes.size())) {
            AddIssue(result.issues, Severity::Info, Msg::OutputWritten,
                     {GetName(out.path), Num(out.bytes.size())});
        } else {
            AddIssue(result.issues, Severity::Error, Msg::ErrWriteFile, {GetName(out.path)});
            allOk = false;
        }
    }

    result.ok = allOk && !result.HasErrors();
    return result.ok;
}

}  // namespace rsc
