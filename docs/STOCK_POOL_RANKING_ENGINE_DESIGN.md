# 조건식 기반 종목풀·지속 순위·성과검증 엔진 설계

## 0. 문서 상태

- 대상 저장소: `hoonoh57/cppChart`
- 대상 브랜치: `p2/kiwoom-mock-gateway`
- 외부 데이터 서버: `hoonoh57/server32`
- 상태: **설계 고정 단계 — 기능 코딩 시작 전**
- 목적: 조건식 편입/탈락 종목을 효율적으로 수집·분석·순위화하고, 장중 지속 순위가 이후 수익률·최고수익률과 실제로 연결되는지를 재현 가능하게 검증한다.

이 문서는 구현 항목을 임의 축소하지 않기 위한 실행 계약이다. 구현 중 새로운 사실이 발견되면 설계 변경 사유, 데이터 계약 변경, 검증 영향도를 먼저 기록한다.

---

# 1. 최종 목표

선택한 키움 조건식에서 실시간으로 편입·탈락하는 종목을 원본 후보군으로 삼는다. 모든 후보를 동일 비용으로 처리하지 않고, 저비용 일괄 데이터와 고비용 시계열 데이터를 계층적으로 배분한다.

```text
조건식 실시간 편입/탈락
→ 원본 후보군 유지
→ MarketEye + 키움 실시간 누적값으로 전 종목 경량 평가
→ 상세분석 대상 승격/강등
→ 승격 종목 1분봉 이력 1회 준비
→ 실시간 틱으로 봉과 특징을 증분 갱신
→ 30초/60초 불변 순위 Snapshot 발행
→ Top-N 종목풀 패널 공급
→ 순위 당시 상태와 이후 MFE/MAE/기간수익률 결합
→ 선별 로직의 예측력 검증 및 버전 개선
```

핵심 목표는 특정 시점의 점수를 예쁘게 표시하는 것이 아니다.

1. 장중 계속 상위에 머문 종목이 실제로 이후 최고수익률도 높은지 검증한다.
2. 순간 순위 상승이 이후 단기 수익으로 이어지는지 검증한다.
3. 누적 거래대금, 상대강도, 수급, 체결강도 중 어떤 원시 특성이 유효한지 분리 검증한다.
4. 당시 알 수 있었던 정보만으로 계산한 순위를 append-only 기록으로 재현한다.
5. 동일 입력 이벤트를 재생하면 C++ 엔진이 동일 순위와 설명을 산출해야 한다.

---

# 2. 비목표와 금지사항

## 2.1 초기 비목표

- 첫 구현부터 자동매수·자동매도를 연결하지 않는다.
- 검증되지 않은 가중합을 최종 매매점수로 선언하지 않는다.
- 조건식 전체 종목에 매 주기마다 600봉 REST 요청을 반복하지 않는다.
- 수십 개 종목을 모두 메인 차트에 항상 렌더링하지 않는다.
- MarketEye를 실시간 틱 또는 정밀 시계열의 대체재로 사용하지 않는다.

## 2.2 금지사항

- 누락 시세·봉·수급값을 0이나 직전값으로 합성해 점수화하지 않는다.
- 데이터 준비가 안 된 종목을 낮은 점수의 정상 종목으로 취급하지 않는다.
- 조건식 탈락 종목을 기록에서 삭제하여 사후성과를 보지 못하게 하지 않는다.
- 현재 시점 이후의 고가·수익률을 점수 계산에 사용하지 않는다.
- UI 스레드에서 REST 요청, MarketEye COM 호출, 다종목 특징 계산, 순위 정렬을 수행하지 않는다.
- renderer에 조건식명·종목명·점수명·프로필명에 따른 분기를 추가하지 않는다.
- source별 유사 상태와 별도 순위 로직을 중복 구현하지 않는다.

---

# 3. 현재 확인된 실제 데이터 계약

## 3.1 server32 조건식 API

현재 `server32/WebApiServer.vb`에는 다음 API가 존재한다.

```text
GET /api/conditions
GET /api/conditions/search?name={name}&index={index}
GET /api/conditions/start?name={name}&index={index}&screen={screen}
GET /api/conditions/stop?name={name}&index={index}&screen={screen}
```

`RealtimeDataService.OnReceiveRealCondition`은 `/ws/realtime`에 다음 형태의 증분 이벤트를 방송한다.

```json
{
  "type": "condition",
  "code": "005930",
  "timestamp": "yyyyMMddHHmmss",
  "data": {
    "condition_name": "조건식명",
    "condition_index": "0",
    "state": "enter | exit"
  }
}
```

### 현재 계약의 결함

`StartConditionStreamAsync()`는 실시간 `SendCondition(..., 1)` 시작 성공만 반환한다. 시작에 의해 발생하는 초기 `OnReceiveTrCondition` 목록은 pending search가 없으면 로그만 남고 WebSocket이나 start 응답으로 전달되지 않는다.

따라서 현재 상태 그대로는 다음 경쟁조건이 존재한다.

```text
실시간 시작 전 snapshot 조회
→ snapshot과 start 사이의 편입/탈락을 놓칠 수 있음

실시간 시작 후 snapshot 조회
→ snapshot 완료 전 들어온 delta의 적용 순서를 별도로 보장해야 함
```

또한 조건식 단건 검색 캐시는 60초 TTL이고 실시간 enter/exit가 캐시를 갱신하지 않는다. 재연결 후 강제 재조회를 해도 TTL 안에서는 오래된 snapshot이 반환될 수 있다.

