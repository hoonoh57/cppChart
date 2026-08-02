from __future__ import annotations

from pathlib import Path

path = Path(__file__).resolve().parents[1] / "platform" / "kiwoom_runtime_runner.cpp"
text = path.read_text(encoding="utf-8-sig")
old = '''    void KiwoomRuntimeRunner::StartReceiver()
    {
        StopReceiver();
        receiverRunning_.store(true, std::memory_order_release);
        receiverThread_ =
            std::thread(&KiwoomRuntimeRunner::ReceiverLoop, this);
    }
'''
new = '''    void KiwoomRuntimeRunner::StartReceiver()
    {
        if (receiverThread_.joinable()) {
            receiverThread_.join();
        }
        receiverRunning_.store(true, std::memory_order_release);
        receiverThread_ =
            std::thread(&KiwoomRuntimeRunner::ReceiverLoop, this);
    }
'''

if old in text:
    text = text.replace(old, new, 1)
    path.write_text("\ufeff" + text, encoding="utf-8")
    print("Fixed receiver startup so a new socket is not immediately closed")
elif new in text:
    print("Receiver lifecycle fix already applied")
else:
    raise RuntimeError("runtime receiver block was not found")
