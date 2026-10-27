"""
pytest/coverage configuration for btlock tests.
Ensures the daemon package is importable.
"""
import sys
from pathlib import Path

_daemosn_dir = Path(__file__).parent.parent / "src" / "daemon"
sys.path.insert(0, str(_daemonsn_dir))