## 3.2 server32 키움 실시간 데이터

현재 `/ws/realtime` tick 데이터에서 확인된 핵심 필드:

```text
current_price
rate
volume                  체결량
cum_volume              당일 누적거래량
cum_turnover_million    FID 14, 당일 누적거래대금 백만원
open
high
low
intensity               체결강도
```

호가 데이터에는 다음이 존재한다.

```text
total_ask_vol
total_bid_vol
ask_price_1..5
ask_vol_1..5
bid_price_1..5
bid_vol_1..5
```

장중 늦게 조건식에 편입된 종목도 FID 14의 세션 누적거래대금을 받을 수 있으므로, 포착 이후 체결가×체결량만 다시 더해 당일 거래대금을 축소 계산해서는 안 된다.

## 3.3 server32 MarketEye

현재 API:

```text
GET /api/cybos/marketeye/supply?codes=005930;000660;...
```

`Cybos.FetchMarketEyeSupply()`의 실제 계약:

- 최대 200종목
- 최대 64필드 가능
- 현재 선택 필드 24개
- 한 번의 `CpSysDib.MarketEye.BlockRequest()`로 조회

현재 반환 필드:

```text
종목코드
종목명
현재가
대비부호
전일대비
시가
고가
저가
거래량
거래대금_원
전일거래량
체결강도
총매도호가잔량
총매수호가잔량
호가잔량비율
외국인보유비율
외국인순매매_주
프로그램순매수
당일외국인잠정구분
당일외국인순매수
당일기관잠정구분
당일기관순매수
당일개인잠정구분
당일개인순매수
신용잔고율
```

서버 도움말에는 15초 60건 기준, 200종목 1회가 1건으로 기록돼 있다. 종목풀에서는 이를 상한으로 사용하지 않고 더 보수적으로 30초 또는 60초에 한 번의 전체 batch만 허용한다.

### 현재 MarketEye 계약의 결함

- 200개 초과 요청을 오류로 거부하지 않고 앞 200개만 조용히 사용한다.
- 응답에 `captured_at`, 요청 수, 반환 수, 누락 코드, partial failure가 없다.
- 각 값의 freshness와 잠정/확정 상태를 엔진이 공통적으로 판단할 메타데이터가 없다.
- COM 오류 시 빈 배열과 정상적인 0건을 명확히 구분하기 어렵다.

이 결함은 순위 엔진 연결 전에 server32 계약 보강 대상으로 둔다.

## 3.4 cppChart 현재 기반

현재 cppChart는 다음 기반을 이미 가진다.

- 실제 종목분봉과 `0B` 실시간
- 지수분봉과 `0J` 실시간
- 최대 32개 비교 정의
- RawClose / Indexed100 / ReturnPercent / RelativeStrength100
- 첫 공통 timestamp 고정 anchor
- immutable completed history + mutable live tail
- generic `RenderDocument`
- JSON 단일 저장소와 readback 검증 원칙

현재 build source에는 조건식·종목풀·순위 전용 모듈이 없다. 따라서 기존 ComparisonModule을 종목풀 상태 저장소로 오용하지 않고 새 major feature module로 분리한다.

---

# 4. 핵심 설계 결정

## 4.1 세 개의 집합을 분리한다

### Condition Universe

선택한 조건식의 현재 실시간 멤버 전체. 최대 수는 키움 조건식 결과 계약과 서버 설정에 따르며, 설계상 200까지 저장 가능하게 한다.

### Scoring Universe

상세 1분봉·실시간 시계열 특징을 계산하는 승격 종목. 기본 상한 32, 설정 가능 범위 8~64. 첫 구현의 실제 상한은 32로 고정한다.

### Published Top-N

메인 종목풀 패널에 발행하는 상위 종목. 기본 10, 설정 범위 1~32.

```text
Condition Universe ⊇ Scoring Universe ⊇ Published Top-N
```

세 집합을 하나의 vector에서 visible flag만 바꾸는 식으로 혼합하지 않는다. 멤버십과 계산 준비상태, 발표상태는 서로 다른 차원이다.

## 4.2 두 단계 순위를 사용한다

### Coarse Rank

Condition Universe 전체를 대상으로 저비용 데이터만 사용한다.

- FID 14 누적거래대금
- MarketEye 현재가/전일대비/시가/고가/저가
- MarketEye 거래대금/체결강도/호가잔량비율
- 당일 외국인/기관/개인/프로그램 수급
- 조건식 편입시각과 멤버십 지속시간

용도는 상세분석 대상 승격 결정이다. 최종 Top-N 신호가 아니다.

### Detailed Rank

Scoring Universe를 대상으로 1분봉과 실시간 증분 특징을 사용한다.

- 장 시작 이후 수익률
- 조건식 편입 이후 수익률
- 1/3/5/10/20/30/60분 수익률
- 기준지수/기준종목 대비 동일 기간 상대강도
- 상대강도 기울기와 가속도
- 거래대금 증가속도
- 고점 대비 낙폭
- 변동성 대비 수익
- 상위권 유지시간과 순위 적분
- 데이터 품질과 freshness

Published Top-N은 Detailed Rank에서만 발행한다.

## 4.3 MarketEye의 역할

MarketEye는 다음 용도로 사용한다.

1. 조건식 전체 후보를 한 번에 경량 평가한다.
2. 신규 편입 종목의 세션 상태를 캔들 준비 전에 즉시 파악한다.
3. 수급·호가·체결강도 등 분봉에 없는 특징을 보강한다.
4. 30초/60초 snapshot 차이를 이용해 거래대금·수급 증가속도를 계산한다.

