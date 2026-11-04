-- Noctalia background service bridge for p5-rice-daemon (C++23 Wayland Layer-Shell) (C++23 Wayland Layer-Shell)
--
-- Responsibilities:
--   * mirror visual settings into the daemon through p5-rice-ctl;
--   * persist asset-path overrides to ~/.config/noctalia/p5-rice/assets.conf;
--   * restart the daemon only when those asset paths actually change.
--
-- The native daemon reads assets.conf once at startup. Keeping the write and
-- restart here means the Settings -> Plugins UI can manage every asset without
-- hard-coded filenames.

local musicEnabled = true
local calendarEnabled = true
local lastAssetSignature = nil
local initialized = false
local lastMusicSent = nil
local lastCalendarSent = nil

local stringAssetKeys = {
    "notification_banner",
    "notification_sound",
    "notification_font",
    "ui_font",
    "music_font",
    "music_banner",
    "music_character_frame",
    "music_character_name",
    "calendar_assets_dir",
    "wallpaper_day",
    "wallpaper_afternoon",
    "wallpaper_night",
}

local assetSettings = {
    respect_noctalia_dnd = true,
    notification_timeout_seconds = 0,
    notification_scratchpad_block = true,
    notification_volume = 100,
    notification_banner = "",
    notification_sound = "",
    notification_font = "",
    ui_font = "",
    music_font = "",
    music_banner = "",
    music_character_frame = "",
    music_character_name = "",
    calendar_assets_dir = "",
    wallpaper_auto_enabled = true,
    wallpaper_day = "",
    wallpaper_afternoon = "",
    wallpaper_night = "",
}

local function syncSettings()
    local mus = noctalia.getConfig("music_widget_enabled")
    if type(mus) == "boolean" then
        musicEnabled = mus
    end

    local cal = noctalia.getConfig("calendar_widget_enabled")
    if type(cal) == "boolean" then
        calendarEnabled = cal
    end

    for _, key in ipairs(stringAssetKeys) do
        local value = noctalia.getConfig(key)
        assetSettings[key] = type(value) == "string" and value or ""
    end

    local dnd = noctalia.getConfig("respect_noctalia_dnd")
    assetSettings.respect_noctalia_dnd = type(dnd) == "boolean" and dnd or true

    local timeout = tonumber(noctalia.getConfig("notification_timeout_seconds")) or 0
    timeout = math.floor(timeout)
    if timeout < 0 then
        timeout = 0
    elseif timeout > 120 then
        timeout = 120
    end
    assetSettings.notification_timeout_seconds = timeout

    local volume = tonumber(noctalia.getConfig("notification_volume")) or 100
    volume = math.floor(volume)
    if volume < 0 then
        volume = 0
    elseif volume > 100 then
        volume = 100
    end
    assetSettings.notification_volume = volume

    local scratchpad = noctalia.getConfig("notification_scratchpad_block")
    assetSettings.notification_scratchpad_block = type(scratchpad) == "boolean" and scratchpad or true

    local wallpaper = noctalia.getConfig("wallpaper_auto_enabled")
    assetSettings.wallpaper_auto_enabled = type(wallpaper) == "boolean" and wallpaper or true
end

local function applyCalendar()
    local wanted = calendarEnabled and "on" or "off"
    if lastCalendarSent == wanted then
        return
    end
    lastCalendarSent = wanted
    noctalia.runAsync("p5-rice-ctl calendar " .. wanted)
end

local function applyMusic()
    local wanted = musicEnabled and "on" or "off"
    if lastMusicSent == wanted then
        return
    end
    lastMusicSent = wanted
    noctalia.runAsync("p5-rice-ctl music " .. wanted)
end



local function configHome()
    local xdg = noctalia.getenv("XDG_CONFIG_HOME")
    if type(xdg) == "string" and xdg ~= "" then
        return xdg
    end
    return noctalia.expandPath("~/.config")
end

local function clean(value)
    return (tostring(value or ""):gsub("[\r\n]", " "))
end

local function assetConfigPath()
    return configHome() .. "/noctalia/p5-rice/assets.conf"
end

