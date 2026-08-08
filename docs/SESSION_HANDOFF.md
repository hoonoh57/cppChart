# cppChart Session Handoff

> 다음 세션은 반드시 `ARCHITECTURE_CONSTITUTION.md` → `MODULARIZATION_PLAN.md` → 이 문서 → `STOCK_POOL_WORKBENCH_IMPLEMENTATION_PLAN.md` 순서로 읽고 시작한다.
>
> 이 문서는 승인된 범위를 임의 축소하지 않고 실제 사용자 화면까지 완성하기 위한 작업 통제 문서다.

---

# 0. 저장소와 작업 기준

- Repository: `hoonoh57/cppChart`
- Branch: `p2/kiwoom-mock-gateway`
- User local checkout: `E:\2026\gpt\cpp\shell`
- PR #1: 실제 계좌·재연결·장중 soak 전까지 Draft 유지
- 일반 수정은 원격 CI를 개발 루프로 사용하지 않는다.
- 원격 코드를 직접 수정하고, 사용자는 로컬에서 `git pull`·빌드·화면 검증한다.

시작 명령:

```powershell
Set-Location "E:\2026\gpt\cpp\shell"
git fetch origin
git switch p2/kiwoom-mock-gateway
git pull --ff-only origin p2/kiwoom-mock-gateway
git status
git rev-parse HEAD
```

로컬 변경이 있으면 덮어쓰지 않는다.

---

# 1. 최상위 불변식

1. 프로덕션은 실제 키움 모의투자·Cybos 데이터만 사용한다.
2. 데이터가 없거나 잘못되면 해당 기능만 fail-closed 한다.
3. 합성 시세·합성 봉·합성 체결·합성 포지션을 프로덕션 경로에 넣지 않는다.
4. fixture는 순수 계산·UI·인과성 검증 전용이며 실제 수익성 근거가 아니다.
5. 렌더러는 이미 계산된 범용 자료를 그리기만 한다.
6. 사용자 편집 상태의 단일 소유자는 workspace JSON이다.
7. UI가 수정하는 객체와 계산·렌더링이 사용하는 객체는 동일 계약이어야 한다.
8. 저장 성공은 파일 쓰기가 아니라 저장 후 재로드 전체 동등성 검증으로 판단한다.
9. 미래 시점 데이터를 사용해 과거 순위·신호를 계산하지 않는다.
10. `1위 존재 = 매수`가 아니다. Gate 실패 시 Published Top-M은 비어 있어야 한다.
11. 상세차트 `shell.exe`는 종목풀 기능을 이유로 더 이상 수정하지 않는다.
12. 종목풀 분석은 독립 `stock_pool_workbench.exe`에서 구현한다.
13. CI·커밋 수·문서량은 완료 증거가 아니다. 사용자 화면에서 동작해야 완료다.

---

# 2. 현재 상세차트 완료 상태

다음 기능은 사용자 화면에서 수용됐다.

## 2.1 실제 시세·차트

- 키움 OAuth·WebSocket
- `ka10080` 실제 분봉
- 선택 종목 `0B` 실시간
- 기본 조회 목표 600봉
- 필요 시 `추가데이터`로 이전 페이지 조회
- 1·3·5·10·15·30·60분
- 기준일 조회
- crosshair·zoom·pan·latest reset
- pane 높이 조절
- 공통 시간축

## 2.2 종목 검색

- KOSPI·KOSDAQ 종목 마스터
- 코드·한글명 autocomplete
- 마우스 선택
- 키보드 Up·Down·Enter
- 선택 code/name/market 확정

## 2.3 동적 지표

- SMA, EMA, JMA
- Bollinger Bands
- RSI
- MACD
- DMI
- SuperTrend
- VWAP
- OBV
- Wilder ADX

지원:

- 추가·복제·표시·감추기·삭제
- 계산 파라미터
- 출력선별 pane·axis
- 색상·보조색·두께·실선·파선·점선
- 패널 높이·축 범위·소수 자릿수
- 기준선 추가·삭제·이름·값·스타일

지표 workspace는 `IndicatorInstanceDefinition` 전체를 JSON으로 저장한다. 지표 종류부터 출력선·패널·축·기준선까지 재기동 복원이 사용자 화면에서 확인됐다.

## 2.4 비교 시계열

- 주식 분봉·`0B`
- 지수 분봉 `ka20005`·`0J`
- RawClose
- Indexed100
- ReturnPercent
- 주 종목 대비 RelativeStrength100
- 별도 pane 또는 가격 pane
- 색상·두께·선종류·패널 높이
- 비교 정의 전체 JSON 저장·재기동 복원

