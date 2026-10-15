"""Speech-to-text providers.

A provider turns 16-bit mono PCM into text. To add one, subclass ``STTProvider``,
implement ``transcribe``, and add a branch to ``build_stt``.
"""

from __future__ import annotations

import asyncio
import logging
from typing import Protocol

from ..audio import pcm16_to_float32, pcm16_to_wav
from ..config import Settings

log = logging.getLogger(__name__)


class STTProvider(Protocol):
    name: str

    async def transcribe(self, pcm: bytes, sample_rate: int) -> str:
        """Return the spoken text, or an empty string if nothing was heard."""
        ...


class NullSTT:
    """Placeholder used when no provider is configured."""

    name = "none"

    async def transcribe(self, pcm: bytes, sample_rate: int) -> str:
        return ""


class OpenAISTT:
    """OpenAI hosted transcription (Whisper and successors)."""

    name = "openai"

    def __init__(self, model: str) -> None:
        from openai import AsyncOpenAI  # imported lazily so the dependency is optional

        self.model = model
        self.client = AsyncOpenAI()

    async def transcribe(self, pcm: bytes, sample_rate: int) -> str:
        wav = pcm16_to_wav(pcm, sample_rate)
        result = await self.client.audio.transcriptions.create(
            model=self.model,
            file=("speech.wav", wav, "audio/wav"),
        )
        return (result.text or "").strip()


class LocalWhisperSTT:
    """Runs Whisper on the server's own CPU/GPU through faster-whisper."""

    name = "local"

    def __init__(self, model_size: str, device: str = "cpu") -> None:
        from faster_whisper import WhisperModel  # optional dependency

        # "cpu" with int8 weights runs well on a laptop; "cuda" needs the
        # CUDA and cuBLAS runtime libraries installed.
        log.info("loading faster-whisper model %s on %s", model_size, device)
        self.model = WhisperModel(model_size, device=device, compute_type="int8")

    def _transcribe_sync(self, pcm: bytes, sample_rate: int) -> str:
        if sample_rate != 16000:
            raise ValueError("faster-whisper expects 16 kHz audio")
        audio = pcm16_to_float32(pcm)
        segments, _info = self.model.transcribe(audio, beam_size=1, vad_filter=True)
        return " ".join(seg.text.strip() for seg in segments).strip()

    async def transcribe(self, pcm: bytes, sample_rate: int) -> str:
        return await asyncio.to_thread(self._transcribe_sync, pcm, sample_rate)


def build_stt(settings: Settings) -> STTProvider:
    kind = settings.stt_provider.lower()
    if kind == "openai":
        return OpenAISTT(settings.openai_stt_model)
    if kind == "local":
        return LocalWhisperSTT(settings.whisper_model, settings.whisper_device)
    if kind in ("none", ""):
        log.warning("STT_PROVIDER is 'none': voice input will not be transcribed")
        return NullSTT()
    raise ValueError(f"unknown STT_PROVIDER {settings.stt_provider!r}")