MarketEye는 다음 용도로 사용하지 않는다.

- 1분봉 대체
- 틱 실시간 대체
- 정확한 기간 수익률의 시계열 원본
- 누락 데이터 보간

## 4.4 C++의 역할

C++은 API 호출 빈도를 높이는 수단이 아니라, 받은 데이터를 반복 복사 없이 증분 계산하고 모든 후보의 다중 기간 특징·순위·성과라벨을 결정적으로 산출하는 데 사용한다.

- 코드→slot 고정 인덱스
- contiguous vector/array
- 고정 용량 ring buffer
- 완료봉 immutable storage
- 현재봉 mutable tail
- window별 running statistics
- 순위 snapshot atomic 교체
- UI와 독립된 worker
- replay와 live에서 같은 계산 함수

병목은 계산이 아니라 외부 데이터 수집이다. C++ 속도를 이유로 REST/COM 호출량을 늘리지 않는다.

---

# 5. 조건식 세션의 신뢰성 계약

## 5.1 server32에 추가할 원자적 세션 API

기존 API는 유지하되 종목풀에서는 새 계약만 사용한다.

```text
POST /api/conditions/sessions/start
GET  /api/conditions/sessions/{session_id}
POST /api/conditions/sessions/{session_id}/stop
```

start 요청:

```json
{
  "condition_index": 0,
  "condition_name": "1516",
  "requested_screen": ""
}
```

start 응답:

```json
{
  "session_id": "cond-20260806-0001",
  "screen": "9201",
  "condition_index": 0,
  "condition_name": "1516",
  "revision": 1,
  "captured_at_ms": 0,
  "codes": ["005930", "000660"]
}
```

WebSocket delta:

```json
{
  "type": "condition_membership",
  "session_id": "cond-20260806-0001",
  "sequence": 42,
  "server_timestamp_ms": 0,
  "condition_index": 0,
  "condition_name": "1516",
  "code": "005930",
  "state": "enter | exit"
}
```

필수 서버 동작:

1. 실시간 조건식을 시작한다.
2. 초기 `OnReceiveTrCondition` 목록을 session membership set에 넣는다.
3. 초기 목록 확정 전 들어온 `OnReceiveRealCondition` delta를 buffer한다.
4. 초기 목록 위에 buffer를 순서대로 적용한다.
5. snapshot과 현재 revision을 한 응답으로 반환한다.
6. 이후 delta마다 sequence와 revision을 단조 증가시킨다.
7. real condition delta가 기존 60초 condition cache도 갱신한다.
8. reconnect 조회로 현재 membership set과 revision을 받을 수 있게 한다.

## 5.2 cppChart 세션 처리

`ConditionPoolCoordinator`는 한 번에 하나의 active condition session만 소유한다.

```text
Disconnected
Starting
Reconciling
Active
Degraded
Stopping
Stopped
```

규칙:

- `sequence <= lastSequence`는 duplicate/stale로 무시하고 계수한다.
- sequence gap은 Active를 Degraded로 바꾸고 snapshot reconciliation을 요청한다.
- WebSocket 재연결 후 기존 로컬 set을 신뢰하지 않고 서버 snapshot과 대조한다.
- condition name/index가 active session과 다르면 격리한다.
- enter/exit는 idempotent하게 처리한다.
- 같은 종목의 재편입은 새로운 membership episode를 만든다.

## 5.3 멤버십 에피소드

종목코드만으로 장중 생명주기를 표현하지 않는다.

```cpp
struct ConditionMembershipEpisode
{
    std::uint64_t episodeId;
    std::string code;
    EpochMillis enteredAt;
    EpochMillis exitedAt;
    bool active;
    std::uint32_t enterSequence;
    std::uint32_t exitSequence;
};
```

탈락한 episode도 당일 종료까지 유지하여 탈락 이후 MFE/MAE를 계산한다. 재편입은 기존 episode를 되살리지 않고 새 episode로 기록한다.

---

# 6. 데이터 수집 계층과 비용 제어

## 6.1 데이터 등급

### Tier A — 전 후보 공통, 매우 저비용

- condition enter/exit
- MarketEye batch
- 키움 실시간 FID 14 및 기본 tick/hoga

### Tier B — 상세분석 승격 종목

- 1분봉 당일 이력 1회
- 완료봉 이후 실시간 현재봉 갱신
- benchmark 분봉

### Tier C — 상위 후보 선택 보강

- 5대 창구
- 프로그램매매 상세 시계열
- 추가 심층 정보

Tier C는 최초 순위 핵심 입력에 넣지 않는다. Top-N 또는 경계 종목에 대한 설명·후속 연구용으로만 제한한다.

## 6.2 신규 편입 처리

```text
enter 수신
→ 멤버십 episode 생성
→ 즉시 키움 실시간 구독 요청
→ 다음 MarketEye batch 포함
→ provisional/coarse 특징 계산
→ coarse admission 순위 산출
→ 상세분석 승격 시 candle hydration queue 삽입
→ hydration 완료 후 Detailed Rank 참여
```

신규 편입마다 즉시 개별 MarketEye를 호출하지 않는다. 1~2초 debounce 후 다음 전체 batch에 합친다.

## 6.3 캔들 hydration

기본 계약:

- 1분봉
- 최신 600봉 단일 페이지
- 같은 code/date/unit 요청 중복 제거
- 한 종목·한 거래일당 최초 1회
- 완료 후 실시간 tick으로 연장

