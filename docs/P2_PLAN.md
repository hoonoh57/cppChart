# P2 Kiwoom mock gateway plan

## Goal

Replace local immediate fills with Kiwoom mock REST orders and asynchronous order/fill/balance events while preserving the existing WYSIWYG manual trading workflow.

## Remote implementation sequence

1. Execution mode contract: `LOCAL_MOCK` and `KIWOOM_MOCK`
2. Environment loading without tracking credentials
3. OAuth token lifecycle
4. WebSocket login, order/fill `00`, and balance `04` subscriptions
5. Single outbound order queue and request correlation
6. REST buy/sell/cancel integration
7. Idempotent cumulative-fill processing
8. Balance reconciliation and reconnect recovery
9. Dashboard connection and order state visualization
10. Windows CI build, unit, parser, and state-machine tests

## Local-only acceptance tests

- Kiwoom mock credential authentication
- Real asynchronous `00` and `04` event reception
- Interactive chart and dashboard rendering
- Manual buy, individual liquidation, selected liquidation, and emergency liquidation
- Physical disconnect and reconnect reconciliation
