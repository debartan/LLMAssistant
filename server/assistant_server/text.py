"""Text helpers shared by apps."""

from __future__ import annotations

import re

_SENTENCE_END = re.compile(r"(.*?[.!?](?:[\"')\]]*)(?:\s+|$))", re.S)


def take_sentences(buffer: str, min_chars: int = 12) -> tuple[list[str], str]:
    """Split complete sentences off the front of ``buffer``.

    Returns ``(sentences, remainder)``. Very short fragments (abbreviations,
    "e.g.") are merged into the next sentence so speech synthesis gets
    natural chunks.
    """
    sentences: list[str] = []
    pos = 0
    pending = ""
    for m in _SENTENCE_END.finditer(buffer):
        if m.end() == m.start():
            break
        chunk = m.group(1).strip()
        pos = m.end()
        if not chunk:
            continue
        pending = f"{pending} {chunk}".strip() if pending else chunk
        if len(pending) >= min_chars:
            sentences.append(pending)
            pending = ""
    remainder = (pending + " " + buffer[pos:]).strip() if pending else buffer[pos:]
    # A paragraph break also ends a chunk even without punctuation.
    if "\n\n" in remainder:
        head, _, tail = remainder.partition("\n\n")
        if head.strip():
            sentences.append(head.strip())
        remainder = tail
    return sentences, remainder


def slugify(text: str, max_len: int = 40) -> str:
    slug = re.sub(r"[^a-z0-9]+", "-", text.lower()).strip("-")
    return slug[:max_len].rstrip("-") or "note"
