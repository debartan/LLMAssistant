"""Small audio helpers: WAV wrapping and sample-rate conversion."""

from __future__ import annotations

import io
import wave

import numpy as np


def pcm16_to_wav(pcm: bytes, sample_rate: int, channels: int = 1) -> bytes:
    buf = io.BytesIO()
    with wave.open(buf, "wb") as w:
        w.setnchannels(channels)
        w.setsampwidth(2)
        w.setframerate(sample_rate)
        w.writeframes(pcm)
    return buf.getvalue()


def pcm16_to_float32(pcm: bytes) -> np.ndarray:
    return np.frombuffer(pcm, dtype="<i2").astype(np.float32) / 32768.0


def resample_pcm16(pcm: bytes, src_rate: int, dst_rate: int) -> bytes:
    """Linear-interpolation resampler. Good enough for speech playback."""
    if src_rate == dst_rate or not pcm:
        return pcm
    samples = np.frombuffer(pcm, dtype="<i2").astype(np.float32)
    n_out = int(round(len(samples) * dst_rate / src_rate))
    if n_out <= 0:
        return b""
    src_positions = np.arange(len(samples), dtype=np.float64)
    dst_positions = np.linspace(0, len(samples) - 1, n_out, dtype=np.float64)
    out = np.interp(dst_positions, src_positions, samples)
    return np.clip(np.rint(out), -32768, 32767).astype("<i2").tobytes()


def rms_level(pcm: bytes) -> float:
    """Root-mean-square level in the range 0..1, used to detect silence."""
    if not pcm:
        return 0.0
    samples = pcm16_to_float32(pcm)
    return float(np.sqrt(np.mean(samples * samples)))
