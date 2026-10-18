#!/usr/bin/env bash
set -euo pipefail

IDF_VERSION="5.5.1"
# Configuration
IDF_DIR="$HOME/esp/v$IDF_VERSION/esp-idf"
# Optional override: path to Homebrew or system Python 3.11 bin (e.g., /opt/homebrew/opt/python@3.11/bin)
PY311_BIN="${PY311_BIN:-}"
WORKSPACE_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROJECT_DIR="$WORKSPACE_ROOT/platforms/esp32p4"
# The serial port: -p PORT, else $ESPPORT, else found by esptool / idf.py
# (names differ per OS and per USB port: /dev/cu.usbmodem*, /dev/ttyACM0,
# /dev/ttyUSB0, COM5, ...).

usage() {
  echo "Usage: $0 [build|flash|monitor|fullclean] [-p PORT]" >&2
  exit 1
}

ACTION="${1:-build}"
PORT="${ESPPORT:-}"
shift || true
while [[ $# -gt 0 ]]; do
  case "$1" in
    -p|--port)
      PORT="$2"; shift 2 ;;
    *) usage ;;
  esac
done

ensure_python311() {
  # 1) Try pyenv first
  if command -v pyenv >/dev/null 2>&1; then
    # Initialize pyenv in this shell
    eval "$(pyenv init -)"
    # Prefer a 3.11.x installed via pyenv; if none, suggest installation
    if ! pyenv versions --bare | grep -q '^3\.11\.'; then
      echo "Error: pyenv has no Python 3.11.x installed." >&2
      echo "Install one: pyenv install 3.11.9 (or similar)" >&2
      exit 2
    fi
    # Activate any available 3.11.x (prefer local/global if set)
    PYENV_311="$(pyenv versions --bare | grep '^3\.11\.' | head -n1)"
    pyenv shell "$PYENV_311"
    if python3 --version | grep -qE '^Python 3\.11\.'; then
      echo "Using pyenv Python: $(python3 --version)"
      return 0
    fi
  fi

  # 2) Fall back to Homebrew/system override if provided
  if [[ -n "$PY311_BIN" ]]; then
    export PATH="$PY311_BIN:$PATH"
  fi

  # 3) Validate python3 version
  if ! python3 --version | grep -qE '^Python 3\.11\.'; then
    echo "Error: python3 must be 3.11.x; found $(python3 --version)" >&2
    echo "Fix options: (a) use pyenv 3.11.x, (b) set PY311_BIN to your Python 3.11 bin, (c) brew install python@3.11." >&2
    exit 2
  fi
}

ensure_python311

# Create/verify ESP-IDF venv for 5.4
if [[ ! -d "$HOME/.espressif/python_env/idf5.5_py3.11_env" ]]; then
  echo "Creating ESP-IDF 5.5 Python environment..."
  if command -v pyenv >/dev/null 2>&1; then
    # Use pyenv-managed interpreter to create the venv
    pyenv exec python3 "$IDF_DIR/tools/idf_tools.py" install-python-env
  else
    python3 "$IDF_DIR/tools/idf_tools.py" install-python-env
  fi
fi

# Export ESP-IDF 5.4.2
export IDF_PYTHON_ENV_PATH="$HOME/.espressif/python_env/idf5.5_py3.11_env"
export IDF_TOOLS_PYTHON="$(command -v python3)"
# shellcheck disable=SC1090
source "$IDF_DIR/export.sh"
source "$HOME/esp/v$IDF_VERSION/esp-idf/export.sh"

cd "$PROJECT_DIR"

case "$ACTION" in
  fullclean)
    idf.py fullclean ;;
  build)
    # Incremental build with caching: avoid set-target/reconfigure unless necessary.
    export IDF_CCACHE_ENABLE=1
    # Set target only if sdkconfig is missing or target is different
    NEED_SET_TARGET=0
    if [[ ! -f sdkconfig ]]; then
      NEED_SET_TARGET=1
    else
      # Check current target in sdkconfig
      if ! grep -q '^CONFIG_IDF_TARGET="esp32p4"' sdkconfig; then
        NEED_SET_TARGET=1
      fi
    fi
    if [[ "$NEED_SET_TARGET" -eq 1 ]]; then
      if command -v pyenv >/dev/null 2>&1; then
        pyenv exec idf.py set-target esp32p4
      else
        idf.py set-target esp32p4
      fi
    fi
    # Build incrementally (Ninja handles caching). No fullclean, no reconfigure.
    if command -v pyenv >/dev/null 2>&1; then
      pyenv exec idf.py build
    else
      idf.py build
    fi ;;
  flash)
    # Flash only; do not reconfigure or build. Use esptool with generated flash_args.
    BUILD_DIR="$PROJECT_DIR/build"
    FLASH_ARGS_FILE="$BUILD_DIR/flash_args"
    if [[ ! -f "$FLASH_ARGS_FILE" ]]; then
      echo "Error: '$FLASH_ARGS_FILE' not found. Run '$0 build' first." >&2
      exit 2
    fi
    # Run esptool from within the build directory so relative paths in flash_args resolve.
    pushd "$BUILD_DIR" >/dev/null
    # Prefer the ESP-IDF managed virtualenv for esptool
    ESPPY="$IDF_PYTHON_ENV_PATH/bin/python"
    PORT_ARGS=()
    [[ -n "$PORT" ]] && PORT_ARGS=(--port "$PORT")
    if [[ -x "$ESPPY" ]]; then
      "$ESPPY" -m esptool --chip esp32p4 "${PORT_ARGS[@]}" -b 460800 --before default_reset --after hard_reset write_flash "@flash_args"
    else
      python3 -m esptool --chip esp32p4 "${PORT_ARGS[@]}" -b 460800 --before default_reset --after hard_reset write_flash "@flash_args"
    fi ;;
  monitor)
    # Do not run set-target/fullclean when only monitoring
    PORT_ARGS=()
    [[ -n "$PORT" ]] && PORT_ARGS=(-p "$PORT")
    if command -v pyenv >/dev/null 2>&1; then
      pyenv exec idf.py "${PORT_ARGS[@]}" monitor
    else
      idf.py "${PORT_ARGS[@]}" monitor
    fi ;;
  *) usage ;;
esac
