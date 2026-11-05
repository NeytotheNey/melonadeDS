-- description: Tilt-shift blur for Pokémon Platinum US. On in the overworld, clears by itself elsewhere. Hold L, tap R: off/auto.
-- option: strength type=number min=0 max=1 step=0.05 default=1 label="Blur strength"
-- option: center type=number min=0 max=1 step=0.05 default=0.5 label="Sharp band center"
-- option: width type=number min=0 max=1 step=0.05 default=0.3 label="Sharp band height"
-- option: radius type=number min=1 max=12 step=1 default=6 label="Blur radius in DS pixels"
-- option: fade type=number min=1 max=120 step=1 default=30 label="Fade length in frames"
-- option: saturation type=number min=0 max=2 step=0.05 default=1.3 label="Saturation"
-- option: contrast type=number min=0 max=2 step=0.05 default=1.1 label="Contrast"
-- option: debug type=toggle default=false label="Show debug readout"

local MESSAGE_FRAMES = 60
local WRONG_GAME_FRAMES = 180
local MAX_TASK_STEPS = 8
local POPUP_FADE_FRAMES = 20
local MAP_COLUMNS = 32
local MAP_ENTRY_BYTES = 2

-- Addresses and offsets per game code. Function values include the Thumb bit.
local GAMES = {
    CPUE = {
        app_pointer = 0x02101D2C,
        app_main = 0x04,
        app_main_field = 0x0203CCCD,
        app_field_system = 0x1C,

        fs_process_manager = 0x00,
        fs_bg_config = 0x08,
        fs_task = 0x10,
        fs_ready = 0x68,

        pm_parent = 0x00,
        pm_child = 0x04,
        child_overlay = 0x0C,
        overlay_battle = 16,

        task_next = 0x00,
        task_func = 0x04,
        task_env = 0x0C,

        func_menu = 0x0203AC45,
        menu_substate = 0x2A,

        func_script = 0x0203E951,
        script_magic = 0x3643F,
        script_box = 0x08,

        -- Overlay func, at a fixed address: the Poké Mart task, above the script while its lists run
        func_mart = 0x0224CEAD,

        -- Tasks that run the transitions into and out of a battle
        func_battle = {
            [0x02050A75] = true,
            [0x02050B31] = true,
            [0x02050EE1] = true,
            [0x02051075] = true,
            [0x0205136D] = true,
            [0x020514E9] = true,
            [0x02050CA9] = true,
            [0x02050D4D] = true,
            [0x02051791] = true,
        },

        -- CANDIDATE: text on screen shows up as a non-zero tile in this BG map
        bg_map = 0x8C,
        map_tile = 0x518,
        tile_mask = 0x3FF,

        -- BG map rows that a window in the top area always crosses. Stops short of Yes/No at row 12.
        window_rows = { 1, 4, 7, 10 },

        -- Town name popup. State: 0 hidden, 1 sliding in, 3 showing, 2 sliding out
        fs_sub = 0x04,
        sub_popup = 0x08,
        popup_state = 0x04,
        popup_y = 0x0C,
    },
}

local game = GAMES[melon.game.code()]

local read8 = melon.mem.read8
local read16 = melon.mem.read16
local read32 = melon.mem.read32

local strength = melon.options.strength
-- A full fade takes the same time at any strength. At strength 0 there's nothing to fade.
local fade_amount = strength > 0 and strength or 1
local fade_step = fade_amount / melon.options.fade
-- The top band has to clear about as fast as the town name popup slides in
local popup_fade_step = fade_amount / math.min(melon.options.fade, POPUP_FADE_FRAMES)
local show_debug = melon.options.debug
local blur_options = {
    center = melon.options.center,
    width = melon.options.width,
    radius = melon.options.radius,
    saturation = melon.options.saturation,
    contrast = melon.options.contrast,
}

