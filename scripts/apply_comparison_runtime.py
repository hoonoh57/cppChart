from pathlib import Path
import runpy
import subprocess

root = Path(__file__).resolve().parents[1]
scripts = root / "scripts"
source = subprocess.check_output(
    [
        "git", "show",
        "53cc7a92fe9c5f189898f2fdcc6ef573adffe091:scripts/apply_comparison_runtime.py"
    ],
    cwd=root,
    text=True,
    encoding="utf-8-sig")

old_include = '''text = replace_once(
    text,
    "#include \\\"../core/kiwoom_market_data.h\\\"\\n",
    "#include \\\"../core/kiwoom_market_data.h\\\"\\n"
    "#include \\\"../core/kiwoom_index_realtime.h\\\"\\n",
    "runner index include")'''
new_include = '''text = replace_once(
    text,
    "#include \\\"../core/kiwoom_runtime_engine.h\\\"\\n",
    "#include \\\"../core/kiwoom_runtime_engine.h\\\"\\n"
    "#include \\\"../core/kiwoom_index_realtime.h\\\"\\n",
    "runner index include")'''
if old_include not in source:
    raise RuntimeError("runner include migration block was not found")
source = source.replace(old_include, new_include, 1)

old_callback = '''text = replace_once(
    text,
    "        std::function<void(const StockTradeTick&)> stockTrade;\\n",
    "        std::function<void(const StockTradeTick&)> stockTrade;\\n"
    "        std::function<void(const IndexValueTick&)> indexValue;\\n",
    "index callback")'''
new_callback = '''text = replace_once(
    text,
    "        std::function<void(\\n"
    "            const StockTradeTick& tick)> stockTrade;\\n",
    "        std::function<void(\\n"
    "            const StockTradeTick& tick)> stockTrade;\\n"
    "        std::function<void(\\n"
    "            const IndexValueTick& tick)> indexValue;\\n",
    "index callback")'''
if old_callback not in source:
    raise RuntimeError("runner callback migration block was not found")
source = source.replace(old_callback, new_callback, 1)

implementation = scripts / "_comparison_runtime_impl.py"
implementation.write_text(source, encoding="utf-8-sig", newline="")
try:
    runpy.run_path(str(implementation), run_name="__main__")
finally:
    implementation.unlink(missing_ok=True)
