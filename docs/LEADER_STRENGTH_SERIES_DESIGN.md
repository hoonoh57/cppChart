# 종목풀 상대강도 시계열·당일 대장주 추적 설계

## 0. 문서 상태

- 대상 저장소: `hoonoh57/cppChart`
- 대상 브랜치: `p2/kiwoom-mock-gateway`
- 선행 문서: `docs/STOCK_POOL_RANKING_ENGINE_DESIGN.md`
- 상태: **설계 고정 단계 — 기능 코딩 전**

이 문서는 비교종목 `kk` 상대강도 차트처럼 선택 종목이 조건식 종목풀 전체에서 얼마나 강한지를 시간축으로 표시하고, 단순 순간 급등이 아니라 장중 지속적으로 대장주 역할을 하는 종목을 검출하기 위한 계산·UI·검증 계약을 정의한다.

수익을 보장하는 점수는 만들지 않는다. 목표는 이후 MFE·MAE·기간수익률과 반복적으로 연결되는지 검증 가능한 대장주 추적 신호를 만드는 것이다.

---

# 1. 사용자 화면 목표

현재 비교종목 상대강도 패널과 같은 별도 하단 pane에 다음을 표시한다.

```text
종목풀 상대강도
200 ─ 최상위
180 ─ 상위 10%
150 ─ 상위 25%
100 ─ 종목풀 중립
 50 ─ 하위 25%
 20 ─ 하위 10%
  0 ─ 최하위
```

선택 종목의 선은 시간에 따라 이어진다.

- 선이 100 위에 있으면 현재 종목풀보다 강하다.
- 선이 180 이상이면 현재 평가 가능한 종목 중 상위 10% 수준이다.
- 선의 상승 기울기가 크면 상대강도가 빠르게 개선되고 있다.
- 높은 구간을 오래 유지하면 순간 급등이 아니라 지속 대장 가능성이 높다.
- 선이 고점에서 빠르게 꺾이면 대장주 지위 약화로 본다.

`180`, `150`은 수익 보장 임계값이 아니라 횡단면 percentile을 읽기 위한 시각적 기준이다.

---

# 2. 비교 기준은 하나가 아니라 세 개다

진정한 당일 대장주는 다음 세 기준을 모두 이겨야 한다.

## 2.1 시장 지수 기준

```text
KOSPI 종목 → KOSPI 지수 0J
KOSDAQ 종목 → KOSDAQ 지수 0J
```

개별 종목 수익률에서 같은 시장 지수 수익률을 차감한다.

```text
market_excess_h(t) = stock_return_h(t) - market_return_h(t)
```

시장 전체 상승 때문에 함께 오른 종목을 단독 대장으로 오인하지 않기 위한 기준이다.

## 2.2 조건식 종목풀 기준

현재 조건식에 속하거나 당일 추적 상태인 평가 가능 종목들의 동일 기간 수익률을 비교한다.

```text
pool_excess_h(t)
    = stock_return_h(t)
    - weighted_median(pool_return_h(t))
```

평균 대신 거래대금 제곱근 가중 중앙값을 기본으로 사용한다.

```text
weight_i = sqrt(max(1, cum_turnover_i))
```

한두 급등주가 전체 기준선을 과도하게 끌어올리는 것을 막기 위함이다.

## 2.3 유동성 동급군 기준

종목풀 안에서도 누적거래대금 percentile이 비슷한 종목끼리 비교한다.

```text
peer cohort = 현재 누적거래대금 percentile ± 15%p
```

초소형 저유동 종목이 작은 체결만으로 급등하여 높은 상대강도를 받는 왜곡을 줄인다.

---

# 3. 시계열의 핵심값

## 3.1 기간별 수익률

기본 관찰 구간:

```text
1분
3분
5분
15분
장 시작 이후
조건식 편입 이후
```

단순 수익률을 원시값으로 보존한다.

```text
return_h(t) = current_price(t) / price(t-h) - 1
```

장 시작 이전 데이터가 없거나 기준시각 가격이 불명확하면 해당 기간은 `not_evaluable`로 둔다.

## 3.2 기간별 횡단면 percentile

각 기간의 `pool_excess_h`를 현재 평가 가능 종목들 사이에서 percentile로 변환한다.

```text
percentile_h(t) ∈ [0, 100]
```

결정적 tie-break는 종목코드 오름차순이다.

## 3.3 대표 상대강도선

임의 가중합 대신 기간별 percentile의 중앙값을 사용한다.

```text
raw_strength_percentile(t)
    = median(
        percentile_1m,
        percentile_3m,
        percentile_5m,
        percentile_15m
      )
```