local function assetSignature()
    local parts = {}
    for _, key in ipairs(stringAssetKeys) do
        parts[#parts + 1] = clean(assetSettings[key])
    end
    parts[#parts + 1] = assetSettings.respect_noctalia_dnd and "true" or "false"
    parts[#parts + 1] = tostring(assetSettings.notification_timeout_seconds)
    parts[#parts + 1] = assetSettings.notification_scratchpad_block and "true" or "false"
    parts[#parts + 1] = tostring(assetSettings.notification_volume)
    parts[#parts + 1] = assetSettings.wallpaper_auto_enabled and "true" or "false"
    return table.concat(parts, "\30")
end

local function assetFileContent()
    local lines = {
        "# Managed by okarin/persona-plugin. Prefer Settings -> Plugins.",
        "respect_noctalia_dnd=" .. (assetSettings.respect_noctalia_dnd and "true" or "false"),
        "notification_timeout_seconds=" .. tostring(assetSettings.notification_timeout_seconds),
        "notification_scratchpad_block=" .. (assetSettings.notification_scratchpad_block and "true" or "false"),
        "notification_volume=" .. tostring(assetSettings.notification_volume),
        "notification_banner=" .. clean(assetSettings.notification_banner),
        "notification_sound=" .. clean(assetSettings.notification_sound),
        "notification_font=" .. clean(assetSettings.notification_font),
        "ui_font=" .. clean(assetSettings.ui_font),
        "music_font=" .. clean(assetSettings.music_font),
        "music_banner=" .. clean(assetSettings.music_banner),
        "music_character_frame=" .. clean(assetSettings.music_character_frame),
        "music_character_name=" .. clean(assetSettings.music_character_name),
        "calendar_assets_dir=" .. clean(assetSettings.calendar_assets_dir),
        "wallpaper_auto_enabled=" .. (assetSettings.wallpaper_auto_enabled and "true" or "false"),
        "wallpaper_day=" .. clean(assetSettings.wallpaper_day),
        "wallpaper_afternoon=" .. clean(assetSettings.wallpaper_afternoon),
        "wallpaper_night=" .. clean(assetSettings.wallpaper_night),
        "",
    }
    return table.concat(lines, "\n")
end

local function writeAssetConfig(restartOnChange)
    local path = assetConfigPath()
    local content = assetFileContent()
    local signature = assetSignature()
    local existing = noctalia.readFile(path)

    if existing == content then
        lastAssetSignature = signature
        return
    end

    local dir = path:match("^(.*)/[^/]+$")
    if dir and dir ~= "" then
        noctalia.mkdirAll(dir)
    end

    local ok, err = noctalia.writeFile(path, content)
    if not ok then
        noctalia.notifyError("P5 Plugin", "Could not write assets.conf: " .. tostring(err))
        return
    end

    local settingsChanged = lastAssetSignature ~= nil and signature ~= lastAssetSignature
    lastAssetSignature = signature

    if restartOnChange and (settingsChanged or existing == nil) then
        -- The daemon applies assets.conf once at startup, so a restart makes
        -- the freshly written values live. Widget visibility defaults are also
        -- reset by the restart: forget what we last sent so the next sync
        -- re-applies the current on/off state to the new daemon instance.
        lastCalendarSent = nil
        lastMusicSent = nil
        noctalia.runAsync("systemctl --user restart p5-widgets.service")
    end
end

local function syncAll()
    syncSettings()
    writeAssetConfig(true)
    applyCalendar()
    applyMusic()
    initialized = true
end

-- Service entries run this chunk at load time; init() is invoked again right
-- after by Noctalia, so guard to avoid a duplicated sync + ctl fork burst.
syncAll()

function init()
    if not initialized then
        syncAll()
    end
end

function onConfigChanged()
    syncSettings()
    writeAssetConfig(true)
    applyCalendar()
    applyMusic()
end

-- Older Noctalia builds used this callback name; keep it as an alias.
function onSettingChanged(_key, _value)
    onConfigChanged()
end

function onIpc(event, payload)
    if event == "toggle-music" then
        musicEnabled = not musicEnabled
        applyMusic()
    elseif event == "calendar" and type(payload) == "string" then
        if payload == "on" or payload == "off" then
            calendarEnabled = (payload == "on")
            applyCalendar()
        end
    elseif event == "toggle-calendar" then
        calendarEnabled = not calendarEnabled
        applyCalendar()
    end
end
