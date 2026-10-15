# Bring-up handoff (2026-10-16)

Where the project stands after the first session on real hardware, written so
another agent (or person) can continue without the chat history. Read this
after `CLAUDE.md`. Machine-specific paths refer to the Windows laptop the
board was flashed from; the hardware facts apply to any copy of the board.

## 1. What works, what does not

Verified on the device (Waveshare ESP32-S3-Touch-AMOLED-1.8, revision 1:
SH8601 panel, FT3168 touch):

| Area | State |
| --- | --- |
| Build | `idf.py build` clean, no warnings from `firmware/main`, ESP-IDF 5.5.5, BSP 2.0.3, LVGL 9.6.0 |
| Boot | Reliable. Launcher appears about 1.5 s after reset |
| Display | Works through the BSP's CO5300 driver |
| Touch | Works once the controller is reset through the IO expander at boot (see section 3). A tap on the Chat tile opened the app |
| Audio codec | Initialises (`codec ready at 16000 Hz`). Microphone capture and speaker playback not yet exercised |
| Wi-Fi | Joins a 2.4 GHz WPA2 network; the radio cannot see 5 GHz networks |
| Link | WebSocket connects, `sys.hello` / `sys.hello_ack` exchanged, reconnects after a server restart |
| Server | Runs with local speech-to-text (faster-whisper, CPU) and local text-to-speech (piper); 19 tests pass |
| Chat with Claude | **Not done.** Blocked on the authentication decision in section 6 |
| Notes app, speech pacing, large-message reassembly | Not exercised on hardware yet |

Uncommitted changes in the working tree when this was written (commit them
or review them first):

- `firmware/main/board/board.c`, `board.h`: boot-time I2C scan and PMU
  register dump (INFO logs), touch reset through expander pin EXIO2, a
  touch-tolerant display start that replaces `bsp_display_start()`,
  `board_touch_available()`, `board_pwr_button_pressed()`.
- `firmware/main/link/link.c`: one Wi-Fi scan at boot logged with channel,
  RSSI and auth mode; disconnect reason codes logged.
- `firmware/sdkconfig.defaults`: console on USB-Serial-JTAG; removed an
  unknown mbedTLS symbol.
- `server/assistant_server/config.py`, `providers/stt.py`, `providers/tts.py`,
  `.env.example`: `WHISPER_DEVICE` (default `cpu`) and `PIPER_BIN` settings;
  sign-in notes.

## 2. The UI work that was requested and not started

The user's notes, in their words, to be done next:

1. The device has two physical buttons (top and bottom). The UI should
   scroll vertically and the buttons should move the selection up and down.
   Icons can go; text-only menus are fine.
2. The screen has rounded corners and content gets clipped by them. Keep
   content inside a safe area.
3. The current LVGL screens do not look like MonoUI. Redo the UI "using
   MonoUI directly if possible".

Findings for item 3:

- "MonoUI" is the UI of Peng Zhihui's HelloWord keyboard Dynamic module. Its
  source was never published: the repo's IDE file points at a private folder
  (`3rdParty/MonoUI`) and the committed Dynamic firmware has no UI code.
- The Swift project the user remembered is `CmST0us/MonoUI`, a reimplementation
  for Embedded Swift on the RISC-V ESP32-C6. It cannot run on this board's
  Xtensa ESP32-S3.
- The closest portable reimplementation is `Sheep118/WouoUI-PageVersion`
  (pure C99, MPL-2.0, screen size set by two macros, 1-bit framebuffer with a
  single "send buffer" callback, message queue with up/down/click/return,
  list and tile pages, message/confirm/value/spin/list popups, the gliding
  selection box). A copy of its `Csource` directory was downloaded to
  `C:\esp\work\wououi_src` for evaluation. The plan sketched but not
  started: render its framebuffer at 2x into an RGB565 strip and push it
  with `esp_lcd_panel_draw_bitmap` inside a safe area inset from the rounded
  corners, drop LVGL, build the launcher and apps on its list and popup
  pages, and drive it from the two buttons plus touch taps.
- Button facts for item 1: BOOT is GPIO0 (low = pressed, already read by
  `board_button_pressed()`); PWR is expander pin EXIO4 (high = pressed,
  `board_pwr_button_pressed()`). Holding PWR for 6 s powers the board off,
  so do not use a long hold of PWR for anything.

## 3. Hardware facts learned the hard way

- **Touch reset.** After a chip reset that interrupts a touch read, the
  FT3168 stays silent on I2C and the BSP's `bsp_display_start()` aborts the
  boot with "Touch not found". Its reset line is TCA9554 pin **EXIO2**; a
  20 ms low pulse wakes it. `board.c` pulses it before the display starts.
- Do **not** call `i2c_master_bus_reset()`: it hung the main task on the
  next I2C transaction.
- `CONFIG_BSP_ERROR_CHECK=n` does not compile (BSP bug in
  `bsp_io_expander_init`). That is why `board.c` reproduces the BSP's display
  start with its public functions instead of turning error checks off.
- Expander state left by the factory firmware: config `0x78` (EXIO0, 1, 2, 7
  outputs high; 3, 4, 5, 6 inputs). EXIO2 touch reset, EXIO4 PWR button,
  EXIO7 SD card chip select. The BSP never touches the expander.
