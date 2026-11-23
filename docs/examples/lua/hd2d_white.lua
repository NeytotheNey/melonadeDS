-- description: HD-2D look for Pokémon White (and Black): sun shadows, shading, depth of field, haze, bloom with glowing windows at night, and vignette in the overworld (the sun shadows, shading, depth of field and haze need the OpenGL renderer; the tilt-shift is a fake one for the others). Text keeps the sun shadows, shading, depth of field, haze and vignette, battles and menus clear it all. Hold L, tap R: off/auto.
-- section: Light and shadow
-- option: sun type=number min=0 max=1 step=0.05 default=0.6 label="Sun shadow strength"
-- option: sun_length type=number min=32 max=160 step=8 default=96 label="Sun shadow length"
-- option: shade type=number min=0 max=1 step=0.05 default=0.35 label="Shading strength"
-- section: Depth of field
-- option: dof type=number min=0 max=1 step=0.05 default=1 label="Strength"
-- option: dof_radius type=number min=1 max=12 step=1 default=4 label="Radius in DS pixels"
-- section: Atmosphere
-- option: haze type=number min=0 max=1 step=0.05 default=0.6 label="Haze strength"
-- option: vignette type=number min=0 max=1 step=0.05 default=0.35 label="Vignette strength"
-- section: Bloom
-- option: bloom type=number min=0 max=2 step=0.05 default=0.6 label="Strength"
-- option: threshold type=number min=0 max=1 step=0.05 default=0.8 label="Threshold"
-- option: bloom_radius type=number min=1 max=8 step=1 default=3 label="Radius in DS pixels"
-- section: Tilt-shift
-- option: blur type=number min=0 max=1 step=0.05 default=0 label="Strength"
-- option: radius type=number min=1 max=12 step=1 default=6 label="Radius in DS pixels"
-- section: Transitions
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
-- Lit windows glow by their brightest channel, since they're orange and their luma stays under the
-- threshold. The game turns them on through a light color: their brightest channel is at most 0.51 at
-- noon and 0.62 at 16:00, 0.78 in the evening and 0.94 from 20:00, so 0.65 leaves the day's windows
-- alone. While lit they stay lit under the sun shadows and the shading
local bloom_options = {
    threshold = melon.options.threshold,
    radius = melon.options.bloom_radius,
    lights = true,
    light_threshold = 0.65,
    light_strength = 2,
}

-- White's (and Black's) lit windows, by content hash like GAME_SHADOW_TEXTURES below. The texture is the
-- same by day, so it's always on the list; the light color decides whether they glow
local LIGHT_TEXTURES = { 0x84D7B868 }
local vignette_options = { size = 0.6 }
-- The far end of a street, past about 0.0005 behind the player's depth; the player and the roofs nearer the camera stay clear
local haze_options = { color = 0xDCE6F0, start = 0.9968, falloff = 0.0008 }
-- Focused on the player wherever she is on screen, through her sprite sheet's texture (FOCUS_TEXTURES
-- below). Indoors the camera looks ahead of her and stops at the room's edge, so she isn't at the center.
-- Without her on screen it's the screen's center, where she stands in the field. The range and falloff
-- are relative to her distance from the camera (the defaults), so they suit the Z-buffered field and the
-- W-buffered rooms alike
local dof_options = {
    radius = melon.options.dof_radius,
    focus_x = 128,
    focus_y = 96,
    range = 0.045,
    falloff = 0.33,
}

-- The player's sprite sheet, the only 32 x 1024 character texture, by content hash from the desktop's
-- --list-textures. The same walking, running, at night, after a state load and indoors
local FOCUS_TEXTURES = {
    0xA762276B, -- White, the female player
    -- The male player's hash isn't known yet
}

-- Shadows from a sun in the north-west, 45 degrees up, in the view space of White's field camera (a tile is
-- 16 units, the tallest roofs about 90). The game's own shadows fall south-east, so this one does too. up is
-- for the characters' own shadows, which the renderer draws standing on their feet. The length is the slider,
-- 96 by default, long enough for a roof's shadow to cross the street. The steps and the guard grow with it,
-- since a longer march reaches farther off screen: at 96 it reaches about 60 DS pixels right and 100 up, so
-- 64 is enough there. Longer shadows cost more on phones, since the guard band renders more
local sun_length = melon.options.sun_length
local sun_reach = math.floor(sun_length * 2 / 3 + 0.5)
local sun_options = {
    direction = { -0.50, 0.83, 0.26 },
    up = { 0.0006, 0.607, 0.794 },
    length = sun_length,
    steps = math.min(128, math.max(16, sun_reach)),
    bias = 1,
    softness = 1.5,
    color = 0x33384F,
    -- Roofs this far past the top and right edges still cast (64 DS pixels at the default length), so
    -- shadows don't grow in while walking
    guard = math.min(128, sun_reach),
}

