"""Thin wrapper around the Anthropic SDK.

Two operations are all the apps need: stream a chat reply, and get a JSON
object back for a summarisation task. Keeping them here means an app never
touches the SDK directly, and tests can swap in a fake.
"""

from __future__ import annotations

import json
import logging
from typing import Any, AsyncIterator, Protocol

import anthropic

log = logging.getLogger(__name__)

# Server-side fallback: if a safety classifier declines a request, the API
# re-runs it on Anthropic's recommended substitute model instead of returning
# a refusal to the device.
FALLBACK_BETA = "server-side-fallback-2026-07-01"


class LLMClient(Protocol):
    async def stream_chat(
        self, system: str, messages: list[dict[str, Any]], *, effort: str, max_tokens: int
    ) -> AsyncIterator[str]: ...

    async def complete_json(
        self, system: str, user_text: str, schema: dict[str, Any], *, effort: str, max_tokens: int
    ) -> dict[str, Any]: ...


class ClaudeClient:
    def __init__(self, model: str, client: anthropic.AsyncAnthropic | None = None) -> None:
        self.model = model
        self.client = client or anthropic.AsyncAnthropic()

    async def stream_chat(
        self,
        system: str,
        messages: list[dict[str, Any]],
        *,
        effort: str = "low",
        max_tokens: int = 2048,
    ) -> AsyncIterator[str]:
        """Yield text deltas as they arrive."""
        async with self.client.beta.messages.stream(
            model=self.model,
            max_tokens=max_tokens,
            system=system,
            messages=messages,
            output_config={"effort": effort},
            betas=[FALLBACK_BETA],
            fallbacks="default",
        ) as stream:
            async for text in stream.text_stream:
                yield text
            final = await stream.get_final_message()
        if final.stop_reason == "refusal":
            category = final.stop_details.category if final.stop_details else None
            log.warning("request refused (category=%s)", category)
            yield "I can't help with that one."
        elif final.stop_reason == "max_tokens":
            log.warning("reply truncated at max_tokens=%d", max_tokens)

    async def complete_json(
        self,
        system: str,
        user_text: str,
        schema: dict[str, Any],
        *,
        effort: str = "medium",
        max_tokens: int = 4096,
    ) -> dict[str, Any]:
        """Return a JSON object that matches ``schema`` (structured output)."""
        async with self.client.beta.messages.stream(
            model=self.model,
            max_tokens=max_tokens,
            system=system,
            messages=[{"role": "user", "content": user_text}],
            output_config={
                "effort": effort,
                "format": {"type": "json_schema", "schema": schema},
            },
            betas=[FALLBACK_BETA],
            fallbacks="default",
        ) as stream:
            final = await stream.get_final_message()
        if final.stop_reason == "refusal":
            raise RuntimeError("summary request was refused")
        text = next((b.text for b in final.content if b.type == "text"), "")
        return json.loads(text)
