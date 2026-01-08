# DraStic R36S (ArkOS) Port

This repository contains a specialized wrapper and compatibility layer for running DraStic (Nintendo DS Emulator) on the R36S handheld running ArkOS.

## Key Features
- **Working Audio**: Fixed hardware 'OFF' path and implemented ADPCM decoding for music/UI.
- **Split-SDL2 Architecture**: The `runner` (GLES server) runs natively while the `drastic` client uses a custom `libSDL2_hook.so` to intercept video and input.
- **R36S Control Mapping**: 
  - Full button support (ABXY, D-pad, Shoulders).
  - **L2**: Toggles PiP Size between Small (173x130) and Quarter-Screen (320x240).
  - **L3**: Chording Modifier (Future expansion).
  - **Right Stick Touch**: The right analog stick is mapped to the DS touch cursor with **Dynamic Velocity Scaling**.
  - **R3 Click**: Mapped to touch screen press.
  - **Fn Button (708)**: Opens the DraStic menu.
- **Enhanced PiP (Picture-in-Picture)**:
  - Repositioned to the **bottom-right** by default for better visibility.
  - Variable sizes available via L2 toggle.
- **Optimized Performance**: 
  - Input loop drains the device queue to eliminate lag.
  - Logging disabled by default for maximum FPS.
- **Stability**: Resolved state-load freezes and menu-to-game transition deadlocks. The **Power Button quit shortcut** has been disabled to prevent accidental exits; use **L3 + L1** or the menu to quit.
- **Robust Save Persistence**: 
  - Saves and states managed in `/opt/drastic/backup` and `/opt/drastic/savestates/`.
  - **Background Auto-saves**: Redirected to **Slot 10** (`_10.dss`) and throttled to 10-minute intervals to prevent I/O micro-lags.
  - **Smart Resume**: Loading Slot 0 will automatically prefer the Slot 10 auto-save if present, preserving manual user saves in slots 0-9.
  - Automatic directory creation and file validation.

## Technical Details: Video Pipeline
The communication between the `drastic` client and the `runner` server happens via Shared Memory (SHM):

- **flush_lcd**: Intercepts DraStic's frame buffer or custom menu surfaces. It copies the pixel data into the SHM buffer and triggers `SHM_CMD_FLUSH`. This function includes a safety timeout to prevent the emulator from hanging if the SHM `valid` bit isn't cleared by the runner (e.g., during a system sleep/wake cycle).
- **flip_lcd**: Triggers the `SHM_CMD_FLIP` command, signaling the runner to present the accumulated textures to the screen. It also implements a safety timeout and busy-wait yield to ensure system stability.

## Controls (R36S)
| Button | Action |
| --- | --- |
| **Fn** | Open DraStic Menu |
| **L3 + L1** | Quit DraStic (Power button quit is disabled) |
| **L2** | Toggle PiP Size (Small 173x130 / Large 320x240) |
| **L3 + R1** | Fast Forward |
| **L3 + R2** | Load State |
| **R3** | Save State |
| **L3 + START** | Wrapper Settings Menu (SDL2) |
| **L3 + Y** | Toggle Layout Background |
| **Left Stick** | Cursor / D-Pad |
| **Right Stick** | Touch Screen Controls |
| **L3 + B** | Toggle Screen Filter (Pixel/Blur) |
| **L3 + A** | Swap Layout Alternate Mode |
| **L3 + UP** | Toggle Microphone |
| **L3 + DOWN** | Toggle Hinge |

## How to Replicate the Build

### 1. Prerequisites
- A Linux host (Ubuntu/Debian recommended).
- **Toolchain**: `arm-linux-gnueabihf-gcc` (Version 8.2.1 recommended to match ArkOS GLIBC 2.30).
- **Sysroot**: You must extract the 32-bit libraries from an R36S/ArkOS device.
  ```bash
  # Example sync from device
  rsync -avz ark@<device_ip>:/lib/ device_sysroot/lib/
  rsync -avz ark@<device_ip>:/usr/lib/ device_sysroot/usr/lib/
  ```

### 2. Build Instructions
1. Navigate to the `nds` directory:
   ```bash
   cd nds
   ```
2. Run the build using the R36S-specific Makefile:
   ```bash
   make -f Makefile.r36s
   ```
   This will compile `libcommon.so`, `libdtr.so`, `libasound.so.2`, `libSDL2_hook.so`, and the `runner` binary.

### 3. Deployment
The `deploy_sftp.sh` script automates the installation process:
1. It packages the build into a zip file.
2. It uploads the zip to the handheld via SFTP.
3. It preserves your existing `/opt/drastic/config` folder.
4. It installs the launch script to `/usr/local/bin/drastic.sh`.

Run it from the project root:
```bash
./deploy_sftp.sh
```

### 4. File Structure
- `/opt/drastic/`: Main emulator directory.
- `/opt/drastic/lib/`: Custom wrapper libraries.
- `/opt/drastic/config/`: Emulator settings (`drastic.cfg`).
- `/opt/drastic/res/nds.cfg`: Wrapper-specific configuration.

## Performance Tuning
To re-enable logging for debugging, edit `deploy_sftp.sh` or `/usr/local/bin/drastic.sh` on the device and set:
```bash
export NDS_DEBUG_LEVEL=TRACE
```
## Troubleshooting & Fixes

### PiP Transparency & Audio Freezes
If you previously experienced opaque Picture-in-Picture (PiP) overlays or system freezes after long sleep cycles, these have been resolved by two key stability changes:

1.  **CPU Core Toggling Optimization**:
    - **Problem**: The emulator attempts to force CPU cores online to maintain performance. On some devices or thermal states, writing to `/sys/devices/system/cpu/.../online` fails (Permission Denied) or is overridden by the kernel.
    - **Impact**: The previous implementation used blocking `system()` calls in a tight loop. When these failed repeatedly, they introduced massive latency (10-50ms per frame), starving the rendering thread. This timing violation prevented the compositor from correctly applying alpha blending to the PiP window, rendering it opaque.
    - **Fix**: A "Smart Retry" mechanism has been added. If a core toggle fails, the system backs off for **60 seconds** before trying again. This eliminates the latency/stutter, allowing the PiP transparency to render correctly, while still allowing the system to recover full multicore performance if thermal conditions improve.

2.  **Audio Recovery**:
    - **Problem**: Resuming from suspend often caused a 0.1s volume burst or a complete freeze due to ALSA driver underruns and infinite wait loops.
    - **Fix**: Internal audio delays are now capped at **1 second** to prevent infinite waits, and a **1-second recovery delay** is enforced after suspend/resume to allow the system mixer to restore correct volume levels before playback resumes.