-- Hold L and tap R flips this between automatic and forced off
local is_automatic = true
local target_top = 0
local target_bottom = 0
local current_top = 0
local current_bottom = 0
local should_snap = false
local is_clearing_for_popup = false

local was_l_held = false
local was_r_held = false

local message = nil
local message_frames = 0
local wrong_game_frames = WRONG_GAME_FRAMES

-- What the last read_state() found, reused so there's no table per frame
local info = {}
local last_state = nil
local last_text = nil
local last_box = nil
local last_popup = nil

-- Reads a pointer: nil unless it's aligned and inside main RAM
local function ptr(addr)
    local p = read32(addr)
    if p >= 0x02000000 and p < 0x02400000 and p & 3 == 0 then
        return p
    end
    return nil
end

local function is_popup_showing()
    return info.popup >= 1 and info.popup <= 3
end

-- Choice lists can sit anywhere across the top of BG3 and the band test's one tile can't see them.
-- Windows are big, so every other column of a few rows is enough to find one without reading the
-- whole map. An open window clears both bands because the Mart item list covers the bottom too,
-- on BG1/BG2 where this can't see it: BG3 only has its Money box.
local function has_top_window(map)
    for _, row in ipairs(game.window_rows) do
        local row_start = map + row * MAP_COLUMNS * MAP_ENTRY_BYTES
        for column = 0, MAP_COLUMNS - 1, 2 do
            if read16(row_start + column * MAP_ENTRY_BYTES) & game.tile_mask ~= 0 then
                return true
            end
        end
    end
    return false
end

local function read_state()
    info.parent, info.child, info.overlay = nil, nil, nil
    info.top, info.tile, info.menu = nil, nil, nil
    info.tasks, info.script, info.box, info.text = 0, false, false, false
    info.popup, info.popup_y = 0, nil

    local app = ptr(game.app_pointer)
    if not app or read32(app + game.app_main) ~= game.app_main_field then
        return "nofield"
    end
    local fs = ptr(app + game.app_field_system)
    local pm = fs and ptr(fs + game.fs_process_manager)
    if not pm then
        return "nofield"
    end

    info.parent = read32(pm + game.pm_parent)
    info.child = read32(pm + game.pm_child)
    local child = ptr(pm + game.pm_child)
    if child then
        info.overlay = read32(child + game.child_overlay)
    end

    if info.parent == 0 then
        if not child then
            return "loading"
        end
        return info.overlay == game.overlay_battle and "battle" or "app"
    end

    local task = ptr(fs + game.fs_task)
    local menu_task = nil
    local is_battle = false
    local is_mart = false
    while task and info.tasks < MAX_TASK_STEPS do
        local func = read32(task + game.task_func)
        info.tasks = info.tasks + 1
        if not info.top then
            info.top = func
        end
        if game.func_battle[func] then
            is_battle = true
        elseif func == game.func_menu then
            -- Not always on top: the save prompt runs a script above it
            menu_task = menu_task or task
        elseif func == game.func_mart then
            is_mart = true
        elseif func == game.func_script then
            info.script = true
            local env = ptr(task + game.task_env)
            if env and read32(env) == game.script_magic and read8(env + game.script_box) == 1 then
                info.box = true
            end
        end
        task = ptr(task + game.task_next)
    end

    -- These pointers are freed unless the field is ready
    local is_ready = read32(fs + game.fs_ready) == 1
    local map = nil
    if is_ready then
        local bg = ptr(fs + game.fs_bg_config)
        map = bg and ptr(bg + game.bg_map)
        if map then
            info.tile = read16(map + game.map_tile)
            info.text = info.tile & game.tile_mask ~= 0
        end

        local sub = ptr(fs + game.fs_sub)
        local popup = sub and ptr(sub + game.sub_popup)
        if popup then
            info.popup = read8(popup + game.popup_state)
            info.popup_y = read32(popup + game.popup_y)
        end
    end

    if is_battle then
        return "battle"
    end
    if menu_task then
        local env = ptr(menu_task + game.task_env)
        if env then
            info.menu = read16(env + game.menu_substate)
        end
        return "menu"
    end
    if not is_ready then
        return "loading"
    end
    if info.script then
        -- The town name popup uses these rows too, and it wins: it stays "script".
        -- The mart task counts on its own because BG3 is empty for about 40 frames between its lists.
        if not is_popup_showing() and (is_mart or (map and has_top_window(map))) then
            return "window"
        end
        return "script"
    end
    return info.top and "locked" or "free"
