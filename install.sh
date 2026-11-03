#!/usr/bin/env bash
# Persona 5 Rice installer for Umbriel + Noctalia (native C++23).
#
#   ./install.sh                 build, install and restart the user service
#   ./install.sh --check         only verify build/runtime prerequisites
#   ./install.sh --umbriel       also add the p5 layer rule to Umbriel config
#   ./install.sh --no-service    install files but do not touch systemd
#   ./install.sh --no-noctalia-settings
#                                do not change Noctalia notification settings
#
# The installer is intentionally conservative: user assets are never
# overwritten, the existing Umbriel config is never replaced, and only the p5
# layer rule is appended (with a backup) when it is missing.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cfg_home="${XDG_CONFIG_HOME:-$HOME/.config}"
noctalia_dir="$cfg_home/noctalia"
data_home="${XDG_DATA_HOME:-$HOME/.local/share}"
plugin_dir="$data_home/noctalia/plugins/persona-rice"
p5_assets_dir="$noctalia_dir/p5-rice"
umbriel_dir="$cfg_home/umbriel"
systemd_user_dir="$cfg_home/systemd/user"
bak="$noctalia_dir/backup-p5-$(date +%Y%m%d_%H%M%S)"
build_dir="${P5_BUILD_DIR:-$here/build}"
service_name="persona5-widgets.service"
legacy_service="p5-rice-daemon.service"

DO_CHECK=0
DO_UMBRIEL=0
DO_SERVICE=1
DO_NOCTALIA_SETTINGS=1

print_help() {
  cat <<'EOF'
Persona 5 Rice installer for Umbriel + Noctalia (native C++23).

  ./install.sh                 build, install and restart the user service
  ./install.sh --check         only verify build/runtime prerequisites
  ./install.sh --umbriel       also add the p5 layer rule to Umbriel config
  ./install.sh --no-service    install files but do not touch systemd
  ./install.sh --no-noctalia-settings
                               do not change Noctalia notification settings

The installer is conservative: user assets are never overwritten, the existing
Umbriel config is never replaced, and only the p5 layer rule is appended (with a
backup) when it is missing.
EOF
}

for arg in "$@"; do
  case "$arg" in
    --check) DO_CHECK=1 ;;
    --umbriel) DO_UMBRIEL=1 ;;
    --no-service) DO_SERVICE=0 ;;
    --no-noctalia-settings) DO_NOCTALIA_SETTINGS=0 ;;
    -h|--help)
      print_help
      exit 0
      ;;
    *)
      echo "unknown option: $arg (try --help)" >&2
      exit 2
      ;;
  esac
done

if [ -t 1 ]; then
  c_ok=$'\033[32m'; c_warn=$'\033[33m'; c_err=$'\033[31m'; c_dim=$'\033[2m'; c_reset=$'\033[0m'
else
  c_ok=""; c_warn=""; c_err=""; c_dim=""; c_reset=""
fi

ok()   { printf "  %sok%s   %s\n" "$c_ok" "$c_reset" "$*"; }
warn() { printf "  %swarn%s %s\n" "$c_warn" "$c_reset" "$*"; }
fail() { printf "  %sfail%s %s\n" "$c_err" "$c_reset" "$*"; }
note() { printf "  %s%s%s\n" "$c_dim" "$*" "$c_reset"; }

missing_cmds=()
missing_pkgs=()
missing_runtime=()
missing_optional=()

need_cmd() {
  if command -v "$1" >/dev/null 2>&1; then
    ok "command: $1"
  else
    fail "command: $1"
    missing_cmds+=("$1")
  fi
}

need_any_cmd() {
  local label="$1"; shift
  for cmd in "$@"; do
    if command -v "$cmd" >/dev/null 2>&1; then
      ok "$label: $cmd"
      return 0
    fi
  done
  fail "$label: none of $*"
  missing_cmds+=("$1")
  return 1
}

need_pkg() {
  if pkg-config --exists "$1" 2>/dev/null; then
    ok "pkg-config: $1 $(pkg-config --modversion "$1" 2>/dev/null)"
  else
    fail "pkg-config: $1"
    missing_pkgs+=("$1")
  fi
}

