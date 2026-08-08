# 종목풀 상대강도 피처 발굴·개선 선순환 설계

## 0. 문서 지위

- 대상 시스템:
  - `stock_pool_workbench.exe`: 미래정보 없는 실시간·인과 replay·순위 생성
  - `stock_pool_evaluator.exe`: 장 종료 후 immutable ranking history와 미래 성과 label 결합
- 상세차트 `shell.exe`: 수정 금지
- 기존 1516 성과검증 화면: 재구현 금지, 결과 label의 원본으로만 사용
- 상태: 구현 전 최상위 연구·개선 계약

이 문서는 다종목 상대강도 시계열을 장 종료까지 생성하고, 상대강도 상승 이후 실제 수익 가능 구간과 그 이전에 집약된 특징을 분석하여, 검증된 특징만 다음 버전 선별 로직으로 승격시키는 폐쇄형 개선 루프를 정의한다.

---

# 1. 시스템의 두 역할

## 1.1 장중·replay 역할: 인과적 선별

```text
시각 t까지 공개된 데이터
→ 종목별 피처 계산
→ 다종목 횡단면 상대강도
→ 현재 순위·상태·Top-N
→ immutable snapshot 저장
```

장중 엔진은 미래 수익률, 장 마감 결과, 이후 고가를 알지 못한다.

## 1.2 장후 역할: 피처 발굴

```text
immutable 상대강도·순위 history
+ 이후 실제 가격 경로
+ 1516 기간수익률·최고수익률 label
→ 유의미한 리더 사건과 수익 가능 구간 식별
→ 사건 직전 피처의 공통성·차별성 분석
→ 새 가설 생성
→ 별도 거래일 walk-forward 검증
```

장후 분석은 과거 snapshot을 다시 계산하거나 수정하지 않는다.

---

# 2. 폐쇄형 개선 루프

```text
[1] 현재 승인된 Leader Profile vN
        ↓
[2] 장중 또는 인과 replay에서 상대강도·순위 생성
        ↓
[3] 장 종료까지 immutable history 축적
        ↓
[4] evaluator가 이후 MFE·MAE·수익구간 label 결합
        ↓
[5] 진짜 리더·거짓 리더·늦은 리더 사건 분류
        ↓
[6] 사건 전 피처 집중도·선행성·안정성 분석
        ↓
[7] 후보 피처·규칙 가설 등록
        ↓
[8] 다른 거래일로 walk-forward·ablation 검증
        ↓
[9] shadow 운용
        ↓
[10] 기준 통과 시 Leader Profile vN+1 승격
```

같은 거래일에서 발견한 특징을 같은 거래일에 적용하여 성능을 주장하지 않는다.

---

# 3. 저장해야 할 인과적 시계열

각 종목·각 `as_of` 시각마다 다음을 저장한다.

## 3.1 원시 시장 피처

```text
price/open/high/low/previous_close
1m/3m/5m/10m/15m/30m return
session return
since-detection return
volume and cumulative volume
FID14 cumulative turnover
turnover velocity/acceleration
trade intensity
bid/ask imbalance
MarketEye foreign/institution/individual/program flow
market/index/sector return
freshness and missing-data flags
```

## 3.2 횡단면 피처

```text
각 rolling return의 cohort percentile
시장 대비 초과수익 percentile
종목풀 중앙값 대비 초과수익 percentile
누적거래대금 percentile
거래대금 가속도 percentile
체결강도 percentile
수급 percentile
```

## 3.3 경로 피처

```text
current rank
rank change 1/3/5/10 snapshots
rank velocity and acceleration
best/worst rank of day
Top-3/5/10 continuous duration
Top-3/5/10 occupancy ratio
relative-strength slope/curvature
relative-strength drawdown from intraday peak
number of new strength highs
failed breakout count
```

## 3.4 상태

```text
WarmingUp
Emerging
ConfirmedLeader
PersistentLeader
Weakening
FailedLeader
Stale
NotEvaluable
```

모든 row는 `as_of`, `feature_schema_version`, `leader_profile_version`, `cohort_id`, `cohort_revision`을 포함한다.

---

# 4. 장후에 식별할 사건

단순 최종 수익률만 비교하지 않고 사건 단위로 분석한다.