end

local function update_targets(state)
    if state == "loading" then
        return
    end
    if state == "free" or state == "locked" or state == "script" then
        is_clearing_for_popup = is_popup_showing()
        target_top = is_clearing_for_popup and 0 or strength
        target_bottom = (info.text or info.box) and 0 or strength
    else
        -- Includes "window": both bands clear at the normal fade
        is_clearing_for_popup = false
        target_top = 0
        target_bottom = 0
    end
end

local function move_towards(level, target, step)
    if level < target then
        return math.min(level + step, target)
    end
    return math.max(level - step, target)
end

local function show(format, value)
    if value == nil then
        return "-"
    end
    return string.format(format, value)
end

local function flag(value)
    return value and "1" or "0"
end

local function describe(state, separator)
    return table.concat({
        "state " .. state,
        "parent " .. show("0x%08X", info.parent) .. " child " .. show("0x%08X", info.child),
        "overlay " .. show("%d", info.overlay) .. " tasks " .. info.tasks .. " top " .. show("0x%08X", info.top),
        "script " .. flag(info.script) .. " box " .. flag(info.box) .. " menu " .. show("%d", info.menu),
        "tile " .. show("0x%04X", info.tile) .. " text " .. flag(info.text),
        "popup " .. info.popup .. " y " .. show("%d", info.popup_y),
        string.format("strength top %.2f bottom %.2f", current_top, current_bottom),
    }, separator)
end

local function show_readout(state)
    melon.draw.text("bottom", 4, 14, describe(state, "\n"), 0xFFFFFFFF, 0xC0000000)

    -- Log on change only, so this can't spam
    if state ~= last_state or info.text ~= last_text or info.box ~= last_box or info.popup ~= last_popup then
        last_state = state
        last_text = info.text
        last_box = info.box
        last_popup = info.popup
        melon.log("frame " .. melon.frame() .. "  " .. describe(state, "  "))
    end
end

function on_state_loaded()
    -- A loaded state that starts out loading mustn't inherit targets from before the load
    target_top = 0
    target_bottom = 0
    is_clearing_for_popup = false
    should_snap = true
end

function on_frame()
    if not game then
        if wrong_game_frames > 0 then
            wrong_game_frames = wrong_game_frames - 1
            melon.draw.text("bottom", 4, 4, "Tilt shift: Platinum US (CPUE) only", 0xFFFFFFFF, 0xC0000000)
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
        message = is_automatic and "Blur automatic" or "Blur off"
        message_frames = MESSAGE_FRAMES
    end

    local state = read_state()
    update_targets(state)

    local goal_top = is_automatic and target_top or 0
    local goal_bottom = is_automatic and target_bottom or 0
    if should_snap then
        -- No fade after a savestate load: the first frame already looks right
        should_snap = false
        current_top = goal_top
        current_bottom = goal_bottom
    else
        local top_step = (is_automatic and is_clearing_for_popup) and popup_fade_step or fade_step
        current_top = move_towards(current_top, goal_top, top_step)
        current_bottom = move_towards(current_bottom, goal_bottom, fade_step)
    end

    -- Called every frame: only frames where a value changes cost an event
    melon.screen.tilt_shift(current_top, current_bottom, blur_options)

    if message_frames > 0 then
        message_frames = message_frames - 1
        melon.draw.text("bottom", 4, 4, message, 0xFFFFFFFF, 0xC0000000)
    end
    if show_debug then
        show_readout(state)
    end
end
