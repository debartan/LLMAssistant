"""Everything an app may need, bundled so apps take one argument."""

from __future__ import annotations

from dataclasses import dataclass

from .config import Settings
from .llm.claude import LLMClient
from .notes_store import NotesStore
from .providers.stt import STTProvider
from .providers.tts import TTSProvider


@dataclass
class Services:
    settings: Settings
    stt: STTProvider
    tts: TTSProvider
    llm: LLMClient
    notes: NotesStore


def build_services(settings: Settings) -> Services:
    from .llm.claude import ClaudeClient
    from .providers.stt import build_stt
    from .providers.tts import build_tts

    return Services(
        settings=settings,
        stt=build_stt(settings),
        tts=build_tts(settings),
        llm=ClaudeClient(settings.claude_model),
        notes=NotesStore(settings.notes_dir),
    )
