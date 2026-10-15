"""Server-side apps. Importing this package registers every built-in app."""

from . import chat, notes  # noqa: F401  (import for side effect: registration)
from .base import REGISTRY, App, register

__all__ = ["REGISTRY", "App", "register"]
