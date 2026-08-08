# 종목풀 시간 레짐별 선별 점수 설계

## 0. 목적

종목풀 순위 엔진은 하루 종일 하나의 점수식을 사용하지 않는다.

장초반에는 대부분의 당일 상승률과 자금 집중이 빠르게 발생하므로 가격강도·상대강도·거래대금 가속도를 우선한다. 이후에는 단순 누적상승률보다 눌림의 질, 상위권 유지, 전고점 회복·돌파, 거래대금 재확대 같은 구조적 패턴을 우선한다.

단, 시간대별 별도 구현을 흩뿌리지 않는다.

```text
공통 Feature Engine
→ TimeRegime 판정
→ 해당 ScoringProfile 선택
→ 동일 Ranking Engine
→ Top-M 발행
```

상세차트 `shell.exe`는 수정하지 않는다. 본 설계는 `stock_pool_workbench.exe`와 순수 C++ 종목풀 엔진 전용이다.

---

# 1. 최상위 원칙

## 1.1 상대강도는 하루 전체의 공통 척도다

시간대에 따라 우선순위는 달라져도 다음은 항상 계산한다.

- 조건식 종목풀 대비 상대강도
- 시장·업종 대비 상대강도
- 1·3·5·10·15분 상대수익
- 장 시작 이후 상대수익
- 순위와 순위 경로
- 누적거래대금과 증가속도

시간대 프로필은 이 공통 피처를 새로 계산하지 않고 선택·해석만 달리한다.

## 1.2 시간대 경계는 설정값이며 진리가 아니다

초기 기본값은 다음과 같이 둔다.

```text
OpeningDiscovery   09:00~09:05
OpeningLeadership  09:05~09:20
MorningExpansion   09:20~10:00
MiddayStructure    10:00~13:30
AfternoonRenewal   13:30~14:50
ClosingRisk        14:50~15:20
```

이 경계는 검증 대상이다. 거래일별 결과를 보고 09:03, 09:10, 09:30처럼 임의 조정하지 않는다. 날짜 단위 walk-forward에서만 변경한다.

## 1.3 점수보다 Gate가 먼저다

순위가 높더라도 다음 Gate를 통과하지 못하면 매수 후보로 발행하지 않는다.

```text
데이터 준비 완료
freshness 정상
최소 누적거래대금 충족
거래정지·관리·이상 종목 제외
종목풀 레짐이 거래 허용 상태
Top-1/2가 나머지와 충분히 분리
남은 수익 가능성이 있다고 판단되는 구조
```

`1위 존재 = 매수`가 아니다.

## 1.4 초기에는 임의 가중합보다 설명 가능한 순위 tuple을 사용한다

예:

```text
OpeningLeadership tuple
1. 5분 종목풀 상대강도 percentile
2. 장 시작 이후 상대수익 percentile
3. 누적거래대금 가속도 percentile
4. Top-5 연속 유지시간
5. 고점 대비 상대강도 낙폭
```

동률 또는 근접값을 다음 항목으로 정렬한다. 여러 날짜 검증 후에만 가중합 또는 학습모델을 검토한다.

---

# 2. 시간 레짐별 의미

## 2.1 OpeningDiscovery — 09:00~09:05

목표:

- 장초반 즉시 자금이 몰리는 종목 탐색
- 갭과 첫 체결 잡음을 구분
- 아직 긴 패턴이 없는 상태에서 빠른 선두 후보 확보

우선 피처:

```text
시가 대비 수익률
전일종가 대비 수익률
1분·3분 종목풀 상대수익 percentile
시장 대비 초과수익
누적거래대금 percentile
거래대금 속도
체결강도 percentile
순위 상승속도
```

주의:

- 첫 1봉 수익률 하나만으로 리더 확정 금지
- 갭상승 자체는 관심도이며, 지속 거래대금과 상대강도 확인 필요
- 데이터 horizon이 부족한 종목은 `WarmingUp`

발행 상태:

```text
Watch
Emerging
```

`ConfirmedLeader`는 최소 유지조건을 충족한 뒤에만 허용한다.

## 2.2 OpeningLeadership — 09:05~09:20

목표:

- 장초반 실제 대장주 분리
- 순간 급등과 지속 리더 구분

우선 피처:

```text
3분·5분 상대강도
장 시작 이후 상대수익
Top-3·Top-5 연속 유지시간
거래대금 속도·가속도
상대강도 신고가 갱신
강도 고점 대비 낙폭
시장 약세 시 방어력
```

핵심 패턴:

```text
상대강도 상승
→ 거래대금 동반
→ 상위권 유지
→ 짧은 눌림에서도 순위 방어
```

이 구간에서는 가격강도 비중이 가장 높지만, 가격 상승률만으로 확정하지 않는다.

## 2.3 MorningExpansion — 09:20~10:00

목표:

- 초기 리더의 확장 지속 여부
- 후발주가 진짜 대장으로 전환되는 과정
- 첫 눌림 후 재상승 포착

추가 피처:

```text
최근 고점 대비 조정폭
조정 중 상대강도 순위 유지
조정 중 거래대금 감소율
재상승 시 거래대금 재확대
직전 상대강도 고점 회복 여부
가격 전고점 거리
전고점 돌파 직전 압축 정도
상대강도 기울기 재상승
```

유의미한 구조:

```text
대장 유지형
  Top-3 유지 → 얕은 조정 → 거래대금 재확대 → 신고가

후발 전환형
  중위권 → 순위 급상승 → 전고점 회복 → Top-3 지속
```

단순히 누적수익률이 이미 높은 종목보다, 조정 중 강도를 잃지 않고 재상승하는 종목을 우선한다.

## 2.4 MiddayStructure — 10:00~13:30

목표:

- 장초반 급등 후 소멸한 종목 제거
- 긴 횡보·눌림 뒤 재출발 준비 종목 탐색

우선 피처:

```text
Top-10 체류율
상대강도 평균과 변동성
상대강도 고점 대비 낙폭
가격·강도 압축
누적거래대금 유지
거래대금 급감 후 안정화
전고점 또는 기준선 근접
실패 돌파 횟수
```

장초반 수익률은 참고값으로만 유지하고 우선순위를 낮춘다. 오전 고수익이었지만 상대강도와 거래대금이 지속 하락하면 리더 지위를 박탈한다.

## 2.5 AfternoonRenewal — 13:30~14:50

목표:

- 오후 재점화 종목
- 전고점 돌파와 장후반 수급 집중

우선 피처:

```text
오후 5분·15분 상대강도 변화
오전 고점 회복률
전고점 돌파 여부와 돌파 품질
거래대금 재가속
프로그램·기관·외인 수급 변화
Top-N 재진입 속도
돌파 후 순위 유지
```

새로운 `AfternoonLeader` 사건을 별도 기록한다. 오전 리더와 오후 리더의 성과를 분리 평가한다.

## 2.6 ClosingRisk — 14:50~15:20

목표:

- 신규 진입보다 리더 유지·청산 판단
- 장 마감 왜곡과 유동성 위험 억제

우선 피처:

```text
상대강도 유지
순위 붕괴
거래대금 비정상 급증
장 마감까지 남은 시간
호가·체결 freshness
```

기본 정책은 신규 진입 제한이며, 별도 검증 없이는 장 마감 급등을 추격하지 않는다.

---

# 3. 공통 Feature Engine

시간대별 프로필이 요구하는 모든 피처는 단일 엔진에서 계산한다.

```cpp
struct StockPoolFeatureVector
{
    EpochMillis asOf;
    std::string code;

    double return1m;
    double return3m;
    double return5m;
    double return10m;
    double return15m;
    double sessionReturn;

    double poolRelative1mPct;
    double poolRelative3mPct;
    double poolRelative5mPct;
    double poolRelative15mPct;
    double marketRelativePct;

    double cumulativeTurnoverPct;
    double turnoverVelocityPct;
    double turnoverAccelerationPct;
    double tradeIntensityPct;

    double rankVelocity;
    double rankAcceleration;
    double top3DurationSeconds;
    double top5DurationSeconds;
    double top10Occupancy;

    double strengthSlope;
    double strengthCurvature;
    double strengthDrawdown;

    double pullbackDepth;
    double recoveryRatio;
    double priorHighDistance;
    double breakoutQuality;
    double compressionScore;

    bool dataReady;
    bool stale;
};
```

프로필마다 계산기를 따로 두지 않는다.

---

# 4. ScoringProfile 계약

```cpp
struct StockPoolScoringProfile
{
    std::string id;
    std::string version;
    TimeRegime regime;
    std::vector<EligibilityRule> gates;
    std::vector<RankingKey> rankingKeys;
    LeaderTransitionPolicy transitions;
};
```

프로필은 JSON으로 저장한다.

```text
data/stock_pool_profiles/
  opening_discovery.v1.json
  opening_leadership.v1.json
  morning_expansion.v1.json
  midday_structure.v1.json
  afternoon_renewal.v1.json
  closing_risk.v1.json
```

