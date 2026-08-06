# 1516 클립보드 Frozen Cohort 가져오기

## 목적

키움 1516 성과검증 화면의 검색 종목 목록은 컨텍스트 메뉴 `복사(Z)`로 탭 구분 텍스트를 클립보드에 복사할 수 있다.

종목풀 워크벤치는 이 텍스트를 받아 종목명과 사후 성과 label을 정규화하고, `gate3.g3_symbol_master`에서 키움 종목코드를 exact name으로 조회해 과거 포착 Frozen Cohort를 만든다.

## 사용자 동선

```text
데이터소스 = 1516 과거 포착
→ 불러오기
→ 1516 클립보드 가져오기 다이얼로그
→ 클립보드 붙여넣기
→ 변환 + 종목코드 조회
→ 코드 확정/폐기 목록 확인
→ 코드 확정 종목으로 Frozen Cohort 생성
```

## 입력 열

```text
종목명
1분간 수익률
3분간 수익률
7시간 수익률
기간 내 최고수익률
검색시점 거래량
기타
```

따옴표, `%`, 부호, 거래량 쉼표를 제거해 수치로 변환한다.

## 종목마스터 계약

```sql
SELECT code, name, COALESCE(market, '')
FROM gate3.g3_symbol_master
WHERE delisted = 0
ORDER BY name, code;
```

자동 확정 조건:

- 종목명 exact match
- 조회 결과 정확히 1건
- 같은 붙여넣기 안에서 종목코드가 중복되지 않음

폐기 조건:

- 0건: `MissingSymbol`
- 2건 이상: `AmbiguousSymbol`
- 이미 확정된 코드 반복: `DuplicateSymbol`
- 입력 수치 변환 실패: `InvalidRow`

폐기 종목은 조용히 사라지지 않고 원본 줄·종목명·상태·사유를 다이얼로그에 표시한다.

## MySQL 접속

워크벤치는 현재 Windows `mysql.exe` CLI를 자식 프로세스로 실행한다.

`.env` 키:

```text
MYSQL_HOST=127.0.0.1
MYSQL_PORT=3306
MYSQL_USER=root
MYSQL_PASSWORD=...
MYSQL_EXE=C:\Program Files\MySQL\MySQL Server 8.4\bin\mysql.exe  # PATH에 있으면 생략 가능
```

암호는 명령행 인수에 넣지 않는다. 임시 `--defaults-extra-file`을 만들고 조회 종료 직후 삭제한다. stdout/stderr도 임시 파일로 격리한다.

## 미래정보 격리

다음 값은 가져오지만 장중 ranking feature로 사용하지 않는다.

```text
1분·3분·7시간 수익률
기간 내 최고수익률
기타 사후 값
```

이 값은 가져오기 미리보기와 향후 성과검증 label로만 보존한다.

Frozen Cohort에 즉시 공급되는 것은 다음뿐이다.

```text
code
name
market
거래일
조건식
포착시각
```

## 현재 완료 범위

- 클립보드 다이얼로그
- TSV 파싱
- MySQL 종목마스터 조회
- exact name 코드 확정
- 폐기 사유 표시
- Frozen Cohort 생성
- parser/resolver 회귀 테스트

## 다음 단계

Frozen Cohort 확정 후 선택 거래일·포착시각 이후의 실제 1분봉을 종목별로 hydration한다. 현재 단계에서는 실제 분봉이 아직 연결되지 않았으므로 cohort 확정 뒤 상태줄에 이를 명시하고 상대강도 replay는 시작하지 않는다.
