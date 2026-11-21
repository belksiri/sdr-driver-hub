#!/bin/bash
# test_driver_logic.sh — Tests hôtes (C pur) de la logique du pilote RTL-SDR :
# règles du protocole client v2, et compilation DriverKit de librtlsdr avec
# la couche de compatibilité (Driver/dkcompat).
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/build/hosttests"
mkdir -p "$OUT"
clang -std=c11 -Wall -Wextra -Werror -I"$ROOT/Driver" \
  "$ROOT/tests/driver/test_brtl_client_policy.c" "$ROOT/Driver/brtl_client_policy.c" \
  -o "$OUT/test_brtl_client_policy"
"$OUT/test_brtl_client_policy"
clang -std=c11 -Wall -Wextra -Werror -I"$ROOT/Driver" \
  "$ROOT/tests/driver/test_brtl_bus.c" "$ROOT/Driver/brtl_bus.c" "$ROOT/Driver/brtl_policy.c" \
  -o "$OUT/test_brtl_bus"
"$OUT/test_brtl_bus"
