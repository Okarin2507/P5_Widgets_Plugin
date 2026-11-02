# Persona 5 Rice — Native C++23 Port for Umbriel Compositor & Noctalia Shell

A complete **native C++23** port of the Persona 5 desktop rice (`p5notify` notification banners + `personaMusic` Futaba confidant music player + compositor configuration), architected specifically for **[Umbriel Compositor](https://docs.noctalia.dev/umbriel/)** and **[Noctalia Shell (v5+)](https://docs.noctalia.dev/noctalia/)** with **zero Qt / QML / Quickshell dependency**.

> Fan made, not affiliated with Atlus/SEGA. Even without external Persona 5 PNG/TTF assets, the C++23 Cairo renderer includes built-in procedural Persona 5 comic-style fallbacks; adding the original artwork, fonts, and sound files unlocks the full custom asset experience.

---

## Architecture & What Is in This Repo

| Path | Description |
|------|-------------|
| `src/` & `meson.build` | **Native C++23 Wayland Layer-Shell daemon (`p5-rice-daemon`) & CLI (`p5-rice-ctl`)** built directly on `libwayland-client`, `zwlr-layer-shell-v1`, `wl_shm`, Cairo, PangoCairo, Fontconfig/FreeType, `libsystemd` (`sd-bus`), and `libcurl`. |
| `src/p5notify_surface.*` | C++23 port of `p5notify/Main.qml`: renders on `ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY` (`namespace = "noctalia-p5notify"`), dynamic click-through `wl_region` input mask, `OutBack` slide-in + 6-step sticker wobble animation, rotated upper tag with random inverted boxed letter, rotated body panel, hover timer pause, click-to-dismiss, and critical popup suppression. |
| `src/p5music_surface.*` & `src/jitter_text.*` | C++23 port of `personaMusic/PersonaMusicWidget.qml`: renders on `ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM` (`namespace = "noctalia-desktop-widget-personaMusic"`), deterministic `sin` PRNG ransom-note cut-out lettering (`Jitter`) with 14-ring spiky sticker outlines and inverted boxes, rotated `17°` album art card (`file://` & HTTP/HTTPS via `libcurl`), rotated `3°` dialogue banner, rotated `5.2°` seek bar with inverse-matrix hit testing, and custom vector `Shuffle` button. |
| `src/dbus_notifications.*` & `src/dbus_mpris.*` | Native `sd-bus` D-Bus integration: passive `org.freedesktop.DBus.Monitoring.BecomeMonitor` eavesdropping on `org.freedesktop.Notifications.Notify` (works alongside Noctalia's notification daemon with DND enabled), control bus `dev.noctalia.PersonaRice`, and full MPRIS2 client (`org.mpris.MediaPlayer2.Player`). |
| `noctalia/plugins/persona-rice/` | **Noctalia v5+ Luau Bridge Plugin** (`plugin.toml`, `service.luau`, `widget.luau`) validated with `noctalia plugins lint`. |
| `umbriel/config.toml` | **Umbriel Compositor Configuration** ported from `mango/*.conf` and validated with `umbriel config validate`. |
| `bin/p5-profile-switcher` | Profile switcher (`persona`, `saber`, `elden-ring`, or `--cycle`) integrating `p5-rice-ctl` and `noctalia msg`. |
| `bin/dms-profile-notify` | Profile-switch notification helper using `p5-rice-ctl` & `noctalia msg`. |
| `bin/toggle-record.sh` | Screen recording toggle (`wf-recorder` with PipeWire monitor audio and automatic `umbriel outputs` detection). |

---

## Requirements (Arch Linux)

- **Compositor & Shell:** `umbriel-git` (`umbriel`), `noctalia` (v5+)
- **C++23 Build & Runtime Libraries:** `gcc` or `clang`, `meson`, `ninja`, `wayland`, `wayland-protocols`, `cairo`, `pango`, `fontconfig`, `freetype2`, `gdk-pixbuf2`, `systemd-libs`, `curl`
- **Utilities:** `wf-recorder`, `playerctl`, `libnotify` (`notify-send`), `pipewire` (`pw-play`)

---

## Optional Custom Assets

Place your custom assets in `~/.config/noctalia/p5-rice/` (or keep them in `~/.config/dms/plugins/` — both locations are searched automatically by `p5-rice-daemon`):

**`~/.config/noctalia/p5-rice/p5notify/`**
- `banner2.png` (or `banner.png`) : notification banner art (transparent PNG)
- `fonts/Persona5main.ttf` : title font
- `fonts/PersonifiedUXRegular.ttf` : body font
- `sounds/papersound.wav` : pop-in sound

**`~/.config/noctalia/p5-rice/personaMusic/`**
- `assets/banner.png` and `assets/futaba_frame.png`
- `fonts/personafont.ttf` and `fonts/PersonifiedUXRegular.ttf`

*(If any PNG is missing, `p5-rice-daemon` automatically renders a procedural Persona 5 crimson/obsidian/white jagged comic fallback!)*

---

## Build & Install

```bash
./install.sh            # Builds C++23 binaries, installs scripts & Noctalia plugin
./install.sh --umbriel  # Also installs ~/.config/umbriel/config.toml (backs up existing)
```

Or compile manually with Meson:

```bash
meson setup build
meson compile -C build
```

---

## Usage & CLI Commands (`p5-rice-ctl`)

```bash
# Start the native C++23 daemon (also autostarted by Umbriel in config.toml)
p5-rice-daemon &

# Trigger a Persona 5 notification banner directly
p5-rice-ctl notify "PHANTOM THIEVES" "Take Your Heart!"

# Or use standard notify-send (captured automatically via D-Bus monitoring)
notify-send "Futaba" "Mementos scan complete!"

# Switch profiles (persona / saber / elden-ring) or cycle with Mod+Shift+M
p5-profile-switcher persona
p5-profile-switcher --cycle

# Toggle the Futaba desktop music widget independently
p5-rice-ctl music on
p5-rice-ctl music off

# Dismiss all active P5 banners
p5-rice-ctl dismiss-all
```
