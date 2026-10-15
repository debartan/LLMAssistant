"""The server half of an app.

An app instance lives for as long as the device has it open. It receives the
JSON messages whose ``type`` starts with its name, and the raw microphone
audio while it is open. It talks back through ``send`` (JSON) and the
session's audio helpers.
"""

from __future__ import annotations

import logging
from typing import TYPE_CHECKING, Any, ClassVar

if TYPE_CHECKING:  # avoid an import cycle at runtime
    from ..session import Session

log = logging.getLogger(__name__)


class App:
    #: Message prefix and the id the device uses to open the app, e.g. "chat".
    name: ClassVar[str] = ""
    #: One line shown in logs and sent to the device in hello_ack.
    description: ClassVar[str] = ""

    def __init__(self, session: "Session") -> None:
        self.session = session
        self.services = session.services
        self.log = logging.getLogger(f"app.{self.name}")

    async def on_open(self) -> None:
        """Called once after the device opens the app."""

    async def on_message(self, verb: str, msg: dict[str, Any]) -> None:
        """Called for every ``<name>.<verb>`` message."""

    async def on_audio(self, pcm: bytes) -> None:
        """Called for every microphone frame (16 kHz, 16-bit, mono)."""

    async def on_close(self) -> None:
        """Called when the device closes the app or disconnects."""

    async def send(self, verb: str, **fields: Any) -> None:
        await self.session.send_json(f"{self.name}.{verb}", **fields)

    async def status(self, text: str) -> None:
        """Convenience: a one-line status the device shows at the bottom."""
        await self.send("status", text=text)


REGISTRY: dict[str, type[App]] = {}


def register(cls: type[App]) -> type[App]:
    if not cls.name:
        raise ValueError(f"{cls.__name__} must set a name")
    if cls.name in REGISTRY:
        raise ValueError(f"app {cls.name!r} registered twice")
    REGISTRY[cls.name] = cls
    return cls
