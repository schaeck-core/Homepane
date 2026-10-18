#!/usr/bin/env bash
# ──────────────────────────────────────────────────────────────
# setup-vcpkg.sh — Install vcpkg and project dependencies
# ──────────────────────────────────────────────────────────────
#
# Usage:
#   ./scripts/setup-vcpkg.sh              # Install to default location
#   VCPKG_ROOT=/path/to/vcpkg ./scripts/setup-vcpkg.sh
#
# Environment Variables:
#   VCPKG_ROOT — Path to vcpkg installation (default: ~/vcpkg)
# ──────────────────────────────────────────────────────────────

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
VCPKG_ROOT="${VCPKG_ROOT:-$HOME/vcpkg}"

echo "════════════════════════════════════════════════════════"
echo "  Homepane vcpkg Setup"
echo "════════════════════════════════════════════════════════"
echo ""

# ── Check for build tools on macOS ───────────────────────

if [[ "$(uname -s)" == "Darwin" ]]; then
  echo "Checking macOS build tools..."
  MISSING_TOOLS=()

  for tool in autoconf automake libtool autoconf-archive; do
    if brew list "$tool" &>/dev/null; then
      echo "  ✓ $tool"
    else
      echo "  ✗ $tool not found"
      MISSING_TOOLS+=("$tool")
    fi
  done

  if [[ ${#MISSING_TOOLS[@]} -gt 0 ]]; then
    echo ""
    echo "Installing missing build tools via Homebrew..."
    brew install "${MISSING_TOOLS[@]}"
  fi
  echo ""
fi

# ── Check for vcpkg ──────────────────────────────────────────

if [[ ! -f "$VCPKG_ROOT/vcpkg" ]]; then
  echo "❌ vcpkg not found at: $VCPKG_ROOT"
  echo ""
  echo "Installing vcpkg..."
  echo ""

  # Clone vcpkg if directory doesn't exist
  if [[ ! -d "$VCPKG_ROOT" ]]; then
    git clone https://github.com/microsoft/vcpkg.git "$VCPKG_ROOT"
  fi

  # Bootstrap vcpkg
  cd "$VCPKG_ROOT"
  ./bootstrap-vcpkg.sh
  echo ""
fi

echo "✓ vcpkg found at: $VCPKG_ROOT"
echo ""

# ── Install dependencies ─────────────────────────────────────

echo "Installing dependencies from vcpkg.json..."
echo ""

cd "$PROJECT_ROOT"

# Detect vcpkg triplet based on platform
ARCH="$(uname -m)"
OS="$(uname -s)"

case "$OS" in
  Darwin)
    # macOS uses 'osx' in vcpkg triplet naming
    if [ "$ARCH" = "arm64" ]; then
      TRIPLET="arm64-osx"
    else
      TRIPLET="x64-osx"
    fi
    ;;
  Linux)
    if [ "$ARCH" = "aarch64" ] || [ "$ARCH" = "arm64" ]; then
      TRIPLET="arm64-linux"
    else
      TRIPLET="x64-linux"
    fi
    ;;
  *)
    echo "Unsupported OS: $OS" >&2
    exit 1
    ;;
esac

echo "Using vcpkg triplet: $TRIPLET"
echo ""

# Use vcpkg in manifest mode (reads vcpkg.json)
"$VCPKG_ROOT/vcpkg" install --triplet "$TRIPLET"

echo ""
echo "════════════════════════════════════════════════════════"
echo "  ✓ vcpkg setup complete!"
echo "════════════════════════════════════════════════════════"
echo ""
echo "Next steps:"
echo "  1. Configure CMake with vcpkg toolchain:"
echo "     cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
echo ""
echo "  2. Build the project:"
echo "     cmake --build build --parallel 8"
echo ""