평가 가능한 기간이 2개 미만이면 대표 상대강도를 발행하지 않는다.

차트용 값:

```text
leader_strength(t) = 2 × raw_strength_percentile(t)
```

따라서:

```text
leader_strength = 100 → 종목풀 중앙
leader_strength = 180 → 상위 10%
leader_strength = 190 → 상위 5%
leader_strength = 200 → 최상위
```

이 값은 확률이나 예상수익률이 아니다. 현재 종목풀에서의 상대적 위치다.

## 3.4 안정화선

원본선은 그대로 저장하고 화면에는 짧은 EMA 안정화선을 기본 표시한다.

```text
strength_smoothed(t)
    = EMA(raw_strength, 3 snapshots)
```

원본값을 덮어쓰지 않는다. Tooltip과 검증 로그에는 raw/smoothed를 모두 제공한다.

---

# 4. 강도 추세 계산

강도값이 큰 것만으로 대장주를 판정하지 않는다. 방향과 지속시간이 필요하다.

## 4.1 단기 기울기

```text
strength_slope_3
strength_slope_5
strength_slope_10
```

최근 3·5·10개 rank snapshot의 선형회귀 기울기를 계산한다.

snapshot 간격이 다르면 실제 경과초를 X축으로 사용한다.

## 4.2 가속도

```text
strength_acceleration
    = recent_slope_3 - previous_slope_3
```

강도가 높지만 둔화되는 종목과 낮은 구간에서 빠르게 치고 올라오는 종목을 분리한다.

## 4.3 고점 대비 강도 낙폭

```text
strength_drawdown
    = session_peak_strength - current_strength
```

가격 고점 대비 낙폭과 별도로 관리한다.

## 4.4 강도 지속성

```text
top10_ratio_5m
top10_ratio_15m
top5_ratio_5m
top5_ratio_15m
consecutive_top10_snapshots
consecutive_top5_snapshots
strength_integral_above_160
strength_integral_above_180
```

시간적분은 실제 경과초를 사용한다.

```text
integral += max(0, strength - threshold) × delta_seconds
```

---

# 5. 대장주 상태기계

한 줄의 점수 대신 상태를 발행한다.

```text
NotEvaluable
Emerging
ConfirmedLeader
PersistentLeader
Weakening
FailedLeader
```

## 5.1 Emerging

다음 특성이 함께 나타나는 종목이다.

```text
상대강도 percentile 급상승
양의 strength slope
rank velocity 상승
거래대금 가속도 상위권
실시간 데이터 정상
```

절대 임계값은 초기 구현에서 고정하지 않는다. 횡단면 순위로 판정한다.

## 5.2 ConfirmedLeader

다음을 모두 충족한 시점의 event를 기록한다.

```text
현재 strength가 상위 10% 구간
최근 K개 snapshot 중 대부분이 상위 20%
5분·15분 pool excess가 모두 양수
누적거래대금과 거래대금 가속도가 최소 평가 범위 충족
가격이 세션 고점에서 과도하게 이탈하지 않음
```

K와 허용 비율은 검증 profile에 저장한다. 코드에 숨은 매직넘버로 넣지 않는다.

## 5.3 PersistentLeader

```text
ConfirmedLeader 이후 상위권 체류 지속
strength_integral 상위권
Top-N 이탈 횟수 제한
strength drawdown 제한
```

장중 한 번 튄 종목이 아니라 계속 시장의 관심과 자금을 받는 종목이다.

## 5.4 Weakening

```text
strength는 높지만 slope가 음수
strength drawdown 증가
rank drawdown 증가
거래대금 가속도 둔화
가격이 고점에서 밀림
```

## 5.5 FailedLeader

```text
상위 10%에서 중위권 아래로 급락
연속 Top-N 유지 종료
strength slope와 price slope가 동시에 음수
재상승 확인 없이 stale 또는 condition exit
```

조건식 탈락만으로 즉시 실패 처리하지 않는다. 탈락 후 유예기간 동안 가격·강도·수익을 계속 추적하여 탈락의 예측력을 평가한다.

---

# 6. 대장주 판정은 사전식 정렬로 시작한다

초기에는 검증되지 않은 가중합으로 하나의 점수를 만들지 않는다.

## 6.1 지속 대장 프로필

```text
1. current_strength 내림차순
2. top10_ratio_15m 내림차순
3. strength_integral_above_160 내림차순
4. pool_excess_15m 내림차순
5. cum_turnover 내림차순
6. strength_drawdown 오름차순
7. code 오름차순
```

## 6.2 급부상 프로필

