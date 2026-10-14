#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
set -euo pipefail
: "${SERIN_SIM_BIN:?Set SERIN_SIM_BIN to the simulator executable}"
python3 "$(dirname "$0")/settings_touch.py"
python3 "$(dirname "$0")/settings_layout.py"
