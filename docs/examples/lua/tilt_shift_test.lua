-- description: Tilt-shift blur test. Hold L and tap R to toggle the blur; hold R and tap L to toggle the bottom band, like a dialog box would.
-- option: strength type=number min=0 max=1 step=0.05 default=1 label="Blur strength"
-- option: center type=number min=0 max=1 step=0.05 default=0.5 label="Sharp band center"
-- option: width type=number min=0 max=1 step=0.05 default=0.3 label="Sharp band height"
-- option: radius type=number min=1 max=12 step=1 default=6 label="Blur radius in DS pixels"
-- option: saturation type=number min=0 max=2 step=0.05 default=1.3 label="Saturation"
-- option: contrast type=number min=0 max=2 step=0.05 default=1.1 label="Contrast"
-- option: fade type=number min=1 max=120 step=1 default=30 label="Fade length in frames"

local MESSAGE_FRAMES = 60

local blur_options = {
    center = melon.options.center,
    width = melon.options.width,
    radius = melon.options.radius,
    saturation = melon.options.saturation,
    contrast = melon.options.contrast,
}
local fade_step = 1 / melon.options.fade

-- Where each toggle is headed, and how far along the fade to it is
local blur_on = true
local bottom_on = true
local blur_level = 1
local bottom_level = 1

local was_l_held = false
local was_r_held = false

local message = "Blur on"
local message_frames = MESSAGE_FRAMES

local function show_message(text)
    message = text
    message_frames = MESSAGE_FRAMES
end

local function move_towards(level, is_on)
    if is_on then
        return math.min(level + fade_step, 1)
    end
    return math.max(level - fade_step, 0)
end

function on_frame()
    local held = melon.input.held()
    local is_l_held = held & melon.KEY.L ~= 0
    local is_r_held = held & melon.KEY.R ~= 0
    local is_l_tapped = is_l_held and not was_l_held
    local is_r_tapped = is_r_held and not was_r_held
    was_l_held = is_l_held
    was_r_held = is_r_held

    if is_l_held and is_r_tapped and not is_l_tapped then
        blur_on = not blur_on
        show_message(blur_on and "Blur on" or "Blur off")
    elseif is_r_held and is_l_tapped and not is_r_tapped then
        bottom_on = not bottom_on
        show_message(bottom_on and "Bottom band on" or "Bottom band off (dialog box)")
    end

    blur_level = move_towards(blur_level, blur_on)
    bottom_level = move_towards(bottom_level, bottom_on)

    -- Called every frame: only frames where a value changes cost an event
    local strength = melon.options.strength * blur_level
    melon.screen.tilt_shift(strength, strength * bottom_level, blur_options)

    if message_frames > 0 then
        message_frames = message_frames - 1
        melon.draw.text("bottom", 4, 4, message, 0xFFFFFFFF, 0xC0000000)
    end
end
