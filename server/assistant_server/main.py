"""Entry point: ``assistant-server`` or ``python -m assistant_server.main``."""

from __future__ import annotations

import argparse
import asyncio
import logging

from . import apps  # noqa: F401  registers the built-in apps
from .config import Settings, load_dotenv
from .services import build_services
from .session import Session

log = logging.getLogger("assistant")


async def serve(settings: Settings) -> None:
    from websockets.asyncio.server import serve as ws_serve

    services = build_services(settings)

    async def handler(ws) -> None:
        peer = getattr(ws, "remote_address", None)
        log.info("device connected from %s", peer)
        await Session(ws, services).run()

    log.info(
        "listening on ws://%s:%d  (model=%s stt=%s tts=%s notes=%s)",
        settings.host, settings.port, settings.claude_model,
        services.stt.name, services.tts.name, settings.notes_dir,
    )
    async with ws_serve(handler, settings.host, settings.port, max_size=4 * 1024 * 1024):
        await asyncio.Future()  # run until cancelled


def main(argv: list[str] | None = None) -> None:
    parser = argparse.ArgumentParser(description="Companion server for the pocket assistant")
    parser.add_argument("--env", default=".env", help="path to a .env file (default: .env)")
    parser.add_argument("--host")
    parser.add_argument("--port", type=int)
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args(argv)

    logging.basicConfig(
        level=logging.DEBUG if args.verbose else logging.INFO,
        format="%(asctime)s %(levelname)-7s %(name)s: %(message)s",
    )
    load_dotenv(args.env)
    settings = Settings()
    if args.host:
        settings.host = args.host
    if args.port:
        settings.port = args.port
    try:
        asyncio.run(serve(settings))
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
