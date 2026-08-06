# 종목풀 분석 워크벤치 UI 설계

## 0. 문서 상태

- 대상 저장소: `hoonoh57/cppChart`
- 대상 브랜치: `p2/kiwoom-mock-gateway`
- 선행 문서:
  - `docs/STOCK_POOL_RANKING_ENGINE_DESIGN.md`
  - `docs/LEADER_STRENGTH_SERIES_DESIGN.md`
- 상태: **구현 전 UI·모듈 경계 고정**

이 문서는 현재 상세차트 기능을 동결하고, 조건식 기반 다종목 분석을 별도 워크벤치로 구현하기 위한 실행 계약이다.

---

# 1. 최우선 불변식

## 1.1 상세차트 동결

현재 상세차트의 다음 코드는 종목풀 분석 기능을 위해 수정하지 않는다.

```text
기존 shell chart layout
지표 계산·프로퍼티·JSON 저장소
비교 시계열 계산·프로퍼티·JSON 저장소
시장데이터 chart module
generic render document
render_document_renderer
pane/axis/viewport/crosshair/zoom/pan
기존 chart toolbar와 chart query path
```

종목풀 분석을 위해 다음을 금지한다.

- 상세차트 `DrawMainChart`에 종목풀 분기 추가
- chart renderer에 종목풀 전용 series·상태·점수 분기 추가
- 기존 indicator/comparison JSON에 종목풀 설정 혼합
- 상세차트 전역 상태를 종목풀 worker가 직접 수정
- 조건식 이벤트나 MarketEye 응답을 상세차트 module로 전달
- 기존 비교 시계열을 32종목 종목풀 계산 저장소로 전용

## 1.2 허용되는 연결은 명령 하나

종목풀 워크벤치에서 사용자가 행을 선택하고 `상세차트 열기`를 실행할 때만 기존 공개 명령을 사용한다.

```text
StockPoolWorkbench
→ OpenDetailChartCommand(code, name, market)
→ 기존 검증된 LoadSymbol 경로
```

종목풀 워크벤치는 상세차트의 지표, 비교선, viewport, pane 높이, 조회 봉 수를 변경하지 않는다.

## 1.3 독립 수명주기

```text
상세차트 창을 닫아도 종목풀 분석은 계속 가능
종목풀 창을 닫아도 상세차트는 그대로 동작
종목풀 분석 오류가 상세차트 데이터를 지우지 않음
상세차트 조회 오류가 종목풀 순위를 중단하지 않음
```

---

# 2. 사용자 화면

별도 최상위 창 또는 독립 dockable window로 구현한다.

```text
┌──────────────────────────────────────────────────────────────────┐
│ 종목풀 분석 워크벤치                                              │
├──────────────────────────────────────────────────────────────────┤
│ 조건식 [▼]  일자 [2026-08-06]  모드 [실시간/재생]  주기 [30초]     │
│ 기준 [KOSDAQ/조건식 중앙값]  프로필 [지속 대장]  Top-N [10]        │
│ [시작] [중지] [해당 일자 불러오기] [재계산] [설정 저장]             │
├──────────────────────────────┬───────────────────────────────────┤
│ 종목풀 그리드                 │ 동시 상대강도 차트                  │
│                              │                                   │
│ 순위/종목/상태/강도/기울기    │ Top-N 또는 선택 종목 상대강도선     │
│ 거래대금/지속성/순위변화      │ 100/150/180 기준선                 │
│ 데이터상태/편입/탈락시각      │ enter/exit/leader state marker     │
│                              │                                   │
├──────────────────────────────┴───────────────────────────────────┤
│ 선택 종목 설명·원시 특징·데이터 품질·이후성과(재생/사후평가)       │
└──────────────────────────────────────────────────────────────────┘
```

## 2.1 상단 제어부

### 조건식

- server32에서 조건식 목록 조회
- 조건식 이름과 index를 별도 보관
- 실시간 시작 시 원자적 condition session 사용
- 조건식 변경 시 기존 세션 명시적 중지 후 새 세션 시작

### 일자

두 운용 모드를 명확히 분리한다.

```text
실시간 모드
  기본값: 오늘
  조건식 실시간 enter/exit + 실시간 tick + 주기적 MarketEye

재생/분석 모드
  과거 일자 선택
  저장된 condition/rank/market event를 event-time 순서로 재생
  동일 입력으로 동일 순위가 나와야 함
```

과거 일자 선택이 실시간 서버의 현재 조건식에 영향을 주면 안 된다.

### 갱신 주기

- 30초
- 60초
- 향후 사용자 프로필 값

Tick은 즉시 내부 상태를 갱신하지만, 정렬된 공개 snapshot은 설정 주기에만 발행한다.

### 프로필

초기 프로필:

```text
누적 대장주
급부상
지속 대장
수급 확인 대장
누적거래대금
```

프로필은 가중합이 아니라 문서에 정의된 사전식 정렬 규칙으로 시작한다.