echo "==> Checking build prerequisites"
need_any_cmd "C++ compiler" c++ g++ clang++
need_cmd meson
need_cmd ninja
need_cmd pkg-config
need_cmd wayland-scanner

for pkg in \
  wayland-client wayland-cursor wayland-protocols \
  cairo pangocairo pangoft2 fontconfig gdk-pixbuf-2.0 gio-2.0 glib-2.0 \
  libsystemd libcurl
do
  need_pkg "$pkg"
done

echo
echo "==> Checking runtime prerequisites"
for cmd in noctalia umbriel; do
  if command -v "$cmd" >/dev/null 2>&1; then
    ok "command: $cmd"
  else
    fail "command: $cmd (required)"
    missing_runtime+=("$cmd")
  fi
done

if command -v noctalia >/dev/null 2>&1; then
  noctalia_ver="$(noctalia --version 2>/dev/null | head -1 || true)"
  noctalia_major="$(printf '%s' "$noctalia_ver" | sed -n 's/.* v\([0-9][0-9]*\).*/\1/p')"
  if [ -n "$noctalia_major" ] && [ "$noctalia_major" -ge 5 ]; then
    ok "Noctalia v5+: $noctalia_ver"
  else
    warn "expected Noctalia v5+ (found: ${noctalia_ver:-unknown})"
  fi
fi

for cmd in notify-send pw-play; do
  if command -v "$cmd" >/dev/null 2>&1; then
    ok "command: $cmd"
  else
    warn "optional command missing: $cmd"
    missing_optional+=("$cmd")
  fi
done

if command -v systemctl >/dev/null 2>&1; then
  ok "command: systemctl"
else
  warn "systemctl not found; the daemon will be started with nohup"
fi

if [ -n "${WAYLAND_DISPLAY:-}" ] || [ "${XDG_SESSION_TYPE:-}" = "wayland" ]; then
  ok "Wayland session detected (${WAYLAND_DISPLAY:-$XDG_SESSION_TYPE})"
else
  warn "WAYLAND_DISPLAY is unset and XDG_SESSION_TYPE is not wayland"
  warn "the daemon needs a running Umbriel/Wayland session"
fi

if [ -n "${XDG_RUNTIME_DIR:-}" ] && [ -d "${XDG_RUNTIME_DIR:-}" ]; then
  ok "XDG_RUNTIME_DIR: $XDG_RUNTIME_DIR"
else
  warn "XDG_RUNTIME_DIR is missing or invalid"
fi

if [ -n "${DBUS_SESSION_BUS_ADDRESS:-}" ]; then
  ok "D-Bus session bus detected"
else
  warn "DBUS_SESSION_BUS_ADDRESS is unset; D-Bus IPC may not work"
fi

echo
echo "==> Checking bundled assets"
if [ -d "$here/assets/p5calendar/Assets" ]; then
  calendar_files=$(find "$here/assets/p5calendar/Assets" -type f | wc -l)
  ok "calendar art: $calendar_files files"
else
  warn "assets/p5calendar/Assets is missing (procedural fallback will be used)"
fi

if [ -d "$here/assets/p5wallpaper" ]; then
  wallpaper_files=$(find "$here/assets/p5wallpaper" -type f | wc -l)
  ok "wallpaper art: $wallpaper_files files"
else
  warn "assets/p5wallpaper is missing (automatic wallpaper stays disabled)"
fi

for asset in \
  "p5notify/banner2.png" \
  "personaMusic/assets/banner.png" \
  "personaMusic/assets/futaba_frame.png"
do
  if [ -e "$here/assets/$asset" ] || [ -e "$p5_assets_dir/$asset" ]; then
    ok "asset: $asset"
  else
    warn "optional asset missing: $asset (built-in fallback will be used)"
  fi
done

case ":$PATH:" in
  *":$HOME/.local/bin:"*) ok "PATH contains ~/.local/bin" ;;
  *) warn "~/.local/bin is not in PATH; add it to your shell profile" ;;
esac

