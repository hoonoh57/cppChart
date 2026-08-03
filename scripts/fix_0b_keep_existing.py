from __future__ import annotations

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def replace_once(path: Path, old: str, new: str, label: str) -> None:
    text = path.read_text(encoding="utf-8-sig")
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected one match, found {count}")
    path.write_text("\ufeff" + text.replace(old, new, 1), encoding="utf-8")


replace_once(
    ROOT / "platform" / "kiwoom_runtime_runner.cpp",
    '''        action.text = BuildWebSocketRegistrationMessage(
            "2",
            false,
            { code },
            { "0B" });
''',
    '''        action.text = BuildWebSocketRegistrationMessage(
            "2",
            true,
            { code },
            { "0B" });
''',
    "additive 0B registration",
)

replace_once(
    ROOT / "tests" / "kiwoom_runtime_runner_tests.cpp",
    '''                    message.find("\\\"type\\\":[\\\"0B\\\"]") !=
                        std::string::npos &&
                    message.find("000660") != std::string::npos)
''',
    '''                    message.find("\\\"type\\\":[\\\"0B\\\"]") !=
                        std::string::npos &&
                    message.find("\\\"refresh\\\":\\\"1\\\"") !=
                        std::string::npos &&
                    message.find("000660") != std::string::npos)
''',
    "0B keep-existing regression assertion",
)

print("Changed selected-stock 0B registration to refresh=1 and strengthened regression test")