사용자 화면에서 비교종목 실제 렌더링과 재기동 복원이 확인됐다.

## 2.5 상세차트 동결

종목풀 개발 때문에 다음을 수정하지 않는다.

```text
shell_main.cpp / shell_main_m*.cpp
build.bat
상세차트 renderer
indicator/comparison workspace
상세차트 pane/axis/viewport
```

종목풀에서 상세차트로 연결할 필요가 생기면 격리 검증 후 `OpenDetailChartCommand(code, name, market)` 같은 단방향 명령만 허용한다.

---

# 3. 종목풀 프로젝트 목표

한 종목의 신호를 추종하는 시스템이 아니다.

```text
조건식 포착종목 전체
= 그 시점의 축소된 시장 레짐

축소시장 전체의 가격·상대강도·거래대금·수급·순위 지속성을 비교
→ 명확한 상위 1~2종목만 선별
→ 명확한 리더가 없으면 거래하지 않음
```

기관·외국인의 속도와 규모에 정면으로 경쟁하는 대신, 거대 자금이 실제로 만든 상대적 차이를 확인하고 시장에 순응하는 기반을 만든다.

핵심 질문:

```text
현재 축소시장에 거래할 만한 진짜 리더가 존재하는가?
존재한다면 어느 1~2종목인가?
지금 따라가도 위험 대비 남은 수익 구간이 있는가?
```

---

# 4. 독립 실행 구조

```text
shell.exe
  기존 상세차트·주문·지표·비교 — 동결

stock_pool_workbench.exe
  종목풀 관리·성과검증·백테스트·시뮬레이션

stock_pool_evaluator
  장후 미래성과 label 결합·피처 연구
```

독립 빌드:

```text
build.bat
  기존 shell.exe

build_stock_pool.bat
  stock_pool_workbench.exe
  stock_pool_engine_tests
```

`build_stock_pool.bat`은 `shell.exe`를 삭제·교체·링크하지 않는다.

---

# 5. 단일 종목풀 UI 계약

## 상단

- 데이터소스
  - 개발 fixture
  - 1516 과거 포착
  - 실시간 조건식
- 조건식
- 일자
- 포착시각
- timeframe
- Top-M
- 재생속도
- 불러오기
- 백테스트
- 재생·일시정지·1스텝·처음

## 좌측 종목풀 그리드

- 순위
- 종목
- Leader 상태
- 상대강도
- 1분·5분·누적 수익률
- 거래대금 percentile
- 거래대금 가속도
- 순위 변화
- Top-M 연속 유지
- Published 상태

## 우측 동시 상대강도 차트

- 포착종목 전체를 서로 다른 색으로 표시
- 선택 종목 굵게 표시
- 현재 Top-M 강조
- 100·150·180 기준선
- 시간 레짐 전환선

## 하단 탭

- 종목풀 관리
- 성과검증
- 백테스트
- 시뮬레이션

백테스트에는 종목별 시간축 heatmap과 Top-M 진입·청산 결과를 함께 표시한다.

---

# 6. 데이터소스 이중 계약

## 6.1 장중 실시간 조건식

동적 cohort:

```text
초기 membership
+ 실시간 편입
+ 실시간 탈락
+ 재편입
```

`server32`에 원자적 condition session이 필요하다.

필수:

- session_id
- revision
- 초기 목록과 delta 순서 보장
- enter/exit sequence
- 재접속 후 현재 membership 복원

## 6.2 장후 1516 Frozen Cohort

선택한 거래일·조건식·포착시각의 실제 포착 종목만 한 번 확정한다.

replay 전에 읽어도 되는 값:

- 거래일
- 조건식
- 포착시각
- 종목코드·종목명·시장

replay 전에 읽으면 안 되는 값:

- 기간별 수익률
- 7시간 수익률
- 최고수익률
- 미래 MFE·MAE
- 실제 결과 순위

순서:

```text
Frozen Cohort
→ 포착 이후 1분봉 인과 replay
→ ranking history append-only 저장
→ replay 종료
→ 기존 1516 미래성과 label 결합
```

기존 1516 사후 수익률 차트는 재구현하지 않는다. 1516은 최종 Oracle·label 원본이다.

---

# 7. Universe와 데이터 수집

```text
Condition Universe
  조건식 전체, 최대 약 200

Scoring Universe
  상세 캔들 분석 대상, 기본 최대 32

Published Top-M
  사용자 후보, 기본 1~2
```

## MarketEye 역할

`hoonoh57/server32` MarketEye를 전체 후보군 경량 평가에 사용한다.

