#!/usr/bin/env python3
"""
Run all Python tests for btlock.

Usage:
    python3 tests/python/run_tests.py
    python3 tests/python/run_tests.py -v
"""

import sys
import unittest
from pathlib import Path

# Add daemon source to path
daemon_dir = Path(__file__).parent.parent / "src" / "daemon"
sys.path.insert(0, str(daemon_dir))

if __name__ == "__main__":
    verbosity = 2 if "-v" in sys.argv else 1

    loader = unittest.TestLoader()
    suite = loader.discover(str(Path(__file__).parent),
                            pattern="test_*.py")
    runner = unittest.TextTestRunner(verbosity=verbosity)
    result = runner.run(suite)
    sys.exit(0 if result.wasSuccessful() else 1)