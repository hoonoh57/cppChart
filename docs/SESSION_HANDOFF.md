# cppChart Session Handoff

> 다음 세션은 반드시 `ARCHITECTURE_CONSTITUTION.md` → `MODULARIZATION_PLAN.md` → 이 문서 순서로 읽고 시작한다.
>
> 이 문서는 단순 진행 요약이 아니라 **승인된 계획을 임의 축소하지 않고 끝까지 실행하기 위한 작업 통제 문서**다.

## 0. 저장소와 현재 기준점

- Repository: `hoonoh57/cppChart`
- Branch: `p2/kiwoom-mock-gateway`
- Local: `E:\2026\gpt\cpp\shell`
- PR: `#1` Draft
- Current branch HEAD: `bd0dc433fbf3b071834f4a116387dfed712d4ffa`
- Last successful Windows CI: `30950313269` / run `#1176`
- Artifact ID: `8908930495`
- Artifact digest: `sha256:1e241704fc48e3f8f860f9b6f5d17e01a0a1dbc45b14eeffd4339e850107f770`
- Workflow permission: `contents: read`

항상 원격 최신 HEAD를 먼저 확인한다.

```powershell
Set-Location "E:\2026\gpt\cpp\shell"
git fetch origin
git switch p2/kiwoom-mock-gateway
git pull --ff-only origin p2/kiwoom-mock-gateway
git status
git rev-parse HEAD
```

`git status`가 clean이 아니면 사용자의 로컬 변경을 덮어쓰지 않는다.

---

# 1. 최상위 불변식

1. 프로덕션은 실제 키움 모의투자 데이터만 사용한다.
2. 데이터가 없거나 잘못되면 해당 기능만 fail-closed 한다.
3. 합성 시세, 합성 봉, 합성 포지션, 합성 체결, 합성 지표를 만들지 않는다.
4. 렌더러는 `RenderDocument`의 범용 pane/axis/series/legend/reference만 해석한다.
5. 지표명, 비교종목명, 브로커명, 전략명에 따른 분기를 renderer에 추가하지 않는다.
6. 사용자에게 승인받은 계획은 실행 계약이다. 승인 없이 항목을 축소·삭제·후순위화하지 않는다.
7. CI 성공은 사용자 기능 완료가 아니다. UI 기능은 실제 사용자 동선이 화면에서 작동해야 완료다.

---

# 2. 프로젝트 구조

```text
core/
  정규화 데이터 계약과 순수 계산
  Bar, protocol, Kiwoom 응답 파서, symbol catalog, indicators

app/
  기능 모듈과 수명주기
  MarketDataModule, ChartWorkspaceModule, IndicatorModule,
  ComparisonModule, configuration, transform, render adapter

platform/
  Windows/네트워크/브로커 어댑터
  Kiwoom runtime runner, WinHTTP transport, WebSocket lifecycle

render/
  범용 렌더 문서와 순수 geometry/layout/viewport
  renderer가 지표·종목·브로커를 알아서는 안 됨

ui/
  ImGui 화면과 기능별 편집기
  indicator_manager_ui, comparison_manager_ui,
  render_document_renderer

shell_main.cpp
  프로세스 조립, toolbar, command bus, runtime callback,
  dock layout, 모듈 연결

tests/
  순수 계산, 모듈, adapter, viewport, protocol 회귀 테스트

scripts/
  검증 스크립트만 유지
  일회성 migration/apply/fix 스크립트는 최종 branch에 남기지 않음
```

핵심 책임 경계:

- 종목 검색 계산/정렬: `core/kiwoom_symbol_catalog.*`
- 비교 정의와 데이터 수명주기: `app/comparison_module.*`
- 비교값 변환: `app/comparison_transform.*`
- 비교 generic render contribution: `app/comparison_render_adapter.*`
- 비교 추가/편집 UI: `ui/comparison_manager_ui.*`
- 상단 메인 종목 입력과 조회: `shell_main.cpp::DrawToolbar()`
- 실제 종목분봉 조회: `Cmd::LoadSymbol` → `MarketDataModule` → `KiwoomRuntimeRunner`

