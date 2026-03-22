# Pi4 Vulkan Warnings in Rerun

When running Rerun on Raspberry Pi 4, several Vulkan warnings appear. These are GPU driver limitations, not application bugs.

## Warnings

### "Suboptimal present of frame X"

The window surface (size/format) doesn't perfectly match what Vulkan expected. Causes:
- Window resized
- Display scaling mismatch (Pi guesses scale factor ~1.4)
- Swapchain not recreated to match current window state

**Potential fixes:**
- Rerun/egui could detect suboptimal condition and recreate swapchain
- Set window to fixed size matching display exactly

### "Missing downlevel flags: FULL_DRAW_INDEX_UINT32"

Hardware limitation of the V3D 4.2 GPU (Broadcom VideoCore VI).

**Potential fixes:**
- None on Pi4 (hardware limitation)
- Pi5 has VideoCore VII which may not have this issue

### "Unable to find extension: VK_EXT_physical_device_drm"

Missing Vulkan extension for direct rendering manager integration.

**Potential fixes:**
- Mesa driver update (Pi runs Mesa 25.0.7, quite recent)
- Wait for upstream V3D Vulkan driver improvements

## Workarounds

For smoother Rerun visualization:

1. **Upgrade to Pi5** - Better GPU (VideoCore VII)
2. **Use X11 instead of Wayland** - May have better Vulkan support
3. **Run headless + web viewer** - Use `rr.serve()` and view from a more capable machine
4. **Suppress warnings** - `RUST_LOG=warn,wgpu_hal=error` (cosmetic only)

## Impact

These warnings are cosmetic - they don't affect:
- Data logging accuracy
- RRD file recording
- C6 packet reception
- Visualization correctness (just performance)

The Pi4's GPU is limited compared to desktop GPUs, but Rerun adapts and functions correctly.
