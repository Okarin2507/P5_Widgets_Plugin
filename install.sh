#!/usr/bin/env bash
# Installs ONLY the native C++23 Persona 5 Notification & Music Detection daemon
# (p5-rice-daemon & p5-rice-ctl) and Noctalia integration without modifying user keybinds.
set -e

here="$(cd "$(dirname "$0")" && pwd)"
cfg_home="${XDG_CONFIG_HOME:-$HOME/.config}"
noctalia_dir="$cfg_home/noctalia"
p5_assets_dir="$noctalia_dir/p5-rice"
systemd_user_dir="$cfg_home/systemd/user"
bak="$noctalia_dir/backup-p5-$(date +%Y%m%d_%H%M%S)"

missing_cmds=""
for cmd in noctalia umbriel meson ninja pkg-config notify-send pw-play; do
  command -v "$cmd" >/dev/null || missing_cmds="$missing_cmds $cmd"
done
if [ -n "$missing_cmds" ]; then
  echo "warning: missing commands:$missing_cmds"
  echo "  on Arch try: sudo pacman -S meson ninja pkgconf libnotify pipewire"
fi

echo "==> Building native C++23 binaries (p5-rice-daemon & p5-rice-ctl)..."
if [ ! -d "$here/build" ]; then
  meson setup "$here/build" "$here"
fi
meson compile -C "$here/build"

mkdir -p "$HOME/.local/bin" "$p5_assets_dir/p5notify/fonts" "$p5_assets_dir/p5notify/sounds" \
         "$p5_assets_dir/personaMusic/assets" "$p5_assets_dir/personaMusic/fonts" \
         "$noctalia_dir/plugins/schiszo/persona-rice" "$systemd_user_dir" "$bak"

case ":$PATH:" in *":$HOME/.local/bin:"*) ;; *) echo "warning: ~/.local/bin is not in your PATH" ;; esac

# Stop running daemon before replacing binary (avoids 'Text file busy')
systemctl --user stop p5-rice-daemon.service >/dev/null 2>&1 || true
pkill -x p5-rice-daemon >/dev/null 2>&1 || true

# Install compiled C++ binaries (notification + music daemon & controller)
for bin_name in p5-rice-daemon p5-rice-ctl; do
  [ -f "$HOME/.local/bin/$bin_name" ] && cp "$HOME/.local/bin/$bin_name" "$bak/"
  rm -f "$HOME/.local/bin/$bin_name"
  install -m 755 "$here/build/$bin_name" "$HOME/.local/bin/$bin_name"
done

# Install profile switcher helper
for name in p5-profile-switcher dms-profile-notify; do
  if [ -f "$here/bin/$name" ]; then
    [ -f "$HOME/.local/bin/$name" ] && cp "$HOME/.local/bin/$name" "$bak/"
    cp "$here/bin/$name" "$HOME/.local/bin/$name"
    chmod +x "$HOME/.local/bin/$name"
  fi
done

# Migrate any existing assets from ~/.config/dms/plugins/ if present
for p in p5notify personaMusic; do
  if [ -d "$cfg_home/dms/plugins/$p" ]; then
    cp -rn "$cfg_home/dms/plugins/$p/." "$p5_assets_dir/$p/" 2>/dev/null || true
  fi
done

# Install Noctalia bridge plugin
cp -r "$here/noctalia/plugins/persona-rice/." "$noctalia_dir/plugins/schiszo/persona-rice/"

# Initialize default profile if not set
if [ ! -f "$noctalia_dir/p5-profile" ]; then
  echo "persona" > "$noctalia_dir/p5-profile"
fi

# Disable Noctalia's stock notification daemon so Discord & all app notifications
# display exclusively through the Persona 5 notification widget
if command -v noctalia >/dev/null 2>&1; then
  noctalia msg config-set notification.enable_daemon false >/dev/null 2>&1 || true
  noctalia msg notification-clear-active >/dev/null 2>&1 || true
fi

# Install systemd user service so p5-rice-daemon runs automatically without touching keybinds
cat > "$systemd_user_dir/p5-rice-daemon.service" <<EOF
[Unit]
Description=Persona 5 Native Wayland Notification & Music Widget Daemon
PartOf=graphical-session.target
After=graphical-session.target

[Service]
Type=simple
ExecStart=$HOME/.local/bin/p5-rice-daemon
Restart=on-failure
RestartSec=2

[Install]
WantedBy=graphical-session.target
EOF

if command -v systemctl >/dev/null 2>&1; then
  systemctl --user daemon-reload >/dev/null 2>&1 || true
  systemctl --user enable p5-rice-daemon.service >/dev/null 2>&1 || true
  systemctl --user restart p5-rice-daemon.service >/dev/null 2>&1 || \
    (nohup "$HOME/.local/bin/p5-rice-daemon" >/dev/null 2>&1 &)
else
  nohup "$HOME/.local/bin/p5-rice-daemon" >/dev/null 2>&1 &
fi

# If a previous install wrote our template with [keybinds] into ~/.config/umbriel/config.toml,
# or if --umbriel was passed, update it without any [keybinds] override so default binds work.
if [ "${1:-}" = "--umbriel" ] || grep -q "Persona 5 Rice — Umbriel Configuration" "$cfg_home/umbriel/config.toml" 2>/dev/null; then
  mkdir -p "$cfg_home/umbriel"
  [ -f "$cfg_home/umbriel/config.toml" ] && cp "$cfg_home/umbriel/config.toml" "$bak/umbriel-config.toml"
  cp "$here/umbriel/config.toml" "$cfg_home/umbriel/config.toml"
  echo "updated $cfg_home/umbriel/config.toml (no keybind overrides)"
  umbriel config validate -c "$cfg_home/umbriel/config.toml" || true
fi

echo
for a in \
  p5notify/banner2.png p5notify/fonts/Persona5main.ttf \
  p5notify/fonts/PersonifiedUXRegular.ttf p5notify/sounds/papersound.wav \
  personaMusic/assets/banner.png personaMusic/assets/futaba_frame.png \
  personaMusic/fonts/personafont.ttf personaMusic/fonts/PersonifiedUXRegular.ttf; do
  if [ ! -e "$p5_assets_dir/$a" ] && [ ! -e "$cfg_home/dms/plugins/$a" ]; then
    echo "optional asset not found: $p5_assets_dir/$a (using built-in C++ Cairo P5 comic fallback)"
  fi
done

echo
echo "Done! Installed Persona 5 Notification & Universal Media Widget (All Monitors, Top-Left):"
echo "  - Binaries: ~/.local/bin/p5-rice-daemon, ~/.local/bin/p5-rice-ctl"
echo "  - Service:  systemctl --user status p5-rice-daemon.service"
echo "  - Test:     notify-send 'Discord' 'Hello from Phantom Thieves!'"