우선순위 queue:

```text
P0  현재 Top-N인데 데이터 복구가 필요한 종목
P1  coarse 승격 경계 상위 신규 종목
P2  기존 scoring universe 재연결 복구
P3  낮은 coarse rank 예비 종목
P4  사후 label 보강
```

각 요청은 다음 key로 deduplicate한다.

```text
(code, trading_date, minute_unit, source_contract_version)
```

## 6.4 캔들 원본 선택

기본 canonical source는 cppChart의 현재 Kiwoom stock-minute normalized path로 둔다. Cybos 분봉은 다음 경우의 선택적 fallback이다.

- 키움 REST rate limit 장기 지속
- 과거 구간 보충
- 장 종료 후 대량 검증

같은 종목·거래일의 한 시계열에 두 source를 임의로 섞지 않는다. source 전환은 완전한 series replacement와 provenance 기록을 요구한다.

```cpp
enum class CandleSource
{
    KiwoomRest,
    CybosStockChart
};
```

## 6.5 scoring universe 입장과 퇴장

상한 32에서 순위가 자주 뒤집혀 hydration을 반복하지 않도록 hysteresis를 둔다.

- 승격: coarse rank가 `admissionRank` 안에 연속 2회 진입
- 유지: 상세 데이터가 정상이고 coarse rank가 `retentionRank` 안에 있음
- 강등: `retentionRank` 밖에 연속 N회 또는 active condition 탈락
- 기본 예: admission 24, retention 40

이 숫자는 설정값이며 결과 이력에 반드시 기록한다. 숫자 자체가 수익 규칙은 아니고 데이터 비용 안정화 규칙이다.

condition exit 시:

- 신규 Top-N 발표 대상에서는 즉시 제외 가능
- 시계열 구독과 outcome tracking은 설정된 grace period 동안 유지
- 당일 MFE/MAE label 계산용 최소 데이터는 유지

---

# 7. 종목 상태기계

```text
Detected
MarketSnapshotPending
CoarseReady
HydrationQueued
Hydrating
DetailedReady
Ranked
PublishedTopN
ExitedTracking
Stale
Excluded
Faulted
Closed
```

상태와 점수는 분리한다.

- `Stale`은 0점이 아니다.
- `Faulted`는 최하위 정상 종목이 아니다.
- `Hydrating`은 provisional rank에만 참여할 수 있다.
- `ExitedTracking`은 신규 발표에서 제외되지만 outcome label은 계속 계산한다.

필수 freshness:

```text
lastConditionEventAt
lastTickAt
lastMarketEyeAt
lastCompletedBarAt
lastHydrationAt
lastRankedAt
```

---

# 8. 특징(feature) 계약

## 8.1 가격·수익률

모든 수익률은 timestamp와 anchor provenance를 함께 가진다.

```text
return_1m
return_3m
return_5m
return_10m
return_20m
return_30m
return_60m
return_since_open
return_since_condition_enter
position_in_session_range
pullback_from_session_high
```

## 8.2 상대강도

benchmark는 설정에서 선택한다.

```text
KOSPI
KOSDAQ
업종지수
지정 종목
현재 메인 종목
후보군 중앙값
```

window별 exact-common-timestamp 계약:

```text
rs_1m
rs_3m
rs_5m
rs_10m
rs_20m
rs_30m
rs_60m
rs_since_open
```

기존 Comparison RelativeStrength100과 동일한 순수 계산 계약을 재사용하되, 차트 정의를 생성하지 않고 feature calculator에서 호출한다.

## 8.3 거래대금·활동성

```text
cum_turnover_won                FID 14 우선
turnover_delta_30s
turnover_delta_60s
turnover_acceleration
turnover_rank
volume_vs_previous_day
volume_per_minute
```

FID 14는 백만원 단위이므로 정규화 경계에서 원 단위 정수로 변환하고 단위를 기록한다.

## 8.4 체결·호가

```text
trade_intensity
trade_intensity_delta
bid_ask_total_ratio
bid_ask_ratio_delta
spread_bps                     사용 가능한 경우
```

호가 누락 시 직전값을 무기한 유지하지 않는다. freshness 만료 후 missing으로 바꾼다.

## 8.5 수급

MarketEye snapshot 차이로 다음을 계산한다.

```text
foreign_net_today
institution_net_today
individual_net_today
program_net
foreign_net_delta
institution_net_delta
program_net_delta
foreign_hold_ratio
credit_ratio
```

잠정/확정 구분을 특징과 함께 보관한다. 잠정값과 확정값을 같은 신뢰도로 취급하지 않는다.

## 8.6 지속 순위 특징

단순 현재 rank 외에 장중 경로를 수치화한다.

```text
rank_current
rank_previous
rank_velocity_30s
rank_velocity_5m
rank_best_today
rank_worst_today
top3_streak_seconds
top5_streak_seconds
top10_streak_seconds
top5_exposure_ratio
top10_exposure_ratio
rank_strength_integral
rank_drawdown
```

### Rank Strength Integral

후보 수 `M`, 현재 순위 `r`일 때 순간 강도를 다음처럼 정규화한다.

```text
rank_strength = (M - r) / max(1, M - 1)
```

시간 적분:

```text
rank_strength_integral += rank_strength × elapsed_seconds
```

후보 수가 바뀌어도 0~1 범위의 상대 위치를 유지한다.

### Top-N Exposure

```text
topN_exposure_ratio = Top-N에 있었던 누적시간 / 평가 가능 누적시간
```

