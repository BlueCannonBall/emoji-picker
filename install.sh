#!/bin/sh

make install
mkdir -p /usr/local/share/icons/hicolor/72x72/apps/
mkdir -p /usr/local/share/applications/
cp icon.png /usr/local/share/icons/hicolor/72x72/apps/emoji-picker.png
cp emoji-picker.desktop /usr/local/share/applications/
