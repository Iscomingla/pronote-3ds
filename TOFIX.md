# TOFIX

## Black screen on boot

ui.c defines `ui_get_target()` and `ui_clear_target()` but neither was declared in `ui.h`.
Without the declarations, C implicitly treats both as returning `int` — the `C3D_RenderTarget*`
pointer gets truncated and passed as garbage to `C2D_TargetClear`, which renders nothing.

Fix in PR #12: add both declarations to `include/ui.h` with correct signatures.