---

# 3. 완료된 기능

## 3.1 M1-M6

완료 및 실화면 수용:

- 실제 키움 OAuth/WebSocket LOGIN/PING
- 주문체결 `00`, 잔고 `04`
- 주문·미체결·체결·잔고 대조
- `ka10080` 실제 분봉
- 선택 종목 `0B` 실시간
- generic renderer와 ordinal X axis
- crosshair, wheel zoom, pan, latest reset
- pane resize, 공통 bar-slot geometry
- viewport/selection/pane 높이의 live update 보존

## 3.2 M7 동적 지표

완료 및 사용자 수용:

- SMA, EMA, JMA
- Bollinger Bands
- RSI
- MACD
- DMI
- SuperTrend
- VWAP
- OBV
- Wilder ADX

지원 기능:

- 동적 인스턴스 추가/복제/감추기/표시/삭제
- 출력별 pane/axis 배치
- 색상/두께/스타일
- 기준선 CRUD와 과매수/과매도
- same-pane overlay
- 고정 footer

## 3.3 차트 관찰 viewport

완료 및 사용자 수용:

- 최신봉 오른쪽 미래 bar 여백
- 차트 본문 좌우 이동
- pane별 상하 이동
- 오른쪽 Y축 drag 확대/축소
- mouse-anchor Y scale
- double-click X/Y 자동 여백 복원
- live-tail 중 수동 X/Y 상태 유지
- 하단 X축 상시 노출
- 차트 내부 세로 scrollbar 제거

## 3.4 M8 비교 기반

코드 구현 및 자동 검증 완료:

- stock minute + `0B`
- index minute `ka20005`
- index realtime `0J`
- KOSPI `001`, KOSDAQ `101`
- 별도 하단 pane
- 가격 pane secondary axis
- 다중 왼쪽 axis
- 비교 source별 오류 격리
- reconnect 후 구독 복원/해지
- raw close
- indexed 100
- cumulative return %
- primary 대비 relative strength 100
- 첫 공통 timestamp를 고정 anchor로 사용
- live-only update에서 완료봉 render point cache 재사용

최종 자동 검증:

- Windows CI `#1176`
- repository policy PASS
- real-data-only/architecture PASS
- M8 static contract PASS
- MSVC x64 `shell.exe` build PASS
- full headless suite PASS
- clean-tree PASS
- artifact publication PASS

중요: 위 PASS는 **계산/빌드/테스트 완료**를 의미한다. 아래 UI 미완료 항목 때문에 M8 전체는 완료가 아니다.

---

# 4. 현재 미완료 상태 — 절대 완료 처리 금지

## 4.1 상단 메인 종목 검색

사용자 화면에서 확인된 실제 상태:

- 상단은 여전히 단순 `ImGui::InputText("##symbol", ...)`다.
- `삼성`을 입력해도 후보 목록이 나오지 않는다.
- `삼성` 문자열이 그대로 `Cmd::LoadSymbol`에 전달된다.
- 종목명 → 종목코드 확정 기능이 없다.
- 키보드 위/아래/Enter 선택이 없다.
- 선택된 종목명과 시장 구분 표시가 없다.

따라서 아래 표현은 사용 금지:

- "종목 검색 완료"
- "M8.2 완료"
- "실화면 확인만 남음"

## 4.2 현재 존재하는 검색 기능

현재 검색 기능은 **비교 탭 → 추가 → 비교 시계열 추가 popup 내부**에만 존재한다.

구현된 내용:

- `ka10099` 기반 symbol catalog
- 코드/한글명 검색
- 검색 결과 mouse 선택
- code/name/market 표시

미완료:

- 상단 toolbar와 공용 component화
- keyboard navigation
- Enter 확정
- 최근 선택 종목
- 중복 비교종목 방지
- catalog loading/error 상태의 명확한 화면 표시
- 재접속 후 catalog lifecycle 확인

---

# 5. 승인된 M8.2 계획 추적표

다음 표는 승인된 범위다. 임의 축소 금지.

