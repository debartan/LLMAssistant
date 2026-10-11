import asyncio
import json

from assistant_server.session import Session


def run(ws, services):
    asyncio.run(Session(ws, services).run())


def hello():
    return json.dumps({"type": "sys.hello", "device": "unit-test", "fw": "0"})


def open_app(name):
    return json.dumps({"type": "app.open", "app": name})


def test_hello_and_routing_errors(services):
    from tests.conftest import FakeSocket

    ws = FakeSocket([hello(), "garbage", json.dumps({"type": "chat.ptt_start"}),
                     open_app("nope"), json.dumps({"type": "sys.ping", "t": 7})])
    run(ws, services)
    msgs = ws.json_messages()
    types = [m["type"] for m in msgs]
    assert types[0] == "sys.hello_ack"
    assert {a["id"] for a in msgs[0]["apps"]} >= {"chat", "notes"}
    assert types.count("sys.error") == 3  # bad json, no app open, unknown app
    assert msgs[-1] == {"type": "sys.pong", "t": 7}


def test_chat_voice_turn(services):
    from tests.conftest import FakeSocket

    half_second = bytes([1]) + b"\0\0" * 8000
    ws = FakeSocket([
        hello(), open_app("chat"),
        json.dumps({"type": "chat.ptt_start"}),
        half_second, half_second,
        json.dumps({"type": "chat.ptt_stop"}),
    ])
    run(ws, services)
    msgs = ws.json_messages()
    types = [m["type"] for m in msgs]

    assert "app.opened" in types
    assert services.stt.calls and len(services.stt.calls[0]) == 32000
    assert [m["text"] for m in msgs if m["type"] == "chat.transcript"] == ["hello world"]
    deltas = "".join(m["text"] for m in msgs if m["type"] == "chat.delta")
    assert deltas.strip() == "Hello there. How are you today?"
    assert types.count("chat.done") == 1
    # speech: both sentences synthesised, audio envelope sent once
    assert services.tts.calls == ["Hello there.", "How are you today?"]
    assert types.count("sys.audio_start") == 1 and types.count("sys.audio_end") == 1
    assert len(ws.audio_frames()) >= 2
    # history carried the turn
    assert services.llm.chat_calls[0][-1] == {"role": "user", "content": "hello world"}


def test_chat_text_turn_and_history(services):
    from tests.conftest import FakeSocket

    ws = FakeSocket([hello(), open_app("chat"),
                     json.dumps({"type": "chat.text", "text": "first"}),
                     0.3,
                     json.dumps({"type": "chat.text", "text": "second"})])
    run(ws, services)
    calls = services.llm.chat_calls
    assert len(calls) == 2
    assert [m["role"] for m in calls[1]] == ["user", "assistant", "user"]
    assert calls[1][2]["content"] == "second"


def test_interrupting_a_reply_keeps_history_well_formed(services):
    from tests.conftest import FakeSocket

    ws = FakeSocket([hello(), open_app("chat"),
                     json.dumps({"type": "chat.text", "text": "first"}),
                     json.dumps({"type": "chat.text", "text": "second"})])
    run(ws, services)
    calls = services.llm.chat_calls
    assert len(calls) == 2
    roles = [m["role"] for m in calls[1]]
    assert all(a != b for a, b in zip(roles, roles[1:])), roles  # strictly alternating
    assert calls[1][-1]["content"] == "second"
    assert "sys.audio_flush" in [m["type"] for m in ws.json_messages()]


def test_too_short_utterance_is_ignored(services):
    from tests.conftest import FakeSocket

    ws = FakeSocket([hello(), open_app("chat"),
                     json.dumps({"type": "chat.ptt_start"}),
                     bytes([1]) + b"\0\0" * 100,
                     json.dumps({"type": "chat.ptt_stop"})])
    run(ws, services)
    assert services.stt.calls == []
    assert not services.llm.chat_calls
    statuses = [m["text"] for m in ws.json_messages() if m["type"] == "chat.status"]
    assert "Too short, try again" in statuses


def test_notes_record_and_summarise(services):
    from tests.conftest import FakeSocket

    one_second = bytes([1]) + b"\0\0" * 16000
    ws = FakeSocket([hello(), open_app("notes"),
                     json.dumps({"type": "notes.start"}),
                     one_second, one_second, one_second,
                     json.dumps({"type": "notes.stop"}),
                     json.dumps({"type": "notes.list"})])
    run(ws, services)
    msgs = ws.json_messages()
    types = [m["type"] for m in msgs]
    assert types.count("notes.line") == 3  # three one-second windows
    summary = next(m for m in msgs if m["type"] == "notes.summary")
    assert summary["title"] == "Weekly sync"
    assert summary["action_items"] == ["Alice: send the deck", "Bob: book the room"]
    assert "Transcript recorded" in services.llm.json_calls[0]

    saved = services.notes.list()
    assert len(saved) == 1 and saved[0].title == "Weekly sync"
    assert saved[0].transcript.count("hello world") == 3
    listing = next(m for m in msgs if m["type"] == "notes.list")
    assert listing["notes"][0]["id"] == saved[0].id


def test_notes_store_roundtrip(tmp_path):
    from datetime import datetime

    from assistant_server.notes_store import Note, NotesStore

    store = NotesStore(tmp_path)
    note = Note.new(datetime(2026, 10, 11, 9, 30), 90, "Plan: Q4!", "Short summary.\n\nSecond para.",
                    ["Do a thing", "Do another"], "line one\nline two")
    store.save(note)
    back = store.get(note.id)
    assert back is not None
    assert back.id == "20261011-093000-plan-q4"
    assert back.summary == "Short summary.\n\nSecond para."
    assert back.action_items == ["Do a thing", "Do another"]
    assert back.transcript == "line one\nline two"
    assert store.get("../../etc/passwd") is None