UI에서 값을 바꿔 실험할 수 있지만 결과에는 profile ID·version·전체 JSON hash를 기록한다.

---

# 5. 상태는 시간대 경계에서 초기화하지 않는다

09:20이 되었다고 종목의 리더 이력을 리셋하면 안 된다.

```text
09:18 ConfirmedLeader
→ 09:20 MorningExpansion profile로 전환
→ 기존 순위·체류시간·강도 경로 유지
→ 새 프로필로 다음 상태 전이만 평가
```

시간 레짐 변경 사건을 history에 기록한다.

```text
ProfileChanged(opening_leadership.v1 → morning_expansion.v1)
```

---

# 6. 종목풀 레짐과 시간 레짐을 결합한다

시계만으로는 충분하지 않다. 같은 09:15라도 후보군 전체가 강한 날과 무너지는 날은 다르다.

공통 종목풀 레짐:

```text
BroadRiskOn
NarrowLeadership
Mixed
BroadRiskOff
NoTrade
```

예:

```text
09:05 OpeningLeadership
+ BroadRiskOn
  리더 1~2종목 발행 허용

09:05 OpeningLeadership
+ BroadRiskOff
  절대강도·분리도가 매우 높은 종목만 허용

10:30 MiddayStructure
+ NoTrade
  순위는 계산하지만 Published Top-M은 비움
```

초기에는 시간대와 종목풀 레짐을 독립적으로 기록하고, 조합별 성과를 검증한다.

---

# 7. WYSIWYG 백테스트 UI

종목풀 리플레이 화면의 시간축에 레짐 배경 band를 표시한다.

```text
09:00~09:05  OpeningDiscovery
09:05~09:20  OpeningLeadership
09:20~10:00  MorningExpansion
...
```

각 종목 lane에는 다음을 겹쳐 그릴 수 있다.

- 상대강도 heatmap
- 선택 프로필의 순위
- Top-M 포함 구간
- Emerging/Confirmed/Persistent/Weakening marker
- 눌림·회복·전고점 돌파 사건
- 실제 매수·청산 시뮬레이션

마우스 hover 정보:

```text
시각
활성 TimeRegime
활성 Profile ID/version
종목풀 Regime
각 원시 피처
각 RankingKey 값
Gate 통과/실패 사유
현재 순위
Top-M 포함 여부
```

결과만 보여주지 않고 왜 해당 시점에 1위였는지 설명해야 한다.

---

# 8. 검증 방법

시간대별로 Top-M 성과를 따로 평가한다.

```text
09:00~09:05 진입
09:05~09:20 진입
09:20~10:00 진입
10:00 이후 진입
```

각 구간에서:

- 실제 최고수익 Top-K 포착률
- Top-1/2 precision
- 5·15·30·60분 MFE/MAE
- 목표수익 선도달률
- 첫 포착 선행시간
- 평균 보유시간
- 거래비용 후 순수익
- 무거래가 더 나았던 비율

을 계산한다.

필수 비교:

```text
하루 단일 profile
vs
시간 레짐별 profile
```

시간 레짐 방식이 실제로 개선되지 않으면 채택하지 않는다.

---

# 9. 피처 발굴 선순환

장후 evaluator는 시간대별로 별도 질문을 던진다.

## 장초반

```text
실제 10% 이상 상승 종목은 09:03·09:05·09:10에 어떤 가격강도·거래대금 경로였는가?
순간 급등 실패주와 차이는 무엇인가?
```

## 09:20 이후

```text
첫 눌림 후 재상승에 성공한 종목은 조정 중 어떤 순위와 거래대금 특성을 유지했는가?
전고점 돌파 전 어떤 압축·강도 기울기가 선행했는가?
```

발견한 피처는 기존 profile에 즉시 넣지 않고 별도 버전으로 생성한다.

```text
morning_expansion.v1
→ candidate morning_expansion.v2
→ 다른 거래일 walk-forward
→ shadow
→ 승인 시 v2 승격
```

---

# 10. 첫 구현 범위

초기에는 두 레짐만 구현한다.

```text
OpeningLeadership  09:00~10:00
LaterStructure     10:00~15:20
```

첫 fixture 목표:

- 10~30종목 Frozen Cohort
- 1분 replay
- 공통 relative-strength engine
- 장초반 가격강도 중심 profile
- 이후 눌림·전고점·지속성 profile
- Top-1·Top-2 발행
- WYSIWYG lane과 profile 전환 band
- profile별 MFE/MAE 비교

두 레짐 수직 검증이 끝난 뒤에만 6개 시간 레짐으로 세분한다.