| ID | 승인 항목 | 현재 상태 | 완료 증거 |
|---|---|---|---|
| M8.2-01 | `ka10099` KOSPI/KOSDAQ catalog | 코드 구현/자동 검증 | parser/search tests |
| M8.2-02 | 코드/한글명 검색 | 비교 popup에서만 구현 | popup code 존재 |
| M8.2-03 | 상단 통합 검색 | 미구현 | 없음 |
| M8.2-04 | 후보 목록 | 상단 미구현 | 없음 |
| M8.2-05 | mouse 선택 | 비교 popup만 구현 | popup code 존재 |
| M8.2-06 | keyboard 위/아래 | 미구현 | 없음 |
| M8.2-07 | Enter 확정 | 미구현 | 없음 |
| M8.2-08 | code/name/market 확정 | 상단 미구현 | 없음 |
| M8.2-09 | 임의 문자열 조회 거부 | 미구현 | 없음 |
| M8.2-10 | 메인/비교 검색 공용화 | 미구현 | 없음 |
| M8.2-11 | RawClose | 구현/자동 검증 | transform tests |
| M8.2-12 | Indexed100 | 구현/자동 검증 | transform tests |
| M8.2-13 | ReturnPercent | 구현/자동 검증 | transform tests |
| M8.2-14 | RelativeStrength100 | 구현/자동 검증 | transform tests |
| M8.2-15 | first common fixed anchor | 구현/자동 검증 | transform tests |
| M8.2-16 | `0B/0J` live point transform | 구현/자동 검증 | module/adapter tests |
| M8.2-17 | live-only cache reuse | 구현/자동 검증 | adapter tests |
| M8.2-18 | 실제 상단 검색 화면 수용 | 미완료 | 사용자 screenshot에서 실패 |
| M8.2-19 | 실제 비교 4모드 화면 수용 | 미완료 | 없음 |

M8.2 전체 상태: **미완료**.

---

# 6. 다음 세션의 첫 작업 — 상단 종목 검색 완성

다른 기능을 시작하지 않는다. 계산 최적화, beta/correlation, 문서 미화, PR 정리는 후순위다.

## 6.1 사용자 완료 동선

```text
상단 입력창에 "삼성" 입력
→ 실제 catalog 후보 목록 표시
→ 005930 삼성전자 [KOSPI] 선택
→ toolbar에 code/name/market 확정 표시
→ 실시세 조회
→ ka10080/0B가 005930으로 전환
```

이 경로가 작동하기 전에는 "구현 완료"라고 말하지 않는다.

## 6.2 파일별 작업 순서

### Step 1 — 공용 search UI state 정의

대상:

- `ui/`에 작은 공용 symbol search UI 파일 추가 또는
- 기존 `comparison_manager_ui`의 search 부분을 독립 helper로 추출

필수 state:

- query buffer
- selected catalog index/code/name/market
- highlighted match index
- popup open state
- catalog loading/error
- valid selection 여부

금지:

- `shell_main.cpp`에 검색 정렬 알고리즘 중복 작성
- comparison popup과 toolbar가 서로 다른 검색 규칙 사용

### Step 2 — toolbar 연결

대상:

- `shell_main.cpp::DrawToolbar()`

현재 단순 `InputText`를 공용 search UI로 교체한다.

필수 동작:

- 입력 변경 시 matches 표시
- mouse selection
- Up/Down highlight 이동
- Enter 선택
- Escape 닫기
- 선택 후 `005930 삼성전자`처럼 명확히 표시
- `실시세 조회`는 valid selected code만 허용
- 직접 6자리 code 입력은 catalog exact match일 때만 확정
- 미선택 텍스트는 `Cmd::LoadSymbol`로 보내지 않음

### Step 3 — comparison popup 공용화

대상:

- `ui/comparison_manager_ui.cpp`

기존 자체 `InputTextWithHint + BeginListBox`를 공용 component로 교체한다.

필수:

- main toolbar와 동일한 검색/정렬/keyboard 규칙
- 이미 추가된 동일 kind+code 중복 방지

