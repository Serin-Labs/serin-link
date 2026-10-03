#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
bin=$(mktemp /tmp/wifi-session.XXXXXX)
trap 'rm -f "$bin"' EXIT
gcc -std=c11 -Wall -Wextra -Werror -I../../main test.c -o "$bin"
"$bin"
