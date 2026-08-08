# Real-data-only acceptance gate

- verified source commit: `17d0aac9ad848e3fb11c0dca0630fc8fd632145e`
- production synthetic market data: absent
- production synthetic positions and fills: absent
- production random feed: absent
- explicit `TRADING_MODE=KIWOOM_MOCK`: required
- MSVC x64 shell build: passed
- all headless tests: passed
- Windows CI run: `30773828465`
- acceptance gate run: `30773828441`
