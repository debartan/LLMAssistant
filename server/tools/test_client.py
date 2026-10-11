"""Talk to the server from a terminal, no device needed.

    python tools/test_client.py ws://localhost:8765 chat
    > what's the tallest mountain?

Type a line to send it as ``chat.text``; the reply streams back. Audio frames
from the server are counted, not played. Use app ``notes`` with commands
``list`` and ``get <id>``.
"""

from __future__ import annotations

import asyncio
import json
import sys

from websockets.asyncio.client import connect


async def reader(ws) -> None:
    audio_bytes = 0
    async for frame in ws:
        if isinstance(frame, bytes):
            audio_bytes += len(frame) - 1
            continue
        msg = json.loads(frame)
        t = msg.pop("type")
        if t == "chat.delta":
            print(msg["text"], end="", flush=True)
        elif t == "chat.done":
            print(f"\n[done, {audio_bytes / 32000:.1f}s of speech received]")
            audio_bytes = 0
        else:
            print(f"[{t}] {json.dumps(msg, ensure_ascii=False)}")


async def main(uri: str, app: str) -> None:
    async with connect(uri) as ws:
        await ws.send(json.dumps({"type": "sys.hello", "device": "test-client", "fw": "dev"}))
        await ws.send(json.dumps({"type": "app.open", "app": app}))
        asyncio.create_task(reader(ws))
        loop = asyncio.get_running_loop()
        while True:
            line = await loop.run_in_executor(None, sys.stdin.readline)
            if not line:
                break
            line = line.strip()
            if not line:
                continue
            if app == "chat":
                await ws.send(json.dumps({"type": "chat.text", "text": line}))
            elif app == "notes":
                cmd, _, arg = line.partition(" ")
                payload = {"type": f"notes.{cmd}"}
                if arg:
                    payload["id"] = arg
                await ws.send(json.dumps(payload))
            else:
                await ws.send(line)


if __name__ == "__main__":
    uri = sys.argv[1] if len(sys.argv) > 1 else "ws://localhost:8765"
    app = sys.argv[2] if len(sys.argv) > 2 else "chat"
    asyncio.run(main(uri, app))