장중 늦게 편입된 종목과 09:00부터 존재한 종목을 동일 분모로 비교하지 않는다. 각 episode의 평가 가능 시간만 분모로 사용한다.

## 8.7 데이터 품질 특징

```text
has_condition_membership
has_marketeye
has_candle_history
has_realtime_tail
benchmark_aligned
marketeye_age_ms
tick_age_ms
bar_age_ms
missing_feature_mask
quality_state
```

점수 설명에 누락 여부를 노출한다.

---

# 9. 점수와 순위 설계

## 9.1 원시 특징을 먼저 보존한다

모든 원시 특징을 저장한 뒤 횡단면 percentile/rank를 계산한다. 원시값 없이 종합점수만 저장하지 않는다.

## 9.2 초기에는 프로필별 사전식 순위

검증되지 않은 가중합 대신 설명 가능한 lexicographic profile로 시작한다.

### Profile A — 누적 대장주

```text
1. rs_since_open 내림차순
2. return_since_open 내림차순
3. cum_turnover_won 내림차순
4. pullback_from_session_high 오름차순
5. code 오름차순 — 결정적 tie-break
```

### Profile B — 순간 부상

```text
1. rs_5m 변화 내림차순
2. rank_velocity_5m 내림차순
3. turnover_acceleration 내림차순
4. rs_since_open 내림차순
5. code 오름차순
```

### Profile C — 지속 강자

```text
1. top5_exposure_ratio 내림차순
2. rank_strength_integral / evaluable_seconds 내림차순
3. rs_since_open 내림차순
4. rank_drawdown 오름차순
5. code 오름차순
```

### Profile D — 수급 동반 강세

```text
1. rs_since_open 내림차순
2. program_net_delta percentile 내림차순
3. institution_net_delta percentile 내림차순
4. turnover_acceleration 내림차순
5. code 오름차순
```

## 9.3 점수는 구성요소와 함께 발행한다

UI용 0~100 score가 필요하면 profile rank를 시각화한 presentation score로만 둔다.

```text
presentation_score = 100 × (M - rank) / max(1, M - 1)
```

이는 매매 확률이 아니다. UI에 `점수`라는 단어만 표시하지 않고 `프로필 순위점수`로 표시한다.

## 9.4 학습된 종합점수 도입 조건

다음 조건을 모두 만족한 뒤에만 weighted/composite score를 도입한다.

1. 최소 여러 거래일의 append-only rank history 존재
2. feature와 future label이 event time 기준으로 결합됨
3. 날짜 단위 walk-forward 분리
4. out-of-sample에서 rank monotonicity 확인
5. 가중치와 feature version 고정
6. 기존 profile 대비 개선이 수익뿐 아니라 MAE·turnover에서도 확인

가중치는 JSON profile version으로 저장하고 당일 중 자동 변경하지 않는다.

---

# 10. 순위와 미래 최고수익률 연결

## 10.1 Snapshot 시점별 미래 label

각 rank snapshot `t`에 대해 이후 데이터가 도착하면 다음 label을 별도 기록한다.

```text
return_5m_after_t
return_15m_after_t
return_30m_after_t
return_60m_after_t
mfe_5m
mfe_15m
mfe_30m
mfe_60m
mae_5m
mae_15m
mae_30m
mae_60m
mfe_until_close
mae_until_close
time_to_mfe
time_to_plus_0_5
time_to_plus_1_0
hit_plus_0_5_before_minus_0_5
hit_plus_1_0_before_minus_0_5
```

수익률 threshold는 평가 파라미터이며 점수 계산에 소급 적용하지 않는다.

## 10.2 지속 랭크 이벤트

다음 event를 별도로 만든다.

```text
FirstTopNEntry
SustainedTopNEntry       N위 이내 K회 연속
TopNExit
TopNReentry
NewBestRank
RankAcceleration
```

각 event 이후 MFE/MAE를 측정한다. 이것으로 다음 질문에 답한다.

- 최초 Top-5 진입 직후보다 3회 연속 Top-5 유지 후 진입이 더 유리한가?
- 순간 15위→3위 상승이 기존 1위 유지보다 이후 MFE가 큰가?
- 상위권 유지시간과 장 마감 전 최고수익률의 관계가 단조적인가?
- 조건식 탈락 직전 순위 하락이 실제 위험 신호인가?

## 10.3 평가 지표

```text
Precision@N
Average future return@N
Median MFE@N
Median MAE@N
MFE/|MAE| ratio
Target-before-stop hit rate
Rank decile monotonicity
Top-N turnover
Median Top-N dwell time
Data-ready coverage
Stale exclusion rate
```

최고수익률만 최적화하면 급등 후 큰 하락을 선호할 수 있으므로 MAE와 time-to-MFE를 반드시 함께 본다.

## 10.4 look-ahead 방지

- feature timestamp 이후 값은 label에만 기록한다.
- 완료되지 않은 1분봉은 `live_tail`로 구분한다.
- rank snapshot은 계산 당시 사용한 source revision을 기록한다.
- MarketEye의 캡처시각이 없으면 순위 핵심 특징으로 사용하지 않는다.
- 사후 정정된 수급값은 당시 snapshot을 덮어쓰지 않고 correction event로 추가한다.

---

# 11. C++ 모듈 경계

## 11.1 core/

```text
stock_pool_types.*
  compact value, enum, snapshot contract

stock_pool_feature_engine.*
  순수 증분 특징 계산

stock_pool_ranking_engine.*
  profile별 결정적 정렬과 설명

stock_pool_outcome_labeler.*
  미래수익률/MFE/MAE label 계산

stock_pool_replay.*
  append-only event 재생
```