---

# 3. 종목풀 그리드

## 3.1 기본 열

```text
현재 순위
이전 순위
순위 변화
종목코드
종목명
시장
조건식 상태: 편입/이탈/유예
Leader 상태
현재 상대강도
5분 강도 기울기
강도 가속도
Top-10 15분 체류율
강도 고점 대비 낙폭
1분/5분/15분 수익률
시장 초과수익률
종목풀 초과수익률
누적거래대금
거래대금 가속도
체결강도
프로그램/기관 수급 freshness
데이터 준비 상태
마지막 갱신시각
```

화면 열 선택은 별도 workspace JSON에 저장한다.

## 3.2 행 상태

```text
Detected
Hydrating
Ready
Ranked
TopN
Stale
Excluded
ExitedTracking
Failed
```

데이터 미준비 종목을 0점으로 표시하지 않는다. 숫자 대신 `준비 중`, `stale`, `평가 불가`를 표시한다.

## 3.3 사용자 동작

- 단일 클릭: 우측 상대강도 차트에서 굵게 강조
- 더블클릭 또는 `상세차트 열기`: 기존 상세차트 LoadSymbol 명령
- 우클릭:
  - 당일 고정 추적
  - 당일 제외
  - 원시 특징 보기
  - rank path 보기
  - 비교군 구성 보기
- 열 제목 클릭: 표시 정렬만 변경. 엔진의 공식 프로필 순위는 변경하지 않음

공식 엔진 순위와 UI 임시 정렬을 반드시 구분해 표시한다.

---

# 4. 우측 동시 상대강도 차트

## 4.1 전용 경량 차트

이 차트는 기존 상세차트 renderer를 수정해 재사용하지 않는다.

별도 UI component가 `StockPoolSnapshot`의 이미 계산된 상대강도 point만 그린다.

```text
StockPoolStrengthChartUi
입력: shared_ptr<const StockPoolSnapshot>
출력: ImGui draw list 기반 2D polyline/marker/axis
```

이 전용 차트는 캔들, 지표, 거래량, 주문, 상세가격축을 모른다.

## 4.2 표시 모드

```text
선택 종목 1개
Top-5
Top-10
고정 추적 종목
Leader 상태만
```

기본은 Top-10 얇은 선 + 선택 종목 굵은 선이다.

## 4.3 Y축

기본 0~200 고정축:

```text
100 종목풀 중앙
150 상위 25%
180 상위 10%
190 상위 5%
200 최상위
```

종목풀 구성 변화로 percentile이 변할 수 있으므로 tooltip에 항상 다음을 표시한다.

```text
universe revision
전체 종목 수
평가 가능 종목 수
feature version
benchmark revision
```

## 4.4 X축

- 실시간: 장중 event time
- 재생: 선택 일자의 event time
- rank snapshot 발행 시점만 point 생성
- 화면 보간값을 분석 데이터로 사용하지 않음

## 4.5 marker

```text
조건식 enter
조건식 exit
FirstTopNEntry
SustainedTopNEntry
ConfirmedLeader
PersistentLeader
Weakening
FailedLeader
stale/data gap
```

---

# 5. 하단 설명·검증 패널

선택 종목에 대해 다음을 분리 표시한다.

## 5.1 현재 시점 설명

```text
왜 현재 순위인가
사전식 정렬의 각 비교값
직전 순위 대비 무엇이 개선/악화됐는가
어떤 데이터가 stale 또는 평가 불가인가
```

## 5.2 기간 경로

```text
최초 편입시각
최초 Ready 시각
최초 Top-N 진입
Top-N 연속 유지 횟수
최고 순위와 시각
현재까지 Top-N 체류시간
최대 강도와 강도 낙폭
```

## 5.3 사후성과

실시간 현재 이후 값은 절대 표시하지 않는다.

재생 또는 완료된 과거 시점에서만:

```text
5/15/30/60분 후 수익률
MFE/MAE
장 마감까지 MFE/MAE
목표수익 도달시간
목표수익-손실기준 선후관계
```

을 label 데이터로 표시한다.

---

# 6. 독립 모듈 경계

## 6.1 core

```text
stock_pool_types.*
stock_pool_feature_engine.*
stock_pool_ranking_engine.*
leader_strength_series.*
stock_pool_outcome_labeler.*
stock_pool_replay.*
```

순수 C++이며 HTTP, Win32, ImGui, 상세차트 전역 상태를 알지 않는다.

## 6.2 app

```text
condition_pool_module.*
stock_pool_module.*
stock_pool_data_coordinator.*
stock_pool_workspace_state.*
stock_pool_workspace_store.*
stock_pool_history_writer.*
stock_pool_workbench_controller.*
```

## 6.3 platform

```text
server32_condition_client.*
server32_marketeye_client.*
kiwoom_stock_pool_transport.*
stock_pool_request_scheduler.*
```

## 6.4 ui

