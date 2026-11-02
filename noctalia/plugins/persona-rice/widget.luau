-- Noctalia Bar Widget for Persona 5 Profile Indicator

local activeProfile = "persona"

local function refreshSettings()
    local prof = noctalia.getConfig("default_profile")
    if type(prof) == "string" and prof ~= "" then
        activeProfile = prof
    end
end

local function renderBar()
    local labelText = (activeProfile == "persona") and "P5" or activeProfile:upper()
    barWidget.render(ui.row({
        gap = 6,
        align = "center",
    }, {
        ui.label({
            text = labelText,
            fontWeight = "bold",
        }),
    }))
end

function init()
    refreshSettings()
    renderBar()
end

function onSettingChanged(_key, _value)
    refreshSettings()
    renderBar()
end

function onIpc(event, payload)
    if event == "profile" and type(payload) == "string" and payload ~= "" then
        activeProfile = payload
        renderBar()
    end
end