```text
1. strength_slope_5 내림차순
2. strength_acceleration 내림차순
3. rank_velocity_5m 내림차순
4. turnover_acceleration percentile 내림차순
5. current_strength 내림차순
6. code 오름차순
```

## 6.3 수급 확인 대장 프로필

```text
1. current_strength 내림차순
2. program_net_delta percentile 내림차순
3. institution_net_delta percentile 내림차순
4. intensity percentile 내림차순
5. turnover_acceleration percentile 내림차순
6. code 오름차순
```

MarketEye 수급값에 캡처시각과 freshness가 없거나 stale이면 이 프로필에서는 해당 종목을 제외한다.

---

# 7. UI 계약

## 7.1 차트 하단 pane

Pane 제목 예시:

```text
종목풀 상대강도 · 현재 186 · 상위 7% · 5분 기울기 +4.8
```

기본 요소:

- 선택 종목 상대강도선
- 100 중립선
- 150·180 기준선
- raw line 선택 표시
- smoothed line 기본 표시
- Leader 상태 변화 marker
- 조건식 enter/exit marker
- stale/data-gap marker

색은 일반 presentation metadata로 관리하며 renderer에 대장주 전용 분기를 넣지 않는다.

## 7.2 Tooltip

```text
시각
평가 가능 종목 수
universe revision
상대강도 raw/smoothed
1m/3m/5m/15m percentile
시장초과수익률
종목풀초과수익률
순위와 순위변화
5분/15분 Top-10 체류비율
strength slope/acceleration/drawdown
누적거래대금
거래대금 가속도
체결강도
MarketEye captured_at/freshness
현재 leader state
```

## 7.3 종목풀 표

```text
순위
종목코드/종목명
대장 상태
현재 강도
5분 기울기
Top-10 15분 체류율
15분 종목풀 초과수익률
누적거래대금
거래대금 가속도
MFE/MAE는 replay·사후평가에서만 표시
```

행을 클릭하면 메인 차트 종목이 변경되고 동일 상대강도 pane이 유지된다.

## 7.4 Top-N 다중선

선택 옵션:

```text
선택 종목만
현재 Top-5 얇은 보조선
현재 Top-10 heat band
```

기본은 선택 종목 한 줄만 굵게 표시한다. 다중선은 화면 혼잡을 막기 위해 명시적으로 켠 경우에만 사용한다.

---

# 8. 동적 종목풀로 인한 왜곡 방지

조건식 편입·탈락으로 비교 대상이 바뀌면 percentile이 가격 변화 없이 움직일 수 있다.

따라서 모든 강도 point는 다음 메타데이터를 가진다.

```text
universe_revision
universe_member_count
evaluable_member_count
benchmark_revision
feature_version
```

## 8.1 최소 평가 종목 수

평가 가능 종목 수가 profile의 최소치보다 작으면 강도선을 발행하지 않는다.

```text
예: evaluable_member_count < 8 → NotEvaluable
```

숫자는 profile 설정이며 데이터 검증으로 조정한다.

## 8.2 편입 직후 warm-up

편입 즉시 1분·3분 수익률만 존재한다고 15분 상대강도와 동일하게 취급하지 않는다.

```text
평가 가능한 horizon 수
이력 준비시간
MarketEye freshness
실시간 tick freshness
```

을 명시하고 대표 강도를 계산한다.

## 8.3 frozen cohort 보조선

분석 모드에서는 특정 시각의 종목풀을 고정한 보조선을 제공할 수 있다.

```text
09:05 cohort
FirstTopNEntry cohort
사용자 지정 snapshot cohort
```

실시간 운영선과 frozen cohort 선을 분리하여 종목풀 구성 변화 효과를 분석한다.

---

# 9. 데이터 수집 최적화

## 9.1 전 종목

```text
조건식 membership
키움 실시간 FID 14 누적거래대금
현재가·등락률·누적거래량·체결강도
MarketEye 30초 또는 60초 batch
```

## 9.2 상세분석 승격 종목

```text
1분봉 이력 1회 hydration
실시간 tick으로 live bar 증분 갱신
완료 1분봉마다 기간 특징 O(1) 갱신
```

## 9.3 계산 복잡도

후보 수 `N ≤ 200`이면 매 snapshot의 횡단면 정렬은 `O(N log N)`이다.

200종목 기준으로 C++ 정렬비용은 미미하다. 실제 병목은 네트워크·COM·분봉 hydration이므로 다음을 우선한다.

- MarketEye는 전체 후보를 한 batch로 조회
- 분봉은 admission queue로 제한
- 동일 종목 이력 중복 요청 금지
- feature window는 ring buffer와 rolling aggregate 사용
- UI는 immutable snapshot만 읽음

