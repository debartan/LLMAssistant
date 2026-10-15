import pytest

from assistant_server import protocol


def test_make_and_parse_roundtrip():
    text = protocol.make("chat.delta", text="héllo")
    msg = protocol.parse(text)
    assert msg == {"type": "chat.delta", "text": "héllo"}


def test_parse_rejects_bad_input():
    with pytest.raises(protocol.ProtocolError):
        protocol.parse("not json")
    with pytest.raises(protocol.ProtocolError):
        protocol.parse('{"no_type": 1}')
    with pytest.raises(protocol.ProtocolError):
        protocol.parse("[1,2]")


def test_split_type():
    assert protocol.split_type("chat.ptt_start") == ("chat", "ptt_start")
    assert protocol.split_type("notes.get.extra") == ("notes", "get.extra")
    with pytest.raises(protocol.ProtocolError):
        protocol.split_type("chat")
    with pytest.raises(protocol.ProtocolError):
        protocol.split_type(".x")


def test_audio_frames():
    pcm = b"\x01\x02\x03\x04"
    frame = protocol.encode_audio(pcm)
    assert frame[0] == protocol.AUDIO_PCM16_16K
    assert protocol.decode_audio(frame) == (protocol.AUDIO_PCM16_16K, pcm)
    with pytest.raises(protocol.ProtocolError):
        protocol.decode_audio(b"")


def test_duration():
    one_second = b"\0" * 32000
    assert protocol.pcm_duration_seconds(one_second) == 1.0