```text
stock_pool_workbench_window.*
stock_pool_toolbar_ui.*
stock_pool_grid_ui.*
stock_pool_strength_chart_ui.*
stock_pool_explain_ui.*
```

## 6.5 상세차트와의 접점

```cpp
using OpenDetailChart = void(*)(
    const std::string& code,
    const std::string& name,
    MarketKind market);
```

이 callback 외에는 상세차트 객체 참조를 전달하지 않는다.

---

# 7. 별도 저장소

```text
data/stock_pool_workspace.json
```

저장 대상:

```text
선택 조건식
실시간/재생 모드
선택 일자
snapshot 주기
기준지수/benchmark
순위 프로필과 version
Top-N
그리드 열 표시·순서·폭
상대강도 차트 표시 모드
고정 추적 종목
창 크기와 splitter 위치
```

지표·비교 저장소와 동일하게:

```text
단일 소유자
전체 상태 직렬화
저장 직후 재로드
완전 비교 후 readback=OK
```

를 적용한다.

---

# 8. 실시간 처리

```text
server32 condition session
→ membership event queue

키움 realtime tick
→ price/live bar/FID14 누적거래대금 증분 갱신

MarketEye 30초/60초 batch
→ 전체 candidate의 수급·체결강도·거래대금 snapshot

minute hydration queue
→ 승격 종목만 600봉 1회 준비

single-writer stock-pool worker
→ feature update
→ rank snapshot
→ atomic shared_ptr publish

UI thread
→ 최신 immutable snapshot 표시만 수행
```

UI 갱신 때문에 REST/COM 요청을 새로 발생시키지 않는다.

---

# 9. 과거 일자 재생

일자 선택은 단순 과거 캔들 조회가 아니다. 당시 조건식 구성과 데이터 상태를 재현해야 한다.

필수 append-only 파일:

```text
data/stock_pool_history/YYYY-MM-DD/condition_events.jsonl
data/stock_pool_history/YYYY-MM-DD/realtime_events.jsonl
data/stock_pool_history/YYYY-MM-DD/marketeye_snapshots.jsonl
data/stock_pool_history/YYYY-MM-DD/rank_snapshots.jsonl
data/stock_pool_history/YYYY-MM-DD/outcome_labels.jsonl
```

재생 시 event-time과 source sequence 순서로 적용한다.

검증 계약:

```text
실시간 당시 rank snapshot digest
== 같은 입력 재생의 rank snapshot digest
```

---

# 10. 단계별 수직 구현

## Phase A — 창과 fixture

- 별도 종목풀 분석 창
- 조건식·일자·주기·프로필 toolbar
- fixture 10종목 grid
- fixture 상대강도 Top-10 chart
- 행 선택/강조
- 상세차트 열기 callback
- 기존 상세차트 파일 diff 없음 확인

## Phase B — 순수 계산 엔진

- 기간수익률
- pool percentile
- 0~200 leader strength
- 기울기·가속도·지속성·drawdown
- 결정적 사전식 순위
- replay 테스트

## Phase C — server32 계약

- 원자적 조건식 session
- sequence/revision/reconciliation
- MarketEye captured_at/누락/부분실패/freshness

## Phase D — 실제 데이터 연결

- 조건식 enter/exit
- 전 종목 MarketEye
- FID14 누적거래대금
- hydration 승격 queue
- 30/60초 snapshot

## Phase E — 이력과 성과검증

- append-only event/rank
- MFE/MAE labeler
- 일자 재생
- rank decile·Top-N 성과 리포트

---

# 11. 첫 UI 수용 기준

다음 화면 경로가 실제로 보여야 한다.

```text
종목풀 분석 창 열기
→ fixture 조건식 선택
→ 날짜 선택
→ 종목 10개 grid 표시
→ 우측 Top-10 상대강도선 표시
→ 행 클릭 시 해당 선 굵게 강조
→ 프로필 변경 시 공식 순위와 선 설명 갱신
→ 상세차트 열기 실행 시 기존 상세차트에 해당 종목 로드
→ 종목풀 창을 닫아도 상세차트 상태 불변
→ 상세차트를 닫아도 종목풀 fixture 분석 지속
```

이 경로가 수용되기 전에는 실제 조건식·MarketEye·실시간 연결을 완료로 선언하지 않는다.

---

# 12. 성공 기준

이 워크벤치의 목표는 수익 보장이 아니라 다음에 한 단계씩 가까워지는 것이다.

1. 장중 상위 강도와 이후 수익률 사이의 단조 관계 확인
2. 지속 대장 상태가 순간 급부상보다 낮은 MAE를 보이는지 확인
3. Top-N 체류시간과 이후 MFE의 관계 확인
4. Weakening/FailedLeader 이후 위험 증가 확인
5. 여러 거래일 out-of-sample에서 동일 결과 반복
6. 실제 주문 연결 전까지 모든 판단 근거가 replay와 원시 데이터로 설명 가능
