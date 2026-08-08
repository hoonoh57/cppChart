# 종목풀 분석 워크벤치 단계별 구현계획

## 0. 문서 지위

이 문서는 `stock_pool_workbench.exe`에서 단일 UI로 다음 기능을 완성하기 위한 실행 계획이다.

- 종목풀 관리
- 성과검증
- 백테스트
- 인과 시뮬레이션
- 다종목 상대강도 시계열
- Top-M 선별
- 피처 발굴과 선별 로직 개선

기존 상세차트 `shell.exe`와 상세차트 렌더러·지표·비교 workspace는 동결한다. 종목풀 기능을 이유로 기존 상세차트 소스를 변경하지 않는다.

---

# 1. 최종 목표

조건식으로 포착된 종목 전체를 하나의 축소시장으로 보고, 매 시각까지 공개된 데이터만으로 시장 레짐과 종목별 상대강도·거래대금·지속순위를 계산한다.

```text
조건식 포착종목 전체
→ 축소시장 레짐
→ 종목별 인과적 상대강도
→ Top-1/Top-2 후보
→ 명확한 리더가 없으면 NoTrade
→ 장후 1516 실제 성과와 대조
→ 리더 직전 피처 발굴
→ 다른 거래일 검증
→ 승인된 피처만 다음 profile에 반영
```

목표는 특정 종목을 계속 추종하는 것이 아니라, 포착종목 전체에서 당일 자금이 집중되고 지속되는 상위 1~2종목을 선별하는 능력을 만드는 것이다.

---

# 2. 실행 파일과 책임 경계

```text
shell.exe
  기존 상세차트·주문·지표·비교
  변경 금지

stock_pool_workbench.exe
  종목풀 관리·상대강도·백테스트·시뮬레이션

stock_pool_evaluator
  장후 미래성과 label 결합과 피처 평가
```

공유 허용:

- 정규화된 시세 타입
- 순수 계산 함수
- 종목코드·시장 식별 계약

공유 금지:

- ImGui context
- 상세차트 RenderDocument
- 상세차트 viewport·pane state
- indicator/comparison workspace JSON
- 상세차트 전역 상태

---

# 3. 단일 UI 최종 구성

## 3.1 상단 제어부

- 데이터소스
  - 개발 fixture
  - 1516 과거 포착 snapshot
  - 실시간 조건식
- 조건식 선택
- 거래일
- 포착시각
- timeframe
- Top-M
- 재계산·재생 주기
- 불러오기
- 백테스트
- 재생·일시정지·1스텝·처음

## 3.2 좌측 종목풀 그리드

- 순위
- 종목코드·종목명·시장
- Leader 상태
- 현재 상대강도
- 1분·5분·누적 수익률
- 누적거래대금 percentile
- 거래대금 가속도
- 순위 변화
- Top-M 연속 유지시간
- 데이터 준비·stale 상태
- Published 여부

## 3.3 우측 동시 상대강도 차트

- 포착종목 전체 상대강도 시계열
- 선택 종목 굵게 강조
- 현재 Top-M 강조
- 100 중립선
- 150 강세 기준선
- 180 대장 후보 기준선
- 시간 레짐 전환선
- 조건식 편입·탈락 marker
- Confirmed/Persistent/Weakening marker

## 3.4 하단 탭

### 종목풀 관리

현재 종목의 점수 근거, Gate 통과 여부, 상태, Top-M 공급 여부를 설명한다.

### 성과검증

사후 미래성과는 여기에서만 표시한다. 장중 ranking engine으로 역유입하지 않는다.

### 백테스트

- 전 종목 시간축 heatmap
- Top-M 진입·청산 marker
- 거래 결과
- 승률·평균수익·최고·최저
- Top-1/2/3/5 비교

### 시뮬레이션

- 인과 replay 위치
- 재생속도
- 당시 시장 레짐
- 당시 순위·Top-M
- 입력 timestamp가 `as_of` 이하인지 확인

---

# 4. 데이터소스 계약

## 4.1 실시간 조건식

동적 cohort다.

```text
초기 membership
+ 실시간 편입
+ 실시간 탈락
+ 재편입
```

필수 선행 작업:

- server32 원자적 condition session
- session_id
- revision
- 초기 목록과 delta 순서 보장
- 편입·탈락 sequence

## 4.2 1516 과거 포착 snapshot

고정 cohort다.

사용 허용:

- 거래일
- 조건식
- 포착시각
- 종목코드·종목명·시장

replay 전에 사용 금지:

- 기간수익률
- 최고수익률
- 미래 MFE·MAE
- 실제 결과 순위

```text
Frozen Cohort 확정
→ 포착 이후 1분봉 인과 replay
→ ranking history 고정
→ replay 종료 후 미래성과 결합
```

## 4.3 개발 fixture