- I2C bus: 0x18 ES8311, 0x20 TCA9554, 0x34 AXP2101, 0x38 FT3168,
  0x51 PCF85063, 0x6b QMI8658. `bsp_i2c_init()` is idempotent.
- AXP2101 rails as left by the factory firmware (they persist across resets
  but not across a power loss): registers 0x90 = 0xFF and 0x91 = 0x01 (all
  LDOs on), ALDO1/2 3.3 V, ALDO3 3.0 V, ALDO4 1.8 V, BLDO1 1.2 V,
  BLDO2 2.8 V. The display came up with these; if a cold board ever boots
  dark, set them in `pmu_init()`. No battery is fitted.
- The ES8311 power-amplifier enable is a real GPIO (46). The factory
  firmware opens the codec as two devices with stereo 16-bit slots; ours
  opens one full-duplex device with mono slots (untested for sound).
- Flash: WinBond 16 MB, QIO at 80 MHz; PSRAM: 8 MB octal at 80 MHz. The
  settings in `sdkconfig.defaults` match the factory image.
- The console is the native USB port (USB-Serial-JTAG). esptool's automatic
  reset into the bootloader works; nobody needs to hold BOOT.

## 4. Network

- The ESP32-S3 radio is 2.4 GHz only. The user's "Flexo" network was 5 GHz
  only at first; once the user enabled 2.4 GHz on it the board joined on
  channel 6 at -22 dBm. The boot log prints the networks the device sees, so
  a failing join (reason 201 = no AP found) is diagnosable from the log.
- Windows Mobile Hotspot on this laptop did not help: on the single-radio
  adapter it would not come up on 2.4 GHz while the laptop was on a 5 GHz
  network. Scripts remain in `C:\esp\work\hotspot*.ps1` if that changes.
- Credentials and the server URI live in `firmware/sdkconfig` (ignored by
  git). `C:\esp\work\apply_config.ps1 -Ssid <ssid> -Uri ws://<ip>:8765`
  pulls the password from the Windows Wi-Fi profile store and writes them.
- Windows Firewall has an inbound allow rule for TCP 8765 ("Pocket
  assistant server (TCP 8765)"), added with elevation.

## 5. Dev machine layout (this laptop)

| Item | Location |
| --- | --- |
| ESP-IDF v5.5.5 | `C:\esp\esp-idf` (shallow clone); tools in `C:\esp\tools` |
| Build dir | `C:\esp\build\llm_assistant` (kept outside OneDrive on purpose) |
| Build/flash wrapper | `C:\esp\work\idf.ps1 <idf.py args>`; use `--port COM17`, not `-p` |
| Serial capture | `python C:\esp\work\serial_log.py COM17 out.log --seconds 45 --reset` |
| Board port | COM17 (Espressif USB-Serial-JTAG, VID 303A PID 1001) |
| Server venv | `server\.venv` (anthropic 1.15.0, websockets 17.2, faster-whisper) |
| piper | `C:\esp\bin\piper\piper.exe`, voice `C:\esp\bin\piper\voices\en_US-lessac-medium.onnx` |
| Anthropic CLI | `C:\esp\bin\ant.exe` (v1.42.0), not signed in |
| WouoUI sources | `C:\esp\work\wououi_src\Csource` |
| Boot logs from this session | `C:\esp\work\boot*.log` (boot11.log is the successful join) |

`server/.env` (ignored by git) is set to `STT_PROVIDER=local`,
`WHISPER_MODEL=base`, `WHISPER_DEVICE=cpu`, `TTS_PROVIDER=piper`, the piper
paths above, and no `ANTHROPIC_API_KEY` line. Start the server with
`server\.venv\Scripts\python.exe -m assistant_server.main` from `server/`.

Measured speech performance on this laptop (22 logical CPUs): piper makes
2 s of speech in 0.5 s; whisper `base` loads in 1.4 s and transcribes a 2 s
utterance in 0.9 s with an exact result.

## 6. Open decision: how the server authenticates to Claude

The user does **not** want to use an API key, and also declined the
Anthropic CLI sign-in (`ant auth login`), because it signs into the Claude
Platform (Console) organisation, which they consider the same thing as an
API key account. The server's `llm/claude.py` calls the Messages API through
the Anthropic SDK, which only accepts an API key, an auth token, or that
Console OAuth profile.

Options a future agent can check with the user:

- Use a Console organisation after all (key or `ant auth login`); everything
  else is ready and this is a two-minute change.
- Route the model calls through something that can use the user's Claude
  subscription login instead, for example the Claude Agent SDK, which honours
  Claude Code's own login. This changes `llm/claude.py` substantially and
  the terms for that use should be checked first.
- Put a different LLM behind the `LLMClient` protocol in `llm/claude.py`
  (two methods: `stream_chat`, `complete_json`).

Until one of these is done, a chat turn on the device gets as far as the
transcript and then reports an authentication error.

## 7. Suggested order for the next session

1. Decide section 6 and run one real chat turn from the device; watch the
   server log and `boot*.log` style serial capture together.
2. Exercise audio on hardware: microphone level on the Chat screen, speaker
   output, then compare the mono-slot codec setup with the factory
   firmware's stereo-slot setup if either direction is silent or distorted.
3. The UI work in section 2, buttons first (they do not depend on the UI
   framework decision), then the safe area, then the MonoUI port.
4. Commit in small steps; `firmware/sdkconfig` and `server/.env` must stay
   out of git.
