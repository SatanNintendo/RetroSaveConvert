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
constexpr size_t kFceuxDatachSize = 512; // [24C01 region 256] + [24C02 256]
constexpr size_t kPunesLegacyNvramSize = 8192;
constexpr size_t kMinFileSize = 16;  // smaller than this = definitely not a save

bool HasPunesHeader(const std::vector<uint8_t>& bytes) {
    static constexpr uint8_t kHeader[8] = {'F', 'H', 'p', 'u', 'N', 'E', 'S', 0};
    return bytes.size() >= sizeof(kHeader) &&
           std::equal(std::begin(kHeader), std::end(kHeader), bytes.begin());
}

// puNES writes the PRG-NVRAM area first, followed by CHR-NVRAM and any
// mapper-specific battery payload. For legacy iNES battery cartridges, puNES
// allocates an 8 KiB PRG-NVRAM region when the header has no RAM size field.
size_t PunesPrgNvramPrefix(const RomInfo& rom) {
    if (!rom.valid) return kPunesLegacyNvramSize;
    if (!rom.isNes20) return rom.battery ? kPunesLegacyNvramSize : 0;
    return rom.batteryRamSize;
}

size_t PunesChrNvramSize(const RomInfo& rom) {
    return (rom.valid && rom.isNes20) ? rom.chrBatteryRamSize : 0;
}

std::vector<uint8_t> PunesChipPayload(const std::vector<uint8_t>& chip,
                                      const RomInfo& rom) {
    size_t prefix = PunesPrgNvramPrefix(rom);
    // With no ROM supplied, support the common legacy iNES layout. When a
    // NES 2.0 ROM is supplied, Convert() validates the declared PRG-NVRAM size
    // before this helper is called.
    if (prefix < chip.size()) prefix = chip.size();
    std::vector<uint8_t> out(prefix + PunesChrNvramSize(rom), 0x00);
    std::copy(chip.begin(), chip.end(), out.begin());
    return out;
}

bool IsAres24C02Path(const std::wstring& path) {
    return ToLower(GetName(path)) == L"save.eeprom";
}

bool Is24C02Path(const std::wstring& path) {
    const std::wstring lower = ToLower(path);
    return lower.find(L".eeprom256") != std::wstring::npos || IsAres24C02Path(path);
}

}  // namespace

