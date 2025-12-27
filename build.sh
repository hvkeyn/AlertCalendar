#!/usr/bin/env bash
set -euo pipefail

BUILD_DIR="${BUILD_DIR:-build}"
CONFIG="${CONFIG:-Release}"
GENERATOR="${GENERATOR:-Ninja}"

usage() {
  cat <<EOF
Использование:
  ./build.sh [--build-dir DIR] [--config Release|Debug] [--generator Ninja|... ] [--clean] [--run]

Переменные окружения (опционально):
  BUILD_DIR, CONFIG, GENERATOR
EOF
}

CLEAN=0
RUN=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --build-dir) BUILD_DIR="$2"; shift 2 ;;
    --config) CONFIG="$2"; shift 2 ;;
    --generator) GENERATOR="$2"; shift 2 ;;
    --clean) CLEAN=1; shift ;;
    --run) RUN=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Неизвестный аргумент: $1"; usage; exit 2 ;;
  esac
done

if [[ "$CLEAN" == "1" ]]; then
  rm -rf "$BUILD_DIR"
fi

mkdir -p "$BUILD_DIR"

CMAKE_ARGS=(-S . -B "$BUILD_DIR" -G "$GENERATOR" "-DCMAKE_BUILD_TYPE=$CONFIG")

echo "==> Конфигурация: cmake ${CMAKE_ARGS[*]}"
cmake "${CMAKE_ARGS[@]}"

echo "==> Сборка: cmake --build $BUILD_DIR"
cmake --build "$BUILD_DIR"

if [[ "$RUN" == "1" ]]; then
  EXE="$BUILD_DIR/AlertCalendar"
  if [[ -f "$EXE" ]]; then
    echo "==> Запуск: $EXE"
    "$EXE"
  else
    echo "Не найден исполняемый файл ($EXE). Возможно, генератор multi-config или другое имя цели." >&2
    exit 3
  fi
fi