## 4.1 리더 사건

```text
최초 Top-10 진입
최초 Top-5 진입
Top-5 K회 연속 유지
ConfirmedLeader 전환
PersistentLeader 전환
당일 최고 상대강도 갱신
순위 급상승
Weakening 전환
Top-N 이탈
FailedLeader 전환
```

## 4.2 수익 가능 구간

사건 시각 이후 다음 label을 계산한다.

```text
5/10/15/30/60분 forward return
5/10/15/30/60분 MFE
5/10/15/30/60분 MAE
장 마감까지 MFE/MAE
+0.5%/+1%/+2% 도달 여부와 도달시간
손실 -0.5%/-1% 도달 여부와 도달시간
목표수익이 위험기준보다 먼저 발생했는지
수익 가능 구간 시작·종료 시각
```

`수익 가능 구간`은 사후 label이며 장중 UI에 노출하지 않는다.

---

# 5. 탑리더 피처를 역으로 찾는 방법

## 5.1 사건 정렬 분석

각 실제 리더 사건을 기준으로 과거 window를 정렬한다.

```text
T-30분
T-15분
T-10분
T-5분
T-3분
T-1분
T = ConfirmedLeader 또는 첫 유의미한 수익구간 직전
```

각 시점의 피처 분포를 비교한다.

## 5.2 비교 cohort

최소 세 그룹을 구성한다.

```text
TrueLeader
  사건 이후 높은 MFE와 제한된 MAE를 보인 종목

FalseLeader
  강도 상위에 올랐지만 이후 MFE가 낮거나 MAE가 큰 종목

MissedWinner
  사후 고수익이었지만 엔진이 제때 Top-N에 올리지 못한 종목
```

필요하면 `LateLeader`, `PersistentLeader`, `OneShotSpike`를 추가한다.

## 5.3 분석 질문

```text
TrueLeader에서만 먼저 상승한 피처는 무엇인가?
FalseLeader와 구분되는 거래대금·수급·순위 경로는 무엇인가?
MissedWinner에서 현재 엔진이 보지 못한 공통 특징은 무엇인가?
강도 상승 후 어느 시점까지 진입 가능성이 유지되는가?
강도 자체보다 강도 기울기·지속시간이 더 예측적인가?
시장 약세/강세, KOSPI/KOSDAQ, 시가총액군별로 패턴이 같은가?
```

---

# 6. 피처 가설 등록소

발견 즉시 운영 점수에 넣지 않는다. 모든 가설은 등록소를 거친다.

```cpp
struct FeatureHypothesis
{
    std::string id;
    std::string description;
    std::string sourceAnalysisId;
    std::string featureSchemaVersion;
    std::string proposedRule;
    std::vector<std::string> discoveryDates;
    std::vector<std::string> reservedValidationDates;
    std::string status;
};
```

상태:

```text
Observed
Candidate
Backtested
WalkForwardPassed
Shadow
Approved
Rejected
Retired
```

가설 예:

```text
5분 상대강도 percentile 상승
+ Top-10 연속 3회
+ 누적거래대금 가속도 상위 20%
+ 상대강도 고점 대비 낙폭 5% 이내
```

이 조건은 설명 가능한 독립 항목으로 보존하며, 임의 가중합으로 숨기지 않는다.

---

# 7. 검증 승격 절차

## 7.1 탐색 데이터와 검증 데이터 분리

```text
Discovery dates
  패턴 발견·가설 생성에 사용

Validation dates
  가설 고정 후 한 번만 평가

Shadow dates
  장중 계산하지만 화면 순위와 주문에는 영향 없음
```

날짜 단위로 분리하며 종목 row를 무작위 분할하지 않는다.

## 7.2 필수 평가

```text
실제 Top-K winner 포착률
Top-N precision
첫 포착 선행시간
선정 이후 MFE/MAE
목표수익 선도달률
Top-N 체류시간과 미래성과의 단조성
시장·조건식·시가총액·시간대별 안정성
데이터 누락 시 성능 저하
```

## 7.3 ablation

새 피처를 추가하기 전후를 동일 replay에서 비교한다.

```text
baseline profile vN
baseline + candidate feature
baseline - existing feature
```

새 피처가 단순히 기존 피처를 복제하는지 확인한다.

