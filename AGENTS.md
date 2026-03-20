# AlphaBoy - Agent Guide

AlphaBoy is a Game Boy (DMG-01) Go implementation built with GBDK-2020.
The build system is a GNU Makefile; see the Makefile for build targets, flags, and emulator integration.

## Gitignored local resources

Several directories are gitignored because they contain large binaries, external toolchains, or cloned reference repos.
They are present locally (run `setup_deps.bat` to populate them) and are essential for development.

**Important: these directories are invisible to workspace search tools (Glob, Grep) because those tools respect .gitignore.**
To access their contents, use the Read tool with an explicit file path, or use shell commands (e.g. `Get-ChildItem`).

### pandocs/ - Game Boy hardware reference

A clone of the Pan Docs repository (https://github.com/gbdev/pandocs).
The markdown sources live in `pandocs/src/`.

Use when answering questions about Game Boy hardware behavior, registers, memory map, graphics pipeline, audio, cartridge types, or interrupt handling.

Key files:
- `pandocs/src/Memory_Map.md` - address space layout
- `pandocs/src/Graphics.md`, `Rendering.md`, `Tile_Data.md`, `Tile_Maps.md`, `OAM.md` - PPU and sprite system
- `pandocs/src/Joypad_Input.md` - input registers
- `pandocs/src/MBC5.md` - the memory bank controller used by this project (MBC5 + RAM + Battery)
- `pandocs/src/Interrupts.md`, `Interrupt_Sources.md` - interrupt system
- `pandocs/src/Audio.md`, `Audio_Registers.md` - sound hardware

### gbdk_source_and_docs/ - GBDK-2020 source, docs, and library code

A clone of the GBDK-2020 repository (https://github.com/gbdk-2020/gbdk-2020).

Use when looking up GBDK API behavior, library source code, or coding guidelines for the GBDK toolchain.

Key areas:
- `gbdk_source_and_docs/docs/pages/` - official documentation as markdown (getting started, coding guidelines, banking/MBCs, FAQ, toolchain settings)
  - `04_coding_guidelines.md` - GBDK-specific C coding patterns, variable sizing, performance tips
  - `03_using_gbdk.md` - API usage, building, linking
  - `05_banking_mbcs.md` - ROM/RAM banking with MBC cartridges
- `gbdk_source_and_docs/gbdk-lib/include/` - library header sources (the authoritative version of the headers shipped in the release)
- `gbdk_source_and_docs/gbdk-lib/examples/` - example projects from the GBDK source tree

### gbdk_release/ - GBDK-2020 compiled toolchain

The prebuilt GBDK-2020 release used by the Makefile (`GBDK_HOME = gbdk_release/`).

Use when checking available compiler/linker binaries, the actual headers the project compiles against, or looking for example code that ships with the release.

Key areas:
- `gbdk_release/bin/` - compiler (`lcc`), linker, and asset tools (`png2asset`)
- `gbdk_release/include/` - C headers the project compiles against; the Game Boy specific ones are in `include/gb/` (e.g. `gb.h`, `hardware.h`, `metasprites.h`, `drawing.h`)
- `gbdk_release/lib/` - precompiled libraries linked into the ROM
- `gbdk_release/examples/gb/` - ready-to-build example projects (e.g. `galaxy`, `paint`, `sound`, `ram_function`)

### Emulators

Three emulators are gitignored and wired into the Makefile via `make run EMU=<name>`:
- `bgbw64/` - BGB
- `Emulicious-with-Java64/` - Emulicious (with debugger/profiler)
- `Mesen_2.1.1_Windows/` - Mesen (default)

See the Makefile `run` target for launch flags and configuration.
