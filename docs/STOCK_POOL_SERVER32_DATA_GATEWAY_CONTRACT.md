# Stock-Pool server32 Data Gateway Contract

## Decision

The isolated C++ stock-pool workbench does not connect to MySQL directly.

```text
stock_pool_workbench.exe
    -> WinHTTP / JSON
KiwoomServer.exe (hoonoh57/server32)
    -> managed MySqlConnector / parameterized SQL / transaction
MySQL gate3
```

This replaces both rejected direct-client approaches:

```text
C++ -> mysql.exe process execution       [removed]
C++ -> vcpkg libmysql native dependency  [removed]
```

The database remains a first-class persistence layer. Only ownership of the database connection has moved out of the UI process.

## Responsibilities

### C++ workbench

- Parse the copied Kiwoom 1516 table.
- Send all parsed names in one batch request.
- Display resolved and rejected rows with explicit reasons.
- Send the resolved members and future labels to the cohort persistence endpoint.
- Commit the in-memory Frozen Cohort only after server-side transaction success.
- Calculate causal relative strength without using 7-hour or maximum-return labels.

### server32

- Own MySQL credentials.
- Resolve exact active symbol names from `gate3.g3_symbol_master`.
- Revalidate code/name pairs before persistence.
- Create and maintain stock-pool persistence tables.
- Save one Frozen Cohort and all members atomically.
- Return `cohort_id`, accepted count, rejected members, and import hash.
- Later own minute-bar cache read/write and ranking/backtest persistence APIs.

## Endpoints

```text
POST /api/stock-pool/symbols/resolve
POST /api/stock-pool/cohorts
```

The detailed chart `shell.exe` is not part of this path and remains frozen.

## Configuration

### cppChart `.env`

```env
STOCK_POOL_SERVER32_BASE_URL=http://127.0.0.1:8082
```

The C++ process must not contain MySQL host, user, password, database, client DLL, or executable path settings.

### server32 `.env`

```env
MYSQL_HOST=127.0.0.1
MYSQL_PORT=3306
MYSQL_USER=root
MYSQL_PASSWORD=...
MYSQL_DATABASE=gate3
```

## 1516 causal-data rule

The copied columns have two different roles.

```text
Allowed as capture-time facts:
- name
- capture volume
- capture date/time

Stored only as future evaluation labels:
- 1-minute return
- 3-minute return
- 7-hour return
- maximum return during period
- other post-result field
```

No future label may enter strength, rank, gate, Top-M publication, replay state, or simulated entry decisions.

## Failure policy

- server32 unavailable: code resolution fails closed.
- MySQL unavailable: code resolution or cohort save fails closed.
- exact name missing or ambiguous: member is rejected.
- cohort transaction failure: no UI Frozen Cohort is committed.
- old `mysql.exe`/`libmysql` error text in the UI proves an obsolete workbench binary is running.

## Build identity

Successful isolated workbench builds write:

```text
stock_pool_workbench.build.txt
adapter=server32-http-mysql
```

No vcpkg restore occurs in `build_stock_pool.bat`.