활용 항목:

- 현재가·시가·고가·저가
- 거래량·거래대금
- 체결강도
- 매도·매수호가잔량
- 외국인·기관·개인·프로그램
- 외국인보유·신용잔고

필수 계약 보강:

- 200종목 초과 명시 오류
- captured_at
- requested/returned count
- missing codes
- partial failure
- freshness

## 실시간 누적거래대금

키움 FID14 누적거래대금을 원본으로 사용한다. 편입 이후 `가격×체결량`만 누적해 당일 거래대금을 대신하지 않는다.

## 캔들 효율

- 신규 Scoring Universe 편성 시 1분봉 600개 1회 hydration
- 30초·60초마다 REST 600봉 반복 요청 금지
- 이후 실시간 체결로 현재 1분봉 증분 갱신
- stale 종목은 0점 처리하지 않고 평가 제외

---

# 8. 상대강도·시간 레짐·Gate

상대강도는 하루 전체의 공통 척도다.

초기 시간 레짐:

```text
OpeningLeadership  09:00~10:00
LaterStructure     10:00~15:20
```

장초반 우선:

- 1·3·5분 가격 상대강도
- 장 시작 이후 상대수익
- 누적거래대금
- 거래대금 속도·가속도
- 순위 상승속도
- Top-M 지속

10시 이후 우선:

- 조정 중 상대강도 유지
- 거래대금 감소 후 재확대
- 눌림 회복
- 전고점 회복·돌파
- Top-N 체류율
- 강도 고점 대비 낙폭

공통 Feature Engine은 한 번만 계산한다. 시간대별 `ScoringProfile`이 공통 피처를 선택·정렬한다.

`1위 = 매수`가 아니다.

Gate:

- 데이터 준비
- freshness
- 최소 거래대금
- 관리·정지·이상 종목 제외
- 종목풀 시장 레짐
- 최소 절대강도
- Top-M과 후순위 분리도
- 연속 유지조건

Gate 실패 시 `NoTrade`이고 Published Top-M은 비어 있다.

---

# 9. 장후 피처 발굴 선순환

```text
승인 profile vN
→ 장중/인과 replay
→ immutable relative-strength/ranking history
→ 미래 MFE·MAE label 결합
→ TrueLeader / FalseLeader / MissedWinner
→ 리더 사건 직전 피처 비교
→ FeatureHypothesis 등록
→ 날짜 단위 walk-forward
→ ablation
→ shadow 운용
→ profile vN+1 승인
```

같은 날짜에서 발견한 피처를 같은 날짜에 적용해 성능을 주장하지 않는다.

분석 사건:

- Top-10·5·3 최초 진입
- 연속 유지
- ConfirmedLeader
- PersistentLeader
- Weakening
- Top-N 이탈
- 순위 급상승

사건 이후 label:

- 5·10·15·30·60분 수익률
- MFE·MAE
- 목표수익·손실 선도달
- 장 마감까지 최고수익과 최대불리폭

---

# 10. 현재 구현 상태

## 10.1 코드 구현됨 `[C]`

```text
core/stock_pool_engine.h
core/stock_pool_engine.cpp
app/stock_pool_fixture.h
app/stock_pool_fixture.cpp
app/stock_pool_evaluator.h
app/stock_pool_evaluator.cpp
tests/stock_pool_engine_tests.cpp
stock_pool_workbench_main.cpp
stock_pool_workbench_entry.cpp
build_stock_pool.bat
```

## 10.2 현재 UI 코드 범위 `[C]`

- 개발 fixture 12종목
- 조건식·일자·포착시각·timeframe·Top-M toolbar
- 종목풀 그리드
- 다종목 상대강도 차트
- 선택 종목 강조
- Top-M 강조
- 시간축 heatmap
- 인과 replay
- Top-M 백테스트
- 승자 포착 평가
- 종목풀 관리·성과검증·백테스트·시뮬레이션 탭
- 두 시간 레짐
- Top-M 연속 유지 후 Published
- NoTrade Gate

## 10.3 자동 테스트 코드 `[C]`

- 최소 history에서 fail-closed
- 미래 봉 변경이 과거 순위·강도에 영향 없음
- 조기 대장의 Top-2·지속상태
- Top-M 백테스트 trade 생성
- 사후 winner capture

## 10.4 아직 완료되지 않은 것

