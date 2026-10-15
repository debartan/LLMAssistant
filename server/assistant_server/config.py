"""Settings, read from environment variables (and an optional .env file)."""

from __future__ import annotations

import os
from dataclasses import dataclass, field
from pathlib import Path


def load_dotenv(path: str | os.PathLike = ".env") -> None:
    """Load KEY=VALUE lines into os.environ without overriding existing values.

    Kept tiny on purpose so the server has no dependency for this.
    """
    p = Path(path)
    if not p.is_file():
        return
    for raw in p.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, _, value = line.partition("=")
        key = key.strip()
        value = value.strip().strip('"').strip("'")
        if key and key not in os.environ:
            os.environ[key] = value


def _env(name: str, default: str) -> str:
    return os.environ.get(name, default)


@dataclass
class Settings:
    host: str = field(default_factory=lambda: _env("HOST", "0.0.0.0"))
    port: int = field(default_factory=lambda: int(_env("PORT", "8765")))

    claude_model: str = field(default_factory=lambda: _env("CLAUDE_MODEL", "claude-opus-5-5"))
    chat_effort: str = field(default_factory=lambda: _env("CHAT_EFFORT", "low"))
    notes_effort: str = field(default_factory=lambda: _env("NOTES_EFFORT", "medium"))

    stt_provider: str = field(default_factory=lambda: _env("STT_PROVIDER", "none"))
    tts_provider: str = field(default_factory=lambda: _env("TTS_PROVIDER", "none"))

    openai_stt_model: str = field(default_factory=lambda: _env("OPENAI_STT_MODEL", "whisper-1"))
    openai_tts_model: str = field(default_factory=lambda: _env("OPENAI_TTS_MODEL", "tts-1"))
    openai_tts_voice: str = field(default_factory=lambda: _env("OPENAI_TTS_VOICE", "alloy"))
    whisper_model: str = field(default_factory=lambda: _env("WHISPER_MODEL", "base"))
    piper_model: str = field(default_factory=lambda: _env("PIPER_MODEL", ""))

    notes_dir: str = field(default_factory=lambda: _env("NOTES_DIR", "./notes"))

    # Audio format shared with the device. The firmware is built for this rate.
    sample_rate: int = 16000

    # How many seconds of meeting audio to transcribe at a time.
    notes_window_seconds: float = field(
        default_factory=lambda: float(_env("NOTES_WINDOW_SECONDS", "20"))
    )
