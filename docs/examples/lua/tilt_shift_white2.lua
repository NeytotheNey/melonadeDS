-- description: Tilt-shift blur for Pokémon White 2 US. On in the overworld, clears by itself elsewhere. Hold L, tap R: off/auto.
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
local MAX_EVENT_STEPS = 8
-- Bubbles open within 8 frames with only 3-4 frames of warning
local FAST_FADE_FRAMES = 10
-- The DS screen is 24 tile rows: a window above this row is at the top, otherwise at the bottom
local ROW_MIDDLE = 12

-- Addresses and offsets per game code. Function values include the Thumb bit. They're only compared
-- with what an event holds, never followed.
local GAMES = {
    IRDO = {
        -- 0 until a game is loaded, and 0 again after a soft reset
        gsys_pointer = 0x02141134,
        gs_proc_manager = 0x08,
        gs_event = 0x18,
        -- NULL while the map is down
        gs_field = 0x20,

        pm_proc = 0x08,
        proc_functions = 0x00,
        -- Data address, so no Thumb bit
        fieldmap_proc = 0x021C9EEC,

        ev_parent = 0x00,
        ev_func = 0x04,
        ev_data = 0x0C,

        fs_subscreen = 0x24,
        -- This and its talk pointer can be NULL for about 150 frames after the field reopens
        fs_msgbg = 0x28,
        fs_place_name = 0x30,
        -- 1 ready, 0 loading, 2 closing
        fs_routine = 0xD0,
        routine_ready = 1,

        sub_screen = 0x00,
        -- The touch screens that count as menu: 1 menu, 7 save, 9 C-Gear power prompt, and 12 the
        -- dungeon retire menu (from the decomp only, not seen live). The rest stay up while walking
        -- or are the C-Gear itself, so they fall through: 0 C-Gear, 5 no C-Gear, 6 Dowsing MCHN, ...
        sub_screen_menu = {
            [1] = true,
            [7] = true,
            [9] = true,
            [12] = true,
        },

        msgbg_talk = 0x15C,

        -- The actor bubbles: 0 top, 1 bottom, 2 auto. A window's state is the u16 at +0, 0 = free.
        talk_windows = 0x18,
        talk_window_size = 0x64,
        talk_window_count = 3,
        win_bmp = 0x0C,
        -- Row 1 is the top window, row 19 the bottom one
        bmp_y = 0x04,

        -- 0 idle, then 1 to 6
        place_name_mode = 0x44,

        -- Overlay 12 bss: only read this while the field is ready
        sub_events = 0x0216E680,
        bit_system = 0x02,
        bit_msg = 0x08,
        bit_sign = 0x10,
        bit_multi = 0x20,
        bit_checker = 0x40,

        -- The script event's data
        script_work = 0x00,
        script_supervisor = 0x04,
        sup_vms = 0x04,
        sup_vm_count = 3,
        vm_native = 0x1C,

        -- ScriptWork to the open system or info window. Only valid while its sub event bit is set.
        work_subwork = 0x148,
        subwork_win = 0xFC,
        system_win_bmp = 0x00,
        info_win_bmp = 0x18,

        func_script = 0x02153821,
        func_battle = {
            [0x02168BED] = true,
            [0x021684FD] = true,
        },
        -- Field menu, its return, save, C-Gear, C-Gear shutdown
        func_menu = {
            [0x0215A8B5] = true,
            [0x0218B1A9] = true,
            [0x0215C589] = true,
            [0x021ED471] = true,
            [0x021775E1] = true,
        },
        -- An app opened from the menu: counts as menu while it's the newest event and the field is up
        func_app = 0x0215B54D,
        func_yes_no = 0x021A82FD,
        func_list = 0x021A83D5,
        func_season = {
            [0x0217EE99] = true,
            [0x021B8F6D] = true,
        },
        native_shop = 0x021ABD91,
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
local fast_fade_step = fade_amount / math.min(melon.options.fade, FAST_FADE_FRAMES)
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
local is_clearing_fast = false

local was_l_held = false
local was_r_held = false

local message = nil
local message_frames = 0
local wrong_game_frames = WRONG_GAME_FRAMES

-- What the last read_state() found, reused so there's no table per frame
local info = {}
local last_state = nil
local last_text_top = nil
local last_text_bottom = nil
local last_place_name = nil
local last_sub_screen = nil
local last_bits = nil

-- Reads a pointer: nil unless it's aligned and inside main RAM
local function ptr(addr)
    local p = read32(addr)
    if p >= 0x02000000 and p < 0x02400000 and p & 3 == 0 then
        return p
    end
    return nil
end

local function note_text_row(bmp)
    if bmp then
        if read8(bmp + game.bmp_y) < ROW_MIDDLE then
            info.text_top = true
        else
            info.text_bottom = true
        end
    end
end

local function read_text_rows(field, script_event, bits)
    -- The window's own state, because bit_actor can stay on with no window open
    local msgbg = ptr(field + game.fs_msgbg)
    local talk = msgbg and ptr(msgbg + game.msgbg_talk)
    if talk then
        for i = 0, game.talk_window_count - 1 do
            local window = talk + game.talk_windows + i * game.talk_window_size
            if read16(window) ~= 0 then
                note_text_row(ptr(window + game.win_bmp))
            end
        end
    end

    if bits & game.bit_sign ~= 0 then
        info.text_bottom = true
    end

    if bits & (game.bit_system | game.bit_msg) ~= 0 then
        -- These bits are 0 without a script event
        local env = ptr(script_event + game.ev_data)
        local work = env and ptr(env + game.script_work)
        local subwork = work and ptr(work + game.work_subwork)
        local window = subwork and ptr(subwork + game.subwork_win)
        if window then
            -- One pointer, and the bit says which kind of window it points at
            local bmp_offset = bits & game.bit_msg ~= 0 and game.info_win_bmp or game.system_win_bmp
            note_text_row(ptr(window + bmp_offset))
        end
    end
end

local function is_shop_running(script_event)
    local env = ptr(script_event + game.ev_data)
    local supervisor = env and ptr(env + game.script_supervisor)
    if supervisor then
        for i = 0, game.sup_vm_count - 1 do
            local vm = ptr(supervisor + game.sup_vms + i * 4)
            if vm and read32(vm + game.vm_native) == game.native_shop then
                return true
            end
        end
    end
    return false
end

local function read_state()
    info.events, info.top, info.script = 0, nil, false
    info.routine, info.place_name, info.sub_screen, info.bits = nil, nil, nil, nil
    info.text_top, info.text_bottom = false, false

    local gsys = ptr(game.gsys_pointer)
    if not gsys then
        return "nogame"
    end

    local is_battle, is_yes_no, is_list, is_season = false, false, false, false
    local script_event = nil
    local event = ptr(gsys + game.gs_event)
    while event and info.events < MAX_EVENT_STEPS do
        local func = read32(event + game.ev_func)
        info.events = info.events + 1
        if not info.top then
            info.top = func
        end
        if game.func_battle[func] then
            is_battle = true
        elseif func == game.func_yes_no then
            is_yes_no = true
        elseif func == game.func_list then
            is_list = true
        elseif func == game.func_script then
            -- The walk starts at the newest event, and the open window belongs to the newest script
            script_event = script_event or event
            info.script = true
        elseif game.func_season[func] then
            is_season = true
        end
        event = ptr(event + game.ev_parent)
    end

    if is_battle then
        return "battle"
    end

    local field = ptr(gsys + game.gs_field)
    if not field then
        local pm = ptr(gsys + game.gs_proc_manager)
        local proc = pm and ptr(pm + game.pm_proc)
        if not proc or read32(proc + game.proc_functions) == game.fieldmap_proc then
            return "loading"
        end
        return "app"
    end

    info.routine = read32(field + game.fs_routine)
    if info.routine ~= game.routine_ready then
        return "loading"
    end
    if is_season then
        return "season"
    end

    local place_name = ptr(field + game.fs_place_name)
    if place_name then
        info.place_name = read32(place_name + game.place_name_mode)
        if info.place_name ~= 0 then
            return "place_name"
        end
    end

    local subscreen = ptr(field + game.fs_subscreen)
    if subscreen then
        info.sub_screen = read32(subscreen + game.sub_screen)
    end
    -- Newest event only: the menu's callback stays on the chain under an item action it started,
    -- like the Bike or fishing, and that action is no longer the menu
    if game.func_menu[info.top] or info.top == game.func_app or game.sub_screen_menu[info.sub_screen] then
        return "menu"
    end

    -- Nothing clears these when a script ends, so they only count while one is running
    local bits = read32(game.sub_events)
    info.bits = bits
    if not script_event then
        bits = 0
    end
    read_text_rows(field, script_event, bits)

    -- These stay up beside whatever the script does next, a Yes/No included, so they go first
    if bits & game.bit_multi ~= 0 then
        return "multi"
    end
    if bits & game.bit_checker ~= 0 then
        return "checker"
    end
    if is_yes_no then
        return "yesno"
    end
    if is_list then
        return "list"
    end
    if script_event and is_shop_running(script_event) then
        return "shop"
    end
    if info.text_top or info.text_bottom then
        return "text"
    end
    return "free"
end

local function update_targets(state)
    if state == "loading" then
        return
    end

    local clear_top, clear_bottom, is_fast = true, true, false
    if state == "free" or state == "text" or state == "yesno" then
        -- Yes/No sits inside the default sharp band, so it keeps whatever the text set
        clear_top = info.text_top
        clear_bottom = info.text_bottom
        is_fast = clear_top or clear_bottom
    elseif state == "menu" then
        -- The top still shows the overworld, with a white tab on its bottom edge
        clear_top = false
    elseif state == "place_name" then
        -- A name across the top and a season label at bottom right. Mode 1 lasts 28 frames with no
        -- text yet, so this gives a head start.
        is_fast = true
    end

    target_top = clear_top and 0 or strength
    target_bottom = clear_bottom and 0 or strength
    is_clearing_fast = is_fast
end

local function move_towards(level, target, step)
    if level < target then
        return math.min(level + step, target)
    end
    return math.max(level - step, target)
end

-- Only clearing is fast: the blur comes back at the normal fade once the text is gone
local function fade_step_for(level, goal)
    if is_automatic and is_clearing_fast and goal < level then
        return fast_fade_step
    end
    return fade_step
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
        "events " .. info.events .. " top " .. show("0x%08X", info.top) .. " script " .. flag(info.script),
        "routine " .. show("%d", info.routine) .. " sub " .. show("%d", info.sub_screen),
        "place " .. show("%d", info.place_name) .. " bits " .. show("0x%04X", info.bits),
        "text top " .. flag(info.text_top) .. " bottom " .. flag(info.text_bottom),
        string.format("strength top %.2f bottom %.2f", current_top, current_bottom),
    }, separator)
