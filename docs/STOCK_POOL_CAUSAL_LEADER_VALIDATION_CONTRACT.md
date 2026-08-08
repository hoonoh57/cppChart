# 종목풀 인과적 대장주 선별·1516 검증 계약

## 0. 문서 지위

- 대상: `stock_pool_workbench.exe`
- 상세차트 `shell.exe`: 수정 금지
- 1516 성과검증 화면: 기존 기능 재사용, 재구현 금지
- 우선순위: 종목풀 분석·백테스트 구현의 최상위 검증 계약

---

# 1. 목표를 정확히 구분한다

1516 성과검증 화면은 장이 끝난 뒤 실제 미래 가격을 모두 알고 그린 **사후 정답 시각화**다.

```text
포착시각
→ 이후 실제 종가 경로
→ 1·3·5·10·15·30·60분·7시간 수익률
→ 기간 내 최고수익률
```

이 기능은 이미 존재한다. 종목풀 워크벤치에서 동일 기간수익률 차트를 다시 구현하지 않는다.

종목풀 워크벤치의 목표는 다음이다.

```text
시각 t까지 실제로 관측 가능한 데이터만 입력
→ 시각 t의 선도강도·순위·대장 상태 계산
→ 다음 시각으로 이동
→ 장 종료 후 1516 정답과 대조
```

즉:

```text
종목풀 워크벤치 = 미래를 모르는 예측·선별 시스템
1516 성과검증    = 미래가 확정된 사후 정답·평가 시스템
```

두 기능을 합치거나 중복 구현하지 않는다.

---

# 2. 절대 금지사항

종목풀 계산·UI·차트에는 다음 값을 입력하거나 노출하지 않는다.

```text
현재 replay 시각보다 뒤의 종가·고가·저가·거래량
1·3·5·10·15·30·60분·7시간 사후 수익률
기간 내 최고수익률
장 마감 수익률
미래 MFE·MAE
목표수익 도달 여부·도달시간
1516 결과 순위
```

다음 구현도 금지한다.

- 1516의 기간수익률 차트를 워크벤치 오른쪽에 복제
- 최고수익률 순서로 색상·선 두께·표시 순서를 정함
- 미래 승자만 선택한 뒤 상대강도 로직을 맞춤
- 장 전체 데이터를 먼저 계산한 뒤 과거 시각의 점수처럼 표시
- 모든 거래일을 보고 가중치를 조정한 뒤 같은 거래일에 성능을 보고
- 특정 종목의 사후 고점을 기준으로 대장 확인 시각을 소급

---

# 3. 워크벤치에 허용되는 시각화

워크벤치 오른쪽 동시 차트에는 **인과적으로 계산된 선도강도 시계열**만 그린다.

허용 예:

```text
09:00까지 데이터로 계산한 09:00 강도
09:01까지 데이터로 계산한 09:01 강도
09:02까지 데이터로 계산한 09:02 강도
...
```

표시 가능한 항목:

- 종목풀 횡단면 상대강도 percentile
- 시장·업종·종목풀 중앙값 대비 상대수익
- 1·3·5·15분 rolling 상대수익
- 강도 기울기와 가속도
- 누적거래대금 percentile과 증가속도
- 체결강도·호가잔량·MarketEye 수급의 당시 값
- Top-3·Top-5·Top-10 진입·이탈
- 상위권 연속 유지시간과 체류율
- `WarmingUp`, `Emerging`, `Confirmed`, `Persistent`, `Weakening`
- 데이터 freshness·누락·stale 상태

이 선은 1516의 사후 수익률 곡선과 모양이 같을 필요가 없다. 목표는 미래 상승이 본격화되기 전에 실제 승자를 상위권에 올리고, 거짓 대장을 빠르게 배제하는 것이다.

---

# 4. 강도 엔진의 시간 경계

각 ranking snapshot은 명시적인 `as_of` 시각을 가진다.

```cpp
struct StockPoolRankingSnapshot
{
    EpochMillis asOf;
    std::string cohortId;
    std::uint64_t cohortRevision;
    std::vector<StockPoolMemberRank> members;
};
```

snapshot 생성 시 사용할 수 있는 데이터의 최대 timestamp는 반드시 `asOf` 이하이다.

```text
max(input.timestamp) <= snapshot.asOf
```

1분봉 replay에서는 완료된 봉만 사용한다.

예:

```text
09:05:00 snapshot
→ 09:04:59까지 확정된 자료만 사용
```

데이터 공급 API가 봉 timestamp를 종료시각으로 정의한다면 그 계약에 맞춰 경계를 단 한 곳에서 정규화한다. UI나 점수 계산기마다 서로 다른 시간 해석을 두지 않는다.

30초 snapshot은 실제 tick replay 자료가 있을 때만 허용한다. 1분봉만 있는 날짜에 30초 값을 보간하거나 미래 1분 종가를 분할 사용하지 않는다.

---

# 5. 1516 자료의 역할

1516 자료에서 후보군 생성에 사용할 수 있는 값은 다음뿐이다.

```text
거래일
조건식 ID·이름·버전
포착시각
포착된 종목코드
종목명·시장
원본 그룹 ID
```

이 값으로 Frozen Cohort를 확정한다.

1516의 미래 결과 값은 replay가 완전히 종료된 뒤 별도 label 단계에서만 결합한다.

```text
Causal ranking history
+ 1516 future outcome labels
→ Validation report
```

