# Emote Gen GFX — test application

Standalone ESP-IDF project that exercises **Emote Gen GFX** (`esp_emote_gen_player`) on real hardware (ESP-Box / P4 EVB, etc.), same asset scenario as `esp_emote_gfx` **`test_anim_emote_gen`**.

## Build

```bash
cd test_apps
idf.py set-target esp32s3   # or esp32p4 for the P4 board package
idf.py build flash monitor
```

Unity menu: run **`emote_gen_player mmap index segment playback`**.

## Assets

- **mmap pack** — on first CMake configure, **`main/CMakeLists.txt`** downloads **`https://dl.espressif.com/AE/emote_assets.bin`** into **`build/prebuilt/emote_assets.bin`** and uses it as `COPY_PREBUILT_BIN`. To use your own blob, change the URL or point `EMOTE_ASSETS_BIN` at a local file after download logic. You can also author packs with **[ESP Emote GFX Packer NEXT](https://emote-gfx-gen-tool-dev.pages.dev/)** (see root **README.md**).
- `main/assets/` — font/icons for the hidden “Next” button (copied from gfx test).

## Partition

`partitions.csv` defines `emote_gen` (5500K) for the SPIFFS image produced by CMake (`COPY_PREBUILT_BIN`).
