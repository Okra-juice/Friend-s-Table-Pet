# Desktop Pet – Build Guide

## 1. Requirements

One of the following compilers is required:

- **Visual Studio 2022 Build Tools** (with the C++ desktop workload; default path `C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools`), or
- **MinGW-w64** (providing `g++`).

## 2. Build

1. Make sure the source files and `build.bat` are in the same folder.
2. Double-click `build.bat`.
3. The script auto-detects the compiler: it prefers VS 2022 Build Tools, and falls back to MinGW g++ if not found.
4. On success it produces `DeskPet.exe` in the current folder.

Source files involved:
`main.cpp`, `chooser.cpp`, `petmenu.cpp`, `chat.cpp`, `appearance.cpp`, `hunger.cpp`, `eatconfirm.cpp` (shared header `common.h`).

## 3. Linked libraries

The build links these system libraries: `gdiplus`, `comdlg32`, `ole32`, `user32`, `gdi32`, `wininet`, `shell32` (MinGW additionally uses `luuid` and `pthread`).

## 4. Troubleshooting

- "Compiler not found": install VS Build Tools or MinGW-w64 and retry.
- Build failure: check the console error output; usually a missing library or an error introduced by source changes.

> The output targets the Windows GUI subsystem, so it runs without a console window.