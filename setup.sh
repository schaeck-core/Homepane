#!/bin/bash
# Homepane Setup Script
# Run this after cloning to set up your development environment

set -e

echo "=== Homepane Setup ==="
echo ""

# Detect OS
OS="$(uname -s)"
case "${OS}" in
    Linux*)     PLATFORM=Linux;;
    Darwin*)    PLATFORM=Mac;;
    *)          PLATFORM="UNKNOWN:${OS}"
esac

echo "Detected platform: $PLATFORM"
echo ""

# Check prerequisites
echo "Checking prerequisites..."

check_command() {
    if command -v "$1" &> /dev/null; then
        echo "  ✓ $1 found"
        return 0
    else
        echo "  ✗ $1 not found"
        return 1
    fi
}

MISSING=()

check_command cmake || MISSING+=("cmake")
check_command git || MISSING+=("git")
check_command node || MISSING+=("node")
check_command python3 || MISSING+=("python3")

# Check for SDL2
if [ "$PLATFORM" == "Mac" ]; then
    if brew list sdl2 &>/dev/null; then
        echo "  ✓ SDL2 found"
    else
        echo "  ✗ SDL2 not found"
        MISSING+=("sdl2")
    fi
elif [ "$PLATFORM" == "Linux" ]; then
    if pkg-config --exists sdl2 2>/dev/null; then
        echo "  ✓ SDL2 found"
    else
        echo "  ✗ SDL2 not found"
        MISSING+=("libsdl2-dev")
    fi
fi

echo ""

# Check for lv_font_conv
if command -v lv_font_conv &> /dev/null; then
    echo "  ✓ lv_font_conv found"
else
    echo "  ✗ lv_font_conv not found"
    MISSING+=("lv_font_conv")
fi

echo ""

if [ ${#MISSING[@]} -gt 0 ]; then
    echo "Missing dependencies: ${MISSING[*]}"
    echo ""
    echo "Install with:"
    if [ "$PLATFORM" == "Mac" ]; then
        echo "  brew install cmake sdl2 curl libmicrohttpd node"
        echo "  npm install -g lv_font_conv"
    elif [ "$PLATFORM" == "Linux" ]; then
        echo "  sudo apt install cmake build-essential libsdl2-dev libcurl4-openssl-dev libmicrohttpd-dev nodejs npm"
        echo "  npm install -g lv_font_conv"
    fi
    echo ""
    read -p "Continue anyway? [y/N] " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        exit 1
    fi
fi

# Initialize submodules
echo ""
echo "Initializing Git submodules..."
git submodule update --init --recursive

# Setup vcpkg and install dependencies
echo ""
echo "Setting up vcpkg and dependencies..."
./scripts/setup-vcpkg.sh

# Generate glyph codepoints
echo ""
echo "Extracting glyph codepoints..."
python3 scripts/extract_glyphs.py

# Create config.ini if not exists
if [ ! -f config.ini ]; then
    echo ""
    echo "Creating config.ini from example..."
    cp config.ini.example config.ini
    echo "  → Edit config.ini with your Home Assistant settings"
fi

# Get vcpkg root (default ~/vcpkg)
VCPKG_ROOT="${VCPKG_ROOT:-$HOME/vcpkg}"

# Configure CMake with vcpkg toolchain
echo ""
echo "Configuring CMake..."
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"

# Generate fonts
echo ""
echo "Generating fonts..."
cmake --build build --target generate_fonts

# Build
echo ""
echo "Building..."
cmake --build build --parallel 8

echo ""
echo "=== Setup Complete ==="
echo ""
echo "Next steps:"
echo "  1. Edit config.ini with your Home Assistant URL and token"
echo "  2. Run ./bin/main to start the simulator"
echo ""