UI와 인과성 계약 검증 전용이다. 실제 성능 주장의 근거로 사용하지 않는다.

---

# 5. 분석 universe 계층

```text
Condition Universe
  조건식 전체, 최대 약 200

Scoring Universe
  상세 캔들 분석 대상, 기본 최대 32

Published Top-M
  메인 후보로 발행되는 기본 1~2종목
```

MarketEye와 FID14 누적거래대금으로 전체 후보를 경량 평가한 뒤 상세 캔들 대상 32개를 편성한다.

REST 분봉은 신규 편성 시 600봉을 한 번 요청한다. 이후 30초·60초마다 같은 600봉을 다시 요청하지 않고 실시간 체결로 현재 봉을 갱신한다.

---

# 6. 공통 계산 엔진

모든 시간대가 같은 Feature Engine을 사용한다.

공통 피처:

- 1·3·5·10·15·30분 수익률
- 장 시작 이후 수익률
- 포착 이후 수익률
- 종목풀 percentile 상대강도
- 시장·업종 대비 상대강도
- 누적거래대금 percentile
- 거래대금 속도·가속도
- 체결강도
- 호가 imbalance
- MarketEye 수급
- 순위 속도·가속도
- Top-3·5·10 체류시간
- 상대강도 기울기·곡률·낙폭
- 조정폭·회복률
- 전고점 거리·돌파 품질
- 압축도
- freshness·missing flags

시간대마다 별도 계산기를 만들지 않는다. `ScoringProfile`이 공통 피처를 선택하고 정렬한다.

---

# 7. 시간 레짐

첫 구현은 두 구간으로 검증한다.

```text
OpeningLeadership  09:00~10:00
LaterStructure     10:00~15:20
```

## OpeningLeadership

- 가격·수익률 상대강도
- 거래대금 속도·가속도
- 순위 상승속도
- 상위권 연속 유지
- 약세 시장에서 방어력

## LaterStructure

- 조정 중 상대강도 유지
- 거래대금 감소 후 재확대
- 전고점 회복·돌파
- Top-N 체류율
- 강도 고점 대비 낙폭
- 오후 재점화

두 구간 방식이 단일 profile보다 개선된다는 증거가 확인된 후 6개 시간대로 확장한다.

---

# 8. Ranking과 Gate

`1위 = 매수`가 아니다.

Gate:

- 데이터 준비 완료
- freshness 정상
- 최소 거래대금
- 거래정지·관리·이상 종목 제외
- 종목풀 레짐 거래 허용
- Top-M과 후순위의 분리도
- 최소 절대 상대강도
- 연속 유지조건

Gate 실패 시 순위는 계산해도 Published Top-M은 비운다.

초기 ranking은 임의 가중합보다 설명 가능한 정렬 tuple을 사용한다. 가중합·학습모델은 여러 거래일의 walk-forward 검증 이후에만 도입한다.

---

# 9. 백테스트 WYSIWYG 계약

`백테스트`를 누르면 포착종목 전체를 동일 시간축에 표시한다.

- Y축: 종목별 lane
- X축: 거래시간
- timeframe 단위 cell
- 선택 항목에 따른 색상
  - 상대강도
  - 순위
  - 거래대금
  - Leader 상태
  - Published 여부
- Top-M 진입·청산 marker
- 시간 레짐 배경
- hover 시 당시 원시 피처와 Gate 사유

Top-1·2·3·5를 동일 cohort와 동일 규칙으로 비교한다.

비용 반영 전·후 결과를 구분한다. 최종 실거래 평가는 수수료·세금·슬리피지를 포함한다.

---

# 10. 장후 피처 발굴 선순환

```text
승인 profile vN
→ 장중/인과 replay
→ immutable ranking history
→ 미래 MFE·MAE label 결합
→ TrueLeader/FalseLeader/MissedWinner
→ 사건 직전 피처 비교
→ 가설 등록
→ 날짜 단위 walk-forward
→ ablation
→ shadow 운용
→ profile vN+1 승인
```

같은 거래일에서 발견한 특징을 같은 거래일 성능으로 주장하지 않는다.

---

# 11. 단계별 구현계획

상태 표기:

- `[ ]` 미착수
- `[~]` 구현 중
- `[C]` 코드 구현
- `[T]` 자동 검증
- `[V]` 사용자 화면 수용

## Phase 0 — 상세차트 동결과 독립 빌드

- `[C]` `stock_pool_workbench.exe` 독립 진입점
- `[C]` `build_stock_pool.bat`
- `[C]` 별도 ImGui layout
- `[C]` 기존 `shell.exe`를 삭제·교체하지 않는 빌드
- `[ ]` 로컬 MSVC 빌드 확인
- `[ ]` 독립 실행 화면 수용

완료 기준:

```text
build.bat 실행 전후 shell.exe 동일
build_stock_pool.bat → stock_pool_workbench.exe 생성
두 실행 파일 동시 실행 가능
```

