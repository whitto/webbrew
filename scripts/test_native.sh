#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
binary=$(mktemp /tmp/webbrew-tests.XXXXXX)
trap 'rm -f "$binary"' EXIT
g++ -std=c++17 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined -g -Iinclude test/native/controller_test.cpp -o "$binary"
"$binary"