`core/`는 HTTP, Win32, ImGui, JSON 파일, renderer를 알지 않는다.

## 11.2 app/

```text
condition_pool_module.*
  condition session, membership episode, reconciliation

stock_pool_module.*
  종목 slot, 상태기계, immutable snapshot

stock_pool_data_coordinator.*
  hydration/admission/eviction 요청 결정

stock_pool_workspace_state.*
stock_pool_workspace_store.*
  설정 JSON 단일 소유자 + readback 검증

stock_pool_history_writer.*
  append-only event/rank/label 기록
```

## 11.3 platform/

```text
server32_condition_client.*
server32_marketeye_client.*
kiwoom_stock_pool_transport.*
stock_pool_request_scheduler.*
```

broker/transport JSON은 platform에서 정규화되어 끝난다.

## 11.4 ui/

```text
stock_pool_manager_ui.*
stock_pool_panel_ui.*
stock_pool_explain_ui.*
```

UI는 snapshot과 command callback만 사용한다.

## 11.5 renderer

종목풀 표는 ImGui feature panel이며 chart renderer 수정 대상이 아니다. Top-N 비교선을 차트에 추가할 때도 기존 ComparisonDefinition 또는 generic render contribution 계약을 사용한다. renderer에 stock-pool 분기를 넣지 않는다.

---

# 12. Hot-path 메모리와 동시성

## 12.1 단일 writer

종목풀 상태는 한 worker가 event queue를 순서대로 적용한다.

```text
ConditionEvent
RealtimeTick
MarketEyeSnapshot
MinuteHistoryLoaded
TimerPulse
ConfigChanged
SessionBoundary
```

복수 worker가 같은 member state를 직접 수정하지 않는다.

## 12.2 종목 slot

```cpp
struct StockPoolSlot
{
    SymbolId symbol;
    MembershipState membership;
    DataReadiness readiness;
    RingBuffer<MinuteBar, 720> minuteBars;
    MarketEyeState marketEye;
    RealtimeTail live;
    FeatureState features;
    RankPathState rankPath;
};
```

`unordered_map<code, slotIndex>`는 탐색에만 사용하고 hot calculation은 contiguous slot vector를 순회한다.

## 12.3 Snapshot 발행

```cpp
struct StockPoolSnapshot
{
    std::uint64_t revision;
    EpochMillis calculatedAt;
    ScoreProfileVersion profile;
    BenchmarkIdentity benchmark;
    std::vector<StockPoolMemberSnapshot> ranked;
    DataQualitySummary quality;
};
```

worker가 새 snapshot을 완성한 뒤 `shared_ptr<const StockPoolSnapshot>`을 atomic 교체한다. UI는 lock 없이 최신 snapshot만 읽는다.

## 12.4 계산 주기

- tick 수신마다 현재봉·누적값 증분 갱신
- 완료 1분봉마다 window feature 갱신
- MarketEye 30초 또는 60초 batch
- rank snapshot 30초 또는 60초
- condition delta는 즉시 적용

순위 interval과 MarketEye interval은 독립 설정이다. MarketEye를 60초마다 받아도 tick/봉 기반 rank는 30초마다 갱신할 수 있다.

---

# 13. 요청 스케줄러

## 13.1 source별 독립 budget

```text
Kiwoom condition budget
Kiwoom candle REST budget
Kiwoom realtime subscription budget
Cybos MarketEye budget
Cybos candle budget
Cybos detail budget
```

한 source의 429/COM 지연이 다른 source queue를 막지 않는다.

## 13.2 요청 상태

```text
Queued
InFlight
Succeeded
RetryBackoff
RateLimited
Cancelled
PermanentFault
```

## 13.3 retry

- 동일 요청의 즉시 반복 금지
- HTTP 429는 server hint 또는 exponential backoff
- condition exit로 더 이상 hydration 필요가 없으면 queued request 취소
- partial MarketEye 응답은 성공 전체로 처리하지 않고 누락 code만 다음 batch 재시도
- retry count와 마지막 오류를 snapshot에 노출

## 13.4 우선순위 역전 방지

오래 기다린 낮은 우선순위 요청에 aging을 적용하되, Top-N 복구 요청보다 앞설 수 없도록 priority class를 먼저 비교한다.

---

# 14. server32 필수 보강 설계

## 14.1 condition session

앞서 정의한 원자적 start/snapshot/delta sequence 계약을 구현한다.

## 14.2 MarketEye 응답 envelope

```json
{
  "success": true,
  "data": {
    "source": "CYBOS_MARKETEYE",
    "captured_at_ms": 0,
    "requested_count": 100,
    "returned_count": 99,
    "missing_codes": ["005930"],
    "truncated": false,
    "items": []
  }
}
```

규칙:

- 200개 초과는 `400`으로 거부한다. 조용한 truncate 금지.
- 모든 code를 정규화하고 duplicate를 제거한 결과를 응답한다.
- COM status/message를 오류 contract에 포함한다.
- 빈 정상 결과와 COM 실패를 구분한다.
- `captured_at_ms`는 COM 응답 완료 직후 서버시각이다.
- 잠정/확정 구분은 원문 그대로 유지한다.

## 14.3 server32 health

종목풀 관리 창에서 확인 가능한 health snapshot이 필요하다.

```text
Kiwoom login
condition session count
WebSocket session count
Cybos connected
MarketEye last success
MarketEye last duration
MarketEye request budget/remaining if available
last COM error
```

