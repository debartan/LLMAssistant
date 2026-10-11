# Device to server protocol

One WebSocket connection per device. The device connects to the companion
server (default `ws://<server>:8765`) and keeps the socket open; it reconnects
with a back-off if the link drops.

## Frames

**Text frames** are UTF-8 JSON objects. Every object has a string `type`
field written as `prefix.verb`:

| Prefix | Owner | Meaning |
| --- | --- | --- |
| `sys` | framework | Connection-level housekeeping |
| `app` | framework | Opening and closing apps |
| anything else | the app with that id | Routed to the open app on the server, or to the active app on the device |

**Binary frames** are audio. Byte 0 is a format code; the remaining bytes are
samples.

| Code | Format |
| --- | --- |
| `0x01` | 16-bit signed little-endian PCM, mono, 16 kHz |

Both directions use the same binary format. The device sends microphone audio
in 20 millisecond frames (640 bytes plus the format byte). The server sends
speech in 100 millisecond frames, paced so that it never runs more than
about 1.5 seconds ahead of real time.

## Framework messages

| Direction | Type | Fields | Notes |
| --- | --- | --- | --- |
| device to server | `sys.hello` | `device`, `fw` | First message after connecting |
| server to device | `sys.hello_ack` | `server`, `apps[]`, `time`, `tz_offset_min` | `apps[]` is a list of `{id, description}`; `time` is Unix seconds |
| device to server | `sys.ping` | `t` | Optional keepalive |
| server to device | `sys.pong` | `t` | |
| server to device | `sys.error` | `message` | Something the framework rejected |
| server to device | `sys.audio_start` | `rate` | Speech is about to arrive |
| server to device | `sys.audio_end` | | No more speech for this turn |
| server to device | `sys.audio_flush` | | Drop any unplayed speech now |
| device to server | `app.open` | `app` | Creates the server-side app instance |
| server to device | `app.opened` | `app` | |
| device to server | `app.close` | | Destroys the server-side app instance |
| server to device | `app.closed` | `app` | |

The device sends `app.open` when the user enters an app and `app.close` when
they leave it. Binary audio frames are delivered to whichever app is open.

## Chat app (`chat.*`)

| Direction | Type | Fields | Notes |
| --- | --- | --- | --- |
| device to server | `chat.ptt_start` | | Start collecting microphone audio |
| device to server | `chat.ptt_stop` | | Stop collecting and run the turn |
| device to server | `chat.text` | `text` | Typed input; skips speech-to-text |
| device to server | `chat.cancel` | | Abort the current turn |
| device to server | `chat.reset` | | Forget the conversation |
| server to device | `chat.status` | `text` | One line for the bottom of the screen |
| server to device | `chat.transcript` | `text` | What speech-to-text heard |
| server to device | `chat.delta` | `text` | A piece of Claude's reply; append it |
| server to device | `chat.done` | | The turn is finished |
| server to device | `chat.error` | `message` | The turn failed |

A turn is: `ptt_start`, audio frames, `ptt_stop`, then `status`,
`transcript`, many `delta`, `sys.audio_start`, audio frames, `sys.audio_end`,
`done`.

## Notes app (`notes.*`)

| Direction | Type | Fields | Notes |
| --- | --- | --- | --- |
| device to server | `notes.start` | | Begin a recording |
| device to server | `notes.stop` | | End it, transcribe the tail, summarise, save |
| device to server | `notes.list` | `limit` | Most recent first |
| device to server | `notes.get` | `id` | |
| server to device | `notes.started` | `at` | Unix seconds |
| server to device | `notes.status` | `text` | |
| server to device | `notes.line` | `text` | A transcribed window, in order |
| server to device | `notes.summary` | `id`, `title`, `date`, `duration_seconds`, `summary`, `action_items[]` | Sent once after `stop` |
| server to device | `notes.list` | `notes[]` | Each is `{id, title, date}` |
| server to device | `notes.note` | same as `summary` | Answer to `get` |
| server to device | `notes.error` | `message` | |

## Adding messages for a new app

Pick an app id (`timer`), prefix every message with it (`timer.start`,
`timer.tick`), and document them here. The framework on both sides routes by
prefix, so nothing else needs to change.
