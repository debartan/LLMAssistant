"""Notes are markdown files with a small front-matter block. One file per note."""

from __future__ import annotations

import re
from dataclasses import dataclass, field
from datetime import datetime
from pathlib import Path

from .text import slugify


@dataclass
class Note:
    id: str
    title: str
    date: datetime
    duration_seconds: int
    summary: str
    action_items: list[str] = field(default_factory=list)
    transcript: str = ""

    @classmethod
    def new(
        cls,
        when: datetime,
        duration: int,
        title: str,
        summary: str,
        action_items: list[str],
        transcript: str,
    ) -> "Note":
        note_id = f"{when:%Y%m%d-%H%M%S}-{slugify(title)}"
        return cls(note_id, title, when, duration, summary, list(action_items), transcript)

    def brief(self) -> dict:
        return {"id": self.id, "title": self.title, "date": self.date.strftime("%Y-%m-%d %H:%M")}

    def full(self) -> dict:
        return {
            **self.brief(),
            "duration_seconds": self.duration_seconds,
            "summary": self.summary,
            "action_items": self.action_items,
        }

    def to_markdown(self) -> str:
        items = "\n".join(f"- {x}" for x in self.action_items) or "- (none)"
        return (
            "---\n"
            f"id: {self.id}\n"
            f"title: {self.title}\n"
            f"date: {self.date.isoformat(timespec='seconds')}\n"
            f"duration_seconds: {self.duration_seconds}\n"
            "---\n\n"
            f"# {self.title}\n\n"
            "## Summary\n\n"
            f"{self.summary.strip()}\n\n"
            "## Action items\n\n"
            f"{items}\n\n"
            "## Transcript\n\n"
            f"{self.transcript.strip()}\n"
        )

    @classmethod
    def from_markdown(cls, text: str) -> "Note":
        m = re.match(r"---\n(.*?)\n---\n", text, re.S)
        if not m:
            raise ValueError("missing front matter")
        meta: dict[str, str] = {}
        for line in m.group(1).splitlines():
            key, _, value = line.partition(":")
            meta[key.strip()] = value.strip()
        body = text[m.end():]

        def section(name: str) -> str:
            sm = re.search(rf"^## {re.escape(name)}\n\n(.*?)(?=^## |\Z)", body, re.S | re.M)
            return sm.group(1).strip() if sm else ""

        items = [
            ln[2:].strip()
            for ln in section("Action items").splitlines()
            if ln.startswith("- ") and ln.strip() != "- (none)"
        ]
        return cls(
            id=meta.get("id", ""),
            title=meta.get("title", "Untitled"),
            date=datetime.fromisoformat(meta.get("date", "1970-01-01T00:00:00")),
            duration_seconds=int(meta.get("duration_seconds", "0") or 0),
            summary=section("Summary"),
            action_items=items,
            transcript=section("Transcript"),
        )


class NotesStore:
    def __init__(self, directory: str | Path) -> None:
        self.dir = Path(directory)
        self.dir.mkdir(parents=True, exist_ok=True)

    def _path(self, note_id: str) -> Path:
        safe = re.sub(r"[^A-Za-z0-9_-]", "", note_id)
        return self.dir / f"{safe}.md"

    def save(self, note: Note) -> Path:
        path = self._path(note.id)
        path.write_text(note.to_markdown(), encoding="utf-8")
        return path

    def get(self, note_id: str) -> Note | None:
        path = self._path(note_id)
        if not path.is_file():
            return None
        return Note.from_markdown(path.read_text(encoding="utf-8"))

    def list(self, limit: int = 30) -> list[Note]:
        notes = []
        for path in sorted(self.dir.glob("*.md"), reverse=True)[:limit]:
            try:
                notes.append(Note.from_markdown(path.read_text(encoding="utf-8")))
            except ValueError:
                continue
        return notes
