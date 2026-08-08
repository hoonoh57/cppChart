# Remote-first workflow

1. Implement on a dedicated remote branch.
2. Run Windows CI and native tests on every push.
3. Fix all deterministic failures remotely.
4. Accumulate a meaningful feature slice before requesting local synchronization.
5. Use local execution only for GUI, credentialed broker integration, live asynchronous events, and physical reconnect acceptance.
6. Merge to `main` only after remote CI and the required local acceptance checkpoint pass.