end

local function show_readout(state)
    melon.draw.text("bottom", 4, 14, describe(state, "\n"), 0xFFFFFFFF, 0xC0000000)

    -- Log on change only, so this can't spam
    if state ~= last_state
        or info.text_top ~= last_text_top
        or info.text_bottom ~= last_text_bottom
        or info.place_name ~= last_place_name
        or info.sub_screen ~= last_sub_screen
        or info.bits ~= last_bits
    then
        last_state = state
        last_text_top = info.text_top
        last_text_bottom = info.text_bottom
        last_place_name = info.place_name
        last_sub_screen = info.sub_screen
        last_bits = info.bits
        melon.log("frame " .. melon.frame() .. "  " .. describe(state, "  "))
    end
end

function on_state_loaded()
    -- A loaded state that starts out loading mustn't inherit targets from before the load
    target_top = 0
    target_bottom = 0
    is_clearing_fast = false
    should_snap = true
end

function on_frame()
    if not game then
        if wrong_game_frames > 0 then
            wrong_game_frames = wrong_game_frames - 1
            melon.draw.text("bottom", 4, 4, "Tilt shift: White 2 US (IRDO) only", 0xFFFFFFFF, 0xC0000000)
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
        current_top = move_towards(current_top, goal_top, fade_step_for(current_top, goal_top))
        current_bottom = move_towards(current_bottom, goal_bottom, fade_step_for(current_bottom, goal_bottom))
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
