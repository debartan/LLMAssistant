import wave
import io

from assistant_server.audio import pcm16_to_wav, resample_pcm16, rms_level
from assistant_server.text import slugify, take_sentences


def test_wav_header():
    wav = pcm16_to_wav(b"\0\0" * 160, 16000)
    with wave.open(io.BytesIO(wav)) as w:
        assert w.getframerate() == 16000
        assert w.getnchannels() == 1
        assert w.getsampwidth() == 2
        assert w.getnframes() == 160


def test_resample_length_and_identity():
    pcm = b"\x00\x10" * 2400  # 0.1 s at 24 kHz
    out = resample_pcm16(pcm, 24000, 16000)
    assert len(out) == 1600 * 2
    assert resample_pcm16(pcm, 16000, 16000) is pcm
    assert resample_pcm16(b"", 24000, 16000) == b""


def test_rms():
    assert rms_level(b"") == 0.0
    assert rms_level(b"\0\0" * 100) == 0.0
    assert rms_level(b"\xff\x7f" * 100) > 0.99


def test_take_sentences_streams_in_chunks():
    sentences, rest = take_sentences("Hello there. How are")
    assert sentences == ["Hello there."]
    assert rest == "How are"
    sentences, rest = take_sentences(rest + " you today? Fine")
    assert sentences == ["How are you today?"]
    assert rest == "Fine"


def test_take_sentences_merges_short_fragments():
    sentences, rest = take_sentences("Dr. Smith is here. Ok.")
    assert sentences == ["Dr. Smith is here."]
    assert rest == "Ok."


def test_take_sentences_paragraph_break():
    sentences, rest = take_sentences("First part without period\n\nsecond")
    assert sentences == ["First part without period"]
    assert rest == "second"


def test_slugify():
    assert slugify("Weekly Sync: Q4 planning!") == "weekly-sync-q4-planning"
    assert slugify("") == "note"
