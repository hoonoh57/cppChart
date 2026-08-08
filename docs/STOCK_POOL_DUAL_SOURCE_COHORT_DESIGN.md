# 종목풀 이중 데이터소스·고정 Cohort 설계

## 0. 목적

종목풀 분석 워크벤치는 장중과 장후에 서로 다른 데이터 원본을 사용하되, 이후 상대강도·순위·지속성·미래성과 계산은 동일한 C++ 엔진을 사용한다.

```text
장중
  키움 실시간 조건식 초기목록 + 편입/탈락 delta

장후/백테스트
  1516 성과검증 자료의 지정일·조건식·지정시각 포착 종목
```

현재 상세차트 `shell.exe`는 수정하지 않는다. 본 기능은 `stock_pool_workbench.exe` 전용이다.

---

# 1. 최상위 원칙

## 1.1 후보군 생성과 성과평가를 분리한다

1516 자료에 이미 저장된 다음 값은 후보군 선택에 사용하지 않는다.

```text
1분·3분·5분·10분·15분·30분·60분·7시간 수익률
기간 내 최고수익률
장 마감 결과
향후 목표수익 도달 여부
```

후보군 구성에는 오직 당시 알 수 있었던 값만 사용한다.

```text
거래일
조건식 ID/이름
포착시각
종목코드
종목명/시장
당시 조회 결과에 포함됐다는 사실
```

미래 수익률과 최고수익률은 ranking replay가 끝난 뒤 label로만 결합한다.

## 1.2 장후 cohort는 한 번 확정하면 변하지 않는다

예:

```text
거래일       2026-08-06
조건식       다량어
포착시각     09:00 또는 09:05
포착 종목 수 36
```

이 36종목은 해당 replay의 `Frozen Cohort`다.

- 장중 이후 조건식에 새로 편입된 종목을 추가하지 않는다.
- 장중 조건식에서 탈락했더라도 cohort에서 제거하지 않는다.
- 데이터 누락 종목은 구성원으로 보존하되 `NotEvaluable` 상태로 표시한다.
- 사용자가 수동 제외한 경우 원본 cohort와 파생 분석 cohort를 모두 기록한다.

## 1.3 장중 cohort는 동적이지만 이력은 삭제하지 않는다

장중 실시간 조건식에서는 membership이 변한다.

```text
InitialSnapshot
Enter
Exit
ReEnter
```

현재 ranking universe에서는 탈락 종목을 제외할 수 있지만, 당일 성과검증을 위해 종목 이력과 탈락 이후 가격 경로를 계속 보존한다.

---

# 2. UI 데이터소스 선택

상단 toolbar에 명시적인 데이터소스 선택을 둔다.

```text
데이터소스 [실시간 조건식 | 과거 포착 스냅샷]
```

## 2.1 실시간 조건식 모드

```text
조건식 [▼]
거래일 [오늘, 읽기 전용]
시작시각 [실제 세션 시작시각]
재계산 주기 [30초 | 60초]
[실시간 시작] [중지]
```

표시 상태:

```text
session_id
condition revision
현재 membership 수
평가 가능 수
마지막 delta sequence
WebSocket freshness
MarketEye freshness
```

## 2.2 과거 포착 스냅샷 모드

```text
거래일 [달력]
조건식 [▼]
포착시각 [09:00 | 09:01 | ... | 09:05 또는 저장된 시각]
평가 종료 [장 마감 | 7시간 | 사용자 시각]
재생 속도 [1x | 10x | 60x | 최대]
[종목 불러오기] [재생] [일시정지] [처음으로]
```

`평가 종료`는 label 산출 범위를 정할 뿐 후보군을 바꾸지 않는다.

---

# 3. 공통 Source 계약

두 원본은 다음 공통 인터페이스로 정규화한다.

