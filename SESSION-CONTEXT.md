# P5 Rice — Project Context (Nov 04, 2026)

> Copy file này sang session/kho chat khác để agent có đầy đủ ngữ cảnh mà không
> cần đọc lại source code hoặc git history.

---

## Thông tin chung

| Hạng mục | Giá trị |
|---|---|
| Repo | `https://github.com/Okarin2507/Persona-rice-main.git` |
| Branch | `main` (origin/HEAD → main; nhánh `master` cũ đã bị xoá bỏ — docs cũ talk về xoá `main` đã OBSOLETE) |
| Thư mục | `/home/okarin/Projects/Personal/Persona 5/Persona-rice-main` |
| Hệ điều hành | CachyOS Linux (Arch-based) |
| Compositor | Umbriel `0.1.0` (native C++) |
| Shell | Noctalia `5.4.1` |
| Màn hình | 2× 1920×1080 (`eDP-1` laptop + `DP-1`) |
| Ngôn ngữ | C++23, không dùng Qt/QML/Quickshell |
| Build | Meson (primary) + CMake (backup) |
| Service | `p5-widgets.service` (systemd user) |
| User | `okarin`, timezone `Asia/Ho_Chi_Minh` (UTC+7) |
| Credit | Sản phẩm fan-made; lấy cảm hứng từ [JDevera101/Persona-rice](https://github.com/JDevera101/Persona-rice), calendar art từ [Mive82/Persona-5-Calendar](https://github.com/Mive82/Persona-5-Calendar) |

### Rebrand "Persona 5" → "P5" (Nov 2026)

- Toàn bộ UI label + tên thư mục/file đổi sang "P5"; tên nhân vật (Futaba/Sumire) đã bỏ khỏi settings labels, default name tag (giờ là "P5"), asset filenames và fallback chain.
- Đổi tên: `personaMusic/` → `p5music/`; `personafont.ttf` → `p5font.ttf`; `Persona5main.ttf` → `p5main.ttf`; `futaba_frame.png` → `character_frame.png`; `sumire_frame.png` xoá khỏi repo bundle; `assets/personaMusic/assets/sumire.png` dọn theo context "Cleanup" cũ.
- Đổi tên khác: `persona5-widgets.service` → `p5-widgets.service`; D-Bus `dev.noctalia.PersonaRice` → `dev.noctalia.P5Rice`; layer namespace `desktop-widget-p5music`; Noctalia filter `p5_suppress_toast` (bản `persona_suppress_toast` cũ bị automatic disable).
- **Plugin id đổi** `schiszo/persona-rice` → **`okarin/persona-plugin`** (Nov 2026): author `okarin`, chữ "rice" đổi thành "plugin" cho hợp lý. Installer disable id cũ + enable id mới; settings cũ của id cũ đã được reset về defaults (toàn bộ đều đang là default nên không mất gì). Folder plugin giờ là `~/.local/share/noctalia/plugins/p5-plugin`; tên hiển thị "P5 Plugin".
- User assets local-only (repo `.gitignore` toàn bộ `assets/` trừ 3 tracked file): installer dùng `cp -a -n` merge từ bundled nếu có, không clobber user file.

---

## Features hiện có

- **Notification** — Noctalia là notification server, widget monitor qua `BecomeMonitor`, hiển thị banner P5.
- **Banner timeout setting** — `notification_timeout_seconds` trong plugin settings (0 = auto: theo `expire_timeout` app gửi, mặc định 6s, clamp 3–15s; 1–120 = ép cố định). Noctalia **không có** setting timeout cho notification (`settings.toml` chỉ có `enable_daemon` + filter), nên phải làm setting riêng ở plugin.
- **Notification click-to-open** — bấm vào vùng **text** (tag title / body panel) banner P5 sẽ mở app đã gửi thông báo, banner tự dismiss; bấm vào artwork ngoài text chỉ dismiss.
- **Scratchpad blocker** — `notification_scratchpad_block` (bool, default true): nếu app gửi notification đang có window ẩn trong Umbriel scratchpad → click text **không mở app** (tránh yank window đã giấu ra màn hình), banner chỉ dismiss. Cơ chế ở section Click-to-open sender app.
- **Notification volume** — `notification_volume` trong plugin settings (0–100%, default 100 = giữ hành vi cũ). Đi qua cùng pipeline: plugin service → `assets.conf` → `AssetConfig` → `play_sound_async` áp volume qua `pw-play --volume` (0–1.0) hoặc `paplay --volume` (0–65536); 0 = tắt tiếng (không spawn player). Chỉ điều chỉnh âm thanh banner P5, **không đụng** volume hệ thống hay `audio.sound_volume` của Noctalia. aplay (fallback cuối) không hỗ trợ volume.
- **Notification queue** — chỉ hiện **từng cái một**, không stack nhiều banner cùng lúc.
- **Calendar** — lịch P5 layered trên **mọi màn hình**, sync thời gian thực.
- **Music** — widget nhạc P5 với MPRIS controls, Jitter ransom-note text, album art.
- **Weather** — đọc cache Noctalia (no network, no API key).
- **Wallpaper automation** — day/afternoon/night dựa trên sunrise/sunset timeline.
- **Asset overrides** — mọi banner/character/calendar/wallpaper/font configure được trong **Settings → Plugins → P5 Rice**.
- **DND gate** — `respect_noctalia_dnd` (default true) ẩn banner P5 khi DND bật.
- **Asset retention** — installer chỉ giữ 2 backup mới nhất.

---

## Tái cấu trúc source tree (completed Nov 2026)

```
src/
  app/
    main.cpp                 daemon entry, Wayland globals, output slots, timer loop
    ctl.cpp                  p5-rice-ctl D-Bus client CLI
  core/
    common.*                 paths, fonts, colours, image loading, process helpers
    shm_buffer.*             double-buffered wl_shm + Cairo surfaces
    image_cache.*            shared weak-ref image cache + pre-scaling
    asset_config.*           assets.conf parser cho Settings overrides
    notification_launcher.*  click-to-open: launch app gửi notification (desktop-entry/app_name/PATH)
    scratchpad_check.*       blocker click-to-open khi sender window ẩn trong Umbriel scratchpad
  ipc/
    dbus_notifications.*     Notify monitor, optional server, control bus
    dbus_mpris.*             MPRIS2 client and album-art fetch
  notifications/
    notification_event.hpp   plain event struct shared by IPC and rendering
    notification_surface.*   P5 notification banner overlay
  music/
    music_surface.*          P5 music widget and hit testing
    jitter_text.*            deterministic ransom-note glyph renderer
  calendar/
    calendar_surface.*       per-output calendar layer + time-of-day mapping
  weather/
    noctalia_weather.*       read-only bridge to Noctalia's weather.json (WMO code, sunrise/sunset)
    day_phase.*              eight-phase sunrise/sunset timeline
  wallpaper/
    wallpaper_automation.*   time-based day/afternoon/night wallpaper switching

assets/
  p5notify/                  notification banner, fonts, sound
  p5music/              music banner, character frame, fonts
  p5calendar/                layered day/month/week/ToD/weather art (607 art files + WTFPL license)
  p5wallpaper/               P5 City day/afternoon/night wallpapers (3 PNG)

noctalia/plugins/p5-plugin/   Luau bridge plugin (settings, bar widget, IPC)
umbriel/config.toml              reference Umbriel config + layer rule
bin/toggle-record.sh             screen recorder helper
protocols/                       wlr-layer-shell XML
```

### Module dependency graph

```
app/main
  ├─ notifications ── core
  ├─ music ── ipc ── core
  ├─ calendar ── weather ── core
  ├─ wallpaper ── weather ── core
  └─ ipc
```

### Build libraries

| Target | Contents |
|---|---|
| `p5core` | common utilities, shm buffers, image cache, asset config |
| `p5weather` | Noctalia weather parser, WMO mapping, day-phase timeline |
| `p5ipc` | notification D-Bus monitor/server, MPRIS client |
| `p5notifications` | notification overlay renderer |
| `p5music` | music overlay renderer, Jitter text |
| `p5calendar` | calendar renderer and layer surface |
| `p5wallpaper` | sunrise/sunset wallpaper automation |
| `p5-rice-daemon` | links all modules, owns Wayland event loop |
| `p5-rice-ctl` | standalone control CLI (notify, dismiss-all, music, calendar, play-pause, next, prev, status) |

---

## Build targets / commands

```bash
meson setup build && meson compile -C build
# or
cmake -S . -B build-cmake && cmake --build build-cmake -j"$(nproc)"
```

Installer:

```bash
./install.sh --check                 # verify all dependencies, no build
./install.sh                         # build + install + restart service
./install.sh --umbriel               # append p5 layer rule to Umbriel
./install.sh --no-service            # install files but leave running daemon alone
./install.sh --no-noctalia-settings  # skip notification/server config writes
./install.sh --help
```

---

## Notification architecture

### Active mode: `monitor-only` (default)

- **Noctalia owns `org.freedesktop.Notifications`** — `notification.enable_daemon=true`.
- `p5-rice-daemon` không request notification name. Nó dùng `BecomeMonitor` nghe notification gọi `Notify` thụ động.
- Logi `[P5 artwork]` trong `notification_surface.cpp` nhận notification từ monitor, vẽ banner P5.
- Noctalia toast bị ẩn bằng filter `persona_suppress_toast` (`show_toast=false`, `play_sound=false`, `save_history=true`, `match_content=".*"`).
- Daemon **bật lại `notification.enable_daemon=true`** lúc khởi động để chắc chắn Noctalia là server.
- DND được để cho user điều chỉnh; filter ẩn toast; DND riêng sẽ ẩn tiếp banner P5 (see DND gate).

### `--no-notify`

Tắt banner P5 module; không thay đổi notification settings.

> Chế độ `--notify-takeover` (đã xoá — Nov 2026): daemon trước đây có thể tự own
> org.freedesktop.Notifications; đã cắt toàn bộ fd-server vtable + claim timer
> vì Noctalia luôn là server (design decision #1). Daemon giờ **chỉ monitor**.

### `respect_noctalia_dnd` (default true, plugin setting)

- Khi Noctalia DND bật, `_p5-rice-daemon` polls `noctalia msg notification-dnd-status` (cache 1s).
- Nếu DND on: bỏ qua `push_notification_to_all` — không banner, không sound.
- DND off banner P5 hiển thị bình thường.
- Noctalia vẫn nhận notification và lưu history.

### Click-to-open sender app (Nov 2026)

- Bấm vào **vùng text** banner (tag title hoặc body panel, tính theo same fractions `kTag*`/`kBody*` + rotation) → mở app gửi thông báo rồi dismiss banner trên mọi output.
- Bấm artwork ngoài text → chỉ dismiss (giữ behavior cũ).
- Resolution (`src/core/notification_launcher.{hpp,cpp}`, thuộc `p5core`):
  1. Hint `desktop-entry` từ `Notify()` (parser giờ luôn capture vào `NotificationEvent.desktop_entry`; fallback app_name giữ nguyên cho display khi sender không gửi app_name).
  2. `app_name` match cây `.desktop` qua `GKeyFile` — Score: Name == app_name (3) > GenericName/StartupWMClass/Exec basename (2) > substring (1); XDG priority + Flatpak exports dirs.
  3. `app_name` như executable trên PATH (`spawn_detached`).
- Launch qua `g_app_info_launch` (GIO, `gio-unix-2.0` thêm vào meson + cmake); env của daemon (đã có Wayland) được kế thừa.
- Resolution cache theo `(desktop_entry, app_name)`; launch vẫn chạy lại mỗi lần click.
- Nếu không resolve được target → click text chỉ dismiss như bình thường.
- Log launch: `[p5-rice] Opened notification app '...' via ...`.

#### Scratchpad blocker (Nov 2026)

- `src/core/scratchpad_check.{hpp,cpp}`: `sender_window_in_scratchpad(desktop_entry, app_name, sender_pid)`.
- Dữ liệu: parser Notify capture `sender_pid` via `GetConnectionUnixProcessID` (caller monitor bus; fail → 0), lưu vào `NotificationEvent.sender_pid` → `BannerItem.sender_pid`.
- Scan `umbriel windows --json` (mini JSON parser — chỉ object fields cần: `scratchpad` non-empty = đang ẩn, `pid`, `app_id`, `title`).
- Match (identity = desktop-entry/app_name, strip `.desktop`, lowercase; `[Xwayland]` prefix handling):
  1. `app_id == identity`
  2. `title` chứa identity
  3. PID ancestry: window PID nằm trên ancestor chain của sender (`/proc/<pid>/stat` ppid walk, max 16 bước) — cho case GUI app thẳng; và `comm` của ancestor == `app_id` — cho case CLI chạy trong terminal (opencode trong kitty).
- Heuristic "OC ⊂ opencode" token-prefix **đã bị loại** (sai logic — "OC" là initial, không phải prefix). PID ancestry mới là bridge đúng.
- Bị block: click text → KHÔNG mở, chỉ dismiss (disabled bằng setting `notification_scratchpad_block=false`).
- `umbriel` vắng mặt / exit ≠ 0 → return false (mở bình thường, graceful).
- Scan chỉ chạy tại thời điểm click, không polling.

### Queue one-at-a-time notification overlay

```cpp
// NotificationSurface.cpp
banners_.front()  = banner hiển thị duy nhất
banners_.back()... = queue tối đa kMaxQueued = 8
banners_.pop_front() khi hết timeout
banners_.erase(begin()+1) nếu queue vượt cap
```

- Surface chỉ cao **1 banner**; không stack 2–3 banner cùng lúc.
- Body text wrap tối đa **3 dòng** (`PANGO_WRAP_WORD_CHAR`, `PANGO_ELLIPSIZE_END`).
- Explicits `\n` được giữ nguyên.
- Sound chỉ phát khi banner trở thành banner hiển thị.
- Không có lệnh `notification-clear-active` nữa (Noctalia giữ history, không clear active).
- `kTagW = 0.15` (original nhỏ, không phải 0.245 hay 0.34); tag text ellipsize trong box gốc.
- `kTagCX = 0.789`, `kTagCY = 0.268`, `kTagRot = 15.7`.

---

## Performance architecture (Nov 2026)

- **Main loop** (`src/app/main.cpp`): event-driven `poll()` + timerfd; **adaptive 16ms** khi surface animate, **250ms idle** — không busy loop.
- **MPRIS cadence**: `poll_position(now)` tự throttle — Position query 500ms chỉ khi `has_player && is_playing`; full `refresh_players()` 1s khi playing / **5s khi idle** (signals `PropertiesChanged`/`NameOwnerChanged`/Noctalia bridge đã cover discovery; poll chỉ là backup). **Gate theo `ctx.enable_music`** — khi music off thì không poll gì cả.
- **Calendar fast-path** (`CalendarSurface::update`): early-exit khi wall-second chưa đổi KHÔNG kèm weather poll due KHÔNG kèm weather-icon frame (450ms animation `weather_frame`) — từ ~4 evaluate/s idle → ~2/s khi có weather anim, ~1/s khi không.
- **Startup forks**: `configure_noctalia_notifications` gom 7 `config-set` vào 1 `spawn_detached({"sh","-c", ...})`.
- Cursor chỉ set trong `pointer_enter` (không mỗi motion) — đã đúng sẵn.
- DND status: spawn `noctalia msg notification-dnd-status` chỉ khi có notification tới (cache 1s). Wallpaper: tick 60s, spawn `wallpaper-set` chỉ khi variant đổi.
- Idle CPU measured: ~5%/40s cumulative bao gồm startup burst (cũ ~30% trong 6s đầu trước khi trim).

---

## Music widget

- Anchor **top-left**, layer `BOTTOM`, namespace `noctalia-desktop-widget-p5music`.
- **Hover hit boxes**: mặc định 4 vùng cố định rộng hơn cả sticker (giúp hover dễ).
  ```cpp
  kBtnPrevX = 62.0, Y = 452.0
  kBtnPlayX = 366.0, Y = 488.0
  kBtnNextX = 596.0, Y = 512.0
  kBtnShuffleX = 910.0, Y = 548.0
  kBtnHitPad = 24.0
  ```
- **Album art** luôn **square cover crop** (213×213 design, crop phần dư).
- Album art fetch chỉ dùng URL gốc `mpris:artUrl`, không fetch high-res variants.
- `CAIRO_FILTER_BEST` cho album art.
- **Playing position repaints capped to once per second** (thay vì 2×/giây).
- Seek bar hover chỉ đổi render khi zone đổi.

---

## Calendar

- Anchor bottom-right (default), layer `BOTTOM`, namespace `noctalia-p5calendar`.
- Một instance per `wl_output`, click-through, no keyboard/exclusive zone.
- Layer class **seven static libraries** (see build table).
- **Not contain/backdrop** render — square **cover crop** 213×213, crop phần dư.
- Config:

| Flag | Env | Default |
|---|---|---|
| `--calendar-size` | `P5_CALENDAR_SIZE` | 400 |
| `--calendar-position` | `P5_CALENDAR_POSITION` | bottom-right |
| `--calendar-margin` | `P5_CALENDAR_MARGIN` | 24 |
| `--calendar-version` | `P5_CALENDAR_VERSION` | 2 (dynamic Jan/Feb Royal) |
| `--calendar-weather` | `P5_CALENDAR_WEATHER` | on |
| — | `P5_CALENDAR_WEATHER_MAX_AGE` | 7200 |
| — | `P5_CALENDAR_WEATHER_CACHE` | `$XDG_CACHE_HOME/noctalia/weather.json` |

### Eight-phase timeline

| Phase | Window | ToD asset | Wallpaper |
|---|---|---|---|
| Midnight | 00:00 – 01:00 | Evening (1) | night |
| Late Night | 01:00 – 03:00 | Evening (1) | night |
| Early Morning | 03:00 – sunrise | Early Morning (2) | night |
| Morning | sunrise – 10:00 | Morning (3) | day |
| Daytime | 10:00 – 12:00 | Daytime (4) | day |
| Afternoon | 12:00 – sunset | Afternoon (5) | afternoon |
| Evening | sunset – dusk (+22 min) | Evening (6) | afternoon |
| Night | dusk – midnight | Evening (1) | night |

Câu trả lời gốc: P5 chỉ có 6 ToD images, Midnight/Late Night/Night dùng Evening (1).

---

## Wallpaper automation

- **3 render variants**: `day`, `afternoon`, `night`.
- Default assets: `~/.config/noctalia/p5-rice/p5wallpaper/p5city{day,afternoon,night}.png`.
- Kiểm tra mỗi **1 phút**; chỉ đổi wallpaper nếu variant đổi.
- Gọi `noctalia msg wallpaper-set <path>` (sync; retry nếu Noctalia chưa ready).
- Chỉ chạy khi profile là `persona`.
- Owner: `p5wallpaper` static library.

---

## D-Bus / IPC

- Control interface: `dev.noctalia.P5Rice` trên `/dev/noctalia/P5Rice`.
- Methods: `Notify`, `GetProfile` (always "persona"), `DismissAll`, `SetMusicVisible`, `ToggleMusic`, `SetCalendarVisible`, `ToggleCalendar`, `PlayPause`, `Next`, `Prev`.
- `p5-rice-ctl status` → `{"running":true}` (không có profile field).
- Noctalia notification server owns `org.freedesktop.Notifications`.
- `p5-rice-daemon` **không own** `org.freedesktop.Notifications`; chỉ dùng `BecomeMonitor`.
- Noctalia's notification server reachable via `org.freedesktop.Notifications on /org/freedesktop/Notifications`; `GetServerInformation` trả `"noctalia" "noctalia-dev" "5.4.1"`.

---

## Noctalia plugin

- ID: `okarin/persona-plugin` — author `okarin`, hiển thị với tên "P5 Plugin" (đổi từ `schiszo/persona-rice` Nov 2026; installer migrate folder + disable id cũ). Previous folder was `p5-rice`, trước đó nữa là `persona-rice`.
- Plugin categories: 1 service, 1 bar widget (shows P5), 1 IPC plugin.

### Plugin settings (18 total)

| Setting | Type | Default | Contents |
|---|---|---|---|
| `music_widget_enabled` | bool | true | Enable/disable music |
| `calendar_widget_enabled` | bool | true | Enable/disable calendar |
| `respect_noctalia_dnd` | bool | true | Suppress P5 banners while DND on |
| `notification_timeout_seconds` | int | 0 (auto) | Banner hiển thị N giây (1–120, 0 = auto theo app); đổi setting → plugin service restart daemon tự động |
| `notification_scratchpad_block` | bool | true | Click-to-open bị chặn khi window của sender đang ẩn trong Umbriel scratchpad |
| `notification_volume` | int | 100 | Âm lượng sound của banner P5 (0–100, 0 = silent); đổi setting → restart daemon tự động (nằm trong signature) |
| `notification_banner` | file | — | Notification banner image |
| `notification_sound` | file | — | Notification sound |
| `notification_font` | file | — | Notification title font |
| `ui_font` | file | — | Shared notification body / music UI font |
| `music_font` | file | — | Jitter music-button font |
| `music_banner` | file | — | Music widget banner |
| `music_character_frame` | file | — | Character portrait frame |
| `music_character_name` | text | — | Name tag text |
| `calendar_assets_dir` | folder | — | Folder with Day/Month/Week/ToD/Weather (or parent with Assets/) |
| `wallpaper_auto_enabled` | bool | true | Enable automatic wallpaper switching |
| `wallpaper_day` | file | — | Morning/day wallpaper |
| `wallpaper_afternoon` | file | — | Afternoon/evening wallpaper |
| `wallpaper_night` | file | — | Night wallpaper |

### `assets.conf` bridge

- Được ghi vào `~/.config/noctalia/p5-rice/assets.conf` bởi Noctalia plugin service.
- Daemon đọc file này **một lần lúc khởi động**.
- Service chỉ restart `p5-widgets.service` khi **signature thay đổi** (asset paths + `respect_noctalia_dnd` / `notification_timeout_seconds` / `notification_scratchpad_block` / `notification_volume` / `wallpaper_auto_enabled`).
- `applyCalendar` / `applyMusic` chỉ spawn `p5-rice-ctl` khi trạng thái thay đổi (save `lastCalendarSent` / `lastMusicSent`), chống double-init bằng guard `initialized`; khi daemon bị restart, 2 state này reset để re-apply visibility.
- Noctalia plugin service là nguồn; edit assets.conf manual sẽ bị overwrite.
- `~` và absolute paths đều hỗ trợ.
- `calendar_assets_dir` chấp nhận folder chứa Day/Month/... **hoặc** parent chứa `Assets/`.

### Noctalia plugin notifications

- Daemon called `noctalia.runAsync` để sync settings bằng `p5-rice-ctl`.
- IPC service render: `calendar on|off`, `toggle-calendar`, `toggle-music`.
- Noctalia plugin `env` không hỗ trợ notifications; notification chỉ từ D-Bus.

---

## Asset overrides (Settings → Plugins)

Bảng asset settings và hành vi:

- Trống = dùng asset mặc định bundled.
- `notification_banner` = ảnh banner w700.
- `music_character_frame` = ảnh frame 352×272 pre-scaled.
- `calendar_assets_dir` = folder chứa Day/Month/..., hoặc parent chứa `Assets/`.
- `wallpaper_*` = 3 ảnh wallpaper.
- Asset config parser đọc `assets.conf` theo format `key=value` (comment `#`).
- `~` expansion + relative paths resolve against `~/.config/noctalia/p5-rice`.
- **Noctalia plugin service** là nguồn, **restart daemon chỉ khi asset path thay đổi**.

---

## Layer namespaces

| Surface | Namespace | Layer | Anchor |
|---|---|---|---|
| Notifications | `noctalia-p5notify` | overlay | top-right |
| Music | `noctalia-desktop-widget-p5music` | bottom | top-left |
| Calendar | `noctalia-p5calendar` | bottom | bottom-right (configurable) |

Umbriel layer rule (optional, bundled template):
```toml
[[layer_rule]]
match.namespace = "^noctalia-(p5notify|p5calendar|desktop-widget-p5music)$"
blur = false
```

---

## Design decisions (user preferences)

User đã chọn cụ thể; **đừng revert các lựa chọn này**:

1. **Notification server**: Noctalia giữ notification server; widget chỉ monitor. Không phải takeover.
2. **Notification**: chỉ hiện **từng cái một**, không stack nhiều banner cùng lúc.
3. **Music button spacing**: giữ original wide spacing (không compact).
4. **Album art**: **square cover crop** (đã revert contain/backdrop version).
5. **Thumbnail resolution**: giữ nguyên **150×83** (đã bỏ high-res fetch logic).
6. **Tag box (notification)**: giữ original **small** (`kTagW=0.15`, không widen).
7. **Presets**: chỉ có **persona** (đã xoá saber/elden-ring).
8. **Backup retention**: chỉ giữ **2 backup mới nhất**.
9. **Tag extension asset**: đã revert (banner2.png original, kTagW=0.15).
10. **Notification queue**: không stack; queue tuần tự với 8-card cap.
11. **Noctalia**: notification server enabled; Noctalia's notification history hoạt động.
12. **DND**: gate by `respect_noctalia_dnd` (default true).
13. **Asset management**: mọi asset đường dẫn trong **Settings → Plugins → P5 Rice**.
14. **Wallpaper**: automatic time-based switching.
15. **Tool**: no Qt/QML/Quickshell; native C++23.
16. **Click-to-open**: banner text mở app gửi; **scratched window sẽ bị block** (`notification_scratchpad_block` default true).
17. **No legacy takeover** (Nov 2026): `--notify-takeover` và toàn bộ fd-server code đã bị xoá hoàn toàn — daemon chỉ monitor Noctalia.
18. **Perf-first**: MPRIS poll adaptive (1s playing / 5s idle) + calendar fast-path; không thêm polling mới cho feature nào.

---

## Cleanup đã hoàn thành

Đã xoá:

- `dms/` — QML/Quickshell legacy.
- `mango/` — config compositor cũ.
- `--notify-takeover` + toàn bộ fd-server D-Bus code (Nov 2026 — daemon chỉ monitor).
- `notify-send`/`libnotify` khỏi installer deps (daemon không dùng; giữ pw-play PipeWire).
- `bin/p5-profile-switcher` — profile switching (chỉ persona).
- `bin/dms-profile-notify` — DMS notification helper.
- `assets/p5music/assets/sumire.png` — asset không dùng.
- `userscripts/` — high-res YouTube artwork (user reverted decision).
- Toàn bộ fallback DMS trong code (dms profile, dms/plugins asset path).

---

## Pending / không hoàn thành

- ~~Xoá nhánh `main`~~ OBSOLETE: repo đã dùng `main` làm default branch (local + GitHub) — thông tin "chuyển sang master" cũ đã hết hạn.
- Notification actions (Reply) chưa xử lý trên P5 banner; Noctalia giữ actions bên trong. Phần "Open" đã có: click-to-open sender app (Nov 2026) + scratchpad blocker — InvokeAction/Reply per-action vẫn cần màn hình riêng nếu user muốn.
- Feature 2 (được user đề cập nhưng chưa mô tả) — chờ user nêu chi tiết.

---

## Troubleshooting

### Calendar không hiện

```bash
systemctl --user status p5-widgets.service
umbriel layers | grep p5calendar
p5-rice-ctl calendar on
```

Layer `BOTTOM` chèn dưới các cửa sổ — check empty workspace.

### Notification không hiện

```bash
noctalia msg config-get notification.enable_daemon
# expected: true
busctl --user list | grep org.freedesktop.Notifications
# expected: noctalia PID, không phải p5-rice-daemon
systemctl --user restart p5-widgets.service
```

### Weather icon không hiện

```bash
noctalia msg config-set weather.enabled true
noctalia msg config-set location.auto_locate true
stat ~/.cache/noctalia/weather.json
```

### Wallpaper không đổi

```bash
noctalia msg wallpaper-get
grep wallpaper ~/.config/noctalia/p5-rice/assets.conf
grep respect_noctalia_dnd ~/.config/noctalia/p5-rice/assets.conf
# respect must be true (hoặc not present)
```

### Asset setting không apply

```bash
cat ~/.config/noctalia/p5-rice/assets.conf
systemctl --user restart p5-widgets.service
```

### DND bật nhưng banner vẫn hiện

```bash
noctalia msg notification-dnd-status
grep respect_noctalia_dnd ~/.config/noctalia/p5-rice/assets.conf
# respect must be true
```

### Notification sound quá to / nhỏ / không có tiếng

```bash
grep notification_volume ~/.config/noctalia/p5-rice/assets.conf
# 0-100; đổi setting trong Noctalia sẽ tự ghi lại + restart daemon (nằm trong signature)
# test thủ công thứ tự player mà daemon dùng (volume 50%):
pw-play --volume=0.50 ~/.config/noctalia/p5-rice/p5notify/sounds/papersound.wav \
  || paplay --volume=32768 ~/.config/noctalia/p5-rice/p5notify/sounds/papersound.wav
```

### CodeGraph stale

Source tree có `app/`, `core/`, `ipc/`, `notifications/`, `music/`, `calendar/`, `weather/`, `wallpaper/`. Re-run CodeGraph index command để update symbol paths.

---

## Files quan trọng

| File / Path | Purpose |
|---|---|
| `~/.config/noctalia/p5-rice/assets.conf` | Asset/DND/timeout/volume/wallpaper overrides |
| `~/.config/noctalia/p5-rice/p5notify/banner2.png(.bak)` | Notification banner art + backup original |
| `~/.config/noctalia/p5-rice/p5notify/fonts/PersonifiedUXRegular.ttf` | Notification body font (Vietnamese) |
| `~/.config/noctalia/p5-rice/p5music/fonts/p5font.ttf` | Music button font |
| `~/.config/noctalia/p5-rice/p5calendar/Assets/` | Layered calendar art |
| `~/.config/noctalia/p5-rice/p5wallpaper/` | P5 City wallpapers |
| `~/.local/share/noctalia/plugins/p5-plugin/` | Noctalia v5 plugin |
| `~/.local/share/noctalia/plugins/p5-plugin/service.luau` | Asset/DND/wallpaper bridge to daemon |
| `~/.local/share/noctalia/plugins/p5-plugin/widget.luau` | Noctalia bar widget |
| `~/.config/systemd/user/p5-widgets.service` | systemd unit |
| `~/.local/bin/p5-rice-daemon` | Main binary |
| `~/.local/bin/p5-rice-ctl` | Control CLI |
| `~/.cache/noctalia/weather.json` | Noctalia weather cache |
| `$XDG_CACHE_HOME/noctalia` | Noctalia cache home |

---

## D-Bus interfaces

| Interface | Bus |
|---|---|
| `dev.noctalia.P5Rice` | Notification/Control bus |
| `org.freedesktop.Notifications` | Noctalia notification server |
| `org.mpris.MediaPlayer2.*` | Noctalia MPRIS service |
| `dev.noctalia.Mpris` | Noctalia MPRIS bridge (GetActivePlayer, GetPlayers) |

---

## Dependencies (Arch packages)

```bash
sudo pacman -S --needed \
    meson ninja pkgconf wayland wayland-protocols \
    cairo pango fontconfig freetype2 gdk-pixbuf2 \
    systemd-libs curl \
    pipewire
# Required compositor:
#   noctalia (v5+), umbriel-git
```

---

## Credits

- Original Persona-rice inspiration: [JDevera101/Persona-rice](https://github.com/JDevera101/Persona-rice).
- Calendar art: [Mive82/Persona-5-Calendar](https://github.com/Mive82/Persona-5-Calendar), WTFPL license (`assets/p5calendar/LICENSE`).
- P5 is a trademark of Atlus/SEGA. Fan-made, non-commercial.
- Built for [Noctalia](https://github.com/noctalia-dev/noctalia) and [Umbriel](https://github.com/noctalia-dev/umbriel).
