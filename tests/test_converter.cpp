// ========================================================================================
// RetroSaveConverter — core engine unit tests (portable, run on any platform)
// Build:  g++ -std=c++17 -O1 -Wall -Wextra -o test_converter tests/test_converter.cpp src/converter.cpp
// ========================================================================================
#include "../src/converter.h"
#include "../src/strings.h"

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
        files[path] = std::vector<uint8_t>(data, data + size);
        return true;
    }
    bool FileExists(const std::wstring& path) override {
        return files.find(path) != files.end();
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
        h[10] = static_cast<uint8_t>((nvramNibble << 4) & 0xF0);
        h[11] = static_cast<uint8_t>(((subMapper & 0x0F) << 4) & 0xF0);
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

    std::vector<uint8_t> junk = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    CHECK(!ParseNesHeader(junk.data(), junk.size()).valid);
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

static void TestStringsTables() {
    std::printf("[test] UI string tables are complete in both languages\n");
    // every UI string must exist in EN and RU
    for (int i = 0; i < rsc::S_COUNT; ++i) {
        CHECK(rsc::Str(static_cast<rsc::StrId>(i), rsc::LANG_EN) != nullptr);
        CHECK(rsc::Str(static_cast<rsc::StrId>(i), rsc::LANG_RU) != nullptr);
    }
    // every core message template must exist in EN and RU
    for (int m = 0; m <= static_cast<int>(rsc::Msg::ErrWriteFile); ++m) {
        CHECK(rsc::MsgTemplate(static_cast<rsc::Msg>(m), rsc::LANG_EN) != nullptr);
        CHECK(rsc::MsgTemplate(static_cast<rsc::Msg>(m), rsc::LANG_RU) != nullptr);
    }
    // the enum order used by MsgTemplate's tables must not drift
    CHECK_EQ(static_cast<int>(rsc::Msg::ErrWriteFile), 18);
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

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