## 14.4 candle endpoint provenance

Cybos candle fallback 응답에 다음을 추가한다.

```text
source
code
minute_unit
from/to
adjusted_price flag
exchange coverage
captured_at
rows
```

---

# 15. 설정 단일 저장소

파일:

```text
data/stock_pool_workspace.json
config/stock_pool_workspace.default.json
```

예시 schema:

```json
{
  "schema_version": 1,
  "condition": {
    "index": 0,
    "name": "1516",
    "exit_policy": "exclude_publish_keep_label",
    "exit_grace_seconds": 1800
  },
  "universe": {
    "condition_capacity": 200,
    "scoring_capacity": 32,
    "published_top_n": 10,
    "admission_rank": 24,
    "retention_rank": 40,
    "admission_consecutive": 2,
    "eviction_consecutive": 3
  },
  "schedule": {
    "ranking_interval_seconds": 30,
    "marketeye_interval_seconds": 60,
    "reconciliation_interval_seconds": 60
  },
  "benchmark": {
    "kind": "market_index",
    "code": "101"
  },
  "profile": {
    "id": "persistent_leader",
    "version": 1
  },
  "ui": {
    "auto_follow_rank1": false,
    "show_columns": []
  }
}
```

모든 적용은 candidate 전체 검증 → runtime 적용 → atomic save → readback 비교 → commit 순서로 처리한다.

---

# 16. Append-only 검증 저장소

디렉터리:

```text
data/stock_pool_history/YYYY-MM-DD/
```

파일:

```text
membership_events.jsonl
source_events.jsonl
feature_snapshots.jsonl
rank_snapshots.jsonl
rank_events.jsonl
outcome_labels.jsonl
fault_events.jsonl
session_manifest.json
```

모든 record 공통 필드:

```text
schema_version
session_id
event_sequence
event_time_ms
received_time_ms
trading_date
source
source_revision
condition_id
profile_id/profile_version
```

## 16.1 불변식

- 과거 record 수정 금지
- correction은 새 record
- shutdown 시 flush와 마지막 sequence 기록
- 시작 시 마지막 complete line까지만 복구
- 손상 line은 이후 전체를 조용히 무시하지 않고 fault로 보고
- label은 원 feature/rank record ID를 참조

## 16.2 session manifest

```text
selected condition
benchmark
all settings
score profile definition
binary/build commit
source contract versions
start/end time
reconnect count
rate-limit count
missing-data summary
```

같은 결과를 재현하려면 코드 commit과 profile version이 필요하다.

---

# 17. UI 설계

## 17.1 종목풀 관리 다이얼로그

### 조건식

- 조건식 목록/새로고침
- 선택 조건식
- 시작/중지
- current session ID/revision
- 현재 편입 수/오늘 누적 episode 수
- reconnect/reconcile 상태

### 데이터

- scoring capacity
- hydration queue 수
- MarketEye 주기
- rank 주기
- source health
- REST 429/COM 오류

### 순위

- benchmark
- profile
- Top-N
- 프로필 설명
- feature별 현재 정렬 우선순위

### 검증

- history recording On/Off
- 현재 파일 위치
- label pending/completed 수
- 오늘 profile별 성과 요약

## 17.2 메인 종목풀 패널

권장 열:

```text
Rank
ΔRank
Code
Name
State
Profile score
RS since open
5m RS
Session return
5m return
Cumulative turnover
Turnover acceleration
Top-5 exposure
Top-5 streak
MFE after first Top-N
Data age
```

기본 화면에는 핵심 열만 보이고 상세 열은 설정에서 선택한다.

행 동작:

- click: 선택만
- double-click: 메인 차트 조회
- context: 비교 시계열 추가
- pin: 순위 밖이어도 패널 유지
- exclude: 당일 발표 대상 제외
- explain: 원시 feature, percentile, profile tie-break 표시

`1위 자동추종`은 기본 Off다. 순위 갱신 때문에 사용자가 관찰 중인 메인 차트를 강제로 바꾸지 않는다.

## 17.3 데이터 상태 표시

순위 숫자보다 데이터 상태를 먼저 신뢰할 수 있어야 한다.

```text
Ready
Provisional
Hydrating
Stale
Exited
Faulted
```

Provisional/ Stale 종목은 색상과 아이콘뿐 아니라 텍스트로 표시한다.

---

# 18. 실패와 복구 정책

## 18.1 condition stream 단절

- rank snapshot은 마지막 membership을 그대로 정상으로 간주하지 않는다.
- module state를 Degraded로 변경한다.
- 새 Top-N 발표를 일시 정지하거나 stale 표시한다.
- session snapshot reconcile 후에만 Active 복귀한다.

## 18.2 MarketEye 장애

- tick/봉 기반 상세 rank는 계속 가능하다.
- MarketEye feature mask를 missing으로 설정한다.
- MarketEye 의존 profile은 발표 중지 또는 명시적 degraded profile로 전환한다.
- 다른 profile로 조용히 자동 변경하지 않는다.

## 18.3 candle 429

- 이미 Ready인 종목의 rank는 계속 계산한다.
- 신규 종목은 Provisional 상태 유지
- primary chart의 오류로 덮어쓰지 않는다.
- source별 backoff와 queue age 표시

## 18.4 benchmark 장애

- relative-strength feature는 invalid
- 절대수익 profile만 선택적으로 유지 가능
- relative-strength profile은 fail closed

## 18.5 partial/stale data

