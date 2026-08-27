# Sesame

A hackable, educational **Sega Master System / Game Gear** emulator in C++17.

Sesame is built for transparency over raw accuracy: the code is meant to be
read. The core (`sesame_core`) has zero dependencies and knows nothing about
the GUI; the `Bus` class *is* the memory map.

## Features (v0.1)

- Full Z80 core (documented + undocumented opcodes, standard cycle counts)
- VDP 315-5124, mode 4: background scrolling, sprites, line & VBlank interrupts
- NTSC and PAL consoles (`--pal`: 313 lines, ~49.70 Hz, PAL CPU clock)
- SN76489 PSG (3 square channels + noise), 44.1 kHz band-limited stereo
  output
- YM2413 FM unit (Japanese SMS): 9 melodic 2-op channels, hardware OPL
  log-sin/exp tables, `$F2` detection (rhythm mode TODO)
- Game Gear mode (auto-detected from `.gg`): 12-bit palette, 160x144
  cropped display, Start button, per-channel stereo panning, GG-specific
  ports
- Standard Sega mapper, `.sms`/`.gg` ROM loading (512-byte dumper headers
  tolerated)
- Two controllers, Pause (NMI), Reset
- BIOS slot with real memory-control port (`$3E`): boots BIOS images and
  BIOS + cartridge combos
- SDSC debug console (ports `$FC`/`$FD`) for homebrew test ROMs
- Save states (F5/F7 in the GUI; `--state-save`/`--state-load` headless),
  deterministic across processes
- Headless frontend: deterministic traces, PPM screenshots, WAV dump
- Windowed frontend: GLFW3 + OpenGL, real-time audio (CoreAudio on macOS),
  USB gamepads (GLFW gamepad API)
- CRT filter (single-pass shader: barrel, scanlines, shadow mask, phosphor
  persistence) and kiosk mode (exclusive fullscreen, hidden cursor)

## Build

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

Targets: `sesame` (GUI, requires GLFW3 — `brew install glfw`),
`sesame-headless`, `sesame_core` (static lib).

## Run

```sh
./build/sesame game.sms
./build/sesame game.sms --bios bios.sms   # boot through a BIOS image
./build/sesame game.sms --pal             # European console (50 Hz)
./build/sesame game.sms --crt             # CRT filter on from the start
./build/sesame game.sms --kiosk           # arcade-cabinet mode: exclusive
                                          # fullscreen, hidden cursor, CRT
```

Kiosk options: `--kiosk-monitor N` picks the target display (0 = primary),
`--menu` opens the in-game menu at startup.

The **in-game menu** (gamepad Start or `F9` anywhere; also `Esc` in kiosk
mode) pauses the game over a fullscreen overlay: pick another game from the
ROM folder (inserting a cartridge power-cycles the console, through the BIOS
if one is loaded), restart the machine, toggle fullscreen, or quit — in kiosk
mode `Esc` alone never kills the cabinet. The console's Pause button is the
`Enter` key.

A file whose name contains "BIOS" is loaded into the BIOS slot automatically
(with an empty cartridge slot, like a console with no cartridge inserted).

Keys: arrows = D-pad, `Z`/`W` = button 1, `X` = button 2, `Enter` = Pause,
`R` = reset, `F` = fullscreen, `C` = CRT filter, `F9` = in-game menu,
`Esc` = quit (desktop) / menu (kiosk).
Battery-backed cartridge RAM is persisted next to the ROM as `<rom>.sav`.
Gamepads: GLFW slots 1/2 map to pads 1/2 — D-pad or left stick, `A`/`X` =
button 1, `B`/`Y` = button 2, `Start` = in-game menu, `Select` = console
Pause.

## Headless & tests

No commercial ROM is required: the test suite generates its own.

```sh
python3 tools/run_selftests.py
./build/sesame-headless roms/selftest.sms --frames 60 --screenshot out.ppm --sdsc
./build/sesame-headless game.sms --frames 600 --trace t.txt --wav out.wav
```

The Z80 core is validated against ZEXDOC/ZEXALL (Maxim Zhao's SMS port,
GPLv2, fetched on demand — not bundled):

```sh
python3 tools/fetch_zexall.py       # downloads roms/zexdoc.sms + zexall.sms
python3 tools/run_zexall.py all     # runs both exercisers (a few minutes)
```

## License

GPL-3.0-or-later — Copyright (C) 2026 VERHILLE Arnaud. See [LICENSE](LICENSE).
Sega and Master System are trademarks of SEGA. No original ROM or BIOS
is included or required.