### Step 4 — catalog lifecycle 표시

필수 상태:

- loading
- loaded N symbols
- failed + exact error
- refresh button

catalog가 비어 있을 때 조용히 빈 결과만 표시하지 않는다.

### Step 5 — 관련 테스트

먼저 작은 테스트만 실행한다.

- `kiwoom_symbol_catalog_tests`
- 새 symbol search state/helper tests
- comparison UI에서 순수 검증 가능한 duplicate/selection helper tests

전체 CI는 이 단계에서 실행하지 않는다.

### Step 6 — 로컬 화면 검증

```powershell
.\build.bat
.\shell.exe
```

최소 확인:

1. `삼성` → 삼성전자 후보
2. `005930` → 삼성전자 exact 후보
3. mouse 선택
4. keyboard Up/Down/Enter
5. 임의 문자열 조회 거부
6. 선택 후 실제 005930 조회
7. 비교 popup에서도 동일 검색
8. 동일 비교종목 중복 추가 거부

### Step 7 — 전체 CI 1회

화면 경로와 관련 테스트가 먼저 통과한 뒤에만 전체 CI를 실행한다.

완료 조건:

- repository policy
- architecture
- MSVC build
- full suite
- clean tree
- artifact

### Step 8 — 사용자 수용

사용자 screenshot에서 상단 후보와 선택 결과가 실제로 보여야 한다.

그 후에만 상태를 `상단 종목 검색 수용 완료`로 바꾼다.

---

# 7. 반복 지연·무한 CI·임의 축소 방지 프로토콜

이 절은 다음 세션의 최우선 작업 규칙이다.

## 7.1 승인 계획은 체크리스트 계약

작업 시작 시 승인 항목을 그대로 적는다.

- `[ ] 미착수`
- `[~] 구현 중`
- `[C] 코드 구현`
- `[T] 관련 테스트 통과`
- `[V] 실제 화면 수용`

`[V]`가 아닌 항목을 전체 완료로 보고하지 않는다.

## 7.2 임의 축소 금지

원 계획의 일부를 미루거나 제거해야 하면 작업을 멈추고 다음을 명시한다.

```text
원 계획 항목:
미룰 항목:
사유:
대체 순서:
승인 필요:
```

승인 없이 내부적으로 축소하지 않는다.

## 7.3 사용자 동선 우선

UI 기능은 다음 순서로 구현한다.

```text
입력 → 후보 → 선택 → 확정 → 실행 → 결과
```

계산 최적화·캐시·문서·PR은 이 동선이 연결된 뒤 처리한다.

## 7.4 30분 무화면 변화 경보

UI 작업에서 30분 동안 사용자 화면에 직접 연결되는 코드 변화가 없으면 즉시 중단하고 확인한다.

- 현재 수정 파일이 실제 UI 경로인가?
- 주변 최적화로 도피하고 있는가?
- 전체 CI를 개발 루프로 오용하고 있는가?
- 승인 계획의 핵심 항목이 빠졌는가?

문제가 있으면 핵심 UI 파일로 즉시 복귀한다.

## 7.5 동일 실패 2회 규칙

같은 유형의 실패가 두 번 발생하면 같은 방법을 세 번째 반복하지 않는다.

강제 전환:

```text
migration script 중단
workflow write mode 중단
직접 source 수정
관련 test만 실행
```

## 7.6 전체 CI 제한

한 기능 단위의 전체 CI는 원칙적으로 2회 이내다.

1. 통합 검증 1회
2. 실패 수정 후 최종 검증 1회

그 전에는 변경 파일 compile과 관련 테스트만 사용한다.

## 7.7 CI write-back 금지 원칙

소스 변경을 위해 workflow를 `contents: write`로 바꾸고 CI bot이 branch를 수정하는 방식을 사용하지 않는다.

예외가 반드시 필요하면 사용자에게 이유를 설명하고 승인을 받는다.

기본:

- GitHub contents API로 직접 파일 수정
- 또는 로컬 source 수정 후 commit
- workflow는 read-only 유지

