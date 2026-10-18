# Homepane Setup Script for Windows
# Run this after cloning to set up your development environment
# Execute: .\setup.ps1

$ErrorActionPreference = "Stop"

Write-Host "=== Homepane Setup (Windows) ===" -ForegroundColor Cyan
Write-Host ""

# Check prerequisites
Write-Host "Checking prerequisites..." -ForegroundColor Yellow

function Test-Command {
    param([string]$Command)
    $exists = $null -ne (Get-Command $Command -ErrorAction SilentlyContinue)
    if ($exists) {
        Write-Host "  [OK] $Command found" -ForegroundColor Green
    } else {
        Write-Host "  [X] $Command not found" -ForegroundColor Red
    }
    return $exists
}

$missing = @()

if (-not (Test-Command "cmake")) { $missing += "cmake" }
if (-not (Test-Command "git")) { $missing += "git" }
if (-not (Test-Command "node")) { $missing += "node" }
if (-not (Test-Command "python")) { $missing += "python" }

# Check for lv_font_conv
$fontConvExists = $null -ne (Get-Command "lv_font_conv" -ErrorAction SilentlyContinue)
if ($fontConvExists) {
    Write-Host "  [OK] lv_font_conv found" -ForegroundColor Green
} else {
    Write-Host "  [X] lv_font_conv not found" -ForegroundColor Red
    $missing += "lv_font_conv"
}

Write-Host ""

if ($missing.Count -gt 0) {
    Write-Host "Missing dependencies: $($missing -join ', ')" -ForegroundColor Red
    Write-Host ""
    Write-Host "Install with:" -ForegroundColor Yellow
    Write-Host "  Option 1 - Using vcpkg (recommended):"
    Write-Host "    git clone https://github.com/microsoft/vcpkg.git C:\vcpkg"
    Write-Host "    C:\vcpkg\bootstrap-vcpkg.bat"
    Write-Host "    # Dependencies will be auto-installed via vcpkg.json"
    Write-Host ""
    Write-Host "  Option 2 - Using winget:"
    Write-Host "    winget install Kitware.CMake Git.Git OpenJS.NodeJS Python.Python.3"
    Write-Host "    npm install -g lv_font_conv"
    Write-Host ""
    Write-Host "  Option 3 - Using MSYS2:"
    Write-Host "    pacman -S mingw-w64-x86_64-cmake mingw-w64-x86_64-gcc mingw-w64-x86_64-SDL2"
    Write-Host "    pacman -S mingw-w64-x86_64-curl mingw-w64-x86_64-nodejs"
    Write-Host "    npm install -g lv_font_conv"
    Write-Host ""

    $continue = Read-Host "Continue anyway? [y/N]"
    if ($continue -ne "y" -and $continue -ne "Y") {
        exit 1
    }
}

# Setup vcpkg (Windows)
Write-Host ""
Write-Host "Setting up vcpkg..." -ForegroundColor Yellow
$VcpkgRoot = if ($env:VCPKG_ROOT) { $env:VCPKG_ROOT } else { "C:\vcpkg" }

if (-not (Test-Path "$VcpkgRoot\vcpkg.exe")) {
    Write-Host "Installing vcpkg to $VcpkgRoot..." -ForegroundColor Yellow
    if (-not (Test-Path $VcpkgRoot)) {
        git clone https://github.com/microsoft/vcpkg.git $VcpkgRoot
    }
    & "$VcpkgRoot\bootstrap-vcpkg.bat"
}

Write-Host "Installing dependencies from vcpkg.json..." -ForegroundColor Yellow
$triplet = "x64-windows"
& "$VcpkgRoot\vcpkg.exe" install --triplet $triplet

# Initialize submodules
Write-Host ""
Write-Host "Initializing Git submodules..." -ForegroundColor Yellow
git submodule update --init --recursive

# Generate glyph codepoints
Write-Host ""
Write-Host "Extracting glyph codepoints..." -ForegroundColor Yellow
python scripts/extract_glyphs.py

# Create config.ini if not exists
if (-not (Test-Path "config.ini")) {
    Write-Host ""
    Write-Host "Creating config.ini from example..." -ForegroundColor Yellow
    Copy-Item "config.ini.example" "config.ini"
    Write-Host "  -> Edit config.ini with your Home Assistant settings" -ForegroundColor Magenta
}

# Configure CMake with vcpkg toolchain
Write-Host ""
Write-Host "Configuring CMake..." -ForegroundColor Yellow
$VcpkgToolchain = "$VcpkgRoot\scripts\buildsystems\vcpkg.cmake"
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE="$VcpkgToolchain"

# Generate fonts
Write-Host ""
Write-Host "Generating fonts..." -ForegroundColor Yellow
cmake --build build --target generate_fonts

# Build
Write-Host ""
Write-Host "Building..." -ForegroundColor Yellow
cmake --build build --parallel

Write-Host ""
Write-Host "=== Setup Complete ===" -ForegroundColor Green
Write-Host ""
Write-Host "Next steps:" -ForegroundColor Cyan
Write-Host "  1. Edit config.ini with your Home Assistant URL and token"
Write-Host "  2. Run .\bin\main.exe to start the simulator"
Write-Host ""
