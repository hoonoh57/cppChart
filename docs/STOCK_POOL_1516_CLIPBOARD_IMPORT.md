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

## MySQL 직접 접속

워크벤치는 외부 `mysql.exe`를 실행하지 않는다.

```text
stock_pool_workbench.exe
→ vcpkg libmysql C API
→ MYSQL_HOST:MYSQL_PORT TCP 접속
→ gate3 schema 선택
→ g3_symbol_master 직접 조회
```

`.env` 키:

```text
MYSQL_HOST=127.0.0.1
MYSQL_PORT=3306
MYSQL_USER=root
MYSQL_PASSWORD=...
```

금지·불필요 항목:

```text
MYSQL_EXE
MySQL Server 설치 경로
외부 프로세스 실행
임시 defaults-extra-file
stdout TSV 재파싱
```

접속은 `mysql_init` → `mysql_real_connect` → `mysql_real_query` → `mysql_store_result` 순서로 수행한다. 연결 제한시간은 5초이고 통신 문자셋은 `utf8mb4`다. 오류 메시지에는 host·port와 MySQL 오류를 표시하지만 비밀번호를 표시하지 않는다.

## C++ 의존성 복원

루트 `vcpkg.json`은 다음 의존성만 선언한다.

```json
{
  "dependencies": ["libmysql"]
}
```

`build_stock_pool.bat`은 다음 순서로 vcpkg를 찾는다.

```text
VCPKG_ROOT
PATH
Visual Studio bundled vcpkg
프로젝트 로컬 .tools/vcpkg bootstrap
```

그 후 manifest를 `x64-windows`로 복원한다.

```text
vcpkg_installed/x64-windows/include
vcpkg_installed/x64-windows/lib
vcpkg_installed/x64-windows/bin
```

워크벤치는 위 헤더와 import library로 링크하고, 런타임 DLL을 `stock_pool_workbench.exe` 옆에 app-local 배치한다. 기존 `shell.exe`와 `build.bat`에는 이 의존성을 연결하지 않는다.

첫 빌드는 libmysql과 종속 라이브러리를 내려받고 빌드하므로 시간이 걸릴 수 있다. 다음 빌드부터 같은 `vcpkg_installed`와 vcpkg binary cache를 재사용한다.

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
- libmysql 직접 종목마스터 조회
- exact name 코드 확정
- 폐기 사유 표시
- Frozen Cohort 생성
- parser/resolver 회귀 테스트
- vcpkg manifest 복원과 app-local DLL 배치

## 다음 단계

Frozen Cohort 확정 후 선택 거래일·포착시각 이후의 실제 1분봉을 종목별로 hydration한다. 현재 단계에서는 실제 분봉이 아직 연결되지 않았으므로 cohort 확정 뒤 상태줄에 이를 명시하고 상대강도 replay는 시작하지 않는다.