최소 필수 feature mask를 profile별로 정의한다. 필수값 하나라도 stale이면 해당 profile rank에서 제외한다.

---

# 19. 검증 전략

## 19.1 순수 deterministic fixture

- 10종목 condition enter/exit/reentry
- MarketEye snapshot 3회
- 1분봉 120개
- tick same-timestamp replacement
- sequence gap/reconcile
- stale/partial source
- benchmark timestamp gap

동일 event log를 100회 replay해 byte-equivalent rank snapshot을 확인한다.

## 19.2 부하 fixture

```text
200 condition members
32 detailed slots
720 bars/slot
초당 다수 tick
30초 rank
60초 MarketEye
```

측정:

- event queue lag
- ranking duration
- snapshot allocation
- retained bytes
- dropped/merged tick count
- UI frame 영향

성능 목표는 측정 후 수치로 고정한다. 근거 없는 목표값을 먼저 선언하지 않는다.

## 19.3 source contract test

- condition initial snapshot + buffered delta
- duplicate enter/exit
- sequence gap
- MarketEye 200 정상
- 201 거부
- partial missing_codes
- captured_at freshness
- FID 14 백만원→원 변환

## 19.4 historical outcome test

인위적으로 미래가 보이는 fixture에서 labeler가 정확히 MFE/MAE와 target-before-stop을 계산하는지 검증한다. ranking engine은 label data에 접근할 수 없어야 한다.

## 19.5 실제 장중 수용

- 조건식 시작 직후 초기 종목과 실시간 편입/탈락 일치
- 100개 수준 원본 후보에서 MarketEye 한 batch
- 상세 32개 hydration queue 동작
- Top-N 30초 갱신
- 행 double-click 메인 차트 전환
- 재기동 설정 복원
- WebSocket 물리 재연결 후 membership reconcile
- 장중 soak
- 장 종료 후 rank→MFE/MAE 리포트 생성

---

# 20. 구현 순서

## Phase 0 — 설계 승인

- 이 문서 검토
- condition session contract 승인
- coarse/detailed 2단계 승인
- 초기 profile A/B/C 승인
- history/label contract 승인

**이 단계에서는 기능 코드를 작성하지 않는다.**

## Phase 1 — server32 계약 보강

1. atomic condition session
2. sequence/revision/snapshot
3. condition cache delta 반영
4. MarketEye envelope/partial metadata
5. 201개 fail-closed
6. health/provenance
7. API contract test

## Phase 2 — C++ 순수 도메인

1. compact types/state machine
2. feature engine
3. deterministic profile ranker
4. rank path/persistence metrics
5. outcome labeler
6. replay fixture

UI와 network 없이 검증한다.

## Phase 3 — ingestion과 scheduler

1. server32 condition client
2. MarketEye client
3. source별 request budget
4. membership episodes
5. hydration queue
6. realtime bar aggregation
7. immutable snapshot

## Phase 4 — fixture 기반 최소 수직 절단

```text
fixture condition events
→ 10종목
→ MarketEye fixture
→ 1분봉 fixture
→ rank snapshot
→ Top-N panel
→ click chart
→ history JSONL
→ outcome report
```

실제 API 전에 사용자 동선을 확인한다.

## Phase 5 — 실제 조건식·MarketEye

- 조건식 선택/시작/중지
- 실시간 편입/탈락
- 200종목 MarketEye batch
- 상세분석 승격
- data readiness UI

## Phase 6 — 실제 1분봉·실시간

- 600봉 1회 hydration
- 0B tail
- 30초/60초 rank
- benchmark relative strength
- source fault isolation

## Phase 7 — 성과검증

- 일자별 replay
- profile별 Precision@N/MFE/MAE
- rank persistence와 최고수익률 관계
- walk-forward profile 개선

## Phase 8 — 전략·주문 연결 검토

장중 실제 데이터에서 선별력이 확인된 profile만 전략 후보로 승격한다. 순위 엔진 자체는 주문 권한을 갖지 않는다.

---

# 21. 구현 시작 전 확정된 결론

1. 조건식 전체 후보는 MarketEye/FID14로 경량 평가한다.
2. 정밀 1분봉 대상은 기본 최대 32개로 제한한다.
3. MarketEye는 30초 또는 60초 전체 batch이며 개별 enter마다 호출하지 않는다.
4. 1분봉은 승격 시 600봉 1회, 이후 실시간 증분이다.
5. 순위는 현재값뿐 아니라 경로·지속시간·rank integral을 가진다.
6. 탈락 종목도 당일 outcome label을 위해 보존한다.
7. 초기 점수는 설명 가능한 profile 순위이며, 검증 전 임의 가중합을 쓰지 않는다.
8. 모든 rank snapshot은 이후 MFE/MAE/기간수익률과 append-only로 연결한다.
9. UI는 immutable snapshot을 표시할 뿐 수집·계산하지 않는다.
10. renderer는 변경하지 않는다.
11. server32의 현재 조건식 초기 snapshot 경쟁조건과 MarketEye partial contract를 먼저 보강한다.
12. C++ 계산 속도는 API 남용이 아니라 다종목 증분 특징·replay·검증에 사용한다.

---

# 22. 첫 코딩 단위

설계 승인 후 첫 코딩 단위는 UI가 아니다.

```text
server32 atomic condition session contract
+ C++ stock_pool_types
+ deterministic ranking fixture
+ append-only rank/outcome contract
```

이 네 계약이 통과한 뒤에 실제 REST/WebSocket과 종목풀 패널을 연결한다.
