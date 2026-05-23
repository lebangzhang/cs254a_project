#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
BUILD_DIR="$ROOT_DIR/build"
APP="wmma_vv"
DRIVER="simx"
THREADS=8
VLEN=256
DO_CLEAN=1
EXTRA_CONFIGS=""
BLACKBOX_ARGS=()

usage() {
  cat <<'EOF'
Usage: ./run.sh [options]

Rebuild and run tests/regression/wmma_vv with matching app/device thread settings.

Options:
  -t, --threads N       Set NUM_THREADS and blackbox --threads (default: 8)
  -d, --driver NAME     Select driver: simx or rtlsim (default: simx)
      --vlen N          Set VLEN macro for app/driver builds (default: 256)
      --configs STR     Extra CONFIGS appended to both app and driver builds
      --debug N         Forward --debug=N to blackbox
      --perf N          Forward --perf=N to blackbox
      --log FILE        Forward --log=FILE to blackbox
      --args STR        Forward --args=STR to blackbox
      --no-clean        Skip 'make clean' before rebuilding the app
  -h, --help            Show this help text
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    -t|--threads)
      THREADS="$2"
      shift 2
      ;;
    --threads=*)
      THREADS="${1#*=}"
      shift
      ;;
    -d|--driver)
      DRIVER="$2"
      shift 2
      ;;
    --driver=*)
      DRIVER="${1#*=}"
      shift
      ;;
    --vlen)
      VLEN="$2"
      shift 2
      ;;
    --vlen=*)
      VLEN="${1#*=}"
      shift
      ;;
    --configs)
      EXTRA_CONFIGS="$2"
      shift 2
      ;;
    --configs=*)
      EXTRA_CONFIGS="${1#*=}"
      shift
      ;;
    --debug|--perf|--log|--args)
      opt="${1#--}"
      BLACKBOX_ARGS+=("--${opt}=$2")
      shift 2
      ;;
    --debug=*|--perf=*|--log=*|--args=*)
      BLACKBOX_ARGS+=("$1")
      shift
      ;;
    --no-clean)
      DO_CLEAN=0
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown option: $1" >&2
      usage >&2
      exit 1
      ;;
  esac
done

if [[ ! -d "$BUILD_DIR/tests/regression/$APP" ]]; then
  echo "Build directory not found: $BUILD_DIR/tests/regression/$APP" >&2
  echo "Run configure/build first so the build tree exists." >&2
  exit 1
fi

if [[ "$DRIVER" != "simx" && "$DRIVER" != "rtlsim" ]]; then
  echo "Unsupported driver: $DRIVER" >&2
  echo "Expected one of: simx, rtlsim" >&2
  exit 1
fi

APP_CONFIGS="-DEXT_V_ENABLE -DEXT_TCU_ENABLE -DNUM_THREADS=$THREADS -DVLEN=$VLEN"
DRIVER_CONFIGS="-DEXT_V_ENABLE -DEXT_TCU_ENABLE -DVLEN=$VLEN"

if [[ -n "$EXTRA_CONFIGS" ]]; then
  APP_CONFIGS+=" $EXTRA_CONFIGS"
  DRIVER_CONFIGS+=" $EXTRA_CONFIGS"
fi

echo "App build CONFIGS: $APP_CONFIGS"
if [[ $DO_CLEAN -eq 1 ]]; then
  make -C "$BUILD_DIR/tests/regression/$APP" clean
fi
make -C "$BUILD_DIR/tests/regression/$APP" CONFIGS="$APP_CONFIGS"

echo "Blackbox CONFIGS: $DRIVER_CONFIGS"
(
  cd "$BUILD_DIR"
  CONFIGS="$DRIVER_CONFIGS" ./ci/blackbox.sh \
    --driver="$DRIVER" \
    --app="$APP" \
    --threads="$THREADS" \
    "${BLACKBOX_ARGS[@]}"
)
