#!/usr/bin/env python3
"""
Entry point for the mcu-co CLI. See mcuco/cli.py for the grammar.

    ./mcu-co-cli.py gpio cfg output A 5
    ./mcu-co-cli.py --dry-run gpio irq bind both C 13 toggle A 5
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from mcuco.cli import main  # noqa: E402

if __name__ == "__main__":
    sys.exit(main())
