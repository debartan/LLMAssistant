"""Chat app: talk to Claude.

Device side: tap to start listening, tap again to stop. The server then
transcribes, streams Claude's answer as text deltas, and speaks it.
"""

from __future__ import annotations

import asyncio
from typing import Any

from ..protocol import pcm_duration_seconds
from ..text import take_sentences
from .base import App, register

SYSTEM_PROMPT = """You are a voice assistant living in a small pocket device with a tiny screen and a speaker.
Latency-sensitive; begin your visible answer immediately.
Your answers are read aloud, so write the way a helpful person speaks: short sentences, plain words, no markdown, no lists, no headings, no emoji.
Keep most answers under 60 words. Go longer only when the person clearly asks for detail.
If a question needs information you do not have, say so in one sentence.
"""

MAX_HISTORY_TURNS = 20
MIN_UTTERANCE_SECONDS = 0.3


@register
class ChatApp(App):
    name = "chat"
    description = "Talk with Claude"

    async def on_open(self) -> None:
        self.history: list[dict[str, Any]] = []
        self.buffer = bytearray()
        self.recording = False
        self.turn_task: asyncio.Task | None = None
        await self.status("Tap to talk")

    async def on_close(self) -> None:
        await self._cancel_turn()

    async def on_message(self, verb: str, msg: dict[str, Any]) -> None:
        if verb == "ptt_start":
            await self._cancel_turn()
            self.buffer = bytearray()
            self.recording = True
            await self.status("Listening")
        elif verb == "ptt_stop":
            if not self.recording:
                return
            self.recording = False
            pcm = bytes(self.buffer)
            self.buffer = bytearray()
            self.turn_task = asyncio.create_task(self._turn(pcm=pcm))
        elif verb == "text":
            # Typed input (test client, future keyboard). Skips speech-to-text.
            await self._cancel_turn()
            self.turn_task = asyncio.create_task(self._turn(text=str(msg.get("text", ""))))
        elif verb == "cancel":
            await self._cancel_turn()
            await self.status("Tap to talk")
        elif verb == "reset":
            await self._cancel_turn()
            self.history = []
            await self.status("New conversation")
        else:
            self.log.warning("unknown verb %s", verb)

    async def on_audio(self, pcm: bytes) -> None:
        if self.recording:
            self.buffer.extend(pcm)

    # ----------------------------------------------------------------- turn

    async def _cancel_turn(self) -> None:
        task, self.turn_task = self.turn_task, None
        if task and not task.done():
            task.cancel()
            try:
                await task
            except (asyncio.CancelledError, Exception):
                pass
            await self.session.audio_flush()

    async def _turn(self, *, pcm: bytes = b"", text: str = "") -> None:
        try:
            if not text:
                text = await self._transcribe(pcm)
                if not text:
                    await self.send("done")
                    return
                await self.send("transcript", text=text)
            await self._answer(text)
        except asyncio.CancelledError:
            raise
        except Exception as exc:  # keep the session alive whatever happens
            self.log.exception("turn failed")
            await self.send("error", message=f"{type(exc).__name__}: {exc}"[:200])
            await self.send("done")

    async def _transcribe(self, pcm: bytes) -> str:
        if pcm_duration_seconds(pcm) < MIN_UTTERANCE_SECONDS:
            await self.status("Too short, try again")
            return ""
        if self.services.stt.name == "none":
            await self.status("No speech-to-text on server")
            return ""
        await self.status("Transcribing")
        text = await self.services.stt.transcribe(pcm, self.services.settings.sample_rate)
        if not text:
            await self.status("Did not catch that")
        return text

    async def _answer(self, user_text: str) -> None:
        await self.status("Thinking")
        self.history.append({"role": "user", "content": user_text})
        self.history = self.history[-2 * MAX_HISTORY_TURNS:]

        sentences: asyncio.Queue[str | None] = asyncio.Queue()
        speaker = asyncio.create_task(self._speak(sentences))
        reply_parts: list[str] = []
        pending = ""
        try:
            async for delta in self.services.llm.stream_chat(
                SYSTEM_PROMPT,
                self.history,
                effort=self.services.settings.chat_effort,
                max_tokens=1024,
            ):
                reply_parts.append(delta)
                await self.send("delta", text=delta)
                pending += delta
                done, pending = take_sentences(pending)
                for s in done:
                    await sentences.put(s)
            if pending.strip():
                await sentences.put(pending.strip())
        except asyncio.CancelledError:
            speaker.cancel()
            self._record_reply(reply_parts, interrupted=True)
            raise
        finally:
            await sentences.put(None)
            if not speaker.cancelled():
                try:
                    await speaker
                except asyncio.CancelledError:
                    pass
        self._record_reply(reply_parts)
        await self.send("done")
        await self.status("Tap to talk")

    def _record_reply(self, parts: list[str], interrupted: bool = False) -> None:
        """Keep the history well-formed: every user turn gets an assistant turn
        or is dropped. An interrupted reply is kept with a marker so Claude
        knows it was cut off."""
        reply = "".join(parts).strip()
        if reply:
            if interrupted:
                reply += " [interrupted]"
            self.history.append({"role": "assistant", "content": reply})
        elif self.history and self.history[-1]["role"] == "user":
            self.history.pop()

    async def _speak(self, sentences: "asyncio.Queue[str | None]") -> None:
        tts = self.services.tts
        if tts.name == "none":
            while await sentences.get() is not None:
                pass
            return
        started = False
        try:
            while True:
                sentence = await sentences.get()
                if sentence is None:
                    break
                pcm = await tts.synthesize(sentence)
                if not pcm:
                    continue
                if not started:
                    await self.session.audio_begin()
                    started = True
                await self.session.audio_send(pcm)
        finally:
            if started:
                await self.session.audio_end()
