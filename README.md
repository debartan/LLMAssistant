# Pocket Claude

A small, grab-and-go voice assistant. The hardware is a Waveshare
ESP32-S3 board with a 1.8 inch AMOLED touch screen, microphone, speaker and
battery connector. The screen uses a MonoUI style: black background, white
pixel graphics, and a selection frame that glides between items.

Two apps ship today:

- **Chat**: tap, ask Claude something, tap again. The answer streams to the
  screen and is read aloud.
- **Notes**: record a meeting. The transcript appears live; when you stop,
  Claude writes a title, summary and action items, and the note is saved as
  a markdown file.

Adding a third app is one C file and one Python file. See
[docs/ADDING_AN_APP.md](docs/ADDING_AN_APP.md).

## How it is built

```
┌────────────────────────┐   WebSocket (JSON + PCM audio)   ┌───────────────────────────┐
│ Device (ESP32-S3)      │ ───────────────────────────────▶ │ Companion server (Python) │
│ screen, touch, mic,    │ ◀─────────────────────────────── │ speech-to-text, Claude,   │
│ speaker, battery       │                                  │ text-to-speech, notes     │
└────────────────────────┘                                  └─────────────┬─────────────┘
                                                                          │ HTTPS
                                                                          ▼
                                                                     Claude API
```

The device is deliberately thin. It draws the user interface, streams the
microphone, and plays what it is sent. Everything that needs an API key or a
real computer runs on the companion server, which can be a laptop on the same
Wi-Fi, a Raspberry Pi, or a cloud machine. The full explanation, including
how a request flows through the system, is in
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md). The wire format is in
[docs/PROTOCOL.md](docs/PROTOCOL.md).

## Hardware

| Item | Notes |
| --- | --- |
| Waveshare ESP32-S3-Touch-AMOLED-1.8 | Either revision works: the board support package detects the touch controller at boot |
| 3.7 V lithium battery with MX1.25 2-pin plug | Optional; the board runs from USB-C without one |
| USB-C data cable | For flashing and logs |

## Repository layout

```
firmware/    ESP-IDF project for the device
server/      Python companion server
docs/        architecture, protocol, how to add an app
```

## Quick start

### 1. Run the server

Requires Python 3.11 or newer.

```bash
cd server
python3 -m venv .venv && source .venv/bin/activate
pip install -e ".[openai,dev]"
cp .env.example .env        # then edit: ANTHROPIC_API_KEY, STT_PROVIDER, TTS_PROVIDER, keys
assistant-server
```

The server prints the address it listens on (default port 8765). Speech-to-text
and text-to-speech are pluggable:

| Setting | Options | Extra install |
| --- | --- | --- |
| `STT_PROVIDER` | `openai` (Whisper API), `local` (faster-whisper on the server's own CPU), `none` | `pip install -e ".[openai]"` or `".[local]"` |
| `TTS_PROVIDER` | `openai`, `piper` (local, needs the `piper` command and a voice file), `none` | same |

With both set to `none` the chat app still works for typed input, which is
handy for checking the Claude side before the device is flashed:

```bash
python tools/test_client.py ws://localhost:8765 chat
> what's the tallest mountain in Europe?
```

Run the tests with `pytest`. They use fakes and need no keys.

### 2. Flash the device

Install [ESP-IDF 5.5](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/get-started/index.html)
and open a terminal with it exported. Then:

```bash
cd firmware
idf.py set-target esp32s3
idf.py menuconfig          # LLM Assistant -> Wi-Fi SSID, password, server URI
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

The first build downloads the board support package and LVGL from the
Espressif component registry. The server URI looks like
`ws://192.168.1.20:8765` (the machine running the server) or
`wss://host/path` when it sits behind a TLS proxy.

If the display stays dark after flashing, build and run Waveshare's own
`13_display_colorbar` example from their repository first to rule out the
hardware.

### 3. Use it

- The launcher shows one app at a time. Swipe left or right to move, tap the
  centre icon to open.
- Swipe right, or press the BOOT button, to go back.
- **Chat**: tap to listen, tap to send. Long press starts a new conversation.
  Swipe up and down to scroll a long answer.
- **Notes**: tap "+ New recording"; tap again to stop. Saved notes are listed
  below it; tap one to read its summary.
- **Settings**: tap a row to change brightness or volume.

The top bar shows the link state (`----` no Wi-Fi, `WIFI` joined,
`LINK` talking to the server), the time from the server, and battery.

## Status

The firmware compiles cleanly against ESP-IDF 5.5 and the board support
package version 2.0.3, and the server passes its tests, but this code has not
yet been run on the physical board. Expect to tune the microphone gain, the
speaker volume and the swipe threshold after the first flash. Those live in
`idf.py menuconfig` under LLM Assistant, and in `firmware/main/apps/app_manager.c`.

## Where to look next

- `firmware/main/apps/app_settings.c` is the smallest app and the best
  template.
- `firmware/main/ui/` holds the MonoUI widgets if you want to change the look.
- `server/assistant_server/apps/chat.py` shows the full voice pipeline in
  about 150 lines.
- `server/assistant_server/llm/claude.py` is the only file that talks to the
  Claude API.
