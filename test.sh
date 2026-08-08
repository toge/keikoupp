#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
ctest --test-dir build --output-on-failure
