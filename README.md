# Retro Save Converter

**A tiny bilingual (English / Русский) GUI utility that converts in-game battery saves — NOT save states — between NES emulators.**

Started playing *Zelda* in MesenCE or Nestopia and want to continue in Nintendulator (or any other direction)? This tool converts the cartridge's own battery save (SRAM / EEPROM) into the format the target emulator understands, so you can pick up right where you left off.

Compatible with **Windows 7 x64 and later** — a single ~330 KB static executable with no runtime dependencies (no .NET, no installer). Choose the interface language from **Language** and the light/dark theme from **View**; both preferences are saved in an INI file beside the executable, with a `%LOCALAPPDATA%\RetroSaveConverter` fallback when that folder is read-only.

---

## Supported emulators (NES/FC tab)

| | Save file(s) used | SRAM | EEPROM 24C01 | EEPROM 24C02 | Datach (157) |
|---|---|:---:|:---:|:---:|:---:|
| **MesenCE** | `<game>.sav`, `<game>.eeprom128`, `<game>.eeprom256` | ✅ | ✅ | ✅ | ✅ (pair of files) |
| **Nestopia** | `<game>.sav` | ✅ | ✅ | ✅ | ✅ (384-byte file) |
| **Nintendulator** | `<game>.sav` (in `SRAM\` subfolder) | ✅ | ✅ | ✅ | ❌ not supported by the emulator itself |
| **FCEUX** | `<game>.sav` (raw concatenated save buffers) | ✅ | ✅ | ✅ | ✅ (512-byte buffer layout) |
| **puNES** | `<game>.prb` (raw NVRAM payload) | ✅ | ✅ | ✅ | ✅ (PRG NVRAM + 24C02 payload) |
| **ares (NES)** | `<game>.pak/save.ram`, `save.eeprom`, `external.eeprom` | ✅ | ✅ | ✅ | ✅ (`save.eeprom` + `external.eeprom`) |
| **Generic (.sav)** | `<game>.sav` | ✅ | ✅ | ✅ | ✅ (canonical 384-byte layout) |

FCEUX uses the same raw `.sav` extension as several other emulators, so select **FCEUX** manually when importing its mapper-157 save: the 512-byte layout is different from ordinary SRAM. puNES is auto-detected from `.prb`. ares is auto-detected from its fixed `save.ram`, `save.eeprom`, and `external.eeprom` names. For output **to** ares, choose the target game's `.pak` directory as the output folder; the converter writes the filenames expected inside that package. When converting **from** ares to another emulator, the default output is named after the `.pak` directory and placed beside the package. The ROM auto-detection also checks for a matching `.nes` beside the package.

EEPROM chip contents are copied byte-for-byte when the source contains the complete chip data; Datach is rearranged only where an emulator uses a different block layout. Target-specific size normalization is reported in the log. A puNES `.prb` can also contain CHR NVRAM or additional mapper-specific data. When such extra payload is detected and the requested target cannot represent it, the converter refuses the conversion rather than silently discarding those bytes. Keeping puNES as the target preserves the complete `.prb` payload.

`SNES/SFC` and `N64` tabs are already in the interface and will receive their converters in future versions.

## How it works

1. Pick the **save file** (or drag & drop it onto the window — the matching `.nes` ROM next to it is detected automatically).
2. Optionally pick the **ROM file** — its iNES / NES 2.0 header lets the tool verify the save type (mapper 16/157/159 ⇒ EEPROM board) and normalize sizes.
3. Choose the **target emulator** and press **Convert**.
4. The source format, save type and content type are auto-detected (by extension, file size and ROM header), with a manual override combo if needed.

Extra safety:

* every file that is about to be replaced gets a **`.bak` backup** (toggleable); existing backups are preserved and subsequent backups use `.bak.1`, `.bak.2`, etc.; if a requested backup cannot be secured, that output is not overwritten;
* the log explains every decision, warning and error;
* converting between incompatible types (e.g. an EEPROM dump into an SRAM-only flow, or a Datach save into Nintendulator) is refused with a clear reason.

## Research: how each emulator stores battery saves

This converter was written by reading the actual emulator sources — this is what makes it correct rather than guesswork:

* **MesenCE** (`nesdev-org/MesenCE`)
  * `Core/Shared/BatteryManager.cpp` — raw read/write of save data, loader takes `min(fileSize, ramSize)`.
  * `Core/NES/BaseMapper.cpp` — regular SRAM goes to `<game>.sav` (8 KB by default, NES 2.0 sizes honored).
  * `Core/NES/Mappers/Bandai/Eeprom24C01.h`, `Eeprom24C02.h`, `BandaiFcg.h` — EEPROM boards keep chips in **separate files**: `.eeprom128` (mapper 159) and `.eeprom256` (mapper 16); the Datach board (mapper 157) may use both (internal 24C02 + external 24C01).
* **Nestopia** (`0ldsk00l/nestopia`)
  * `source/core/board/NstBoard.cpp` — battery WRAM is written raw into `<game>.sav`.
  * `source/core/board/NstBoardBandaiLz93d50ex.cpp` + `NstBoardBandai24c0x.hpp` — EEPROM data is also stored in the same `<game>.sav`; a Datach game stores **both chips concatenated**: 256 bytes of 24C02 followed by 128 bytes of 24C01 (384 bytes total).
  * `source/win32/NstManagerEmulator.cpp`, `NstManagerPaths.cpp` — confirms the shared `.sav` path for battery/EEPROM data.
* **Nintendulator** (`quietust/nintendulator` + `quietust/nintendulator-mappers`)
  * `src/NES.cpp` (`SaveSRAM`/`LoadSRAM`) — saves raw `SRAM_Size` bytes into `<DataPath>\SRAM\<game>.sav`; size mismatch only produces a warning.
  * `src/iNES/mapper016.cpp` — mapper 16 = 24C02 (256 B), mapper 159 = 24C01 (128 B); the EEPROM lives in the PRG-RAM window at $6000, so the `.sav` is exactly the chip dump.
  * `src/Dll/d_iNES.cpp` — plain iNES battery games get `SRAM_Size = 8192`; NES 2.0 sizes come from the header. Mapper 157 (Datach) is not supported at all.
* **FCEUX** (`TASEmulators/fceux`)
  * `src/cart.cpp` (`FCEU_SaveGameSave` / `FCEU_LoadGameSave`) writes/reads each registered battery buffer sequentially to `<game>.sav`, without a header.
  * `src/boards/bandai.cpp` registers mapper 16 as 256 bytes, mapper 159 as 128 bytes, and mapper 157 as a 512-byte buffer. For mapper 157, the 24C01 data is at bytes 0–127, bytes 128–255 are unused padding, and the 24C02 is at bytes 256–511. The converter translates this to/from its canonical `[24C02 256 B | 24C01 128 B]` order.
* **puNES** (`punesemu/puNES`)
  * `src/core/memmap.c` (`nvram_load_file` / `nvram_save_file`) stores raw NVRAM sequentially in `<game>.prb`: PRG NVRAM, CHR NVRAM, then mapper-specific battery data. It may accept an older `FHpuNES\0` prefix, but the current writer has its header write disabled.
  * `src/core/ines.c` defaults legacy iNES battery RAM to 8 KiB. `src/core/mappers/mapper_157.c` stores the external 24C01 in PRG NVRAM and appends the internal 256-byte 24C02 via `extcl_battery_io_157`.
  * Because `.prb` can combine several kinds of NVRAM in one file, conversion to a simpler save format is refused when extra unsupported battery blocks would be lost. Mapper-16/159 EEPROM is taken from the beginning of the PRG NVRAM prefix; Datach is unpacked using the ROM's declared prefix size.
* **ares (NES)** (`ares-emulator/ares`)
  * `ares/fc/cartridge/board/bandai-lz93d50.cpp` reads/writes raw `save.ram` and `save.eeprom`; EEPROM file size distinguishes 128-byte 24C01 from 256-byte 24C02.
  * `ares/fc/cartridge/board/bandai-datach.cpp` additionally reads/writes `external.eeprom` (128 bytes). The files live inside the game `.pak` directory, not in a single `.sav` file.

EEPROM bytes are transferred as raw bytes; the converter does not reverse bits or add proprietary headers. Datach is rearranged only where an emulator uses a different block layout. The implementation above was checked against the source files shipped in the supplied FCEUX, puNES, and ares repositories.

The core engine is covered by **341 unit checks** (`tests/test_converter.cpp`), including FCEUX's 512-byte Datach layout, puNES headers/NVRAM prefix handling and composite-file refusal, ares fixed filenames, and the existing backup/refusal logic.

## Building

### GitHub Actions (recommended)

The repository ships a manual-build workflow:

1. Push the repository to GitHub.
2. Open the **Actions** tab, select **Manual build (Windows exe)**.
3. Press **Run workflow** — unit tests run first, then the exe is cross-compiled.
4. Download the `RetroSaveConverter-win64` artifact.

### Locally on Windows — `build.cmd`

Double-click or run from a console; the script auto-detects the toolchain:

* **MinGW-w64** (`g++` in PATH, e.g. [WinLibs](https://winlibs.com/) or MSYS2) → fully static exe;
* **MSVC** — run from the *"x64 Native Tools Command Prompt"* (`cl` in PATH) → static CRT (`/MT`).

### Cross-compile on Linux — `build.sh`

```sh
sudo apt-get install g++-mingw-w64-x86-64 binutils-mingw-w64-x86-64
./build.sh            # produces RetroSaveConverter.exe
```

### Windows publisher warning

The project build produces an **unsigned** executable. Depending on how it is obtained and on Windows security policy, the EXE may trigger an unknown-publisher or SmartScreen reputation warning. The manifest declares `asInvoker` because the converter does not need administrator privileges, but that does not establish publisher identity. A trusted Authenticode signature is needed to identify the publisher; a new file or certificate can still trigger a SmartScreen warning until sufficient reputation is established. A local “Unblock” action only removes the download mark for that copy and does not sign or otherwise certify the binary.

### Unit tests

```sh
g++ -std=c++17 -O1 -Wall -Wextra -o test_converter tests/test_converter.cpp src/converter.cpp
./test_converter
```

## Project layout

```
RetroSaveConverter/
├── src/
│   ├── main.cpp        # Win32 GUI: tabs, EN/RU switching, drag & drop, log
│   ├── converter.h/.cpp# platform-independent conversion engine (the "brains")
│   ├── strings.h       # all UI strings + message templates, English & Russian
│   ├── resource.h      # control IDs
│   ├── app.rc          # version info + embedded manifest + icon
│   ├── app.manifest    # Win7+ compatibility, common controls v6, DPI aware
│   └── app.ico         # generated pixel-art icon (16/32/48)
├── tests/test_converter.cpp
├── build.cmd           # local Windows build (MinGW or MSVC)
├── build.sh            # Linux / CI cross-build
└── .github/workflows/build.yml   # manual GitHub Actions build
```

## Notes & limitations

* Converts **battery saves only** — save states (`*.nst`, `*.mes`, Mesen save states, etc.) are inherently emulator-specific and are out of scope.
* FDS disk saves (`.fsav`) and VS System high-score RAM are not converted yet.
* The old Mesen 0.9.9 (wxWidgets build) stored EEPROM data inside `.sav` with a header — that legacy layout is not supported; current **MesenCE** is fully supported.
* SNES/SFC and N64 converters are planned (the tabs are already there).

## License

MIT — see [LICENSE](LICENSE).

## Credits

* Format research is based on the open source code of [MesenCE](https://github.com/nesdev-org/MesenCE), [Nestopia](https://github.com/0ldsk00l/nestopia) and [Nintendulator](https://github.com/quietust/nintendulator) (plus its [mapper DLLs](https://github.com/quietust/nintendulator-mappers)), [FCEUX](https://github.com/TASEmulators/fceux), [puNES](https://github.com/punesemu/puNES) and [ares](https://github.com/ares-emulator/ares). All trademarks belong to their respective owners.

---

# Конвертер сохранений Retro

**Маленькая двуязычная (Русский / English) утилита с GUI, конвертирующая игровые сохранения (НЕ сейв-стейты) между эмуляторами NES.**

Начали проходить *Zelda* в MesenCE или Nestopia и хотите продолжить в Nintendulator (или в любом другом направлении)? Программа конвертирует батарейное сохранение картриджа (SRAM / EEPROM) в формат, который поймёт целевой эмулятор, — и вы продолжаете игру с того же места.

Совместима с **Windows 7 x64 и новее** — один статический exe ~330 КБ, без рантаймов и установщика. Язык переключается в меню «Язык», светлая/тёмная тема — в меню «Вид». Настройки сохраняются в INI рядом с EXE; если папка недоступна для записи, используется `%LOCALAPPDATA%\RetroSaveConverter`.

## Как пользоваться

1. Выберите **файл сохранения** (или перетащите его в окно — лежащий рядом `.nes` будет найден автоматически).
2. При желании укажите **файл ROM** — заголовок iNES / NES 2.0 позволяет проверить тип сохранения и нормализовать размер.
3. Выберите **целевой эмулятор** и нажмите **«Конвертировать»**.
4. Формат источника, тип содержимого и тип чипа определяются автоматически (по расширению, размеру файла и заголовку ROM); при необходимости тип можно задать вручную.

Безопасность:

* каждая перезаписываемая файловая копия сохраняется как **`.bak`** (отключается галочкой);
* в журнале объясняется каждое решение, предупреждение и ошибка;
* несовместимые преобразования (например, Datach-сохранение в Nintendulator) отклоняются с понятным объяснением.

## Что выяснено при изучении исходников эмуляторов

Кратко (полностью — в английской части выше, с указанием конкретных файлов исходного кода):

* **MesenCE**: обычный SRAM — в `<игра>.sav`; EEPROM — в `<игра>.eeprom128` / `<игра>.eeprom256`. Datach (маппер 157) использует оба файла.
* **Nestopia**: всё — в один `<игра>.sav`; для Datach оба чипа склеены в один файл 384 байта (24C02 ‖ 24C01).
* **Nintendulator**: `<папка>\SRAM\<игра>.sav` — дамп SRAM (обычно 8 КБ; 256/128 байт для мапперов 16/159). Datach не поддерживается самим эмулятором.
* **FCEUX**: `<игра>.sav` — последовательность raw-буферов батарейной памяти. Datach занимает 512 байт: 24C01 в начале, 128 байт неиспользуемого промежутка, затем 24C02.
* **puNES**: `<игра>.prb` — raw NVRAM без обязательного заголовка: PRG NVRAM, CHR NVRAM и mapper-данные могут быть объединены. Для Datach 24C01 хранится в PRG NVRAM, а 24C02 добавляется в конец.
* **ares (NES)**: внутри `<игра>.pak` используются отдельные `save.ram`, `save.eeprom` и, для Datach, `external.eeprom`.
* При конвертации **в** ares указывайте `.pak` нужной игры как папку результата. При конвертации **из** ares обычный файл по умолчанию создаётся рядом с `.pak` и называется по имени игры; поиск ROM также проверяет соседний `<игра>.nes`. Если puNES `.prb` содержит дополнительные блоки CHR/mapper NVRAM, которые выбранный целевой формат не поддерживает, конвертер откажется от преобразования, чтобы не потерять данные.

## Сборка

* **GitHub Actions**: вкладка *Actions* → *Manual build (Windows exe)* → *Run workflow* → скачать артефакт `RetroSaveConverter-win64`.
* **Windows локально**: `build.cmd` (сам найдёт MinGW-w64 или MSVC).
* **Linux (кросс)**: `sudo apt-get install g++-mingw-w64-x86-64 binutils-mingw-w64-x86-64 && ./build.sh`.
* **Тесты**: `g++ -std=c++17 -O1 -Wall -Wextra -o test_converter tests/test_converter.cpp src/converter.cpp && ./test_converter` — 341 проверка.

### Предупреждение Windows об издателе

Сборка проекта создаёт **неподписанный** EXE. В зависимости от способа получения файла и политик безопасности Windows может показать предупреждение о неизвестном издателе или репутации SmartScreen. В манифесте указано `asInvoker`, поскольку программе не нужны права администратора, но это не подтверждает издателя. Для отображения проверенного издателя нужна доверенная подпись Authenticode; даже у нового подписанного файла/сертификата SmartScreen может продолжать показывать предупреждение, пока не накопится достаточная репутация. Локальное действие «Разблокировать» снимает метку скачивания только у этой копии и не подписывает и не сертифицирует EXE.

## Лицензия

MIT — см. [LICENSE](LICENSE).
