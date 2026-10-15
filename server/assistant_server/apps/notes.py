"""Notes app: record a meeting, see the transcript live, get a summary.

The device streams microphone audio for the whole recording. The server
transcribes it in windows so the user sees text appear while the meeting is
still running, then asks Claude for a title, summary and action items.
"""

from __future__ import annotations

import asyncio
import time
from datetime import datetime
from typing import Any

from ..notes_store import Note
from ..protocol import pcm_duration_seconds
from .base import App, register

SUMMARY_SYSTEM = """You turn raw meeting transcripts into concise notes.
The transcript comes from automatic speech recognition and may contain errors; infer the intended meaning.
Write the summary as short plain-text paragraphs. Action items are imperative sentences, one per item, with the owner's name first when it is clear.
"""

SUMMARY_SCHEMA = {
    "type": "object",
    "properties": {
        "title": {"type": "string", "description": "At most 6 words"},
        "summary": {"type": "string", "description": "2 to 6 sentences"},
        "action_items": {"type": "array", "items": {"type": "string"}},
    },
    "required": ["title", "summary", "action_items"],
    "additionalProperties": False,
}


@register
class NotesApp(App):
    name = "notes"
    description = "Record and summarise meetings"

    async def on_open(self) -> None:
        self.recording = False
        self.window = bytearray()
        self.lines: list[str] = []
        self.started_at = 0.0
        self.queue: asyncio.Queue[bytes | None] = asyncio.Queue()
        self.worker: asyncio.Task | None = None
        self.finisher: asyncio.Task | None = None

    async def on_close(self) -> None:
        for task in (self.worker, self.finisher):
            if task and not task.done():
                task.cancel()

    async def on_message(self, verb: str, msg: dict[str, Any]) -> None:
        if verb == "start":
            await self._start()
        elif verb == "stop":
            await self._stop()
        elif verb == "list":
            notes = self.services.notes.list(limit=int(msg.get("limit", 30)))
            await self.send("list", notes=[n.brief() for n in notes])
        elif verb == "get":
            note = self.services.notes.get(str(msg.get("id", "")))
            if note is None:
                await self.send("error", message="note not found")
            else:
                await self.send("note", **note.full())
        else:
            self.log.warning("unknown verb %s", verb)

    async def on_audio(self, pcm: bytes) -> None:
        if not self.recording:
            return
        self.window.extend(pcm)
        if pcm_duration_seconds(self.window) >= self.services.settings.notes_window_seconds:
            await self.queue.put(bytes(self.window))
            self.window = bytearray()

    # ------------------------------------------------------------ recording

    async def _start(self) -> None:
        if self.recording:
            return
        self.recording = True
        self.window = bytearray()
        self.lines = []
        self.started_at = time.time()
        self.queue = asyncio.Queue()
        self.worker = asyncio.create_task(self._transcriber())
        await self.send("started", at=int(self.started_at))
        await self.status("Recording")

    async def _stop(self) -> None:
        if not self.recording:
            return
        self.recording = False
        if self.window:
            await self.queue.put(bytes(self.window))
            self.window = bytearray()
        await self.queue.put(None)
        await self.status("Finishing transcript")
        self.finisher = asyncio.create_task(self._finish())

    async def _transcriber(self) -> None:
        stt = self.services.stt
        rate = self.services.settings.sample_rate
        while True:
            pcm = await self.queue.get()
            if pcm is None:
                return
            if stt.name == "none":
                continue
            try:
                text = await stt.transcribe(pcm, rate)
            except Exception as exc:
                self.log.exception("transcription failed")
                await self.send("error", message=f"transcription: {exc}"[:200])
                continue
            if text:
                self.lines.append(text)
                await self.send("line", text=text)

    async def _finish(self) -> None:
        try:
            if self.worker:
                await self.worker
            duration = int(time.time() - self.started_at)
            await self.status("Summarising")
            note = await self._summarise(duration)
            self.services.notes.save(note)
            await self.send("summary", **note.full())
            await self.status("Saved")
        except asyncio.CancelledError:
            raise
        except Exception as exc:
            self.log.exception("finishing note failed")
            await self.send("error", message=f"{type(exc).__name__}: {exc}"[:200])

    async def _summarise(self, duration: int) -> Note:
        transcript = "\n".join(self.lines).strip()
        when = datetime.fromtimestamp(self.started_at)
        if not transcript:
            return Note.new(when, duration, "Empty recording", "No speech was transcribed.", [], "")
        try:
            data = await self.services.llm.complete_json(
                SUMMARY_SYSTEM,
                f"Transcript recorded {when:%Y-%m-%d %H:%M}, {duration} seconds long:\n\n{transcript}",
                SUMMARY_SCHEMA,
                effort=self.services.settings.notes_effort,
                max_tokens=4096,
            )
            title = str(data.get("title") or "Untitled")
            summary = str(data.get("summary") or "")
            items = [str(x) for x in data.get("action_items", [])]
        except Exception:
            self.log.exception("summary failed; saving transcript only")
            title = " ".join(transcript.split()[:5]) or "Untitled"
            summary = transcript[:400]
            items = []
        return Note.new(when, duration, title, summary, items, transcript)