-- A speech bubble's tail is the game's UI drawn in 3D, right in front of the camera (about 65 units away,
-- where the town is 91 or more). As 3D it would cast a long sun shadow across the street, and the shading
-- would darken the ground beside it. Its texture's content hash, from the desktop's --list-textures, is the
-- same for every bubble, so the effects take its pixels as 2D, like the box's
local UI_TEXTURES = { 0xBE2606E4 }

-- While the sun shadows are on, the game's own shadows go, or there'd be two of everything. All are
-- translucent textures drawn over the ground, by content hash from the desktop's --list-textures, which
-- stays the same wherever the game loads them in VRAM. Hiding them only lightens the ground they cover
local GAME_SHADOW_TEXTURES = {
    -- The dark ovals under the player and the NPCs (16 x 16, four colors, alpha 19, polygon ID 2)
    0xC9FDE257,
    -- Buildings, the Pokémon Center, the gym and signs
    0xB5B35936,
    -- Railings
    0x0FE21474,
    -- Trees, one per season: Spring, Summer, Autumn, Winter
    0xDF404B5E,
    0xF2C1055E,
    0x14620641,
    0x38B6693C,
    -- Under the furniture inside the Pokémon Center
    0xCB6BD3DD,
}
local NO_TEXTURES = {}

-- Soft shadows where something stands in front of what's behind it: along walls, inside roof rims, around
-- trees and the player. A smaller range in raw depth draws dark outlines around everything instead
local shade_options = {
    radius = 8,
    range = 0.0008,
    color = 0x33384F,
}

-- Each effect fades from 0 to its full strength (the option), at the same speed whatever that is
local current = { dof = 0, sun = 0, shade = 0, haze = 0, blur = 0, bloom = 0, vignette = 0 }
local target = { dof = 0, sun = 0, shade = 0, haze = 0, blur = 0, bloom = 0, vignette = 0 }
local should_snap = false

local is_automatic = true
local was_l_held = false
local was_r_held = false
local message = nil
local message_frames = 0
local wrong_game_frames = WRONG_GAME_FRAMES

local function set_targets(state)
    -- A fade is a door or a map change, through black or a cross-fade from the old map's last frame: keep
    -- what the scene had, so it doesn't flicker on every door
    if state == "fade" then
        return
    end
    local is_field = state == "field"
    local is_text = state == "text"
    target.blur = is_field and melon.options.blur or 0
    target.bloom = is_field and melon.options.bloom or 0
    -- White text boxes would glow and take the tilt-shift's blur, so text keeps only the vignette, and the
    -- sun shadows, shading, depth of field and haze, which leave 2D as it is
    target.dof = (is_field or is_text) and melon.options.dof or 0
    target.sun = (is_field or is_text) and melon.options.sun or 0
    target.shade = (is_field or is_text) and melon.options.shade or 0
    target.haze = (is_field or is_text) and melon.options.haze or 0
    target.vignette = (is_field or is_text) and melon.options.vignette or 0
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
    melon.screen.sun(current.sun, sun_options)
    -- The game's own shadows go while the sun is on, and through a fade while it's wanted: a door's
    -- cross-fade shows the new map before the sun comes back
    local hides_game_shadows = current.sun > 0 or (state == "fade" and is_automatic and melon.options.sun > 0)
    melon.gpu.hide_textures(hides_game_shadows and GAME_SHADOW_TEXTURES or NO_TEXTURES)
    melon.gpu.light_textures(LIGHT_TEXTURES)
    melon.gpu.ui_textures(UI_TEXTURES)
    melon.gpu.focus_textures(FOCUS_TEXTURES)
    melon.screen.shade(current.shade, shade_options)
    melon.screen.depth_of_field(current.dof, dof_options)
    melon.screen.haze(current.haze, haze_options)
    melon.screen.tilt_shift(current.blur, current.blur, blur_options)
    melon.screen.bloom(current.bloom, bloom_options)
    melon.screen.vignette(current.vignette, vignette_options)

    if message_frames > 0 then
        message_frames = message_frames - 1
        melon.draw.text("bottom", 4, 4, message, 0xFFFFFFFF, 0xC0000000)
    end
end
