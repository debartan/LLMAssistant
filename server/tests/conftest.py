"""Fakes shared by the tests: no network, no API keys."""

from __future__ import annotations

import asyncio
import json
from typing import Any, AsyncIterator

import pytest

from assistant_server import apps  # noqa: F401  registers the built-in apps
from assistant_server.config import Settings
from assistant_server.notes_store import NotesStore
from assistant_server.services import Services


class FakeSTT:
    name = "fake"

    def __init__(self, text: str = "hello world") -> None:
        self.text = text
        self.calls: list[bytes] = []

    async def transcribe(self, pcm: bytes, sample_rate: int) -> str:
        self.calls.append(pcm)
        return self.text


class FakeTTS:
    name = "fake"

    def __init__(self, seconds_per_call: float = 0.05) -> None:
        self.seconds = seconds_per_call
        self.calls: list[str] = []

    async def synthesize(self, text: str) -> bytes:
        self.calls.append(text)
        return b"\0\0" * int(16000 * self.seconds)


class FakeLLM:
    def __init__(self, reply: str = "Hello there. How are you today?") -> None:
        self.reply = reply
        self.chat_calls: list[list[dict[str, Any]]] = []
        self.json_calls: list[str] = []

    async def stream_chat(self, system, messages, *, effort, max_tokens) -> AsyncIterator[str]:
        self.chat_calls.append([dict(m) for m in messages])
        for word in self.reply.split(" "):
            yield word + " "
            await asyncio.sleep(0)

    async def complete_json(self, system, user_text, schema, *, effort, max_tokens) -> dict:
        self.json_calls.append(user_text)
        return {
            "title": "Weekly sync",
            "summary": "We discussed the roadmap.",
            "action_items": ["Alice: send the deck", "Bob: book the room"],
        }


class FakeSocket:
    """Looks enough like a websockets connection for Session.run()."""

    def __init__(self, incoming: list[str | bytes | float]) -> None:
        self.incoming = list(incoming)
        self.sent: list[str | bytes] = []

    async def send(self, data: str | bytes) -> None:
        self.sent.append(data)

    def __aiter__(self):
        return self

    async def __anext__(self):
        if not self.incoming:
            # give background tasks a chance to finish before the session ends
            for _ in range(50):
                await asyncio.sleep(0.01)
            raise StopAsyncIteration
        item = self.incoming.pop(0)
        if isinstance(item, float):  # a pause, in seconds
            await asyncio.sleep(item)
            return await self.__anext__()
        await asyncio.sleep(0)
        return item

    def json_messages(self) -> list[dict[str, Any]]:
        return [json.loads(s) for s in self.sent if isinstance(s, str)]

    def audio_frames(self) -> list[bytes]:
        return [b for b in self.sent if isinstance(b, bytes)]


@pytest.fixture
def services(tmp_path) -> Services:
    settings = Settings()
    settings.notes_dir = str(tmp_path / "notes")
    settings.notes_window_seconds = 1.0
    return Services(
        settings=settings,
        stt=FakeSTT(),
        tts=FakeTTS(),
        llm=FakeLLM(),
        notes=NotesStore(settings.notes_dir),
    )
