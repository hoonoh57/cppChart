from __future__ import annotations

from pathlib import Path

script_path = Path(__file__).with_name("integrate_realtime_feature_levels_v2.py")
source = script_path.read_text(encoding="utf-8-sig")
cut_marker = "\nci = read(CI)\n"
cut_index = source.find(cut_marker)
if cut_index < 0:
    raise RuntimeError("real-time integration CI section was not found")

source = source[:cut_index] + r'''

ci = read(CI)
transport_gate = """          if (-not $transport.Contains('CPPCHART_CONTINUATION_HEADERS')) {
"""
removal_gate = """          if (-not $runner.Contains('UnsubscribeStockTrades') -or
              -not $runner.Contains('BuildWebSocketRemovalMessage')) {
            throw 'Selected-stock 0B removal is not integrated'
          }
          if (-not $runnerTests.Contains('StockTradeRemovalCount') -or
              -not $runnerTests.Contains('must not return after reconnect')) {
            throw '0B REMOVE and reconnect regression gate is missing'
          }

"""
if "Selected-stock 0B removal is not integrated" not in ci:
    ci = replace_once(
        ci,
        transport_gate,
        removal_gate + transport_gate,
        "CI upstream removal gate",
    )
write(CI, ci)

print("Integrated upstream 0B REMOVE with feature execution levels")
'''

namespace = {
    "__name__": "__main__",
    "__file__": str(script_path),
}
exec(compile(source, str(script_path), "exec"), namespace)
