// ========================================================================================
// RetroSaveConverter — core engine unit tests (portable, run on any platform)
// Build:  g++ -std=c++17 -O1 -Wall -Wextra -o test_converter tests/test_converter.cpp src/converter.cpp
// ========================================================================================
#include "../src/converter.h"
#include "../src/strings.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace rsc;

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        ++g_checks;                                                          \
        if (!(cond)) {                                                       \
            ++g_failures;                                                    \
            std::printf("  FAIL line %d: %s\n", __LINE__, #cond);            \
        }                                                                    \
    } while (0)

#define CHECK_EQ(a, b)                                                       \
    do {                                                                     \
        ++g_checks;                                                          \
        if (!((a) == (b))) {                                                 \
            ++g_failures;                                                    \
            std::printf("  FAIL line %d: %s == %s\n", __LINE__, #a, #b);     \
        }                                                                    \
    } while (0)

// ---------------------------------------------------------------------------------------
// In-memory filesystem
// ---------------------------------------------------------------------------------------
struct RamEnv : IEnv {
    std::map<std::wstring, std::vector<uint8_t>> files;

    bool ReadFileBytes(const std::wstring& path, std::vector<uint8_t>& out) override {
        auto it = files.find(path);
        if (it == files.end()) return false;
        out = it->second;
        return true;
    }
    bool WriteFileBytes(const std::wstring& path, const uint8_t* data, size_t size) override {
        if (size == 0) files[path].clear();
        else files[path] = std::vector<uint8_t>(data, data + size);
        return true;
    }
    bool FileExists(const std::wstring& path) override {
        return files.find(path) != files.end();
    }
};

struct BackupWriteFailEnv : RamEnv {
    bool WriteFileBytes(const std::wstring& path, const uint8_t* data, size_t size) override {
        if (path.find(L".bak") != std::wstring::npos) return false;
        return RamEnv::WriteFileBytes(path, data, size);
    }
};

static std::vector<uint8_t> MakeBytes(size_t n, uint8_t seed) {
    std::vector<uint8_t> v(n);
    for (size_t i = 0; i < n; ++i) v[i] = static_cast<uint8_t>(seed + i * 7);
    return v;
}

// build a minimal iNES header; NES2.0 flag, mapper, battery, nvram nibble
static std::vector<uint8_t> MakeNes(int mapper, bool battery, bool nes20 = false,
                                    int subMapper = 0, int nvramNibble = 0) {
    std::vector<uint8_t> h(16, 0);
    h[0] = 'N'; h[1] = 'E'; h[2] = 'S'; h[3] = 0x1A;
    h[4] = 2;   // 32 KB PRG
    h[5] = 1;   // 8 KB CHR
    h[6] = static_cast<uint8_t>(((mapper & 0x0F) << 4) | (battery ? 0x02 : 0) | 0x01);
    h[7] = static_cast<uint8_t>((((mapper >> 4) & 0x0F) << 4) | (nes20 ? 0x08 : 0x00));
    if (nes20) {
        h[8] = static_cast<uint8_t>(((subMapper & 0x0F) << 4) & 0xF0);
        h[10] = static_cast<uint8_t>((nvramNibble << 4) & 0xF0);
    }
    return h;
}

static bool HasMsg(const ConvertResult& r, Msg m) {
    for (const auto& i : r.issues)
        if (i.code == m) return true;
    return false;
}

static ConvertRequest BasicReq(const wchar_t* srcPath, const std::vector<uint8_t>& bytes) {
    ConvertRequest q;
    q.sourcePath = srcPath;
    q.sourceBytes = bytes;
    return q;
}

// ---------------------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------------------
static void TestHeaderParsing() {
    std::printf("[test] iNES header parsing\n");
    RomInfo r = ParseNesHeader(MakeNes(1, true).data(), 16);
    CHECK(r.valid);
    CHECK_EQ(r.mapper, 1);
    CHECK(r.battery);
    CHECK(!r.isNes20);

    r = ParseNesHeader(MakeNes(159, true, true, 3, 7).data(), 16);
    CHECK(r.valid);
    CHECK_EQ(r.mapper, 159);
    CHECK(r.isNes20);
    CHECK_EQ(r.subMapper, 3);
    CHECK_EQ(r.batteryRamSize, 64u << 7);  // 8192
    CHECK_EQ(r.chrBatteryRamSize, 0u);

    std::vector<uint8_t> withChrNvram = MakeNes(16, true, true, 5, 7);
    withChrNvram[11] = 0x30;  // 512 bytes of CHR NVRAM
    r = ParseNesHeader(withChrNvram.data(), withChrNvram.size());
    CHECK_EQ(r.subMapper, 5);
    CHECK_EQ(r.chrBatteryRamSize, 512u);

    std::vector<uint8_t> junk = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    CHECK(!ParseNesHeader(junk.data(), junk.size()).valid);
    CHECK(!ParseNesHeader(nullptr, 16).valid);
}

static void TestSramRoundtrip() {
    std::printf("[test] SRAM (Zelda-like) roundtrip between all emulators\n");
    RamEnv env;
    std::vector<uint8_t> zelda = MakeBytes(8192, 0x11);

    // MesenCE -> Nestopia
    ConvertRequest q = BasicReq(L"C:/saves/Zelda.sav", zelda);
    q.sourceFormat = Emu::MesenCE;
    q.target = Emu::Nestopia;
    ConvertResult r = Convert(q, env);
    CHECK(r.ok);
    CHECK_EQ(r.outputs.size(), (size_t)1);
    CHECK_EQ(r.outputs[0].path, L"C:/saves/Zelda.sav");
    CHECK(r.outputs[0].bytes == zelda);

    // Nestopia -> Nintendulator (no ROM: passthrough size)
    q = BasicReq(L"C:/saves/Zelda.sav", zelda);
    q.sourceFormat = Emu::Nestopia;
    q.target = Emu::Nintendulator;
    r = Convert(q, env);
    CHECK(r.ok);
    CHECK(r.outputs[0].bytes == zelda);

    // Nintendulator -> MesenCE with ROM: plain iNES mapper 1 battery -> expected 8192
    q = BasicReq(L"C:/saves/Zelda.sav", zelda);
    q.rom = ParseNesHeader(MakeNes(1, true).data(), 16);
    q.target = Emu::MesenCE;
    r = Convert(q, env);
    CHECK(r.ok);
    CHECK(r.outputs[0].bytes == zelda);
    CHECK(!HasMsg(r, Msg::Normalized));  // already the right size
}

static void TestNintendulatorNormalization() {
    std::printf("[test] Nintendulator SRAM size normalization via NES 2.0 header\n");
    RamEnv env;
    std::vector<uint8_t> half = MakeBytes(4096, 0x22);  // some emu wrote 4 KB

    ConvertRequest q = BasicReq(L"C:/saves/FF3.sav", half);
    q.target = Emu::Nintendulator;
    q.rom = ParseNesHeader(MakeNes(4, true, true, 0, 7).data(), 16);  // 8192 NVRAM
    ConvertResult r = Convert(q, env);
    CHECK(r.ok);
    CHECK_EQ(r.outputs[0].bytes.size(), (size_t)8192);
    // first 4 KB preserved, rest zero-padded
    CHECK(std::memcmp(r.outputs[0].bytes.data(), half.data(), 4096) == 0);
    bool tailZero = true;
    for (size_t i = 4096; i < 8192; ++i)
        if (r.outputs[0].bytes[i] != 0x00) tailZero = false;
    CHECK(tailZero);
    CHECK(HasMsg(r, Msg::Normalized));

    // same file to Nestopia: NOT normalized (Nestopia tolerates)
    q.target = Emu::Nestopia;
    r = Convert(q, env);
    CHECK_EQ(r.outputs[0].bytes.size(), (size_t)4096);
}

static void TestEeprom24C01() {
    std::printf("[test] EEPROM 24C01 (mapper 159) conversions\n");
    RamEnv env;
    std::vector<uint8_t> chip = MakeBytes(128, 0xAB);

    // MesenCE .eeprom128 -> Nintendulator .sav(128)
    ConvertRequest q = BasicReq(L"C:/saves/rpge.sav", chip);  // wrong: use right ext
    q.sourcePath = L"C:/saves/RPG.something";                 // (placeholder, replaced below)
    q = BasicReq(L"C:/saves/RPG.eeprom128", chip);
    q.target = Emu::Nintendulator;
    ConvertResult r = Convert(q, env);
    CHECK(r.ok);
    CHECK_EQ(r.outputs.size(), (size_t)1);
    CHECK_EQ(r.outputs[0].path, L"C:/saves/RPG.sav");
    CHECK(r.outputs[0].bytes == chip);

    // Nintendulator .sav(128) -> Nestopia .sav(128) (raw copy)
    q = BasicReq(L"C:/saves/RPG.sav", chip);
    q.target = Emu::Nestopia;
    r = Convert(q, env);
    CHECK(r.ok);
    CHECK(r.outputs[0].bytes == chip);

    // Nestopia .sav(128) -> MesenCE .eeprom128
    q = BasicReq(L"C:/saves/RPG.sav", chip);
    q.target = Emu::MesenCE;
    r = Convert(q, env);
    CHECK(r.ok);
    CHECK_EQ(r.outputs[0].path, L"C:/saves/RPG.eeprom128");
    CHECK(r.outputs[0].bytes == chip);

    // with ROM (mapper 159) confirming the type
    q = BasicReq(L"C:/saves/RPG.sav", chip);
    q.rom = ParseNesHeader(MakeNes(159, true).data(), 16);
    q.target = Emu::MesenCE;
    r = Convert(q, env);
    CHECK(r.ok);
    CHECK_EQ(r.outputs[0].path, L"C:/saves/RPG.eeprom128");
    CHECK(r.outputs[0].bytes == chip);
}

static void TestEeprom24C02() {
    std::printf("[test] EEPROM 24C02 (mapper 16) conversions\n");
    RamEnv env;
    std::vector<uint8_t> chip = MakeBytes(256, 0xCD);

    // Nestopia .sav(256) -> MesenCE .eeprom256
    ConvertRequest q = BasicReq(L"C:/saves/dbz.sav", chip);
    q.target = Emu::MesenCE;
    ConvertResult r = Convert(q, env);
    CHECK(r.ok);
    CHECK_EQ(r.outputs[0].path, L"C:/saves/dbz.eeprom256");
    CHECK(r.outputs[0].bytes == chip);

    // MesenCE .eeprom256 -> Nintendulator .sav(256)
    q = BasicReq(L"C:/saves/dbz.eeprom256", chip);
    q.target = Emu::Nintendulator;
    r = Convert(q, env);
    CHECK(r.ok);
    CHECK_EQ(r.outputs[0].path, L"C:/saves/dbz.sav");
    CHECK(r.outputs[0].bytes == chip);

    // Nintendulator .sav(256) -> Nestopia .sav(256)
    q = BasicReq(L"C:/saves/dbz.sav", chip);
    q.target = Emu::Nestopia;
    r = Convert(q, env);
    CHECK(r.ok);
    CHECK(r.outputs[0].bytes == chip);

    // ROM says mapper 16 (iNES) but the picked .sav is 8 KB MesenCE leftover ->
    // warning + SRAM passthrough (identity copy)
    std::vector<uint8_t> junk = MakeBytes(8192, 0x00);
    q = BasicReq(L"C:/saves/dbz.sav", junk);
    q.rom = ParseNesHeader(MakeNes(16, true).data(), 16);
    q.target = Emu::MesenCE;
    r = Convert(q, env);
    CHECK(r.ok);
    CHECK(HasMsg(r, Msg::WarnMesenSramUnused));
    CHECK_EQ(r.outputs[0].bytes.size(), (size_t)8192);
}

static void TestDatach() {
    std::printf("[test] Datach (mapper 157) split & merge\n");
    RamEnv env;
    std::vector<uint8_t> full = MakeBytes(384, 0x5A);
    std::vector<uint8_t> c02(full.begin(), full.begin() + 256);
    std::vector<uint8_t> c01(full.begin() + 256, full.end());

    // Nestopia .sav(384) -> MesenCE pair
    ConvertRequest q = BasicReq(L"C:/saves/datach.sav", full);
    q.target = Emu::MesenCE;
    ConvertResult r = Convert(q, env);
    CHECK(r.ok);
    CHECK_EQ(r.outputs.size(), (size_t)2);
    CHECK_EQ(r.outputs[0].path, L"C:/saves/datach.eeprom256");
    CHECK_EQ(r.outputs[1].path, L"C:/saves/datach.eeprom128");
    CHECK(r.outputs[0].bytes == c02);
    CHECK(r.outputs[1].bytes == c01);

    // MesenCE pair -> Nestopia .sav(384): pick the .eeprom256, sibling merged
    env.files[L"C:/saves/datach.eeprom256"] = c02;
    env.files[L"C:/saves/datach.eeprom128"] = c01;
    q = BasicReq(L"C:/saves/datach.eeprom256", c02);
    q.target = Emu::Nestopia;
    r = Convert(q, env);
    CHECK(r.ok);
    CHECK_EQ(r.outputs.size(), (size_t)1);
    CHECK_EQ(r.outputs[0].path, L"C:/saves/datach.sav");
    CHECK(r.outputs[0].bytes == full);
    CHECK(HasMsg(r, Msg::SiblingMerged));

    // .eeprom256 without sibling, ROM says Datach -> padded 384 + warning
    RamEnv env2;
    q = BasicReq(L"C:/saves/datach.eeprom256", c02);
    q.rom = ParseNesHeader(MakeNes(157, true).data(), 16);
    q.target = Emu::Nestopia;
    r = Convert(q, env2);
    CHECK(r.ok);
    CHECK_EQ(r.outputs[0].bytes.size(), (size_t)384);
    CHECK(HasMsg(r, Msg::WarnNoSibling));
    CHECK(std::memcmp(r.outputs[0].bytes.data(), c02.data(), 256) == 0);

    // Datach -> Nintendulator: unsupported
    q = BasicReq(L"C:/saves/datach.sav", full);
    q.target = Emu::Nintendulator;
    r = Convert(q, env2);
    CHECK(!r.ok);
    CHECK(HasMsg(r, Msg::ErrDatachUnsupported));
    CHECK_EQ(r.outputs.size(), (size_t)0);
}

static void TestDetectionAndErrors() {
    std::printf("[test] auto-detection rules and error cases\n");
    RamEnv env;

    // empty file
    ConvertRequest q = BasicReq(L"C:/saves/x.sav", {});
    ConvertResult r = Convert(q, env);
    CHECK(!r.ok);
    CHECK(HasMsg(r, Msg::ErrEmptyFile));

    // tiny file
    q = BasicReq(L"C:/saves/x.sav", {1, 2, 3});
    r = Convert(q, env);
    CHECK(!r.ok);
    CHECK(HasMsg(r, Msg::ErrTooSmall));

    // 128 B file with ROM saying plain SRAM game (mapper 1): ROM wins -> SRAM + warning
    std::vector<uint8_t> b128 = MakeBytes(128, 0);
    q = BasicReq(L"C:/saves/x.sav", b128);
    q.rom = ParseNesHeader(MakeNes(1, true).data(), 16);
    q.target = Emu::Nestopia;
    r = Convert(q, env);
    CHECK(r.ok);
    CHECK(HasMsg(r, Msg::WarnRomTypeConflict));
    CHECK_EQ(r.outputs[0].bytes.size(), (size_t)128);

    // manual type overrides size detection (24C02 chosen for a 384 B file)
    std::vector<uint8_t> b384 = MakeBytes(384, 0);
    q = BasicReq(L"C:/saves/x.sav", b384);
    q.saveType = SaveType::Eeprom24C02;
    q.target = Emu::Nintendulator;
    r = Convert(q, env);
    CHECK(r.ok);
    CHECK_EQ(r.outputs[0].bytes.size(), (size_t)256);
    CHECK(HasMsg(r, Msg::WarnTruncating));

    // short EEPROM file is padded with 0xFF
    std::vector<uint8_t> b64 = MakeBytes(64, 0);
    q = BasicReq(L"C:/saves/x.eeprom128", b64);
    q.target = Emu::MesenCE;
    r = Convert(q, env);
    CHECK(r.ok);
    CHECK_EQ(r.outputs[0].bytes.size(), (size_t)128);
    CHECK(HasMsg(r, Msg::WarnPadding));
    CHECK(std::memcmp(r.outputs[0].bytes.data(), b64.data(), 64) == 0);
    bool tailFF = true;
    for (size_t i = 64; i < 128; ++i)
        if (r.outputs[0].bytes[i] != 0xFF) tailFF = false;
    CHECK(tailFF);
}

static void TestWriteOutputs() {
    std::printf("[test] WriteOutputs: backup, identity no-op, overwrite\n");
    RamEnv env;
    std::vector<uint8_t> zelda = MakeBytes(8192, 0x11);

    // in-place conversion where the content actually changes (4 KB -> 8 KB
    // normalized for Nintendulator): the old file must be backed up first
    std::vector<uint8_t> half = MakeBytes(4096, 0x77);
    env.files[L"C:/saves/Zelda.sav"] = half;
    ConvertRequest q = BasicReq(L"C:/saves/Zelda.sav", half);
    q.target = Emu::Nintendulator;
    q.rom = ParseNesHeader(MakeNes(1, true).data(), 16);  // expect 8192
    ConvertResult r = Convert(q, env);
    CHECK(r.ok);
    CHECK(HasMsg(r, Msg::Normalized));
    bool written = WriteOutputs(r, q, env);
    CHECK(written);
    CHECK(HasMsg(r, Msg::BackupCreated));
    CHECK(HasMsg(r, Msg::WarnOverwrite));
    CHECK_EQ(env.files[L"C:/saves/Zelda.sav"].size(), (size_t)8192);
    CHECK(env.files[L"C:/saves/Zelda.sav.bak"] == half);

    // Existing backups must never be overwritten; new backups get a numbered name.
    RamEnv envBackup;
    envBackup.files[L"C:/saves/Zelda.sav"] = half;
    const std::vector<uint8_t> previousBackup = MakeBytes(32, 0xA5);
    envBackup.files[L"C:/saves/Zelda.sav.bak"] = previousBackup;
    q = BasicReq(L"C:/saves/Zelda.sav", half);
    q.target = Emu::Nintendulator;
    q.rom = ParseNesHeader(MakeNes(1, true).data(), 16);
    r = Convert(q, envBackup);
    CHECK(r.ok);
    written = WriteOutputs(r, q, envBackup);
    CHECK(written);
    CHECK(envBackup.files[L"C:/saves/Zelda.sav.bak"] == previousBackup);
    CHECK(envBackup.files[L"C:/saves/Zelda.sav.bak.1"] == half);

    // If a requested backup cannot be created, keep the old save untouched.
    BackupWriteFailEnv envFail;
    envFail.files[L"C:/saves/Zelda.sav"] = half;
    q = BasicReq(L"C:/saves/Zelda.sav", half);
    q.target = Emu::Nintendulator;
    q.rom = ParseNesHeader(MakeNes(1, true).data(), 16);
    r = Convert(q, envFail);
    CHECK(r.ok);
    written = WriteOutputs(r, q, envFail);
    CHECK(!written);
    CHECK(envFail.files[L"C:/saves/Zelda.sav"] == half);
    CHECK(r.HasErrors());

    // identity (source already equals output) -> NoAction, file untouched
    RamEnv env2;
    env2.files[L"C:/saves/Zelda.sav"] = zelda;
    q = BasicReq(L"C:/saves/Zelda.sav", zelda);
    q.target = Emu::Nestopia;
    r = Convert(q, env2);
    CHECK(r.ok);
    written = WriteOutputs(r, q, env2);
    CHECK(written);
    CHECK(HasMsg(r, Msg::NoAction));
    CHECK(env2.files[L"C:/saves/Zelda.sav"] == zelda);
    CHECK(env2.files.find(L"C:/saves/Zelda.sav.bak") == env2.files.end());

    // explicit output dir
    RamEnv env3;
    q = BasicReq(L"C:/saves/Zelda.sav", zelda);
    q.target = Emu::MesenCE;
    q.outputDir = L"C:/mesen/saves/";
    r = Convert(q, env3);
    CHECK_EQ(r.outputs[0].path, L"C:/mesen/saves/Zelda.sav");
    WriteOutputs(r, q, env3);
    CHECK(env3.files[L"C:/mesen/saves/Zelda.sav"] == zelda);

    // manually typed output dir without a trailing slash still yields a valid path
    q.outputDir = L"C:/mesen/saves2";   // no trailing slash
    r = Convert(q, env3);
    CHECK_EQ(r.outputs[0].path, L"C:/mesen/saves2\\Zelda.sav");
}

static void TestNewEmulators() {
    std::printf("[test] FCEUX, puNES and ares battery-save layouts\n");
    RamEnv env;

    const RomInfo mapper1 = ParseNesHeader(MakeNes(1, true).data(), 16);
    const RomInfo mapper16 = ParseNesHeader(MakeNes(16, true).data(), 16);
    const RomInfo mapper157 = ParseNesHeader(MakeNes(157, true).data(), 16);
    const std::vector<uint8_t> c01 = MakeBytes(128, 0x21);
    const std::vector<uint8_t> c02 = MakeBytes(256, 0x62);

    // FCEUX stores ordinary SRAM as a raw .sav and can be used as a source/target.
    std::vector<uint8_t> sram = MakeBytes(8192, 0x11);
    ConvertRequest q = BasicReq(L"C:/fceux/Zelda.sav", sram);
    q.sourceFormat = Emu::FCEUX;
    q.target = Emu::Nestopia;
    q.rom = mapper1;
    ConvertResult r = Convert(q, env);
    CHECK(r.ok);
    CHECK_EQ(r.outputs.size(), (size_t)1);
    CHECK_EQ(r.outputs[0].path, L"C:/fceux/Zelda.sav");
    CHECK(r.outputs[0].bytes == sram);

    q.target = Emu::Ares;
    q.outputDir = L"C:/ares/Game.pak";
    r = Convert(q, env);
    CHECK(r.ok);
    CHECK_EQ(r.outputs[0].path, L"C:/ares/Game.pak\\save.ram");
    CHECK(r.outputs[0].bytes == sram);

    // FCEUX mapper 157 .sav is 512 bytes: first 128 external 24C01, 128 unused,
    // then 256 internal 24C02. The converter canonicalizes this to [24C02|24C01].
    std::vector<uint8_t> fceuxDatach(512, 0xEE);
    std::copy(c01.begin(), c01.end(), fceuxDatach.begin());
    std::copy(c02.begin(), c02.end(), fceuxDatach.begin() + 256);
    q = BasicReq(L"C:/fceux/Datach.sav", fceuxDatach);
    q.sourceFormat = Emu::FCEUX;
    q.rom = mapper157;
    q.target = Emu::Nestopia;
    r = Convert(q, env);
    CHECK(r.ok);
    CHECK_EQ(r.outputs[0].bytes.size(), (size_t)384);
    CHECK(std::equal(c02.begin(), c02.end(), r.outputs[0].bytes.begin()));
    CHECK(std::equal(c01.begin(), c01.end(), r.outputs[0].bytes.begin() + 256));

    // Nestopia canonical Datach -> FCEUX's padded 512-byte representation.
    std::vector<uint8_t> canonical = c02;
    canonical.insert(canonical.end(), c01.begin(), c01.end());
    q = BasicReq(L"C:/saves/Datach.sav", canonical);
    q.sourceFormat = Emu::Nestopia;
    q.rom = mapper157;
    q.target = Emu::FCEUX;
    r = Convert(q, env);
    CHECK(r.ok);
    CHECK_EQ(r.outputs[0].bytes.size(), (size_t)512);
    CHECK(std::equal(c01.begin(), c01.end(), r.outputs[0].bytes.begin()));
    CHECK(std::all_of(r.outputs[0].bytes.begin() + 128,
                      r.outputs[0].bytes.begin() + 256,
                      [](uint8_t b) { return b == 0; }));
    CHECK(std::equal(c02.begin(), c02.end(), r.outputs[0].bytes.begin() + 256));

    // puNES stores raw .prb contents; accept its optional legacy 8-byte header.
    q = BasicReq(L"C:/punes/Zelda.prb", sram);
    q.sourceFormat = Emu::Auto;
    q.rom = mapper1;
    q.target = Emu::Nestopia;
    r = Convert(q, env);
    CHECK(r.ok);
    CHECK_EQ(r.outputs[0].path, L"C:/punes/Zelda.sav");
    CHECK(r.outputs[0].bytes == sram);

    const uint8_t punesHeader[] = {'F','H','p','u','N','E','S',0};
    std::vector<uint8_t> oldPunes = sram;
    oldPunes.insert(oldPunes.begin(), std::begin(punesHeader), std::end(punesHeader));
    q = BasicReq(L"C:/punes/Zelda.prb", oldPunes);
    q.sourceFormat = Emu::Auto;
    q.rom = mapper1;
    q.target = Emu::Nestopia;
    r = Convert(q, env);
    CHECK(r.ok);
    CHECK_EQ(r.outputs[0].bytes.size(), sram.size());
    CHECK(r.outputs[0].bytes == sram);

    // For mapper 16, puNES places the 24C02 at the start of its PRG-NVRAM area.
    std::vector<uint8_t> punesMapper16(8192, 0x99);
    std::copy(c02.begin(), c02.end(), punesMapper16.begin());
    q = BasicReq(L"C:/punes/Mapper16.prb", punesMapper16);
    q.sourceFormat = Emu::PuNES;
    q.rom = mapper16;
    q.target = Emu::MesenCE;
    r = Convert(q, env);
    CHECK(r.ok);
    CHECK_EQ(r.outputs[0].path, L"C:/punes/Mapper16.eeprom256");
    CHECK(r.outputs[0].bytes == c02);

    // Other emulator EEPROM -> puNES: the chip is padded into the target PRG-NVRAM
    // block (classic iNES defaults to 8 KiB), without an FHpuNES header.
    q = BasicReq(L"C:/saves/Mapper16.eeprom256", c02);
    q.sourceFormat = Emu::MesenCE;
    q.rom = mapper16;
    q.target = Emu::PuNES;
    r = Convert(q, env);
    CHECK(r.ok);
    CHECK_EQ(r.outputs[0].path, L"C:/saves/Mapper16.prb");
    CHECK_EQ(r.outputs[0].bytes.size(), (size_t)8192);
    CHECK(std::equal(c02.begin(), c02.end(), r.outputs[0].bytes.begin()));
    CHECK(std::all_of(r.outputs[0].bytes.begin() + 256, r.outputs[0].bytes.end(),
                      [](uint8_t b) { return b == 0; }));

    // puNES mapper 157 stores the external 24C01 in the first PRG-NVRAM bytes and
    // appends the internal 24C02 after the PRG-NVRAM block.
    std::vector<uint8_t> punesDatach(8192 + 256, 0xA5);
    std::copy(c01.begin(), c01.end(), punesDatach.begin());
    std::copy(c02.begin(), c02.end(), punesDatach.begin() + 8192);
    q = BasicReq(L"C:/punes/Datach.prb", punesDatach);
    q.sourceFormat = Emu::PuNES;
    q.rom = mapper157;
    q.target = Emu::Nestopia;
    r = Convert(q, env);
    CHECK(r.ok);
    CHECK_EQ(r.outputs[0].bytes.size(), (size_t)384);
    CHECK(std::equal(c02.begin(), c02.end(), r.outputs[0].bytes.begin()));
    CHECK(std::equal(c01.begin(), c01.end(), r.outputs[0].bytes.begin() + 256));

    q = BasicReq(L"C:/saves/Datach.sav", canonical);
    q.sourceFormat = Emu::Nestopia;
    q.rom = mapper157;
    q.target = Emu::PuNES;
    r = Convert(q, env);
    CHECK(r.ok);
    CHECK_EQ(r.outputs[0].path, L"C:/saves/Datach.prb");
    CHECK_EQ(r.outputs[0].bytes.size(), (size_t)(8192 + 256));
    CHECK(std::equal(c01.begin(), c01.end(), r.outputs[0].bytes.begin()));
    CHECK(std::all_of(r.outputs[0].bytes.begin() + 128,
                      r.outputs[0].bytes.begin() + 8192,
                      [](uint8_t b) { return b == 0; }));
    CHECK(std::equal(c02.begin(), c02.end(), r.outputs[0].bytes.begin() + 8192));

    // A .prb with more data than the ROM's PRG-NVRAM prefix contains additional
    // CHR/mapper battery blocks. Refuse conversion to a simpler format rather
    // than silently discard that data; retaining puNES as target preserves it.
    std::vector<uint8_t> composite = MakeBytes(8192 + 256, 0x31);
    q = BasicReq(L"C:/punes/Composite.prb", composite);
    q.sourceFormat = Emu::PuNES;
    q.rom = mapper1;
    q.target = Emu::Nestopia;
    r = Convert(q, env);
    CHECK(!r.ok);
    CHECK(HasMsg(r, Msg::ErrPunesCompositeNvram));
    CHECK(r.outputs.empty());

    q.target = Emu::PuNES;
    r = Convert(q, env);
    CHECK(r.ok);
    CHECK_EQ(r.outputs[0].bytes.size(), composite.size());
    CHECK(r.outputs[0].bytes == composite);

    // NES 2.0 declares exact NVRAM sizes. Refuse a 256-byte EEPROM when the
    // target ROM only declares 128 bytes of PRG NVRAM.
    const RomInfo tooSmallMapper16 =
        ParseNesHeader(MakeNes(16, true, true, 5, 1).data(), 16);
    q = BasicReq(L"C:/saves/Mapper16.eeprom256", c02);
    q.sourceFormat = Emu::MesenCE;
    q.rom = tooSmallMapper16;
    q.target = Emu::PuNES;
    r = Convert(q, env);
    CHECK(!r.ok);
    CHECK(HasMsg(r, Msg::ErrPunesNvramTooSmall));
    CHECK(r.outputs.empty());

    // For NES 2.0 Datach, puNES writes PRG NVRAM, then CHR NVRAM, then 24C02.
    // The converter must locate the EEPROM after CHR NVRAM and refuse to drop it.
    std::vector<uint8_t> mapper157Nes20Header = MakeNes(157, true, true, 0, 7);
    mapper157Nes20Header[11] = 0x30;  // 512 bytes of CHR NVRAM
    const RomInfo mapper157Nes20 =
        ParseNesHeader(mapper157Nes20Header.data(), mapper157Nes20Header.size());
    std::vector<uint8_t> punesDatachWithChr(8192 + 512 + 256, 0x5A);
    std::copy(c01.begin(), c01.end(), punesDatachWithChr.begin());
    std::copy(c02.begin(), c02.end(), punesDatachWithChr.begin() + 8192 + 512);
    q = BasicReq(L"C:/punes/DatachNes20.prb", punesDatachWithChr);
    q.sourceFormat = Emu::PuNES;
    q.rom = mapper157Nes20;
    q.target = Emu::Nestopia;
    r = Convert(q, env);
    CHECK(!r.ok);
    CHECK(HasMsg(r, Msg::ErrPunesCompositeNvram));
    CHECK(r.outputs.empty());
    q.target = Emu::PuNES;
    r = Convert(q, env);
    CHECK(r.ok);
    CHECK(r.outputs[0].bytes == punesDatachWithChr);

    // ares identifies RAM and EEPROM by fixed file names inside the game's .pak.
    // save.ram must stay SRAM even if its size happens to match an EEPROM chip.
    q = BasicReq(L"C:/ares/Game.pak/save.ram", c01);
    q.sourceFormat = Emu::Auto;
    q.target = Emu::Nestopia;
    r = Convert(q, env);
    CHECK(r.ok);
    CHECK_EQ(r.outputs[0].path, L"C:/ares/Game.sav");
    CHECK(r.outputs[0].bytes == c01);

    // ares Datach uses save.eeprom (24C02) plus external.eeprom (24C01).
    env.files[L"C:/ares/Datach.pak/external.eeprom"] = c01;
    q = BasicReq(L"C:/ares/Datach.pak/save.eeprom", c02);
    q.sourceFormat = Emu::Auto;
    q.rom = mapper157;
    q.target = Emu::Nestopia;
    r = Convert(q, env);
    CHECK(r.ok);
    CHECK_EQ(r.outputs[0].path, L"C:/ares/Datach.sav");
    CHECK_EQ(r.outputs[0].bytes.size(), (size_t)384);
    CHECK(std::equal(c02.begin(), c02.end(), r.outputs[0].bytes.begin()));
    CHECK(std::equal(c01.begin(), c01.end(), r.outputs[0].bytes.begin() + 256));

    // Any canonical Datach source -> ares creates the two fixed-name package files.
    q = BasicReq(L"C:/saves/Datach.sav", canonical);
    q.sourceFormat = Emu::Nestopia;
    q.rom = mapper157;
    q.target = Emu::Ares;
    q.outputDir = L"C:/ares/Converted.pak";
    r = Convert(q, env);
    CHECK(r.ok);
    CHECK_EQ(r.outputs.size(), (size_t)2);
    CHECK_EQ(r.outputs[0].path, L"C:/ares/Converted.pak\\save.eeprom");
    CHECK_EQ(r.outputs[1].path, L"C:/ares/Converted.pak\\external.eeprom");
    CHECK(r.outputs[0].bytes == c02);
    CHECK(r.outputs[1].bytes == c01);
}

static void TestStringsTables() {
    std::printf("[test] UI string tables are complete in both languages\n");
    // every UI string must exist in EN and RU
    for (int i = 0; i < rsc::S_COUNT; ++i) {
        CHECK(rsc::Str(static_cast<rsc::StrId>(i), rsc::LANG_EN) != nullptr);
        CHECK(rsc::Str(static_cast<rsc::StrId>(i), rsc::LANG_RU) != nullptr);
    }
    // every core message template must exist in EN and RU
    for (int m = 0; m <= static_cast<int>(rsc::Msg::ErrPunesNvramTooSmall); ++m) {
        CHECK(rsc::MsgTemplate(static_cast<rsc::Msg>(m), rsc::LANG_EN) != nullptr);
        CHECK(rsc::MsgTemplate(static_cast<rsc::Msg>(m), rsc::LANG_RU) != nullptr);
    }
    // the enum order used by MsgTemplate's tables must not drift
    CHECK_EQ(static_cast<int>(rsc::Msg::ErrDatachUnsupportedByTarget), 19);
    CHECK_EQ(static_cast<int>(rsc::Msg::ErrPunesCompositeNvram), 21);
    CHECK_EQ(static_cast<int>(rsc::Msg::ErrPunesNvramTooSmall), 22);
}

int main() {
    TestStringsTables();
    TestHeaderParsing();
    TestSramRoundtrip();
    TestNintendulatorNormalization();
    TestEeprom24C01();
    TestEeprom24C02();
    TestDatach();
    TestDetectionAndErrors();
    TestWriteOutputs();
    TestNewEmulators();

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
