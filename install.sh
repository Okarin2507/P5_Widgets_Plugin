#!/usr/bin/env bash
# Installs the native C++23 Persona 5 Rice daemon (p5-rice-daemon & p5-rice-ctl),
# Noctalia bridge plugin, helper scripts, and (with --umbriel) the Umbriel compositor config.
set -e

here="$(cd "$(dirname "$0")" && pwd)"
cfg_home="${XDG_CONFIG_HOME:-$HOME/.config}"
noctalia_dir="$cfg_home/noctalia"
p5_assets_dir="$noctalia_dir/p5-rice"
bak="$noctalia_dir/backup-p5-$(date +%Y%m%d_%H%M%S)"

missing_cmds=""
for cmd in noctalia umbriel meson ninja pkg-config wf-recorder playerctl notify-send pw-play; do
  command -v "$cmd" >/dev/null || missing_cmds="$missing_cmds $cmd"
done
if [ -n "$missing_cmds" ]; then
  echo "warning: missing commands:$missing_cmds"
  echo "  on Arch try: sudo pacman -S meson ninja pkgconf wf-recorder playerctl libnotify pipewire"
fi

echo "==> Building native C++23 binaries (p5-rice-daemon & p5-rice-ctl)..."
if [ ! -d "$here/build" ]; then
  meson setup "$here/build" "$here"
fi
meson compile -C "$here/build"

mkdir -p "$HOME/.local/bin" "$p5_assets_dir/p5notify/fonts" "$p5_assets_dir/p5notify/sounds" \
         "$p5_assets_dir/personaMusic/assets" "$p5_assets_dir/personaMusic/fonts" \
         "$noctalia_dir/plugins/schiszo/persona-rice" "$bak"

case ":$PATH:" in *":$HOME/.local/bin:"*) ;; *) echo "warning: ~/.local/bin is not in your PATH" ;; esac

# Install compiled C++ binaries
for bin_name in p5-rice-daemon p5-rice-ctl; do
  [ -f "$HOME/.local/bin/$bin_name" ] && cp "$HOME/.local/bin/$bin_name" "$bak/"
  cp "$here/build/$bin_name" "$HOME/.local/bin/$bin_name"
  chmod +x "$HOME/.local/bin/$bin_name"
done

# Install helper scripts
for f in "$here"/bin/*; do
  name="$(basename "$f")"
  [ -f "$HOME/.local/bin/$name" ] && cp "$HOME/.local/bin/$name" "$bak/"
  cp "$f" "$HOME/.local/bin/$name"
  chmod +x "$HOME/.local/bin/$name"
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

if [ "${1:-}" = "--umbriel" ]; then
  if [ -d "$cfg_home/umbriel" ]; then
    cp -r "$cfg_home/umbriel" "$bak/umbriel"
    echo "backed up existing umbriel config to $bak/umbriel"
  fi
  mkdir -p "$cfg_home/umbriel"
  cp "$here/umbriel/config.toml" "$cfg_home/umbriel/config.toml"
  echo "installed Umbriel configuration to $cfg_home/umbriel/config.toml"
  umbriel config validate -c "$cfg_home/umbriel/config.toml" || true
fi

echo
missing=0
for a in \
  p5notify/banner2.png p5notify/fonts/Persona5main.ttf \
  p5notify/fonts/PersonifiedUXRegular.ttf p5notify/sounds/papersound.wav \
  personaMusic/assets/banner.png personaMusic/assets/futaba_frame.png \
  personaMusic/fonts/personafont.ttf personaMusic/fonts/PersonifiedUXRegular.ttf; do
  if [ ! -e "$p5_assets_dir/$a" ] && [ ! -e "$cfg_home/dms/plugins/$a" ]; then
    echo "optional asset not found: $p5_assets_dir/$a (using built-in C++ Cairo P5 comic fallback)"
    missing=1
  fi
done

echo
echo "Done! Installed native C++23 Persona 5 Rice for Umbriel + Noctalia:"
echo "  - Binaries: ~/.local/bin/p5-rice-daemon, ~/.local/bin/p5-rice-ctl, ~/.local/bin/p5-profile-switcher"
echo "  - Noctalia plugin: $noctalia_dir/plugins/schiszo/persona-rice"
echo "  - Test notification: p5-rice-ctl notify 'PHANTOM THIEVES' 'Take Your Heart!'"
echo "  - Switch profile:    p5-profile-switcher persona  (or --cycle)"
