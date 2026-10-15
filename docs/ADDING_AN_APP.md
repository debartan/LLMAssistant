# Adding an app

An app is two small pieces of code that agree on a few message types. This
walks through a "timer" app: the device shows a countdown and the server
keeps the authoritative clock and tells the device when it rings. (A timer
does not strictly need a server, but it shows every hook in the framework.)

## 1. Pick an id and the messages

The id is `timer`. It is the launcher id, the message prefix, and the name
on both sides.

| Direction | Type | Fields |
| --- | --- | --- |
| device to server | `timer.set` | `seconds` |
| device to server | `timer.cancel` | |
| server to device | `timer.tick` | `remaining` |
| server to device | `timer.done` | |

Add that table to [PROTOCOL.md](PROTOCOL.md).

## 2. The server half

Create `server/assistant_server/apps/timer.py`:

```python
import asyncio
from .base import App, register


@register
class TimerApp(App):
    name = "timer"
    description = "Countdown timer"

    async def on_open(self):
        self.task = None

    async def on_close(self):
        if self.task:
            self.task.cancel()

    async def on_message(self, verb, msg):
        if verb == "set":
            if self.task:
                self.task.cancel()
            self.task = asyncio.create_task(self._run(int(msg.get("seconds", 60))))
        elif verb == "cancel" and self.task:
            self.task.cancel()
            self.task = None

    async def _run(self, seconds):
        for remaining in range(seconds, 0, -1):
            await self.send("tick", remaining=remaining)
            await asyncio.sleep(1)
        await self.send("done")
```

Then import it in `server/assistant_server/apps/__init__.py`:

```python
from . import chat, notes, timer  # noqa: F401
```

That is the whole server side. `self.send("tick", ...)` becomes
`{"type": "timer.tick", "remaining": 59}` on the wire, and `on_audio` would
receive microphone frames if the app needed them.

Write a test in `server/tests/` using the fakes in `conftest.py`; the
existing `test_notes_record_and_summarise` is a good template.

## 3. The device half

Draw an icon: `firmware/main/assets/icons/timer.txt`, 24 by 24 characters,
`#` for a lit pixel. Then regenerate the bitmaps:

```bash
cd firmware && python3 tools/gen_icons.py
```

Create `firmware/main/apps/app_timer.c`:

```c
#include <string.h>
#include "apps/app.h"
#include "ui/mono.h"
#include "link/link.h"
#include "assets/icons.h"

static lv_obj_t *s_big;

static void on_enter(app_t *self, lv_obj_t *root)
{
    lv_obj_t *title = mono_label(root, "TIMER", MONO_FONT_SMALL);
    lv_obj_set_pos(title, MONO_PAD, 8);
    s_big = mono_label(root, "tap: 1 min", MONO_FONT_LARGE);
    lv_obj_align(s_big, LV_ALIGN_CENTER, 0, 0);
}

static bool on_input(app_t *self, const ui_input_t *ev)
{
    if (ev->type == UI_INPUT_TAP) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "type", "timer.set");
        cJSON_AddNumberToObject(o, "seconds", 60);
        link_send_json(o);
        cJSON_Delete(o);
        return true;
    }
    if (ev->type == UI_INPUT_LONG_PRESS) {
        link_send_type("timer.cancel");
        lv_label_set_text(s_big, "cancelled");
        return true;
    }
    return false;    /* BACK and swipe-right close the app */
}

static void on_message(app_t *self, const char *verb, const cJSON *msg)
{
    if (strcmp(verb, "tick") == 0) {
        int r = cJSON_GetObjectItem(msg, "remaining")->valueint;
        lv_label_set_text_fmt(s_big, "%02d:%02d", r / 60, r % 60);
    } else if (strcmp(verb, "done") == 0) {
        lv_label_set_text(s_big, "DONE");
        mono_toast("Timer finished");
    }
    lv_obj_align(s_big, LV_ALIGN_CENTER, 0, 0);
}

app_t app_timer = {
    .id = "timer",
    .title = "Timer",
    .icon = &icon_timer,
    .on_enter = on_enter,
    .on_input = on_input,
    .on_message = on_message,
};
```

Register it in `firmware/main/apps/app_registry.c`:

```c
extern app_t app_timer;
app_t *const app_registry[] = { &app_chat, &app_notes, &app_timer, &app_settings };
```

And add the file to `firmware/main/CMakeLists.txt`. Build and flash.

## What the framework gives you

On the device:

- `on_enter(self, root)` gets a black, full-content container. Build the
  screen with `mono_label`, `mono_paragraph`, `mono_frame`, `mono_fill`,
  `mono_container`, or the ready-made `mono_list`.
- `on_input` receives taps, swipes, long presses and the BOOT button. Return
  `false` for BACK or SWIPE_RIGHT and the manager closes the app.
- `on_message(self, verb, msg)` gets every `timer.*` message, already parsed.
- `on_tick` runs ten times a second while the app is open.
- `audio_capture_start()` streams the microphone to the server;
  `audio_playback_*` reports what the speaker is doing.
- `link_send_type`, `link_send_type_str`, `link_send_json` send messages.
- `mono_toast` shows a two-second message on any screen.
- Every callback runs on the LVGL thread, so no locking is needed.

On the server:

- `on_open`, `on_message`, `on_audio`, `on_close`, `send`, `status`.
- `self.services` holds `stt`, `tts`, `llm`, `notes` and `settings`.
- `self.session.audio_begin()`, `audio_send(pcm)`, `audio_end()`,
  `audio_flush()` play speech on the device, paced for its buffer.

## Rules of thumb

- Keep the device half dumb: draw, forward input, show results.
- Put decisions, network calls and state on the server.
- Use `status` messages for one-line progress; the device shows them.
- A new message type is only a dictionary key: no schema registry to edit.
