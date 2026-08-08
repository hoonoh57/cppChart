# Remote validation policy

The `p2/kiwoom-mock-gateway` branch is developed and verified remotely before local synchronization.

Remote gates:

1. Windows MSVC release build of `shell.exe`
2. Core unit tests for `CommandBus` and `FaultPolicy`
3. Repository policy and secret-file checks
4. Verified executable artifact upload

Local verification is reserved for capabilities that require the user's Windows desktop, Kiwoom mock credentials, interactive chart rendering, or live asynchronous broker events.
