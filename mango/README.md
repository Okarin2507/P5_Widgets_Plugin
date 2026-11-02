# mangoWM Configuration

MangoWM compositor configuration files. These define the window manager
behavior: keybinds, layouts, decoration, monitors, input devices, window
rules, and autostart programs.

Managed via `mangoconf-editor` (SUPER+M) or `mangoconf-viewer` (SUPER+ALT+M).

## Key files

| File | Purpose |
|------|---------|
| `config.conf` | Main entry point, sources all other configs |
| `binds.conf` | Keybinds, mousebinds, axisbinds, gestures |
| `layouts.conf` | Layout selection and per-layout settings |
| `decoration.conf` | Borders, gaps, blur, shadows, colors, opacity |
| `monitors.conf` | Monitor resolution, position, scale, VRR |
| `input.conf` | Keyboard repeat rate, mouse/trackpad settings |
| `autostart.conf` | Programs to run on compositor start |
| `windowrules.conf` | Per-window rules (floating, fullscreen, etc.) |
| `environment.conf` | Environment variables and mangoWM features |
| `dms/` | DMS-managed bind overrides |
