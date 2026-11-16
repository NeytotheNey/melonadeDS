-- description: WideMelon camera test. Shows the camera's x and zoom over the middle 256. Hold L and tap R to hold 1x; hold R and tap L to zoom all the way out from the script.
-- option: show_frame type=toggle default=true label="Outline the middle 256"

-- Needs widescreen and the Camera switch on in the Mods tab. The overlay follows the camera window, so the text and outline should stay on
-- the game's real 256 columns while the right stick pans and the Camera Zoom hotkey (the right stick's click) steps the zoom.

local holding_native = false
local script_zoom = false
local was_l_held = false
local was_r_held = false

local function toggle_hold()
    holding_native = not holding_native
    melon.wide.hold_native(holding_native)
end

local function toggle_zoom()
    script_zoom = not script_zoom
    -- Clamped to the widest view this game allows; nil hands the zoom back to the player
    melon.wide.set_zoom(script_zoom and 3 or nil)
end

function on_frame_end()
    local held = melon.input.held()
    local l_held = held & melon.KEY.L ~= 0
    local r_held = held & melon.KEY.R ~= 0
    if l_held and r_held and not was_r_held then
        toggle_hold()
    elseif r_held and l_held and not was_l_held then
        toggle_zoom()
    end
    was_l_held = l_held
    was_r_held = r_held

    local x, zoom = melon.wide.camera()
    local text = string.format("x %.1f  zoom %.2f", x, zoom)
    if holding_native then
        text = text .. "\nholding 1x"
    end
    if script_zoom then
        text = text .. "\nscript zoom"
    end
    melon.draw.text("top", 2, 2, text, 0xFFFFFFFF, 0x80000000)

    if melon.options.show_frame then
        melon.draw.rect("top", 0, 0, 256, 192, 0xFFFF00FF)
    end
end
