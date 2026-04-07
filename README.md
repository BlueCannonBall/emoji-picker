# emoji-picker

A high-performance, searchable emoji picker for Linux built with **FLTK 1.4** and **libxdo**.

<img width="605" height="766" alt="image" src="https://github.com/user-attachments/assets/095f7ca3-f0e8-4573-9bfb-179aea02696e" />

## Features
- **🚀 Instant Startup**: All ~1,850 Twemoji assets are embedded directly into the binary as byte arrays, resulting in zero filesystem I/O during rendering.
- **🔍 Fast Search**: Instant filtering as you type.
- **🎨 Native Theming**: Respects system dark mode via `GSettings` (detecting `color-scheme` preferences).
- **📋 Direct Paste**: Automatically pastes the selected emoji into your previously active window using `libxdo`.
- **📐 Responsive Design**: Dynamic layout using `Fl_Flex` that gracefully handles resizing.

## Prerequisites
Ensure you have the following system dependencies installed (Debian/Ubuntu example):
```bash
sudo apt install build-essential cmake python3 libx11-dev libxext-dev libxft-dev \
                 libxinerama-dev libxcursor-dev libxrender-dev libxfixes-dev \
                 libpng-dev libjpeg-dev libglib2.0-dev libxdo-dev
```

## Build & Installation

### 1. Fetch Assets
First, download the latest Twemoji assets and generate the C++ header:
```bash
python3 fetch_assets.py
```

### 2. Build
```bash
make
```

### 3. Install System-Wide
Use the provided `install.sh` to build and install the binary, launcher, and icon to `/usr/local/`:
```bash
sudo ./install.sh
```

## Technical Details
- **GUI Framework**: FLTK 1.4
- **Asset Pipeline**: Python script scrapes `emojilib` and converts PNGs into static C-style arrays.
- **Input Injection**: Direct `libxdo` C API integration for low-latency text injection.