```cpp
struct StockPoolCohortMember
{
    std::string code;
    std::string name;
    std::string market;
    EpochMillis firstDetectedAt;
    EpochMillis lastMembershipChangedAt;
    bool currentlyMember;
};

struct StockPoolCohortSnapshot
{
    std::string sourceId;
    std::string sourceType;       // LiveCondition | HistoricalSnapshot
    std::string conditionId;
    std::string conditionName;
    std::string tradingDate;
    EpochMillis cohortAnchorAt;
    std::uint64_t revision;
    bool frozen;
    std::vector<StockPoolCohortMember> members;
};

class IStockPoolCohortSource
{
public:
    virtual ~IStockPoolCohortSource() = default;
    virtual StockPoolCohortSnapshot Snapshot() const = 0;
    virtual std::vector<StockPoolMembershipEvent> DrainEvents() = 0;
};
```

ranking engine은 `sourceType`에 따른 분기를 갖지 않는다. 전달받은 cohort snapshot과 시계열 데이터만 계산한다.

---

# 4. 실시간 조건식 Source

## 4.1 시작 계약

```text
StartConditionSession
→ initial members 확보
→ initial 조회 중 수신한 delta 순서대로 적용
→ session_id/revision/current members 반환
→ 이후 sequence 포함 enter/exit 전달
```

초기 목록과 실시간 delta 사이에 종목을 놓치는 구조는 허용하지 않는다.

## 4.2 종목별 분석 기준시각

실시간 편입시각이 서로 다르므로 두 기준을 동시에 보존한다.

```text
session_anchor_at
  워크벤치 분석을 시작한 공통 시각

first_detected_at
  각 종목이 조건식에 최초 편입된 시각
```

상대강도 특징:

```text
session_since_return
since_detection_return
1m/3m/5m/15m rolling return
```

신규 편입 종목은 필요한 horizon이 준비되기 전까지 `WarmingUp`이다.

## 4.3 탈락 정책

```text
현재 membership ranking
  탈락 후 profile별 grace 기간을 거쳐 제외 가능

검증 history
  장 마감까지 계속 추적
```

조건식 탈락 자체가 매도 신호라고 가정하지 않는다. 탈락 이후 MFE/MAE를 별도로 평가한다.

---

# 5. 과거 1516 포착 스냅샷 Source

## 5.1 조회 키

과거 source는 다음 키로 단 하나의 cohort를 선택한다.

```text
trading_date
condition_id 또는 condition_name
capture_time
```

`hold_period`나 `7시간 수익률`은 후보군 조회 키가 아니라 결과 label 그룹 식별에만 사용한다.

## 5.2 중복과 불완전 자료 처리

동일 키에 여러 결과 그룹이 존재하면 자동 선택하지 않는다.

워크벤치에 다음을 표시하고 사용자가 명시적으로 선택한다.

```text
원본 레코드 그룹 ID
포착 종목 수
생성시각
조건식 버전/설명
보유기간 label 종류
데이터 완전성
```

종목코드 중복은 하나로 정규화하고 원본 중복 수를 diagnostic으로 보존한다.

## 5.3 캔들 범위

상대강도 출력 시작은 반드시 포착시각 이후다.

```text
출력 시작 = capture_time
```

계산 warm-up을 위해 포착시각 이전 캔들을 내려받을 수 있다.

```text
pre-roll 예: capture_time 이전 30~120분
```

그러나 pre-roll 데이터로 포착 이전 rank나 leader event를 생성하면 안 된다.

## 5.4 Frozen Cohort 비교 기준

과거 replay에서는 cohort가 고정되므로 percentile 변화가 membership 변화 때문에 왜곡되지 않는다.

```text
09:05 cohort 36종목
→ 09:05 이후 매 snapshot마다 동일 36종목을 분모로 사용
```

해당 시각에 데이터가 준비되지 않은 종목은 그 snapshot에서만 `NotEvaluable`이며 cohort 자체에서는 제거하지 않는다.

---

# 6. 공통 캔들·재생 계약

## 6.1 과거 모드

```text
Frozen cohort 확정
→ 전체 종목의 1분봉을 capture_time 이후 장 마감까지 수집
→ 동일 거래일 timestamp로 정렬
→ 30초 또는 60초 ranking clock 생성
→ 당시까지 공개된 캔들만 엔진에 투입
```

1분봉 데이터로 30초 snapshot을 만들 때 미래 1분 종가를 미리 사용하면 안 된다.

