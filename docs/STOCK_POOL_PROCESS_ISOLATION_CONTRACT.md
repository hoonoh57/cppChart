# 종목풀 분석 프로세스 격리 계약

## 상태

- 적용 브랜치: `p2/kiwoom-mock-gateway`
- 선행 문서: `STOCK_POOL_ANALYSIS_WORKBENCH_UI_DESIGN.md`
- 우선순위: 최상위 아키텍처 불변식

## 1. 실행 파일 분리

```text
shell.exe
  기존 상세차트 전용
  현재 기능 동결

stock_pool_workbench.exe
  조건식 기반 종목풀 분석 전용
  조건식 선택·일자 선택·종목풀 그리드·동시 상대강도 차트
```

종목풀 기능을 `shell_main_mXX.cpp` 래퍼에 추가하지 않는다. 종목풀 구현 때문에 기존 `shell.exe` 진입점, 상세차트 레이아웃, chart renderer, indicator/comparison manager를 수정하지 않는다.

## 2. 공유 가능 코드

두 실행 파일은 다음 순수 라이브러리만 공유할 수 있다.

```text
core value types
JSON parser
server32 normalized protocol types
symbol catalog/cache
stock-pool pure feature/ranking/replay engine
```

다음 객체는 공유하지 않는다.

```text
ImGui context
D3D device/context
chart RenderDocument
chart viewport/pane/axis state
indicator definitions/workspace
comparison definitions/workspace
market chart module globals
window procedure/global UI state
```

## 3. 상세차트 연결

첫 구현에서는 프로세스 간 자동 연결도 필수로 두지 않는다.

```text
1단계: 종목풀에서 종목코드 복사
2단계: 검증 후 명시적 IPC OpenDetailChartCommand 추가 가능
```

IPC를 추가할 경우에도 허용 명령은 다음 하나뿐이다.

```text
OpenDetailChartCommand(code, name, market)
```

상세차트의 지표, 비교, 기간, 봉 수, viewport, 패널 상태를 종목풀에서 변경하는 명령은 금지한다.

## 4. 빌드 분리

```text
build.bat
  기존 shell.exe 빌드 계약 유지

build_stock_pool.bat
  stock_pool_workbench.exe 전용 빌드
```

종목풀 빌드 실패가 기존 `shell.exe`를 삭제하거나 교체하면 안 된다. 상세차트 빌드 실패와 종목풀 빌드 실패를 별도 결과로 관리한다.

## 5. 저장소 분리

```text
data/indicator_workspace.json          shell.exe 전용
data/comparison_workspace.json         shell.exe 전용
data/stock_pool_workspace.json         workbench 전용
data/stock_pool_history/...             workbench 전용
```

한 실행 파일이 다른 실행 파일의 workspace JSON을 쓰지 않는다.

## 6. 데이터 소스

`stock_pool_workbench.exe`가 server32와 직접 통신한다.

```text
condition session
realtime WebSocket
MarketEye REST
minute-history REST
```

상세차트가 받은 데이터를 종목풀에 전달하거나, 종목풀이 상세차트 전역 market module을 읽는 구조는 금지한다.

## 7. UI 수용 기준

```text
shell.exe 실행 상태에서 workbench 실행
→ 두 창이 독립적으로 동작
→ workbench 조건식/fixture 분석 수행
→ shell 차트 상태 불변
→ workbench 종료 후 shell 계속 정상
→ shell 종료 후 workbench 계속 정상
```

소스 검증에서는 종목풀 첫 수직 구현 커밋과 그 직전 커밋을 비교하여 다음 상세차트 핵심 파일의 변경이 없어야 한다.

```text
shell_main*.cpp 기존 파일
ui/render_document_renderer.*
ui/indicator_manager_ui.*
ui/comparison_manager_ui.*
app/market_data_module.*
app/indicator_* 기존 파일
app/comparison_* 기존 파일
render/* 기존 파일
```

새로운 공용 순수 타입 추가는 허용하지만 기존 상세차트 동작 변경은 허용하지 않는다.

## 8. 첫 구현 순서

```text
1. stock_pool_workbench.exe 빈 독립 창
2. fixture 조건식·일자 toolbar
3. fixture 종목풀 grid
4. fixture Top-10 상대강도 전용 경량 chart
5. 행 선택과 선 강조
6. workbench workspace JSON 저장·복원
7. 기존 shell.exe와 동시 실행 격리 검증
8. 순수 C++ ranking engine 연결
9. server32 condition/MarketEye 계약 연결
10. 실제 실시간·과거 재생 연결
```

이 계약을 위반하는 구현은 기능이 화면에 보이더라도 완료로 인정하지 않는다.
