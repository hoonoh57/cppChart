# Local acceptance boundary

Local execution is requested only after remote build and automated tests are green and only for checks that cannot be completed on a GitHub-hosted Windows runner.

Required local checks are limited to:

- Dear ImGui rendering and interactive layout
- Kiwoom mock credentials and account-specific responses
- Live WebSocket ordering, partial fills, and balance events
- Physical reconnect and reconciliation
- Emergency liquidation against the actual mock account state

All deterministic parsing, state transitions, duplicate-event handling, order correlation, and reconciliation calculations must be covered by remote tests first.