// ---------------------------------------------------------------------------------------
// iNES / NES 2.0 header
// ---------------------------------------------------------------------------------------
bool RomInfo::Parse(const uint8_t* d, size_t n) {
    *this = RomInfo{};
    if (d == nullptr || n < 16) return false;
    if (d[0] != 'N' || d[1] != 'E' || d[2] != 'S' || d[3] != 0x1A) return false;

    isNes20 = ((d[7] & 0x0C) == 0x08);
    mapper = (d[6] >> 4) | (((d[7] & 0xF0) >> 4) << 4);
    if (isNes20) {
        // NES 2.0 byte 8 carries the mapper's upper bits and submapper.
        mapper |= static_cast<int>(d[8] & 0x0F) << 8;
        subMapper = (d[8] >> 4) & 0x0F;
        const uint8_t prgNvramNibble = (d[10] >> 4) & 0x0F;
        const uint8_t chrNvramNibble = (d[11] >> 4) & 0x0F;
        if (prgNvramNibble > 0 && prgNvramNibble < 0x0F)
            batteryRamSize = 64u << prgNvramNibble;
        if (chrNvramNibble > 0 && chrNvramNibble < 0x0F)
            chrBatteryRamSize = 64u << chrNvramNibble;
    }
    battery = (d[6] & 0x02) != 0;

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
        case Emu::FCEUX:        return L"FCEUX";
        case Emu::PuNES:        return L"puNES";
        case Emu::Ares:         return L"ares (NES)";
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

    std::vector<uint8_t> src = req.sourceBytes;
    const std::wstring ext = GetExtLower(req.sourcePath);
    const std::wstring sourceName = ToLower(GetName(req.sourcePath));

    // Resolve the source format before interpreting the file. ares uses fixed names
    // inside a per-game .pak directory; puNES normally uses a .prb file.
    Emu srcFmt = req.sourceFormat;
    if (srcFmt == Emu::Auto) {
        if (ext == L".eeprom128" || ext == L".eeprom256") {
            srcFmt = Emu::MesenCE;
        } else if (ext == L".prb") {
            srcFmt = Emu::PuNES;
        } else if (sourceName == L"save.ram" || sourceName == L"save.eeprom" ||
                   sourceName == L"external.eeprom") {
            srcFmt = Emu::Ares;
        } else {
            srcFmt = Emu::Raw;
        }
    }

    // puNES used to recognize an 8-byte signature, but its current save writer
    // does not emit it. Accept either variant and never pass the legacy prefix on.
    if (srcFmt == Emu::PuNES && HasPunesHeader(src))
        src.erase(src.begin(), src.begin() + 8);

    const size_t size = src.size();
    if (size == 0) {
        AddIssue(res.issues, Severity::Error, Msg::ErrEmptyFile);
        return res;
    }
    if (size < kMinFileSize) {
        AddIssue(res.issues, Severity::Error, Msg::ErrTooSmall, {Num(size)});
        return res;
    }

    const bool aresRamFile = srcFmt == Emu::Ares && sourceName == L"save.ram";
    const bool aresEepromFile = srcFmt == Emu::Ares && sourceName == L"save.eeprom";
    const bool aresExternalEepromFile = srcFmt == Emu::Ares && sourceName == L"external.eeprom";

    // ---- resolve the save type -------------------------------------------------------
    const bool manual = (req.saveType != SaveType::Auto);
    const SaveType romType = ExpectedSaveTypeForRom(req.rom);

    SaveType extType = SaveType::Auto;
    if (ext == L".eeprom128") extType = SaveType::Eeprom24C01;
    else if (ext == L".eeprom256") extType = SaveType::Eeprom24C02;
    else if (aresExternalEepromFile) extType = SaveType::Eeprom24C01;
    else if (aresEepromFile)
        extType = (size <= kSize24C01) ? SaveType::Eeprom24C01 : SaveType::Eeprom24C02;

    SaveType sizeType = SaveType::SRAM;
    if (size == kSize24C01) sizeType = SaveType::Eeprom24C01;
    else if (size == kSize24C02) sizeType = SaveType::Eeprom24C02;
    else if (size == kSizeDatach) sizeType = SaveType::EepromDatach;

    const std::wstring dir = GetDir(req.sourcePath);
    std::wstring base = GetBaseNoExt(req.sourcePath);
    // ares stores saves under fixed filenames inside <game>.pak. When converting
    // away from ares, name the ordinary output after the package/game, not "save".
    if (srcFmt == Emu::Ares &&
        (sourceName == L"save.ram" || sourceName == L"save.eeprom" ||
         sourceName == L"external.eeprom")) {
        std::wstring packageDir = dir;
        while (!packageDir.empty() &&
               (packageDir.back() == L'\\' || packageDir.back() == L'/'))
            packageDir.pop_back();
        const std::wstring packageBase = GetBaseNoExt(packageDir);
        if (!packageBase.empty()) base = packageBase;
    }
    auto siblingOf = [&](const wchar_t* e) -> std::wstring { return dir + base + e; };
    std::wstring siblingPath;
    bool siblingFound = false;

    if (srcFmt == Emu::Ares && (aresEepromFile || aresExternalEepromFile)) {
        siblingPath = dir + (aresEepromFile ? L"external.eeprom" : L"save.eeprom");
        std::vector<uint8_t> tmp;
        siblingFound = env.ReadFileBytes(siblingPath, tmp);
    } else if (extType == SaveType::Eeprom24C02 || extType == SaveType::Eeprom24C01) {
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
    } else if (aresRamFile) {
        // ares' save.ram must not be mistaken for EEPROM purely because the
        // board's ROM header uses mapper 16/157/159.
        type = SaveType::SRAM;
        if (romType == SaveType::Eeprom24C01 || romType == SaveType::Eeprom24C02 ||
            romType == SaveType::EepromDatach) {
            AddIssue(res.issues, Severity::Warning, Msg::WarnAresRamUnused);
        }
    } else if (extType != SaveType::Auto) {
        type = extType;
        const bool wantDatach = (romType == SaveType::EepromDatach) ||
                                (siblingFound && (!req.rom.valid || srcFmt == Emu::Ares));
        if (wantDatach) {
            type = SaveType::EepromDatach;
        } else if (romType != SaveType::Auto && romType != type &&
                   romType != SaveType::EepromDatach) {
            AddIssue(res.issues, Severity::Warning, Msg::WarnRomTypeConflict,
                     {SaveTypeToName(type), SaveTypeToName(romType)});
        }
    } else if (req.rom.valid && req.rom.battery && romType != SaveType::Auto) {
        const bool mesenGarbageSram = srcFmt != Emu::FCEUX && srcFmt != Emu::PuNES &&
            (romType == SaveType::Eeprom24C01 || romType == SaveType::Eeprom24C02 ||
             romType == SaveType::EepromDatach) &&
            sizeType == SaveType::SRAM && size >= 512;
        const bool knownPunesChipContainer = srcFmt == Emu::PuNES &&
            (romType == SaveType::Eeprom24C01 || romType == SaveType::Eeprom24C02 ||
             romType == SaveType::EepromDatach);
        const bool fceuxDatachLayout = srcFmt == Emu::FCEUX &&
            romType == SaveType::EepromDatach && size == kFceuxDatachSize;
        if (mesenGarbageSram) {
            type = SaveType::SRAM;
            AddIssue(res.issues, Severity::Warning, Msg::WarnMesenSramUnused);
        } else {
            type = romType;
            if (sizeType != romType && !knownPunesChipContainer && !fceuxDatachLayout)
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
    // Canonical Datach layout throughout this converter is [24C02 256 B | 24C01 128 B].
    std::vector<uint8_t> canonical;
    switch (type) {
        case SaveType::SRAM: {
            canonical = src;
            if (srcFmt == Emu::PuNES) {
                const size_t prefix = PunesPrgNvramPrefix(req.rom);
                if (prefix > 0 && size > prefix && req.target != Emu::PuNES) {
                    // puNES stores PRG NVRAM, CHR NVRAM, and mapper-specific battery
                    // payloads in one file. We cannot safely discard the latter blocks.
                    AddIssue(res.issues, Severity::Error, Msg::ErrPunesCompositeNvram,
                             {Num(prefix), Num(size - prefix)});
                    return res;
                }
                if (prefix > 0 && size >= prefix && req.target != Emu::PuNES)
                    canonical.assign(src.begin(), src.begin() + prefix);
            }
            break;
        }
        case SaveType::Eeprom24C01: {
            if (srcFmt == Emu::PuNES && req.rom.valid && romType == SaveType::Eeprom24C01 &&
                size >= kSize24C01) {
                canonical.assign(src.begin(), src.begin() + kSize24C01);
            } else {
                canonical = NormalizeChip(src, kSize24C01, res.issues);
            }
            break;
        }
        case SaveType::Eeprom24C02: {
            if (srcFmt == Emu::PuNES && req.rom.valid && romType == SaveType::Eeprom24C02 &&
                size >= kSize24C02) {
                canonical.assign(src.begin(), src.begin() + kSize24C02);
            } else {
                canonical = NormalizeChip(src, kSize24C02, res.issues);
            }
            break;
        }
        case SaveType::EepromDatach: {
            if (srcFmt == Emu::FCEUX && req.rom.valid && romType == SaveType::EepromDatach &&
                size == kFceuxDatachSize) {
                // FCEUX mapper 157 writes a 512-byte buffer: the 24C01 is at 0..127,
                // 128..255 is padding/unused, and the 24C02 is at 256..511.
                canonical.assign(src.begin() + 256, src.begin() + 512);
                canonical.insert(canonical.end(), src.begin(), src.begin() + kSize24C01);
            } else if (srcFmt == Emu::PuNES && type == SaveType::EepromDatach) {
                const size_t prefix = PunesPrgNvramPrefix(req.rom);
                const size_t chrNvram = PunesChrNvramSize(req.rom);
                const size_t mapperDataOffset = prefix + chrNvram;
                if (mapperDataOffset > 0 && size >= mapperDataOffset + kSize24C02) {
                    // puNES writes PRG NVRAM, then CHR NVRAM, then the internal
                    // 24C02. The external 24C01 overlaps the beginning of PRG NVRAM.
                    if (req.target != Emu::PuNES && chrNvram > 0) {
                        AddIssue(res.issues, Severity::Error, Msg::ErrPunesCompositeNvram,
                                 {Num(prefix), Num(size - prefix)});
                        return res;
                    }
                    if (req.target != Emu::PuNES && size > mapperDataOffset + kSize24C02) {
                        AddIssue(res.issues, Severity::Error, Msg::ErrPunesCompositeNvram,
                                 {Num(mapperDataOffset + kSize24C02),
                                  Num(size - mapperDataOffset - kSize24C02)});
                        return res;
                    }
                    canonical.assign(src.begin() + mapperDataOffset,
                                     src.begin() + mapperDataOffset + kSize24C02);
                    std::vector<uint8_t> external(kSize24C01, 0xFF);
                    const size_t availableExternal = std::min(prefix, kSize24C01);
                    std::copy(src.begin(), src.begin() + availableExternal, external.begin());
                    canonical.insert(canonical.end(), external.begin(), external.end());
                    if (availableExternal < kSize24C01)
                        AddIssue(res.issues, Severity::Warning, Msg::WarnNoSibling,
                                 {L"puNES external 24C01 EEPROM (partial PRG-NVRAM area)"});
                } else if (prefix == 0 && chrNvram + kSize24C02 == size) {
                    // NES 2.0 may declare CHR NVRAM but no PRG NVRAM. puNES
                    // writes CHR NVRAM before the mapper's 24C02 battery payload.
                    canonical.assign(src.begin() + chrNvram, src.end());
                    canonical.resize(kSizeDatach, 0xFF);
                    AddIssue(res.issues, Severity::Warning, Msg::WarnNoSibling,
                             {L"puNES external 24C01 EEPROM"});
                } else if (size == kSizeDatach) {
                    canonical = src;
                } else {
                    std::vector<uint8_t> part24c02(kSize24C02, 0xFF);
                    std::vector<uint8_t> part24c01(kSize24C01, 0xFF);
                    if (size >= kSize24C02)
                        part24c02.assign(src.begin(), src.begin() + kSize24C02);
                    else
                        part24c01.assign(src.begin(), src.end());
                    if (siblingFound) {
                        std::vector<uint8_t> sib;
                        env.ReadFileBytes(siblingPath, sib);
                        if (Is24C02Path(siblingPath))
                            part24c02 = NormalizeChip(sib, kSize24C02, res.issues);
                        else
                            part24c01 = NormalizeChip(sib, kSize24C01, res.issues);
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
            } else if (srcFmt == Emu::FCEUX && size == kFceuxDatachSize) {
                // Explicit FCEUX selection without a ROM header: this is the known
                // 512-byte Datach representation, unless the user overrides type.
                canonical.assign(src.begin() + 256, src.begin() + 512);
                canonical.insert(canonical.end(), src.begin(), src.begin() + kSize24C01);
            } else if (size == kSizeDatach) {
                canonical = src;  // Nestopia layout: [24C02 | 24C01]
            } else {
                std::vector<uint8_t> part24c02(kSize24C02, 0xFF);
                std::vector<uint8_t> part24c01(kSize24C01, 0xFF);
                if (size >= kSize24C02)
                    part24c02.assign(src.begin(), src.begin() + kSize24C02);
                else
                    part24c01.assign(src.begin(), src.end());
                if (siblingFound) {
                    std::vector<uint8_t> sib;
                    env.ReadFileBytes(siblingPath, sib);
                    if (Is24C02Path(siblingPath))
                        part24c02 = NormalizeChip(sib, kSize24C02, res.issues);
                    else
                        part24c01 = NormalizeChip(sib, kSize24C01, res.issues);
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
    std::wstring outDir = req.outputDir;
    if (outDir.empty()) {
        outDir = GetDir(req.sourcePath);
        if (srcFmt == Emu::Ares && req.target != Emu::Ares &&
            (sourceName == L"save.ram" || sourceName == L"save.eeprom" ||
             sourceName == L"external.eeprom")) {
            // A normal emulator's save belongs beside the ares package, not
            // inside the package where ares expects only its fixed save names.
            while (!outDir.empty() && (outDir.back() == L'\\' || outDir.back() == L'/'))
                outDir.pop_back();
            outDir = GetDir(outDir);
        }
    }
    if (!outDir.empty() && outDir.back() != L'\\' && outDir.back() != L'/')
        outDir += L'\\';
    const std::wstring outBase = outDir.empty() ? base : (outDir + base);
    auto addOut = [&](const wchar_t* extW, std::vector<uint8_t> bytes) {
        res.outputs.push_back(OutputFile{outBase + extW, std::move(bytes)});
    };
    auto addNamedOut = [&](const wchar_t* name, std::vector<uint8_t> bytes) {
        res.outputs.push_back(OutputFile{outDir + name, std::move(bytes)});
    };
    auto makePunesChip = [&](const std::vector<uint8_t>& chip) {
        return PunesChipPayload(chip, req.rom);
    };

    switch (type) {
        case SaveType::SRAM: {
            std::vector<uint8_t> bytes = canonical;
            if (req.target == Emu::Nintendulator && req.rom.valid && req.rom.battery) {
                size_t expected = req.rom.isNes20 ? req.rom.batteryRamSize : 8192;
                if (expected > 0 && bytes.size() != expected) {
                    bytes.resize(expected, 0x00);
                    AddIssue(res.issues, Severity::Info, Msg::Normalized,
                             {Num(canonical.size()), Num(expected)});
                }
            } else if (req.target == Emu::PuNES) {
                const size_t prefix = PunesPrgNvramPrefix(req.rom);
                const size_t chrNvram = PunesChrNvramSize(req.rom);
                if (srcFmt == Emu::PuNES && prefix > 0 && bytes.size() > prefix) {
                    // This is already puNES' composite payload. Keep every byte.
                } else {
                    if (req.rom.valid && req.rom.isNes20 && prefix == 0) {
                        AddIssue(res.issues, Severity::Error, Msg::ErrPunesNvramTooSmall,
                                 {Num(bytes.size()), Num(prefix)});
                        return res;
                    }
                    if (prefix > 0 && bytes.size() > prefix) {
                        AddIssue(res.issues, Severity::Error, Msg::ErrPunesCompositeNvram,
                                 {Num(prefix), Num(bytes.size() - prefix)});
                        return res;
                    }
                    const size_t before = bytes.size();
                    if (prefix > 0) bytes.resize(prefix, 0x00);
                    bytes.resize(prefix + chrNvram, 0x00);
                    if (bytes.size() != before)
                        AddIssue(res.issues, Severity::Info, Msg::Normalized,
                                 {Num(before), Num(bytes.size())});
                }
            }
            if (req.target == Emu::Ares)
                addNamedOut(L"save.ram", std::move(bytes));
            else if (req.target == Emu::PuNES)
                addOut(L".prb", std::move(bytes));
            else
                addOut(L".sav", std::move(bytes));
            break;
        }
        case SaveType::Eeprom24C01: {
            if (req.target == Emu::MesenCE)
                addOut(L".eeprom128", canonical);
            else if (req.target == Emu::Ares)
                addNamedOut(L"save.eeprom", canonical);
            else if (req.target == Emu::PuNES) {
                const size_t available = PunesPrgNvramPrefix(req.rom);
                if (req.rom.valid && req.rom.isNes20 && available < canonical.size()) {
                    AddIssue(res.issues, Severity::Error, Msg::ErrPunesNvramTooSmall,
                             {Num(canonical.size()), Num(available)});
                    return res;
                }
                addOut(L".prb", makePunesChip(canonical));
            }
            else
                addOut(L".sav", canonical);
            break;
        }
        case SaveType::Eeprom24C02: {
            if (req.target == Emu::MesenCE)
                addOut(L".eeprom256", canonical);
            else if (req.target == Emu::Ares)
                addNamedOut(L"save.eeprom", canonical);
            else if (req.target == Emu::PuNES) {
                const size_t available = PunesPrgNvramPrefix(req.rom);
                if (req.rom.valid && req.rom.isNes20 && available < canonical.size()) {
                    AddIssue(res.issues, Severity::Error, Msg::ErrPunesNvramTooSmall,
                             {Num(canonical.size()), Num(available)});
                    return res;
                }
                addOut(L".prb", makePunesChip(canonical));
            }
            else
                addOut(L".sav", canonical);
            break;
        }
        case SaveType::EepromDatach: {
            if (req.target == Emu::Nintendulator) {
                AddIssue(res.issues, Severity::Error, Msg::ErrDatachUnsupported);
                return res;
            }
            const std::vector<uint8_t> p24c02(canonical.begin(), canonical.begin() + kSize24C02);
            const std::vector<uint8_t> p24c01(canonical.begin() + kSize24C02,
                                               canonical.begin() + kSizeDatach);
            if (req.target == Emu::MesenCE) {
                addOut(L".eeprom256", p24c02);
                addOut(L".eeprom128", p24c01);
            } else if (req.target == Emu::Ares) {
                addNamedOut(L"save.eeprom", p24c02);
                addNamedOut(L"external.eeprom", p24c01);
            } else if (req.target == Emu::PuNES) {
                if (srcFmt == Emu::PuNES) {
                    // Keep the complete original NVRAM payload (including any CHR
                    // NVRAM) instead of reconstructing a partial file.
                    addOut(L".prb", src);
                    break;
                }
                const size_t prefix = PunesPrgNvramPrefix(req.rom);
                if (prefix < kSize24C01) {
                    AddIssue(res.issues, Severity::Error, Msg::ErrDatachUnsupportedByTarget,
                             {EmuToName(req.target)});
                    return res;
                }
                const size_t chrNvram = PunesChrNvramSize(req.rom);
                std::vector<uint8_t> bytes(prefix + chrNvram, 0x00);
                std::copy(p24c01.begin(), p24c01.end(), bytes.begin());
                bytes.insert(bytes.end(), p24c02.begin(), p24c02.end());
                addOut(L".prb", std::move(bytes));
            } else if (req.target == Emu::FCEUX) {
                std::vector<uint8_t> bytes(kFceuxDatachSize, 0x00);
                std::copy(p24c01.begin(), p24c01.end(), bytes.begin());
                std::copy(p24c02.begin(), p24c02.end(), bytes.begin() + 256);
                addOut(L".sav", std::move(bytes));
            } else {
                addOut(L".sav", canonical);
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