## 7.8 진행 질문 대응

사용자가 `진행 중인가요?`, `완료됐나요?`라고 물으면 같은 상태 문장을 반복하지 않는다.

즉시 확인할 것:

- 승인 항목 총수
- `[V]` 수
- 최근 30분 실제 source 변경
- 최근 30분 화면 경로 변경
- 현재 blocker 1개
- 같은 실패 반복 횟수

실제 진척이 없으면 방법을 바꾼다.

## 7.9 상태 용어

- `구현 중`: 사용자 경로 미연결
- `코드 구현 완료`: 경로 코드는 연결, 화면 미확인
- `자동 검증 완료`: 빌드/테스트 통과
- `화면 수용 완료`: 실제 사용자 화면과 조작 확인

`완료` 단독 표현은 `화면 수용 완료`에만 사용한다.

## 7.10 활동량을 성과로 보고하지 않음

아래는 완료 증거가 아니다.

- CI 횟수
- commit 수
- 로그 길이
- artifact 생성
- migration script 수
- 설명 분량

완료 증거는 승인 항목의 사용자 동선이 실제로 작동하는 것이다.

---

# 8. 이번 실패 사례에서 금지할 패턴

이번 M8.2에서 실제로 발생한 잘못:

1. 승인 계획 전체를 받음
2. 상단 검색을 빼고 catalog/비교 popup/계산만 구현
3. CI와 cache test를 반복
4. workflow write-back/migration script로 복잡도 증가
5. 사용자 화면은 그대로인데 `완료` 보고
6. 사용자가 진행 여부를 물어도 같은 방식 반복
7. 약 7시간 소비 후 핵심 UI 미구현 확인

다음 세션은 이를 `UI 하나 누락`으로 축소 해석하면 안 된다.

근본 실패는:

- 계획 이탈 감지 실패
- 임의 범위 축소
- 주변 작업 우선
- 전체 CI 오용
- 동일 실패 반복
- 내부 활동을 제품 완료로 오판
- 사용자 경고 신호 무시

---

# 9. 다음 단계 전체 순서

## P0 — 즉시

- 상단 통합 종목 검색
- 공용 검색 component
- keyboard/mouse selection
- valid code 확정
- 임의 문자열 조회 거부
- 비교 popup 공용화
- 중복 방지
- 실제 화면 수용

## P1 — P0 수용 후

- 비교 4모드 실제 화면 확인
- KOSPI/KOSDAQ/삼성전자 동시 비교
- anchor label/legend 현재값 확인
- timeframe 변경 시 모든 비교 재조회
- one-source failure isolation 실화면 확인

## P2

- 최근 선택 종목
- catalog reconnect/refresh 정책
- 검색 결과 시장 필터
- normalized pane common-axis UX

## P3

- beta/correlation
- relative-strength ranking
- multi-workspace shared subscription
- measured subscription limits

## P4 — PR Draft 해제 전

- 실제 계좌 주문/체결 수용
- 물리 다중소스 재접속
- 장중 soak
- 장애·복원 기록

---

# 10. 다음 세션 시작 보고 형식

다음 세션은 긴 계획을 새로 만들지 말고 아래 형식으로 바로 시작한다.

```text
읽은 문서:
- ARCHITECTURE_CONSTITUTION.md
- MODULARIZATION_PLAN.md
- SESSION_HANDOFF.md

현재 HEAD:
현재 P0 체크리스트:
수정할 첫 파일:
첫 화면 결과 목표:
전체 CI 실행 예정 시점:
```

첫 수정 대상은 `shell_main.cpp::DrawToolbar()`와 공용 symbol search UI다.

---

# 11. 로컬 최종 확인 명령

```powershell
Set-Location "E:\2026\gpt\cpp\shell"
git fetch origin
git switch p2/kiwoom-mock-gateway
git pull --ff-only origin p2/kiwoom-mock-gateway
git status
git rev-parse HEAD
.\build.bat
.\shell.exe
```

PR #1은 실제 계좌 주문/체결, 물리 재접속, 장중 soak까지 Draft를 유지한다.
