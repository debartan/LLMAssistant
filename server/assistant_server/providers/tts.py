"""Text-to-speech providers.

A provider turns text into 16-bit mono PCM at the device sample rate (16 kHz).
To add one, subclass ``TTSProvider``, implement ``synthesize``, and add a branch
to ``build_tts``.
"""

from __future__ import annotations

import asyncio
import json
import logging
from pathlib import Path
from typing import Protocol

from ..audio import resample_pcm16
from ..config import Settings

log = logging.getLogger(__name__)


class TTSProvider(Protocol):
    name: str

    async def synthesize(self, text: str) -> bytes:
        """Return 16 kHz, 16-bit, mono PCM. An empty result means 'no audio'."""
        ...


class NullTTS:
    name = "none"

    async def synthesize(self, text: str) -> bytes:
        return b""


class OpenAITTS:
    name = "openai"
    source_rate = 24000  # the OpenAI pcm format is 24 kHz mono 16-bit

    def __init__(self, model: str, voice: str, target_rate: int) -> None:
        from openai import AsyncOpenAI

        self.model = model
        self.voice = voice
        self.target_rate = target_rate
        self.client = AsyncOpenAI()

    async def synthesize(self, text: str) -> bytes:
        response = await self.client.audio.speech.create(
            model=self.model,
            voice=self.voice,
            input=text,
            response_format="pcm",
        )
        pcm = response.content
        return resample_pcm16(pcm, self.source_rate, self.target_rate)


class PiperTTS:
    """Local synthesis through the ``piper`` command line tool."""

    name = "piper"

    def __init__(self, model_path: str, target_rate: int) -> None:
        if not model_path:
            raise ValueError("PIPER_MODEL must point at a .onnx voice file")
        self.model_path = model_path
        self.target_rate = target_rate
        self.source_rate = self._read_sample_rate(model_path)

    @staticmethod
    def _read_sample_rate(model_path: str) -> int:
        cfg = Path(model_path + ".json")
        if cfg.is_file():
            try:
                return int(json.loads(cfg.read_text())["audio"]["sample_rate"])
            except (KeyError, ValueError, json.JSONDecodeError):
                pass
        return 22050

    async def synthesize(self, text: str) -> bytes:
        proc = await asyncio.create_subprocess_exec(
            "piper", "--model", self.model_path, "--output-raw",
            stdin=asyncio.subprocess.PIPE,
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.DEVNULL,
        )
        pcm, _ = await proc.communicate(text.encode("utf-8"))
        return resample_pcm16(pcm, self.source_rate, self.target_rate)


def build_tts(settings: Settings) -> TTSProvider:
    kind = settings.tts_provider.lower()
    if kind == "openai":
        return OpenAITTS(settings.openai_tts_model, settings.openai_tts_voice, settings.sample_rate)
    if kind == "piper":
        return PiperTTS(settings.piper_model, settings.sample_rate)
    if kind in ("none", ""):
        log.warning("TTS_PROVIDER is 'none': replies will be shown on screen only")
        return NullTTS()
    raise ValueError(f"unknown TTS_PROVIDER {settings.tts_provider!r}")
