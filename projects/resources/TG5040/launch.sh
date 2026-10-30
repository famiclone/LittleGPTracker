#!/bin/sh
# TrimUI Brick / Smart Pro (NextUI / MinUI Tools pak entry).
PROGDIR=$(dirname "$0")
cd "$PROGDIR" || exit 1

# Prefer minui-power-control when available (handles sleep/shutdown for
# apps that do not watch KEY_POWER). Native LGPT sleep then backs off via
# LGPT_NO_NATIVE_SLEEP so both do not fight over the same button.
if command -v minui-power-control >/dev/null 2>&1; then
	minui-power-control lgpt-tg5040.elf &
	export LGPT_NO_NATIVE_SLEEP=1
elif [ -x "$PROGDIR/bin/minui-power-control" ]; then
	"$PROGDIR/bin/minui-power-control" lgpt-tg5040.elf &
	export LGPT_NO_NATIVE_SLEEP=1
fi

# NextUI usually puts .system/*/bin on PATH already; keep local libs first.
export LD_LIBRARY_PATH="$PROGDIR:${LD_LIBRARY_PATH:-}"

./lgpt-tg5040.elf "$@" >>./lgpt.log 2>&1
