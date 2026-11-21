-- description: HD-2D look for Pokémon White (and Black): a mild tilt-shift, bloom and vignette in the overworld. Text keeps only the vignette, battles and menus clear it all. Hold L, tap R: off/auto.
-- option: blur type=number min=0 max=1 step=0.05 default=0.6 label="Tilt-shift strength"
-- option: radius type=number min=1 max=12 step=1 default=6 label="Tilt-shift radius in DS pixels"
-- option: bloom type=number min=0 max=2 step=0.05 default=0.6 label="Bloom strength"
-- option: threshold type=number min=0 max=1 step=0.05 default=0.8 label="Bloom threshold"
-- option: bloom_radius type=number min=1 max=8 step=1 default=3 label="Bloom radius in DS pixels"
-- option: vignette type=number min=0 max=1 step=0.05 default=0.35 label="Vignette strength"
-- option: fade type=number min=1 max=120 step=1 default=30 label="Fade length in frames"

-- Where the overworld is comes from melon.game.state(), the app's native field read, which knows Black
-- and White. tilt_shift_white2.lua reads White 2's own structures instead (its addresses are IRDO's,
-- so they don't carry over to White), which tells more apart, like which half a text box is on; this
-- one keeps to the coarse scene: field, text, battle, fade, menu or off.

local MESSAGE_FRAMES = 60
local WRONG_GAME_FRAMES = 180

local blur_options = {
    center = 0.5,
    width = 0.35,
    radius = melon.options.radius,
    saturation = 1.15,
    contrast = 1.05,
}
local bloom_options = {
    threshold = melon.options.threshold,
    radius = melon.options.bloom_radius,
}
local vignette_options = { size = 0.6 }

-- Each effect fades from 0 to its full strength (the option), at the same speed whatever that is
local current = { blur = 0, bloom = 0, vignette = 0 }
local target = { blur = 0, bloom = 0, vignette = 0 }
local should_snap = false

local is_automatic = true
local was_l_held = false
local was_r_held = false
local message = nil
local message_frames = 0
local wrong_game_frames = WRONG_GAME_FRAMES

local function set_targets(state)
    -- A fade is a door or a map change: keep what the scene had, so it doesn't flicker on every door
    if state == "fade" then
        return
    end
    local is_field = state == "field"
    target.blur = is_field and melon.options.blur or 0
    target.bloom = is_field and melon.options.bloom or 0
    -- White text boxes would glow and blur, so text keeps only the vignette
    target.vignette = (is_field or state == "text") and melon.options.vignette or 0
end

local function move_towards(level, goal, full)
    local step = math.max(full, 0.05) / melon.options.fade
    if level < goal then
        return math.min(level + step, goal)
    end
    return math.max(level - step, goal)
end

function on_state_loaded()
    -- The first frame after a load already looks right, without a fade
    should_snap = true
end

function on_frame()
    local state = melon.game.state()
    if state == nil then
        if wrong_game_frames > 0 then
            wrong_game_frames = wrong_game_frames - 1
            melon.draw.text("bottom", 4, 4, "HD-2D: Pokemon Black or White only", 0xFFFFFFFF, 0xC0000000)
        end
        return
    end

    local held = melon.input.held()
    local is_l_held = held & melon.KEY.L ~= 0
    local is_r_held = held & melon.KEY.R ~= 0
    local is_l_tapped = is_l_held and not was_l_held
    local is_r_tapped = is_r_held and not was_r_held
    was_l_held = is_l_held
    was_r_held = is_r_held
    if is_l_held and is_r_tapped and not is_l_tapped then
        is_automatic = not is_automatic
        message = is_automatic and "HD-2D automatic" or "HD-2D off"
        message_frames = MESSAGE_FRAMES
    end

    set_targets(state)
    for name, level in pairs(current) do
        local goal = is_automatic and target[name] or 0
        current[name] = should_snap and goal or move_towards(level, goal, melon.options[name])
    end
    should_snap = false

    -- Called every frame: only frames where a value changes cost an event
    melon.screen.tilt_shift(current.blur, current.blur, blur_options)
    melon.screen.bloom(current.bloom, bloom_options)
    melon.screen.vignette(current.vignette, vignette_options)

    if message_frames > 0 then
        message_frames = message_frames - 1
        melon.draw.text("bottom", 4, 4, message, 0xFFFFFFFF, 0xC0000000)
    end
end