---

# 10. 미래성과 연결

각 상대강도 point와 state event 이후 다음 label을 기록한다.

```text
return_5m
return_15m
return_30m
return_60m
mfe_5m / mae_5m
mfe_15m / mae_15m
mfe_30m / mae_30m
mfe_60m / mae_60m
mfe_until_close / mae_until_close
time_to_mfe
time_to_plus_0_5
time_to_plus_1_0
hit_target_before_stop
```

특히 다음 event를 독립 평가한다.

```text
StrengthTop10Cross
StrengthTop5Cross
EmergingStarted
ConfirmedLeaderStarted
PersistentLeaderStarted
WeakeningStarted
FailedLeaderStarted
```

---

# 11. 최적화 목적함수

최고수익률 하나만 최대화하지 않는다.

## 11.1 필수 평가

```text
Average future return@N
Median MFE@N
Median MAE@N
MFE / abs(MAE)
Target-before-stop hit rate
Rank decile monotonicity
Top-N turnover
Median leader dwell time
False leader rate
Data-ready coverage
```

## 11.2 진정한 대장주 판정 기준

좋은 로직은 다음을 동시에 만족해야 한다.

1. 상대강도 상위 decile일수록 미래 MFE가 단조롭게 증가한다.
2. PersistentLeader가 Emerging보다 평균 MAE가 낮다.
3. Top-N 교체가 지나치게 잦지 않다.
4. 여러 거래일의 out-of-sample에서도 성능이 유지된다.
5. 특정 하루의 급등주 몇 개에만 의존하지 않는다.
6. 조건식 편입시각이 늦어도 FID 14와 MarketEye 때문에 구조적으로 불리하지 않다.

---

# 12. C++ 모듈 추가

선행 종목풀 설계의 모듈에 다음 책임을 추가한다.

```text
core/leader_strength_engine.*
  기간별 excess return
  횡단면 percentile
  대표 강도·기울기·가속도·낙폭

core/leader_state_machine.*
  Emerging/Confirmed/Persistent/Weakening/Failed

app/leader_strength_render_adapter.*
  generic line/reference/marker contribution

ui/leader_strength_panel_ui.*
  종목풀 표와 설명 tooltip
```

`leader_strength_engine`은 정규화된 가격·benchmark·universe snapshot만 입력받는 순수 계산 모듈이어야 한다.

---

# 13. 구현 순서

```text
LS-01  순수 strength types와 percentile helper
LS-02  기간별 market/pool excess 계산
LS-03  대표 강도 0~200 mapping
LS-04  slope/acceleration/drawdown/persistence
LS-05  leader state machine
LS-06  결정적 replay tests
LS-07  stock-pool snapshot 연결
LS-08  generic render contribution
LS-09  종목풀 표와 chart pane
LS-10  append-only rank/strength/event 기록
LS-11  미래 MFE/MAE label 결합
LS-12  실제 조건식 하루 관찰
LS-13  여러 거래일 walk-forward 평가
```

server32의 원자적 조건식 세션과 MarketEye 캡처 메타데이터 보강이 `LS-07` 이전 선행조건이다.

---

# 14. 수용 기준

## 14.1 계산 수용

- 동일 event replay에서 동일 강도 point와 상태가 생성된다.
- 미래 데이터 없이 모든 point가 계산된다.
- 종목풀 편입·탈락과 universe revision이 기록된다.
- stale·누락 데이터가 정상 저점수로 위장하지 않는다.
- 현재 종목과 시장·종목풀 benchmark timestamp가 일치한다.

## 14.2 화면 수용

```text
조건식 선택
→ 종목풀 가동
→ 종목 행 선택
→ 메인 차트와 하단 종목풀 상대강도 표시
→ 100/150/180 기준선 확인
→ 강도 상승·하락과 leader state marker 확인
→ 다른 종목 선택 시 동일 기준으로 즉시 비교
→ 재실행 후 설정과 pane 높이 복원
```

## 14.3 전략 수용

`대장주 선별 완료`라는 표현은 여러 거래일 out-of-sample에서 다음이 확인된 뒤에만 사용한다.

- 상위 강도 구간의 미래성과 단조성
- 지속 대장 state의 MFE/MAE 개선
- 과도하지 않은 Top-N 교체율
- 데이터 누락·stale 제외가 정상 작동
- 사용자가 실제 화면에서 강도·상태·종목 전환을 확인

그 전 상태는 `설계`, `코드 구현`, `자동 검증`, `실화면 관찰`, `성과 검증`으로 구분한다.
