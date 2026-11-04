-- Noctalia bar widget: P5 Plugin indicator.

local function renderBar()
    barWidget.render(ui.label({
        text = "P5",
        fontWeight = "bold",
    }))
end

renderBar()

function init()
    renderBar()
end

function onConfigChanged()
    renderBar()
end

-- Older Noctalia builds used this callback name.
function onSettingChanged(_key, _value)
    renderBar()
end