초기 버전은 안전하게 60초 snapshot만 지원한다. 30초 replay는 실제 tick 자료가 확보된 날짜에서만 허용한다.

## 6.2 실시간 모드

```text
최초 1분봉 history hydration 1회
→ 이후 실시간 tick으로 live bar 갱신
→ ranking clock마다 현재까지의 값으로 snapshot 발행
```

REST minute history를 30초마다 반복 요청하지 않는다.

---

# 7. 장중과 장후의 동일성 검증

동일 거래일에 실제 장중 기록이 있다면 다음을 비교한다.

```text
장중 09:05 실시간 cohort snapshot
장후 1516 09:05 historical cohort snapshot
```

비교 항목:

```text
종목코드 집합
종목 수
누락/추가 종목
capture timestamp 차이
조건식 버전
```

두 집합이 다르면 병합하지 않고 각각 별도 run으로 기록한다.

```text
run_source = LIVE_RECORDED
run_source = PERFORMANCE_SNAPSHOT
```

이 차이 자체가 조건식 source의 신뢰성 지표다.

---

# 8. 사후성과 label 결합

ranking 결과와 미래성과는 별도 단계에서 결합한다.

```text
RankSnapshot
  당시 강도·순위·지속성·거래대금만 포함

OutcomeLabel
  이후 5/15/30/60분 return, MFE, MAE, 최고수익률 포함
```

결합 키:

```text
run_id
trading_date
condition_id
capture_time
symbol
rank_timestamp
```

1516 화면의 `기간 내 최고수익률`은 ranking 입력값이 아니라 `OutcomeLabel`의 보조 검증값이다.

---

# 9. 워크벤치 화면 상태

## 9.1 공통 그리드

```text
원본 포함 여부
현재 membership
종목코드/종목명
최초 포착시각
데이터 상태
현재 강도
현재 순위
순위 변화
Top-N 체류시간
누적거래대금
최종 MFE/MAE — replay 완료 후 별도 열
```

## 9.2 Source별 시각 표시

실시간:

```text
편입 marker
탈락 marker
재편입 marker
```

과거 스냅샷:

```text
공통 capture_time 수직선
Frozen Cohort 배지
```

---

# 10. 저장 계약

```text
data/stock_pool_workspace.json
  마지막 UI 설정

data/stock_pool_runs/{date}/{run_id}/cohort.json
  원본 cohort 스냅샷

data/stock_pool_runs/{date}/{run_id}/membership_events.jsonl
  실시간 편입/탈락 이벤트

data/stock_pool_runs/{date}/{run_id}/rank_snapshots.jsonl
  당시 계산값

data/stock_pool_runs/{date}/{run_id}/outcome_labels.jsonl
  미래성과 label
```

과거 run의 cohort와 rank snapshot은 append-only이며 재계산 결과는 새 `run_id`로 생성한다.

---

# 11. 구현 순서

```text
1. fixture HistoricalSnapshotSource
2. 날짜/조건식/포착시각 selector
3. Frozen cohort grid
4. fixture 1분봉 replay와 상대강도 동시차트
5. rank snapshot JSONL
6. outcome labeler
7. 실제 1516/performance_result adapter
8. fixture LiveConditionSource
9. server32 원자적 condition session
10. 장중 실시간 조건식 연결
11. 동일 날짜 live-vs-historical cohort 비교 리포트
```

첫 수직 구현은 과거 스냅샷 fixture부터 시작한다. 장후에도 반복 검증할 수 있고, 상세차트나 실시간 서버 상태에 의존하지 않기 때문이다.

---

# 12. 완료 기준

```text
과거 모드
  지정일·조건식·포착시각으로 동일 cohort 재현
  포착 이후 데이터만으로 순위 재생
  미래수익률 입력 누출 없음
  반복 실행 시 동일 rank snapshot 산출

실시간 모드
  초기목록과 delta 사이 누락 없음
  편입/탈락/재편입 결정적 기록
  REST 반복 없이 실시간 갱신

공통
  동일 ranking engine 사용
  상세차트 핵심 파일 변경 없음
  source 오류가 다른 mode의 저장자료를 오염시키지 않음
```