## 7.4 승격 조건

다음이 모두 충족돼야 승인한다.

```text
out-of-sample 개선
여러 날짜·시장 regime에서 방향 일관
거짓 양성 증가가 허용 범위 이내
MFE 개선과 MAE 악화의 trade-off 명시
실시간 계산 비용과 데이터 freshness 충족
재현 가능한 feature definition
```

---

# 8. UI 분리

## 8.1 stock_pool_workbench.exe

미래정보 없는 화면만 제공한다.

```text
조건식/일자/source 선택
종목풀 grid
동시 다종목 상대강도 chart
현재 Top-N
leader state
현재 피처와 순위 근거
```

## 8.2 stock_pool_evaluator.exe

장후 연구 화면을 제공한다.

```text
거래일·조건식·profile version 선택
immutable 상대강도 history
실제 수익 가능 구간 overlay
리더 사건 marker
TrueLeader/FalseLeader/MissedWinner 비교
사건 전 feature heatmap/table
가설 등록
walk-forward report
```

1516의 기존 기간수익률 차트를 복제하지 않는다. 필요한 미래 outcome은 기존 결과 테이블/계약에서 label로만 읽는다.

## 8.3 synchronized research view

한 종목 또는 여러 종목을 선택하면 같은 시간축으로 다음을 동기화한다.

```text
상대강도 시계열
순위 시계열
누적거래대금·가속도
체결강도·수급
leader state
사후 수익 가능 구간(label, evaluator only)
```

커서를 이동하면 그 시점의 모든 피처 값과 percentile을 보여준다.

---

# 9. 데이터 불변성

```text
data/stock_pool_history/<date>/<cohort>/<profile>.jsonl
  장중·replay 인과 snapshot, append-only

data/stock_pool_labels/<date>/<cohort>.json
  장후 미래 outcome label

data/stock_pool_hypotheses/registry.json
  피처 가설과 상태

data/stock_pool_reports/<analysis_id>/...
  재현 가능한 분석 산출물
```

ranking history에 미래 label을 덧붙여 원본을 수정하지 않는다.

모든 보고서는 다음 hash/version을 기록한다.

```text
cohort hash
candle source hash
feature schema version
leader profile version
evaluator version
1516 result group id
```

---

# 10. 자동화와 사람의 역할

자동화 가능:

```text
사건 추출
피처 window 생성
그룹별 분포·effect size 계산
rank correlation
monotonicity 검사
후보 피처 조합의 replay
walk-forward report
```

사람이 승인해야 하는 것:

```text
새 피처의 경제적·시장구조적 의미
데이터 누출 가능성 검토
복잡도 대비 효과
실전 위험과 실패 사례
Leader Profile 승격
```

자동 탐색기가 발견한 규칙을 바로 실시간 운영에 배포하지 않는다.

---

# 11. 첫 구현 수직 절단

```text
1. fixture 10종목의 causal ranking history 생성
2. 장 종료까지 상대강도·순위·상태 저장
3. 별도 labeler로 15/30/60분 MFE·MAE 결합
4. ConfirmedLeader 사건 추출
5. T-10/T-5/T-1 피처 snapshot 생성
6. TrueLeader와 FalseLeader 비교표
7. 단일 후보 피처 가설 등록
8. 다른 fixture 날짜에서 walk-forward
9. baseline 대비 ablation report
10. 승인 전 shadow 상태로 종료
```

이 수직 절단이 통과하기 전에는 자동 피처 조합 탐색이나 종합 가중점수 학습을 시작하지 않는다.

---

# 12. 완료 기준

```text
상대강도 시계열은 미래정보 없이 반복 재현된다.
장후 label 결합이 원본 history를 수정하지 않는다.
탑리더 사건 이전 피처를 시간축으로 역추적할 수 있다.
TrueLeader/FalseLeader/MissedWinner를 같은 계약으로 비교할 수 있다.
가설의 발견 날짜와 검증 날짜가 분리된다.
새 피처의 추가 효과가 ablation으로 증명된다.
승격된 profile은 versioned artifact로 저장된다.
상세차트 shell.exe는 변경되지 않는다.
```

이 계약을 위반하는 개선은 화면상 성능이 좋아 보여도 승인하지 않는다.
