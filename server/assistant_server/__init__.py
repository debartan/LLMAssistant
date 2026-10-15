"""Companion server for the pocket Claude assistant.

The device keeps one WebSocket open to this server. Text frames are JSON
messages routed by their ``type`` prefix to an app; binary frames are audio.
See docs/PROTOCOL.md in the repository root.
"""

__version__ = "0.1.0"
