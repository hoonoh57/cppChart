# Remote test matrix

| Area | Remote test |
|---|---|
| Build | MSVC x64 release build |
| Core commands | FIFO, payload preservation, UI wake |
| Fault policy | counters, escalation, observe mode |
| Repository | whitespace and tracked-secret checks |
| P2 parser | REST and WebSocket fixture parsing |
| P2 orders | correlation and idempotent cumulative fills |
| P2 recovery | reconnect and balance reconciliation state machine |
| P2 liquidation | deterministic liquidation-plan generation |

Only desktop rendering and credentialed live mock-broker acceptance remain local.
