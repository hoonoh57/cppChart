from pathlib import Path
import runpy

here = Path(__file__).resolve().parent
fix = here / "fix_comparison_migration.py"
if fix.exists():
    runpy.run_path(str(fix), run_name="__migration_fix__")
    fix.unlink()
Path(__file__).unlink()