if [ "${#missing_cmds[@]}" -gt 0 ] || [ "${#missing_pkgs[@]}" -gt 0 ]; then
  echo
  echo "${c_err}Missing build dependencies.${c_reset}"
  if [ "${#missing_cmds[@]}" -gt 0 ]; then
    echo "  commands: ${missing_cmds[*]}"
  fi
  if [ "${#missing_pkgs[@]}" -gt 0 ]; then
    echo "  pkg-config modules: ${missing_pkgs[*]}"
  fi
  echo
  if command -v pacman >/dev/null 2>&1; then
    echo "  Arch:   sudo pacman -S --needed meson ninja pkgconf wayland wayland-protocols cairo pango fontconfig gdk-pixbuf2 systemd-libs curl libnotify pipewire"
  elif command -v apt-get >/dev/null 2>&1; then
    echo "  Debian: sudo apt install meson ninja-build pkg-config libwayland-dev wayland-protocols libcairo2-dev libpango1.0-dev libfontconfig1-dev libgdk-pixbuf-2.0-dev libsystemd-dev libcurl4-openssl-dev libnotify-bin pipewire"
  elif command -v dnf >/dev/null 2>&1; then
    echo "  Fedora: sudo dnf install meson ninja-build pkgconf wayland-devel wayland-protocols-devel cairo-devel pango-devel fontconfig-devel gdk-pixbuf2-devel systemd-devel libcurl-devel libnotify pipewire"
  fi
fi

if [ "$DO_CHECK" -eq 1 ]; then
  echo
  if [ "${#missing_cmds[@]}" -gt 0 ] || [ "${#missing_pkgs[@]}" -gt 0 ] || [ "${#missing_runtime[@]}" -gt 0 ]; then
    echo "Dependency check failed. Install the missing packages above, then rerun ./install.sh --check."
    [ "${#missing_runtime[@]}" -gt 0 ] && echo "Missing runtime: ${missing_runtime[*]}"
    exit 1
  fi
  if [ "${#missing_optional[@]}" -gt 0 ]; then
    echo "Required prerequisites are complete. Optional tools missing: ${missing_optional[*]}"
    echo "The daemon will run, but notification sounds or manual notify-send tests may be unavailable."
  else
    echo "All build and runtime prerequisites look good."
  fi
  exit 0
fi

if [ "${#missing_cmds[@]}" -gt 0 ] || [ "${#missing_pkgs[@]}" -gt 0 ]; then
  echo
  echo "Refusing to build until the missing build dependencies are installed."
  exit 1
fi

echo
echo "==> Building native C++23 modules"
if [ ! -d "$build_dir" ]; then
  meson setup "$build_dir" "$here"
fi
meson compile -C "$build_dir"

echo
echo "==> Installing binaries and assets"
mkdir -p "$HOME/.local/bin" "$p5_assets_dir/p5notify/fonts" \
         "$p5_assets_dir/p5notify/sounds" "$p5_assets_dir/personaMusic/assets" \
         "$p5_assets_dir/personaMusic/fonts" "$p5_assets_dir/p5calendar" "$p5_assets_dir/p5wallpaper" \
         "$plugin_dir" "$systemd_user_dir" "$bak"

if [ "$DO_SERVICE" -eq 1 ]; then
  if command -v systemctl >/dev/null 2>&1; then
    systemctl --user stop "$legacy_service" >/dev/null 2>&1 || true
    systemctl --user stop "$service_name" >/dev/null 2>&1 || true
  fi
  pkill -x p5-rice-daemon >/dev/null 2>&1 || true

  # Migrate the old p5-rice-daemon.service unit to persona5-widgets.service.
  if [ -f "$systemd_user_dir/$legacy_service" ]; then
    cp "$systemd_user_dir/$legacy_service" "$bak/$legacy_service" 2>/dev/null || true
    systemctl --user disable "$legacy_service" >/dev/null 2>&1 || true
    rm -f "$systemd_user_dir/$legacy_service"
  fi
  legacy_dropin="$systemd_user_dir/${legacy_service}.d"
  new_dropin="$systemd_user_dir/${service_name}.d"
  if [ -d "$legacy_dropin" ]; then
    cp -a "$legacy_dropin" "$bak/${legacy_service}.d" 2>/dev/null || true
    if [ -d "$new_dropin" ]; then
      cp -a "$legacy_dropin/." "$new_dropin/" 2>/dev/null || true
      rm -rf "$legacy_dropin"
    else
      mv "$legacy_dropin" "$new_dropin"
    fi
  fi
