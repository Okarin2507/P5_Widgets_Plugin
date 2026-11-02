-- Noctalia background service bridge for p5-rice-daemon (C++23 Wayland Layer-Shell)

local currentProfile = "persona"
local musicEnabled = true

local function syncSettings()
    local prof = noctalia.getConfig("default_profile")
    if type(prof) == "string" and prof ~= "" then
        currentProfile = prof
    end
    local mus = noctalia.getConfig("music_widget_enabled")
    if type(mus) == "boolean" then
        musicEnabled = mus
    end
end

function init()
    syncSettings()
end

function onSettingChanged(key, _value)
    if key == "default_profile" or key == "music_widget_enabled" then
        syncSettings()
    end
end

function onIpc(event, payload)
    if event == "profile" and type(payload) == "string" and payload ~= "" then
        currentProfile = payload
        noctalia.notify("Persona Rice", "Switched active profile to " .. currentProfile)
    elseif event == "toggle-profile" then
        if currentProfile == "persona" then
            currentProfile = "saber"
        elseif currentProfile == "saber" then
            currentProfile = "elden-ring"
        else
            currentProfile = "persona"
        end
        noctalia.notify("Persona Rice", "Switched active profile to " .. currentProfile)
    elseif event == "toggle-music" then
        musicEnabled = not musicEnabled
    end
end
