# Pocket Claude: notes for Claude Code

A voice assistant device (Waveshare ESP32-S3-Touch-AMOLED-1.8) plus a Python
companion server. Read `README.md` for the overview, `docs/ARCHITECTURE.md`
for how the pieces connect, `docs/PROTOCOL.md` for the messages, and
`docs/ADDING_AN_APP.md` before adding a feature.

## Layout

```
firmware/   ESP-IDF 5.5 project (C). main/ holds board/, audio/, link/, ui/, apps/, assets/, fonts/
server/     Python 3.11+ package assistant_server/ with apps/, providers/, llm/; tests/ ; tools/test_client.py
docs/       architecture, protocol, adding an app
```

## Commands

Firmware (needs ESP-IDF 5.5 exported in the shell; the first build downloads
managed components from the Espressif registry, see `firmware/main/idf_component.yml`):

```bash
cd firmware
idf.py set-target esp32s3        # once
idf.py build                     # must finish with no warnings from firmware/main
idf.py -p /dev/ttyACM0 flash monitor
idf.py size                      # check internal RAM after memory-related changes
python3 tools/gen_icons.py       # after editing main/assets/icons/*.txt
python3 tools/gen_font2x.py managed_components/lvgl__lvgl/src/font/lv_font_unscii_16.c main/fonts/mono_font_32.c mono_font_32
```

Server:

```bash
cd server
python3 -m venv .venv && source .venv/bin/activate
pip install -e ".[dev]"          # add openai or local extras for real providers
pytest                           # fakes only, no keys or network
assistant-server                 # reads .env; see .env.example
python tools/test_client.py ws://localhost:8765 chat
```

Generated files are committed on purpose: `firmware/main/assets/icons.c`,
`icons.h`, `firmware/main/fonts/mono_font_32.c`, `firmware/dependencies.lock`.
Do not commit `firmware/build/`, `firmware/managed_components/`,
`firmware/sdkconfig`, or `server/.env`.

## Status

- Runs on the physical board as of 2026-10-16: boot, display, touch, codec
  init, Wi-Fi (2.4 GHz only), WebSocket link and the hello handshake all
  verified; the server runs with local speech-to-text and text-to-speech.
  A Claude chat turn has not been completed yet (authentication decision
  pending) and audio has not been heard yet. `docs/HANDOFF.md` has the full
  state, the hardware facts, the open decisions and the requested UI rework
  (button navigation, rounded-corner safe area, MonoUI look). Read it before
  touching the firmware.
- `firmware/sdkconfig` holds the Wi-Fi credentials and server URI and is
  ignored by git; `server/.env` likewise.

## Conventions

- Every app is two halves that share a message prefix: `firmware/main/apps/app_<id>.c`
  defining `app_t app_<id>`, registered in `app_registry.c` and `main/CMakeLists.txt`;
  `server/assistant_server/apps/<id>.py` with `@register`, imported in
  `apps/__init__.py`. Document new message types in `docs/PROTOCOL.md`.
- Keep the device dumb: draw, forward input, show results. Decisions, network
  calls and state live on the server.
- All device app callbacks run on the LVGL thread; never take `bsp_display_lock`
  inside them. Code outside LVGL (link, audio tasks) must not touch widgets;
  post to the app manager queue instead.
- Build screens with the `mono_*` helpers in `ui/mono.h`; they keep the MonoUI
  look (black background, white one-bit graphics, pixel fonts, eased
  animations) and keep widgets non-clickable. Large text is `MONO_FONT_LARGE`
  (16x32), body text is `MONO_FONT_SMALL` (8x16).
- Big buffers go in PSRAM (`heap_caps_calloc` with `MALLOC_CAP_SPIRAM`), not
  static `.bss`. Internal RAM is the scarce resource; the settings at the end of
  `firmware/sdkconfig.defaults` exist to keep Wi-Fi, TLS, LVGL and audio
  fitting. Do not revert them without re-checking `idf.py size`.
- Server providers implement one method (`transcribe` or `synthesize`) and are
  chosen by environment variable in `providers/stt.py` and `providers/tts.py`.
  Claude is called only from `llm/claude.py` (Anthropic SDK, streaming,
  structured output, server-side fallbacks on).
- Tests use the fakes in `server/tests/conftest.py`; keep new tests offline.

## Gotchas already hit

- The touch controller must be reset through IO expander pin EXIO2 before
  the display starts, or the BSP aborts the boot; `board.c` does this and
  starts the display with the BSP's public pieces so a silent touch is not
  fatal. Never call `i2c_master_bus_reset()` (hangs the next transaction).
  `CONFIG_BSP_ERROR_CHECK=n` does not compile with BSP 2.0.3.
- The radio is 2.4 GHz only; the boot log lists the networks the device sees.
- LVGL 9.6: `lv_obj_add_flag` / `lv_obj_remove_flag` are deprecated; use
  `lv_obj_set_hidden`, `lv_obj_set_clickable`, `lv_obj_set_scrollable`.
  The real headers are under `managed_components/lvgl__lvgl/include/`; the
  files under `src/` are redirect stubs, so grep `include/` when checking an API.
- The C library already defines `on_exit`; do not name a static function that.
- The board package drives both display revisions with the CO5300 driver and
  probes for the touch controller at boot; on ESP-IDF 5.5 it logs an expected
  I2C negative acknowledgement for the absent controller. Not a bug.
- The board package's `bsp_audio_init` keeps its I2S data interface private,
  which is why `audio/audio.c` creates the I2S channels itself.
- `esp_websocket_client` delivers a large message as several DATA events with
  increasing `payload_offset`; `op_code` 0 is a continuation frame.