fi

for bin_name in p5-rice-daemon p5-rice-ctl; do
  if [ -f "$HOME/.local/bin/$bin_name" ]; then
    cp "$HOME/.local/bin/$bin_name" "$bak/"
  fi
  install -m 755 "$build_dir/$bin_name" "$HOME/.local/bin/$bin_name"
done


# Preserve any user-modified assets: -n never clobbers existing files.
for p in p5notify personaMusic; do
  if [ -d "$here/assets/$p" ]; then
    cp -a -n "$here/assets/$p/." "$p5_assets_dir/$p/"
  fi
done

if [ -d "$here/assets/p5calendar/Assets" ]; then
  cp -a -n "$here/assets/p5calendar/." "$p5_assets_dir/p5calendar/"
fi

if [ -d "$here/assets/p5wallpaper" ]; then
  cp -a -n "$here/assets/p5wallpaper/." "$p5_assets_dir/p5wallpaper/"
fi

# The bundled PersonifiedUXRegular.ttf carries the Vietnamese/NFC glyph fixes;
# replace the stock copy on purpose (other user assets stay untouched).
for p in p5notify personaMusic; do
  src_font="$here/assets/$p/fonts/PersonifiedUXRegular.ttf"
  if [ -f "$src_font" ]; then
    mkdir -p "$p5_assets_dir/$p/fonts"
    cp -f "$src_font" "$p5_assets_dir/$p/fonts/PersonifiedUXRegular.ttf"
  fi
done

if [ -d "$plugin_dir" ]; then
  cp -a "$plugin_dir" "$bak/persona-rice-plugin" 2>/dev/null || true
fi
cp -a "$here/noctalia/plugins/persona-rice/." "$plugin_dir/"

echo
echo "==> Configuring Noctalia bridge"
if [ "$DO_NOCTALIA_SETTINGS" -eq 1 ] && command -v noctalia >/dev/null 2>&1; then
  noctalia msg config-set notification.enable_daemon true >/dev/null 2>&1 || true
  noctalia msg config-set notification.filter.persona_suppress_toast.enabled true >/dev/null 2>&1 || true
  noctalia msg config-set notification.filter.persona_suppress_toast.match_content '".*"' >/dev/null 2>&1 || true
  noctalia msg config-set notification.filter.persona_suppress_toast.show_toast false >/dev/null 2>&1 || true
  noctalia msg config-set notification.filter.persona_suppress_toast.play_sound false >/dev/null 2>&1 || true
  noctalia msg config-set notification.filter.persona_suppress_toast.save_history true >/dev/null 2>&1 || true
  noctalia msg config-set notification.filter_order '["persona_suppress_toast"]' >/dev/null 2>&1 || true
  # Restore normal DND; the persona_suppress_toast filter hides the toast/sound.
  noctalia msg notification-dnd-set false >/dev/null 2>&1 || true
  ok "Noctalia notification server + P5 banner monitor configured"
  ok "Noctalia plugin installed at $plugin_dir"
else
  note "skipped Noctalia notification settings"
fi

if [ "$DO_UMBRIEL" -eq 1 ]; then
  echo
  echo "==> Configuring Umbriel layer rule"
  mkdir -p "$umbriel_dir"
  umbriel_cfg="$umbriel_dir/config.toml"
  if [ ! -f "$umbriel_cfg" ]; then
    cp "$here/umbriel/config.toml" "$umbriel_cfg"
    ok "installed $umbriel_cfg"
  elif grep -q "Persona 5 Rice — Umbriel Configuration" "$umbriel_cfg" 2>/dev/null; then
    cp "$umbriel_cfg" "$bak/umbriel-config.toml"
    cp "$here/umbriel/config.toml" "$umbriel_cfg"
    ok "updated bundled $umbriel_cfg (backup in $bak)"
  else
    layer_rules="$umbriel_dir/layer_rules.toml"
    if [ -f "$layer_rules" ] && ! grep -q "p5calendar" "$layer_rules"; then
      cp "$layer_rules" "$bak/layer_rules.toml"
      cat >> "$layer_rules" <<'EOF'

