# Desktop Pet

A lightweight Windows desktop pet written in C++ with GDI+. It walks around your screen, can chat with you via an AI backend (e.g. DeepSeek), and has ste hunger and mood systems.

## Usage

Run `DeskPet.exe` to start. The pet walks around the screen with soft body animations.

On the first run an image picker appears — choose a head image or just click "Start".

### Right-click menu

Right-click the pet to open the menu:

| Menu item | What it does |
| --- | --- |
| Chat | Open the chat input bar and talk to the pet |
| Appearance | Adjust overall size / body size / thickness, change head image |
| Info | View the pet's current hunger and mood values |
| Exit | Quit the program |

### Chat

- Choose "Chat", type and press Enter or click Send.
- Use the "Settings" button to configure the AI API URL, key, model, persona (prompt) and memory size.
- Clicking the blank area of the input bar, outside it, or on Settings closes the bar without sending.
- Replies show in a bubble above the pet's head; its expression follows its mood.

### Feeding

- Drag a file onto the pet; it asks "Can I eat this file?". On confirm, the file goes to the Recycle Bin and the pet gains hunger.
- Feeding also makes the pet happier (raises its mood).

### Hunger & mood

- Hunger slowly decreases over time. When low the pet looks unhappy and asks for food; at very low hunger it lies down and stops walking.
- Mood rises through feeding and chatting, and the pet's expression follows it.

> Chat needs an internet connection and a valid API key in "Settings".

## Build

Requirements: **Visual Studio 2022 Build Tools** (C++ desktop workload) or **MinGW-w64** (g++).

Put `build.bat` in the same folder as the sources and double-click it. It auto-detects the compiler (VS first, then MinGW) and outputs `DeskPet.exe`.

Sources: `main.cpp`, `chooser.cpp`, `petmenu.cpp`, `chat.cpp`, `appearance.cpp`, `hunger.cpp`, `eatconfirm.cpp` (shared header `common.h`).

Links: `gdiplus`, `comdlg32`, `ole32`, `user32`, `gdi32`, `wininet`, `shell32` (MinGW adds `luuid`, `pthread`).

See [UserGuide_EN.md](UserGuide_EN.md) and [BuildGuide_EN.md](BuildGuide_EN.md) for details.
