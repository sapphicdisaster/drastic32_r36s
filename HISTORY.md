# Drastic R36S Port Progress Task List

## 1. Completed Tasks
- [x] Fix emulator freeze when pressing B at root menu level.
    - Root Cause: Deadlock between `video_handler` and `prehook_update_screen`.
    - Fix: Ensured menu is disabled when game frame updates are requested.
- [x] Implement theme items from `customtheme/`.
    - Assets deployed to `nds/assets/r36s/res/menu/0/`.
- [x] Disable "power button to quit" shortcut.
    - Hooked internal `quit` function to prevent accidental exits.
- [x] Resolve Audio loss and Menu freeze after sleep/wake cycle.
    - Fix (Audio): Robust ALSA recovery using `snd_pcm_recover`.
    - Fix (Freeze): Added safety timeouts to SHM busy-wait loops in `flush_lcd` and `flip_lcd`.
- [x] Fix "double speed and no move" bugs after resume.
    - Fix (Speed): Changed Fast Forward (L3+R1) to a synchronized toggle.
    - Fix (Input): Implemented `release_key()` sanitization when DraStic menu opens to clear stuck modifiers.
- [x] Resolve periodic "micro-lags" during emulation.
    - Root Cause: Background auto-savestates (6MB writes) saturating I/O.
    - Fix: Implemented manual save blocking in `prehook_save_state_index`.
- [x] Update chording to use L3 + Right Stick directions.
    - Hotkeys moved from D-Pad/Left Stick to Right Stick directions while holding L3 to avoid accidental triggers during movement.
- [x] Disable "close hinge" hotkey.
- [x] Map standalone R3 click to touch screen click.
- [x] Map L3 + R3 to manual Quick Save.
- [x] Resolved Quick Save/Load failures by fixing path formatting bug in `hook.c`.
- [x] Modularized auto-save blocking via `manual_save_triggered` flag in `nds_hook`.
- [x] Direct SD card save paths (/roms/nds/backup and /roms/nds/savestates).
- [x] Manual save OSD confirmation message.
- [x] Robust hotkey masking using bitwise-AND.
- [x] Hotkey debouncing/locking to prevent repetitive triggers (infinite load loops).
- [x] Native input suppression when L3 is held to prevent "leaking" into DraStic.
- [x] Performance: Resolved FPS drop by silencing high-frequency memory logging.
- [x] Stability: Restored core button controls by fixing input loop draining and event ordering.
- [x] Restore R36S Fn/Menu button functionality (keycode 708).
- [x] Fix Slot 0 savestate overwrite (Manual saves from menu now permitted via `menu_active` flag).
- [x] Optimize deployment script (Preserve config only, fixed nested folder restoration logic).
- [x] Performance testing: Switched to ERROR log level in launch script.

## 2. Release 0.95b Summary
- Optimized for ArkOS on R36S.
- Eliminated periodic stalls and watchdog SIGTERMs.
- Full analog touch support.
- Robust save management.
- Direct-to-SD save persistence.

- [x] Fix resume freeze after long sleep (cap audio delays to 1sec).
- [x] Fix volume burst after resume (add 1sec delay after recovery).
- [x] Fix CPU core permission noise (disable core toggling after first failure).
- [x] Implement "Smart Retry" for CPU cores (60s backoff).
- [x] Document PiP transparency fix reasoning.

## 3. Post-1.0 Regressions & Hotfixes
- [x] **Regression: Broken Saves & Black Previews (The "Double Hooking" Bug)**
    - **Mistake**: Attempted to fix input suppression by adding a new hook to `load_state_index` within `libSDL2_hook.so`.
    - **Impact**: This accidentally overwrote the primary hook in `libdtr.so`, which handles critical path redirection to `/roms/nds/savestates` and generates preview thumbnails. As a result, older saves were "missing" and new ones had black thumbnails.
    - **Resolution**: Implemented a cross-library callback mechanism in the `nds_hook` structure. The primary hook in `libdtr.so` now triggers a `reset_menu` callback in `libSDL2_hook.so`, allowing both libraries to react to state changes without conflicting hooks.
- [x] **Fix: Input Suppression after OSD Messages**
    - **Root Cause**: `queue_copy` (menu rendering) was being called for simple OSD notifications, which the wrapper interpreted as "The Menu is Open," causing permanent input suppression.
    - **Fix**: Modified `queue_copy` to only set the menu-active flag if an interactive menu layer is actually detected.
- [x] **Fix: Lost "Dual Control" Logic**
    - **Root Cause**: Premature updating of `pre_bits` in `send_key_event` prevented `send_touch_event` from detecting the same input frame.
    - **Fix**: Deferred `pre_bits` update to the end of the global input loop, ensuring simultaneous D-pad (player) and Right Stick (touch) functionality.
- [x] **Fix: Slot 0 Input Suppression & Black Previews (Forced Menu Exit)**
    - **Root Cause**: State loads could inherit an "Open Menu" state, locking input. Native return values were being lost in hooks.
    - **Fix**: Implemented forced internal menu exit via `set_screen_menu_off()` on load/save. Restored native return value passing.
- [x] **Feature: Exit Progress UI & Deadlock Prevention**
    - **Feature**: Added a "FLUSHING TO DISK..." OSD message during the exit sequence to provide visual feedback during the mandatory system `sync`.
    - **Fix (Deadlock)**: Resolved a shutdown freeze by implementing a `pthread_self()` check in `quit_device`. This prevents the video thread from attempting to "join" itself during cleanup.
    - **Implementation**: Deferring shutdown via a `quit_triggered` flag allowed the final UI frame to be rendered and flipped before the main process terminates.

## 4. Post-Release Stability
- [x] **Fix: ALSA Queue Deadlock on Resume**
    - **Root Cause**: Blocking `snd_pcm_writei` calls during audio buffer saturation (common after sleep/resume) caused the main thread to hang indefinitely.
    - **Fix**: Switched ALSA to `SND_PCM_NONBLOCK` mode. To prevent audio popping, implemented a "Smart Retry" loop that waits for buffer space using `snd_pcm_wait` before falling back to dropping frames (`-EAGAIN`). Also added retry limits and sleeps for generic ALSA errors (like Broken Pipe) to prevent CPU-hogging infinite loops.
- [x] **Feature: Recovery OSD Status**
    - **Implementation**: Added `rcv_active` and `rcv_msg` to the shared `nds_hook` structure.
    - **Usage**: The audio driver now triggers an on-screen "ALSA Recovering..." status during long hangs, providing feedback to the user that the system is not frozen.
- [ ] **Unsolved: In-game Manual Save Hang**
    - **Status**: Manual saving from within game menus (writing to .dsv) occasionally hangs indefinitely.
    - **Investigated**: Reverted backup initialization logic to match original proprietary DraStic behavior. Verified path redirection to `/roms/nds/backup/`.
    - **Note**: Issue remains intermittent and difficult to trace due to the proprietary nature of the core save thread. Quick Saves (States) are recommended as a reliable alternative.