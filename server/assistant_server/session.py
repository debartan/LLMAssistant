"""One Session per connected device.

Routes framework messages (``sys.*``, ``app.*``) itself and hands app
messages and audio to the open app. Also paces outgoing speech so the device's
small playback buffer never overflows.
"""

from __future__ import annotations

import asyncio
import logging
import time
from datetime import datetime
from typing import Any, Protocol

from . import __version__, protocol
from .apps.base import REGISTRY, App
from .protocol import ProtocolError
from .services import Services

log = logging.getLogger(__name__)

AUDIO_CHUNK_MS = 100
AUDIO_PREROLL_MS = 1500   # how far ahead of real time we let playback run


class Socket(Protocol):
    async def send(self, data: str | bytes) -> None: ...
    def __aiter__(self) -> Any: ...


class Session:
    def __init__(self, ws: Socket, services: Services) -> None:
        self.ws = ws
        self.services = services
        self.device_name = "unknown"
        self.app: App | None = None
        self._send_lock = asyncio.Lock()
        self._play_until = 0.0

    # -------------------------------------------------------------- lifecycle

    async def run(self) -> None:
        try:
            async for frame in self.ws:
                if isinstance(frame, (bytes, bytearray, memoryview)):
                    await self._on_binary(bytes(frame))
                else:
                    await self._on_text(str(frame))
        finally:
            await self._close_app()
            log.info("session for %s ended", self.device_name)

    # ------------------------------------------------------------- outbound

    async def send_json(self, msg_type: str, **fields: Any) -> None:
        async with self._send_lock:
            await self.ws.send(protocol.make(msg_type, **fields))

    async def send_audio(self, pcm: bytes) -> None:
        async with self._send_lock:
            await self.ws.send(protocol.encode_audio(pcm))

    async def audio_begin(self) -> None:
        self._play_until = time.monotonic()
        await self.send_json("sys.audio_start", rate=self.services.settings.sample_rate)

    async def audio_send(self, pcm: bytes) -> None:
        """Send PCM in small chunks, no faster than the device can play it."""
        rate = self.services.settings.sample_rate
        chunk_bytes = rate * protocol.BYTES_PER_SAMPLE * AUDIO_CHUNK_MS // 1000
        for start in range(0, len(pcm), chunk_bytes):
            chunk = pcm[start:start + chunk_bytes]
            now = time.monotonic()
            ahead = self._play_until - now
            if ahead > AUDIO_PREROLL_MS / 1000:
                await asyncio.sleep(ahead - AUDIO_PREROLL_MS / 1000)
                now = time.monotonic()
            await self.send_audio(chunk)
            self._play_until = max(self._play_until, now) + protocol.pcm_duration_seconds(chunk, rate)

    async def audio_end(self) -> None:
        await self.send_json("sys.audio_end")

    async def audio_flush(self) -> None:
        """Tell the device to drop any speech it has not played yet."""
        self._play_until = 0.0
        await self.send_json("sys.audio_flush")

    # -------------------------------------------------------------- inbound

    async def _on_text(self, text: str) -> None:
        try:
            msg = protocol.parse(text)
            prefix, verb = protocol.split_type(msg["type"])
        except ProtocolError as exc:
            await self.send_json("sys.error", message=str(exc))
            return

        if prefix == "sys":
            await self._on_sys(verb, msg)
        elif prefix == "app":
            await self._on_app(verb, msg)
        elif self.app is not None and prefix == self.app.name:
            try:
                await self.app.on_message(verb, msg)
            except Exception as exc:
                log.exception("app %s failed on %s", prefix, verb)
                await self.send_json("sys.error", message=f"{prefix}.{verb}: {exc}"[:200])
        else:
            await self.send_json("sys.error", message=f"no open app for {msg['type']}")

    async def _on_binary(self, frame: bytes) -> None:
        try:
            fmt, pcm = protocol.decode_audio(frame)
        except ProtocolError as exc:
            await self.send_json("sys.error", message=str(exc))
            return
        if fmt != protocol.AUDIO_PCM16_16K:
            await self.send_json("sys.error", message=f"unsupported audio format {fmt}")
            return
        if self.app is not None:
            await self.app.on_audio(pcm)

    async def _on_sys(self, verb: str, msg: dict[str, Any]) -> None:
        if verb == "hello":
            self.device_name = str(msg.get("device", "unknown"))
            log.info("hello from %s (firmware %s)", self.device_name, msg.get("fw", "?"))
            now = datetime.now().astimezone()
            await self.send_json(
                "sys.hello_ack",
                server=f"assistant-server {__version__}",
                apps=[{"id": name, "description": cls.description} for name, cls in REGISTRY.items()],
                time=int(now.timestamp()),
                tz_offset_min=int((now.utcoffset() or 0).total_seconds() // 60) if now.utcoffset() else 0,
            )
        elif verb == "ping":
            await self.send_json("sys.pong", t=msg.get("t"))
        else:
            await self.send_json("sys.error", message=f"unknown sys verb {verb}")

    async def _on_app(self, verb: str, msg: dict[str, Any]) -> None:
        if verb == "open":
            app_id = str(msg.get("app", ""))
            cls = REGISTRY.get(app_id)
            if cls is None:
                await self.send_json("sys.error", message=f"unknown app {app_id!r}")
                return
            await self._close_app()
            self.app = cls(self)
            await self.app.on_open()
            await self.send_json("app.opened", app=app_id)
        elif verb == "close":
            closed = self.app.name if self.app else None
            await self._close_app()
            await self.send_json("app.closed", app=closed)
        else:
            await self.send_json("sys.error", message=f"unknown app verb {verb}")

    async def _close_app(self) -> None:
        app, self.app = self.app, None
        if app is not None:
            try:
                await app.on_close()
            except Exception:
                log.exception("app %s failed to close", app.name)
