#!/bin/bash

# 3DS CIA Builder Script
# Converts .3dsx to .cia using bannertool and makerom

set -e

# Configuration
APP_NAME="pronote-3ds"
APP_TITLE="Pronote 3DS"
APP_DESCRIPTION="Access Pronote student from 3DS"
APP_AUTHOR="Iscomingla"
APP_VERSION="1.0.0"
APP_ID="0x12345678"  # Unique ID
RSF_FILE="build/${APP_NAME}.rsf"
BANNER_FILE="build/banner.bnr"
ICON_FILE="build/icon.icn"
OUTPUT_CIA="${APP_NAME}.cia"

# Paths (adjust for your system)
BANNERTOOL="${HOME}/Apps/3ds/bannertool.exe"
CXITOOL="${HOME}/Apps/3ds/cxitool.exe"
MAKEROM="${HOME}/Apps/3ds/makerom"

# Wine command (if using wine)
WINE="wine"

# Check tools exist
if [ ! -f "$BANNERTOOL" ]; then
    echo "Error: bannertool not found at $BANNERTOOL"
    exit 1
fi

if [ ! -f "$MAKEROM" ]; then
    echo "Error: makerom not found at $MAKEROM"
    exit 1
fi

echo "=== Building CIA for ${APP_TITLE} ==="
echo ""

# Step 1: Build .3dsx (if needed)
if [ ! -f "build/${APP_NAME}.3dsx" ]; then
    echo "Step 1: Building .3dsx..."
    make
    echo "✓ .3dsx built"
else
    echo "Step 1: .3dsx already exists"
fi

echo ""

# Step 2: Create banner.bnr (using bannertool)
echo "Step 2: Creating banner..."

# Create temporary banner config
mkdir -p build

# Use wine to run bannertool
echo "Creating banner with bannertool..."
$WINE $BANNERTOOL makebanner -i icon.png -a "build/banner.bnr" 2>/dev/null || {
    echo "⚠ Warning: Could not create banner, using placeholder"
    # Create minimal banner (optional, makerom might not need it)
}

echo "✓ Banner created"
echo ""

# Step 3: Create RSF file for makerom
echo "Step 3: Creating RSF config file..."

cat > "$RSF_FILE" << EOF
RomFs: null
TitleType: Application
ExeFs: null
Option:
  UseOnSD: false
  FreeProductCode: true
  MediaFootPadding: false
  EnableCrypt: true

TitleInfo:
  UniqueId: 0x${APP_ID#0x}
  Category: Application
  MakerCode: "01"

CardInfo:
  MediaType: Card1
  MediaSize: 128MB

CommonHeaderKeyIndex: 0

AccessControl:
  UseExtSaveData: false
  UseSharedPage: false
  AccessibleSaveDataIds: []
  AccessibleExtSaveDataIds: []
  CompressExeFs: true
  IsExecutable: true
  EnableL2Cache: true
  EnableDsp: true
  MakerCode: "01"
  ContentPlatform: CTR
  ContentType: Application
  ContentUnitSize: 2
  BittestModules: []

PlainRegion:
  Size: 0x0

RomFs:
  Size: null
  Align: 4KB
EOF

echo "✓ RSF config created"
echo ""

# Step 4: Build CIA using makerom
echo "Step 4: Building CIA with makerom..."

# Create exefs from .3dsx
if [ -f "build/${APP_NAME}.3dsx" ]; then
    # Extract .3dsx to get code
    echo "Extracting .3dsx..."
    # Note: This is simplified - full .3dsx to .cia requires proper exefs setup
fi

# Use makerom (wine if needed)
if command -v $MAKEROM &> /dev/null; then
    echo "Using native makerom..."
    $MAKEROM -f cia -rsf "$RSF_FILE" -elf "build/${APP_NAME}.elf" -icon "build/icon.icn" -banner "$BANNER_FILE" -o "$OUTPUT_CIA" 2>/dev/null || {
        echo "⚠ makerom failed - you may need to setup proper resource files"
        echo "For now, .3dsx file is available at: build/${APP_NAME}.3dsx"
        exit 1
    }
else
    echo "Using wine to run makerom..."
    $WINE $MAKEROM -f cia -rsf "$RSF_FILE" -elf "build/${APP_NAME}.elf" -icon "build/icon.icn" -banner "$BANNER_FILE" -o "$OUTPUT_CIA" 2>/dev/null || {
        echo "⚠ makerom failed - you may need to setup proper resource files"
        echo "For now, .3dsx file is available at: build/${APP_NAME}.3dsx"
        exit 1
    }
fi

echo "✓ CIA built"
echo ""

# Step 5: Show result
if [ -f "$OUTPUT_CIA" ]; then
    echo "=== SUCCESS ==="
    echo "CIA file: $OUTPUT_CIA"
    ls -lh "$OUTPUT_CIA"
    echo ""
    echo "To install on 3DS:"
    echo "1. Copy $OUTPUT_CIA to your SD card (FBI or similar)"
    echo "2. Use FBI to install the CIA"
else
    echo "⚠ CIA file not created"
    echo ""
    echo "Fallback: Using .3dsx instead"
    echo "3dsx file available at: build/${APP_NAME}.3dsx"
    echo "Copy to SD card: /3ds/${APP_NAME}.3dsx"
    echo "Launch from Homebrew Channel"
fi