- `[ ]` 로컬 MSVC 컴파일
- `[ ]` fixture UI 사용자 화면 수용
- `[ ]` workspace JSON
- `[ ]` 실제 1516 Frozen Cohort
- `[ ]` 실제 조건식
- `[ ]` MarketEye
- `[ ]` 실제 600봉 hydration
- `[ ]` 실시간 30초·60초 갱신
- `[ ]` 실제 여러 거래일 성과검증
- `[ ]` 32종목 장중 soak

따라서 현재 상태는 **fixture 기반 첫 수직 절단의 코드 구현 완료**이며, 전체 종목풀 기능 완료가 아니다.

---

# 11. 단계별 구현계획

상세 계획은 `docs/STOCK_POOL_WORKBENCH_IMPLEMENTATION_PLAN.md`를 따른다.

상태 표기:

- `[ ]` 미착수
- `[~]` 구현 중
- `[C]` 코드 구현
- `[T]` 자동 검증
- `[V]` 사용자 화면 수용

## Phase 0 — 독립 빌드

- `[C]` 독립 entry
- `[C]` 독립 build script
- `[ ]` 로컬 build
- `[ ]` `shell.exe` 무영향 확인
- `[ ]` 두 프로그램 동시 실행

## Phase 1 — Fixture UI

- `[C]` 순수 엔진·fixture·evaluator
- `[C]` 단일 UI
- `[C]` 테스트 소스
- `[ ]` 로컬 테스트 통과
- `[ ]` 사용자 화면 수용

## Phase 2 — Workspace·설명 가능성

- 종목풀 설정 JSON
- profile ID/version/hash
- Gate 근거
- 저장 후 readback

## Phase 3 — 1516 Frozen Cohort

- 실제 저장 구조 확인
- cohort 조회
- 미래 label 격리
- 1분 replay
- ranking history JSONL

## Phase 4 — 백테스트 강화

- Top-1/2/3/5 비교
- 비용·MFE·MAE
- 시간대별 성과
- 사건 정렬 피처 분석

## Phase 5 — server32 계약

- 원자적 condition session
- MarketEye metadata·freshness

## Phase 6 — 실제 조건식·MarketEye

- Condition Universe
- Scoring Universe 32
- hydration queue

## Phase 7 — 실시간 순위

- 30초·60초 snapshot
- 편입·탈락
- Top-1·2 발행
- NoTrade

## Phase 8 — 피처 연구

- 가설 등록
- walk-forward
- ablation
- shadow
- profile 승격

## Phase 9 — 운영 수용

- 재연결
- 부분 장애
- 32종목 soak
- 실제 사용자 화면 수용

---

# 12. 다음 세션의 첫 작업

다른 기능을 먼저 시작하지 않는다.

```powershell
Set-Location "E:\2026\gpt\cpp\shell"
git pull
.\build_stock_pool.bat
```

성공 표식:

```text
stock pool engine tests passed
*** BUILD OK -> stock_pool_workbench.exe [fixture vertical slice] ***
Existing shell.exe was not modified.
```

실행:

```powershell
.\stock_pool_workbench.exe
```

첫 화면 수용 항목:

1. fixture 12종목 로드
2. `재생` 또는 `1스텝`으로 순위 변화
3. 좌측 종목풀 그리드 갱신
4. 우측 다종목 상대강도선 갱신
5. 선택 종목 굵게 강조
6. Top-M 연속 유지 후 `*` Published
7. `백테스트` 결과와 heatmap
8. 성과검증의 winner capture
9. 시뮬레이션 slider의 인과 replay
10. 기존 `shell.exe` 정상 실행

빌드가 실패하면 전체 CI를 실행하지 않는다. 오류 로그를 받아 독립 종목풀 파일만 직접 수정한다.

---

# 13. 관련 설계 문서

- `STOCK_POOL_RANKING_ENGINE_DESIGN.md`
- `STOCK_POOL_WORKBENCH_UI_DESIGN.md`
- `STOCK_POOL_DETAIL_CHART_FREEZE_CONTRACT.md`
- `STOCK_POOL_DUAL_SOURCE_COHORT_DESIGN.md`
- `STOCK_POOL_CAUSAL_LEADER_VALIDATION_CONTRACT.md`
- `LEADER_STRENGTH_SERIES_DESIGN.md`
- `STOCK_POOL_FEATURE_DISCOVERY_FEEDBACK_LOOP.md`
- `STOCK_POOL_TIME_REGIME_SCORING_DESIGN.md`
- `STOCK_POOL_WORKBENCH_IMPLEMENTATION_PLAN.md`

문서 간 충돌 시 이 인수인계 문서와 `ARCHITECTURE_CONSTITUTION.md`의 동결·인과성·단일소유 원칙을 우선한다.
