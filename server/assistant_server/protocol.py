"""Wire format shared with the firmware.

Text frames are JSON objects with a ``type`` field such as ``"chat.ptt_start"``.
The part before the first dot names the app (or ``sys`` / ``app`` for the
framework). Binary frames carry audio: byte 0 is a format code, the rest is
samples.
"""

from __future__ import annotations

import json
from typing import Any

# Binary frame format codes.
AUDIO_PCM16_16K = 0x01  # 16-bit little-endian signed PCM, mono, 16 kHz

SAMPLE_RATE = 16000
BYTES_PER_SAMPLE = 2


class ProtocolError(ValueError):
    pass


def make(msg_type: str, **fields: Any) -> str:
    """Build a JSON text frame."""
    payload = {"type": msg_type}
    payload.update(fields)
    return json.dumps(payload, ensure_ascii=False, separators=(",", ":"))


def parse(text: str) -> dict[str, Any]:
    """Parse a JSON text frame, requiring a string ``type``."""
    try:
        msg = json.loads(text)
    except json.JSONDecodeError as exc:
        raise ProtocolError(f"invalid JSON: {exc}") from exc
    if not isinstance(msg, dict) or not isinstance(msg.get("type"), str):
        raise ProtocolError("message must be an object with a string 'type'")
    return msg


def split_type(msg_type: str) -> tuple[str, str]:
    """``"chat.ptt_start"`` -> ``("chat", "ptt_start")``."""
    prefix, dot, verb = msg_type.partition(".")
    if not dot or not prefix or not verb:
        raise ProtocolError(f"type must look like 'app.verb', got {msg_type!r}")
    return prefix, verb


def encode_audio(pcm: bytes, fmt: int = AUDIO_PCM16_16K) -> bytes:
    return bytes((fmt,)) + pcm


def decode_audio(frame: bytes) -> tuple[int, bytes]:
    if not frame:
        raise ProtocolError("empty binary frame")
    return frame[0], frame[1:]


def pcm_duration_seconds(pcm: bytes, sample_rate: int = SAMPLE_RATE) -> float:
    return len(pcm) / (BYTES_PER_SAMPLE * sample_rate)
