# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

XiaoZhi AI Chatbot — an ESP32 firmware for a voice-interactive AI device. It streams audio to a cloud server via WebSocket or MQTT+UDP, performs wake-word detection locally (ESP-SR), and drives OLED/LCD displays with emotion animations. Supports 70+ hardware boards, multiple languages, OTA updates, and MCP-based device control. Licensed under MIT.

- **Platform**: ESP-IDF v5.4+ (C++17 with exceptions & RTTI enabled)
- **Chip targets**: ESP32, ESP32-C3, ESP32-C5, ESP32-C6, ESP32-S3, ESP32-P4
- **Code style**: Google C++ style
- **Project version**: 2.0.5

## Build Commands

```bash
# One-time setup: set target chip (esp32, esp32s3, esp32c3, esp32p4, etc.)
idf.py set-target esp32s3

# Configure board type via Kconfig (selects CONFIG_BOARD_TYPE_*)
idf.py menuconfig
# Navigate to "Xiaozhi Application" → select your board type

# Build
idf.py build

# Flash to device (single command)
idf.py flash

# Flash + open serial monitor
idf.py flash monitor

# Clean build
idf.py fullclean build

# Generate default assets only (without full build)
python scripts/build_default_assets.py --sdkconfig sdkconfig --output build/generated_assets.bin
```

The CMake build system in `main/CMakeLists.txt` auto-selects board source files, audio codecs, wake-word implementations, fonts, and language assets based on Kconfig values. Board-specific `config.h` defines pin mappings and hardware features for each board.

## Architecture

### Startup Flow

```
app_main()                          # main/main.cc
  → esp_event_loop_create_default()
  → nvs_flash_init()
  → Application::GetInstance().Start()  # main/application.cc
      → Board::GetInstance()         # selects board via create_board()
      → board.StartNetwork()
      → Ota::CheckVersion()
      → AudioService::Start()        # 3 FreeRTOS tasks: input, output, opus codec
      → protocol_->Start()           # WebSocket or MQTT connection
      → MainEventLoop()              # event-group based state machine
```

### Core Classes

- **`Application`** (`main/application.h`): Central singleton managing the device state machine, event loop, and coordinating all subsystems. States: Starting → WifiConfiguring → Idle → Connecting → Listening → Speaking.
- **`Board`** (`main/boards/common/board.h`): Abstract base for all hardware. Each board in `main/boards/<board-name>/` implements `GetAudioCodec()`, `GetDisplay()`, `GetNetwork()`, etc. Selected at compile time via `CONFIG_BOARD_TYPE_*` and `DECLARE_BOARD()` macro.
- **`Protocol`** (`main/protocols/protocol.h`): Abstract base for server communication. Two implementations: `WebsocketProtocol` and `MqttProtocol`. Handles audio streaming (OPUS), JSON messages, v5.0+ ACK responses, status/event/log reporting.
- **`AudioService`** (`main/audio/audio_service.h`): Runs 3 FreeRTOS tasks: audio input (mic → processor → encode queue), audio output (decode queue → speaker), and OPUS codec (encode/decode). Supports server-side AEC, device-side AEC, wake-word detection, and audio testing modes.

### Audio Pipeline

```
[MIC] → AudioCodec → (optional)AudioProcessor → EncodeQueue → OpusEncoder → SendQueue → Protocol → Server
Server → Protocol → DecodeQueue → OpusDecoder → PlaybackQueue → AudioCodec → [Speaker]
```

Audio codecs (`main/audio/codecs/`) implement I2S/PDM microphone and speaker drivers per chip. Processors (`main/audio/processors/`) handle AEC/AFE. Wake words (`main/audio/wake_words/`) use ESP-SR for ESP32-S3/P4 or a basic implementation for other chips.

### Display System

Two display backends:
- **OLED** (`main/display/oled_display.cc`): SSD1306/SH1106 via I2C
- **LVGL** (`main/display/lvgl_display/`): Full GUI framework for LCD panels with emoji collections, GIF animation support, JPEG rendering, and themes

Emotion display is handled through `EmoteDisplay` / `LcdDisplay` with icon fonts and bitmap emoji sprites.

### Board Architecture

Each board directory contains:
- `<board>.cc` — board implementation inheriting from `Board` or `WifiBoard`/`Ml307Board`/`DualNetworkBoard`
- `config.h` — pin definitions, hardware features, audio/video caps
- Optional: custom audio codec, power manager, LCD driver

Base board types in `main/boards/common/`:
- `wifi_board.h` — Wi-Fi only boards
- `ml307_board.h` — 4G/Cat.1 cellular boards (ML307 module)
- `dual_network_board.h` — Wi-Fi + 4G dual-mode
- Helper classes: `button`, `backlight`, `camera`, `power_save_timer`, `sleep_timer`, etc.

### Lock Control Module (`main/lock_control/`)

Custom extension (active on `cat-eye-lock-feature-2` branch) for smart door lock integration:
- `lock_protocol.h` — 7-byte UART protocol (0xAA header + CAT + TYPE + 3 data bytes + checksum) at 9600 baud
- `lock_control.h` — `LockControlService` class managing ESP32 ↔ STM32 communication for unlock/lock, OLED icons, beeper, light control, fingerprint/NFC/password user management
- Events from STM32 are routed to `Application` via callback, which then sends protocol messages to the cloud server

### Key Files

| File | Purpose |
|---|---|
| `main/CMakeLists.txt` | Board selection, font/emoji config, asset embedding, partition flashing |
| `main/application.cc` | State machine, event handling, TTS/audio coordination, lock integration |
| `main/mcp_server.h` | Device-side MCP server for exposing GPIO/display/audio as MCP tools |
| `main/ota.cc` | OTA firmware upgrade and asset download logic |
| `main/assets.cc` | Asset loading from flash partition (fonts, emojis, sounds) |
| `main/settings.cc` | NVS-based persistent key-value storage |
| `main/system_info.cc` | System info (chip model, free heap, partition table) |
| `scripts/gen_lang.py` | Generates language header from JSON locale files with en-US fallback |
| `scripts/build_default_assets.py` | Builds default assets.bin from fonts, emoji collections, and SR model |
| `partitions/v2/16m.csv` | Default 16MB flash partition table |
| `sdkconfig.defaults` | Default Kconfig values (LVGL, mbedTLS, Wi-Fi, compiler options) |

### i18n

Language files are in `main/assets/locales/<lang-code>/language.json`. The `gen_lang.py` script merges the target language with en-US as fallback for missing strings. Audio prompts (`.ogg` files) follow the same fallback pattern. Build the language header with:

```bash
python scripts/gen_lang.py --language zh-CN --output main/assets/lang_config.h
```

### Adding a New Board

1. Create `main/boards/<board-name>/` with `<board>.cc` and `config.h`
2. Add `CONFIG_BOARD_TYPE_<NAME>` to `main/Kconfig.projbuild`
3. Add the corresponding `elseif()` block in `main/CMakeLists.txt` to set `BOARD_TYPE` and font/emoji defaults
4. Use `DECLARE_BOARD(ClassName)` in the `.cc` file

### Branch Context

Current branch `cat-eye-lock-feature-2` adds the smart lock (cat-eye door lock) integration with the lock_control module, v5.0+ protocol extensions (ACK, status/event/log reporting), and pending command queue management for STM32 communication reliability.
