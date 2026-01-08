# DraStic R36S (ArkOS) Port Wrapper

This repository contains the source code for the **DraStic Wrapper and Compatibility Layer** specifically optimized for the R36S handheld running ArkOS (32-bit ARMhf).

**IMPORTANT: This repository DOES NOT contain the proprietary DraStic emulator binaries or BIOS files.** You must acquire these separately and place them in the correct directories to build and run the emulator.

## Project Goal
The goal of this project is to provide a stable, high-performance Nintendo DS emulation experience on the R36S by using a "Split-SDL2" architecture. A native GLES `runner` handles the display, while a custom `libSDL2_hook.so` intercepts events and video from the proprietary DraStic blob.

## How to Acquire DraStic
Since DraStic is proprietary, you must source the following files from a legitimate DraStic distribution (e.g., from the Raspberry Pi build or other officially supported handheld ports):

1.  **Emulator Binaries**: `drastic` (32-bit ARM binary). Place in `nds/drastic/`.
2.  **BIOS Files**: `drastic_bios_arm7.bin` and `drastic_bios_arm9.bin`. Place in `nds/drastic/system/`.
3.  **Resources**: The `res/` folder content (fonts, backgrounds, etc.).

## Build Instructions
1.  **Set up Toolchain**: You need an `arm-linux-gnueabihf-gcc` toolchain.
2.  **Prepare Sysroot**: Copy the `/lib` and `/usr/lib` folders from your R36S device to a `device_sysroot` folder in the root of this project.
3.  **Compile**:
    ```bash
    cd nds
    make -f Makefile.r36s
    ```
4.  **Install**: Follow the deployment instructions in `README_DRASTIC.md`.

## Features
- **Smart ALSA Recovery**: Prevents hangs during sleep/resume.
- **Dynamic Touch Scaling**: Precise touch controls via the right analog stick.
- **Picture-in-Picture (PiP)**: Configurable screen layouts for dual-screen gaming.
- **Optimized Save Redirection**: Saves are directed to `/roms/nds/backup` and `/roms/nds/savestates` for easy management.

## Credits
- **Steward Fu**: Original wrapper architecture.
- **DraStic Team**: For the excellent emulator.
- **R36S Community**: For testing and feedback.

## License
The wrapper source code is licensed under **LGPL-2.1**. DraStic itself remains the property of its respective owners.