## Phase 1 — Fixture 기반 최소 수직 절단

- `[C]` 결정적 12종목 fixture
- `[C]` 인과적 RankingEngine
- `[C]` 미래 봉 변경 시 과거 순위 불변 테스트
- `[C]` 두 시간 레짐
- `[C]` Top-M 연속 유지 후 Published
- `[C]` 종목풀 그리드
- `[C]` 다종목 상대강도 차트
- `[C]` heatmap
- `[C]` 종목풀 관리·성과검증·백테스트·시뮬레이션 탭
- `[ ]` 로컬 테스트·UI 수용

## Phase 2 — Workspace와 설명 가능성

- `[ ]` `stock_pool_workspace.json`
- `[ ]` 데이터소스·조건식·일자·timeframe·Top-M 저장
- `[ ]` profile ID/version/hash
- `[ ]` 각 종목의 ranking 근거 표시
- `[ ]` Gate 통과·실패 사유
- `[ ]` readback 전체 속성 검증

## Phase 3 — 1516 Frozen Cohort

- `[ ]` 실제 성과검증 저장 구조 확인
- `[ ]` 거래일·조건식·포착시각 그룹 조회
- `[ ]` cohort hash
- `[ ]` 미래 수익 컬럼 격리
- `[ ]` 포착 이후 600봉 hydration
- `[ ]` 1분 replay
- `[ ]` immutable ranking history JSONL
- `[ ]` 기존 1516 label과 장후 결합

## Phase 4 — 백테스트 강화

- `[ ]` Top-1/2/3/5 동시 비교
- `[ ]` MFE·MAE
- `[ ]` 목표수익·손실 선도달
- `[ ]` 수수료·세금·슬리피지
- `[ ]` 시간대별 결과
- `[ ]` TrueLeader/FalseLeader/MissedWinner
- `[ ]` 사건 정렬 T-30/T-15/T-10/T-5/T-1

## Phase 5 — server32 계약 보강

- `[ ]` 원자적 condition session
- `[ ]` 초기 membership과 delta sequence
- `[ ]` condition cache 실시간 갱신
- `[ ]` MarketEye 200종목 초과 명시 오류
- `[ ]` captured_at
- `[ ]` requested/returned count
- `[ ]` missing codes
- `[ ]` partial failure·freshness

## Phase 6 — 실제 조건식·MarketEye 연결

- `[ ]` 조건식 목록 선택
- `[ ]` Condition Universe
- `[ ]` MarketEye 경량 평가
- `[ ]` 최대 32종목 Scoring Universe
- `[ ]` hydration queue·rate limit
- `[ ]` 종목별 준비·stale 상태

## Phase 7 — 실시간 30초·60초 순위

- `[ ]` 실시간 체결 구독
- `[ ]` 현재 1분봉 증분 갱신
- `[ ]` 30초·60초 ranking snapshot
- `[ ]` 편입·탈락·재편입
- `[ ]` 탈락 이후 성과 추적
- `[ ]` Top-1·Top-2 발행
- `[ ]` NoTrade

## Phase 8 — 피처 연구·승격

- `[ ]` 가설 등록소
- `[ ]` discovery/validation/shadow 날짜 분리
- `[ ]` ablation
- `[ ]` walk-forward
- `[ ]` profile version 승격·회수

## Phase 9 — 운영 수용

- `[ ]` 32종목 장중 soak
- `[ ]` 재연결·부분 장애
- `[ ]` 데이터 freshness 경보
- `[ ]` 동일 replay 결정성
- `[ ]` 장애 후 ranking 복구
- `[ ]` 사용자 화면 수용

---

# 12. 현재 코드 기준점

코드 구현됨:

```text
core/stock_pool_engine.h/.cpp
app/stock_pool_fixture.h/.cpp
app/stock_pool_evaluator.h/.cpp
tests/stock_pool_engine_tests.cpp
stock_pool_workbench_main.cpp
stock_pool_workbench_entry.cpp
build_stock_pool.bat
```

현재 연결 상태:

- 개발 fixture: 연결
- 1516 과거 포착: 미연결, fail-closed
- 실시간 조건식: 미연결, fail-closed

따라서 현재 버전을 실제 성능 완료로 표현하지 않는다. 현재 완료 범위는 fixture 기반 독립 UI의 코드 구현이다.

---

# 13. 다음 작업

1. 로컬 `build_stock_pool.bat`
2. 컴파일 오류가 있으면 독립 워크벤치 파일만 수정
3. fixture 화면 확인
4. `shell.exe`가 영향받지 않았는지 확인
5. Phase 2 workspace
6. Phase 3 실제 1516 Frozen Cohort

실제 데이터 연결 전에는 fixture 결과로 수익성을 주장하지 않는다.