워크벤치 실시간·replay 화면에는 label을 전달하지 않는다.

권장 프로세스 분리:

```text
stock_pool_workbench.exe
  causal cohort/replay/ranking/UI

stock_pool_evaluator.exe 또는 독립 report command
  ranking history + 1516 labels 결합
```

최소한 모듈 수준에서도 evaluator는 ranking engine과 별도 라이브러리·namespace로 분리한다.

---

# 6. 1516과의 비교 대상

1516의 사후 곡선 자체를 복제하지 않고 다음 지표로 선별 품질을 평가한다.

## 6.1 승자 포착률

```text
1516 실제 최고수익률 Top-K 중
우리 엔진이 지정 시각까지 Top-N에 올린 비율
```

예:

```text
실제 최고수익률 Top-3
→ 09:10까지 Ranking Top-5 포착률
→ 09:20까지 Ranking Top-5 포착률
```

## 6.2 조기성

```text
실제 승자가 최초로 Top-N에 진입한 시각
실제 승자가 ConfirmedLeader가 된 시각
실제 급등 시작 또는 목표수익 도달까지 남은 선행시간
```

## 6.3 지속성

```text
실제 승자의 Top-N 연속 유지시간
Top-N 체류율
순위 변동성
Confirmed 이후 재이탈 횟수
```

## 6.4 거짓 양성 억제

```text
Top-N·Confirmed로 선정됐지만
이후 MFE가 낮거나 MAE가 큰 종목의 비율
```

## 6.5 순서 유사성

시각 t의 인과 순위와 장 종료 후 실제 성과 순위를 비교한다.

```text
Spearman rank correlation
Top-K overlap
NDCG@K
winner reciprocal rank
```

상관계수 하나만으로 완료 판단하지 않는다. 조기성·지속성·거짓 양성·실제 거래 가능성을 함께 본다.

---

# 7. 최적화 목표

점수값이 사후 수익률 곡선과 수치적으로 같아지는 것이 목표가 아니다.

좋은 로직의 조건:

```text
실제 고수익 종목을 상승 초기에 상위권으로 이동
상위권에서 충분히 지속
고점 직전 한 번만 튄 종목보다 일찍 감지
거래대금이 부족한 허상 종목 억제
급등 후 끝난 종목을 늦게 추격하지 않음
시장 전체 동반 상승을 개별 대장으로 오인하지 않음
```

따라서 최적화 목적은 다목적이어야 한다.

```text
maximize winner recall before move
maximize lead time
maximize Top-N persistence of true winners
minimize false leader rate
minimize post-selection MAE
minimize ranking churn
```

임의 가중합 하나로 즉시 합치지 않는다. 각 목적을 독립 측정하고 Pareto trade-off를 확인한 뒤 프로필을 결정한다.

---

# 8. 검증 분할

동일 날짜에서 로직을 조정하고 같은 날짜 성능을 최종 성능으로 보고하지 않는다.

```text
개발 날짜군
검증 날짜군
최종 보류 날짜군
```

분할은 종목 단위가 아니라 거래일 단위다.

권장:

```text
walk-forward
과거 날짜로 파라미터 확정
→ 다음 미사용 날짜 평가
→ 창을 앞으로 이동
```

조건식 버전이 바뀌면 별도 cohort family로 관리한다.

---

# 9. 첫 구현 수직 절단

첫 버전은 사후 수익률 차트를 만들지 않는다.

```text
1. 과거 날짜·조건식·포착시각으로 Frozen Cohort 읽기
2. 1분봉을 timestamp 순으로 causal replay
3. 당시까지의 수익률·상대강도·거래대금 특징 계산
4. 매 1분 전체 순위 snapshot JSONL 기록
5. 워크벤치에 그리드와 causal strength line 표시
6. replay 종료
7. 별도 evaluator가 1516 결과와 결합
8. 승자 포착률·조기성·지속성·거짓 양성 보고서 생성
```

UI 수용 기준:

- replay 도중 미래 결과 열이 없음
- 현재 replay clock 이후 데이터 접근이 없음
- 선택 종목의 causal strength line만 표시
- Top-N 변화가 그 시각의 snapshot과 일치
- replay를 같은 입력으로 반복하면 바이트 동일한 ranking history 생성

평가 수용 기준:

- evaluator를 실행하기 전 ranking history 해시를 고정
- label 결합 이후 ranking history가 변경되지 않음
- 1516 실제 Top-K에 대한 시각별 포착률을 재현 가능
- 미래 데이터 차단 테스트가 통과하지 않으면 결과 무효

---

# 10. 완료 정의

다음은 완료가 아니다.

- 1516 사후 곡선과 비슷한 차트를 다시 표시
- 특정 하루 실제 1등을 결과적으로 찾음
- 전체 데이터를 사용하면 순위가 잘 맞음
- 개발에 사용한 날짜에서 상관계수가 높음

완료 조건은 다음이다.

```text
미래 데이터 없이 생성된 고정 ranking history가 존재
→ 미사용 거래일에서도 실제 승자를 조기에 Top-N으로 포착
→ 상위권 지속성과 거짓 양성 억제가 반복 재현
→ 1516 결과와의 평가가 독립 evaluator에서 수행
```

이 계약을 위반하는 구현은 화면이 그럴듯해도 종목풀 대장주 선별 기능으로 인정하지 않는다.