# Persona 5 rice: keep notification, music and calendar layer surfaces crisp.
[[layer_rule]]
match.namespace = "^noctalia-(p5notify|p5calendar|desktop-widget-personaMusic)$"
blur = false
EOF
      ok "appended p5 layer rule to $layer_rules (backup in $bak)"
    elif [ -f "$layer_rules" ]; then
      ok "Umbriel layer rule already present"
    else
      warn "split Umbriel config found but no layer_rules.toml"
      note "add the rule from umbriel/config.toml manually"
    fi
  fi
  if command -v umbriel >/dev/null 2>&1; then
    umbriel config validate -c "$umbriel_cfg" >/dev/null 2>&1 \
      && ok "umbriel config validate" \
      || warn "umbriel config validate failed; check $umbriel_cfg"
  fi
else
  note "Umbriel config untouched (pass --umbriel to add the layer rule)"
fi

if [ "$DO_SERVICE" -eq 1 ]; then
  echo
  echo "==> Installing systemd user service"
  cat > "$systemd_user_dir/$service_name" <<EOF
[Unit]
Description=Persona 5 Widgets Service
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
    systemctl --user enable "$service_name" >/dev/null 2>&1 || true
    systemctl --user restart "$service_name" >/dev/null 2>&1 || \
      (nohup "$HOME/.local/bin/p5-rice-daemon" >/dev/null 2>&1 &)
    ok "$service_name restarted"
  else
    nohup "$HOME/.local/bin/p5-rice-daemon" >/dev/null 2>&1 &
    ok "p5-rice-daemon started with nohup"
  fi
else
  note "systemd service untouched (--no-service)"
fi

echo
echo "==> Verifying"
"$HOME/.local/bin/p5-rice-daemon" --version
if command -v noctalia >/dev/null 2>&1; then
  # Enable after the daemon is running so the Luau service can immediately
  # mirror calendar/music settings through p5-rice-ctl.
  noctalia msg plugins enable schiszo/persona-rice >/dev/null 2>&1 || true
  noctalia plugins lint "$plugin_dir" >/dev/null 2>&1 \
    && ok "Noctalia plugin lint passed" \
    || warn "Noctalia plugin lint reported a problem"
fi
if command -v umbriel >/dev/null 2>&1; then
  calendar_visible=0
  for _ in 1 2 3 4 5; do
    if umbriel layers 2>/dev/null | grep -q "noctalia-p5calendar.*yes"; then
      calendar_visible=1
      break
    fi
    sleep 0.4
  done
  if [ "$calendar_visible" -eq 1 ]; then
    ok "calendar layer is live"
  else
    warn "calendar layer not visible yet (check the service)"
  fi
fi

echo
echo "==> Pruning old backups (keeping 2)"
mapfile -t backup_dirs < <(find "$noctalia_dir" -maxdepth 1 -type d -name 'backup-p5-*' -printf '%T@ %p\n' | sort -nr | cut -d' ' -f2-)
if [ "${#backup_dirs[@]}" -gt 2 ]; then
  removed_backups=0
  for old_backup in "${backup_dirs[@]:2}"; do
    rm -rf "$old_backup"
    removed_backups=$((removed_backups + 1))
  done
  ok "kept 2 newest backups; removed $removed_backups old one(s)"
else
  note "kept ${#backup_dirs[@]} backup(s)"
fi

echo
echo "Installed:"
echo "  binaries : ~/.local/bin/p5-rice-daemon, ~/.local/bin/p5-rice-ctl"
echo "  assets   : $p5_assets_dir"
echo "  plugin   : $plugin_dir"
echo "  backups  : $bak"
echo
echo "Next steps:"
echo "  systemctl --user status $service_name"
echo "  p5-rice-ctl calendar on|off     # calendar on every monitor"
echo "  p5-rice-daemon --calendar-preview /tmp/p5cal.png"
echo "  noctalia msg config-set weather.enabled true   # weather icon"
echo "  # Wallpaper automation is in Settings -> Plugins -> Persona 5 Rice"
echo
echo "Done."
