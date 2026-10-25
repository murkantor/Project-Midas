/**
 * port_option_help.c - one short line per setting row, shown in the help box
 * under the frontend folder's options sheets (front.c portSheetDrawHelp) while
 * the [display] setting_help option is on.
 *
 * Keyed by the row's midas.ini key. Rows with no key of their own use an "@"
 * name (front.c portSheetHelpKey). A pad's padN_ key reads as pad1_. A row with
 * no entry shows no box. Keep each line short: one fragment, plain verbs.
 */
#include <string.h>
#include <ultra64.h>

struct PortOptHelp { const char *key; const char *text; };

static const struct PortOptHelp sHelp[] = {
    /* GENERAL SETTINGS */
    { "look_updown",        "Upright: push up to look up. Reverse inverts it." },
    { "auto_aim",           "Gun aims at enemies for you." },
    { "aim_control",        "Hold: aim while held. Toggle: press to aim, again to stop." },
    { "look_ahead",         "View tilts to follow slopes and stairs." },
    { "aim_lock",           "Crosshair stays centred." },
    { "scope_steady",       "No sway or drift while zoomed in." },
    { "head_roll",          "Camera tilt and breathing sway." },

    /* PORT OPTIONS */
    { "unlock_all",         "Unlocks all missions and cheats, still saves progress." },
    { "fix_shootthrough",   "Fixes shots passing through some walls." },
    { "distance_fading",    "Distant props and characters fade out." },
    { "true_enemy_rockets", "Enemy Rockets cheat stops guards keeping their old guns." },
    { "unlimited_explosions", "Up to 100 explosions at once." },
    { "watch_static",       "Shows the watch screen's static." },
    { "fast_pause",         "Watch opens and closes at full speed." },
    { "mouse_auto_aim",     "Lets Auto-Aim work with the mouse." },
    { "high_lod",           "Always draws the most detailed models." },
    { "setting_help",       "Shows this help box." },

    /* HUD */
    { "sight_on_screen",    "Shows the aiming sight." },
    { "always_crosshair",   "Small target at the gun's aim point." },
    { "crosshair_color",    "Colour of both crosshairs." },
    { "ammo_on_screen",     "Shows your ammo count." },
    { "mission_timer",      "Shows elapsed mission time." },
    { "pause_pillars",      "Black side bars ease in while paused." },
    { "hud_wide",           "Wide moves ammo and messages to the screen edges." },
    { "health_wide",        "Wide moves the health and armor arcs to the screen edges." },
    { "hud_scale",          "High draws the HUD at half size." },
    { "show_fps",           "Shows frames per second." },
    { "armor_hud",          "Shows health and armor when you pick up armor." },

    /* MULTIPLAYER OPTIONS */
    { "mp_sight_on_screen", "Shows the aiming sight." },
    { "mp_no_auto_aim",     "Turns Auto-Aim off for every player." },
    { "mp_aim_lock",        "Crosshair stays centred." },
    { "mp_show_names",      "Shows each player's name." },
    { "mp_always_crosshair", "Small target at the gun's aim point." },
    { "mp_fix_shootthrough", "Fixes shots passing through some walls." },
    { "mp_spawn_weapon",    "Spawn holding the weapon set's first gun." },
    { "mp_random_spawns",   "Players spawn at random points." },
    { "mp_head_roll",       "Camera tilt and breathing sway." },
    { "mp_unlimited_explosions", "Up to 100 explosions at once." },
    { "mp_ltk_health",      "One-hit kills in any mode." },
    { "mp_no_armor",        "Removes body armor pickups." },
    { "mp_hud_wide",        "Wide moves each view's HUD to its edges." },
    { "mp_pillarbox",       "Plays the match in 4:3 with side bars." },
    { "mp_friendly_fire",   "Team-mates can hurt each other." },
    { "mp_invisibility",    "Every player is invisible." },
    { "mp_dk_mode",         "Big heads and long arms for every player." },
    { "mp_tiny_players",    "Every player is tiny." },
    { "mp_paintball",       "Shots leave paint splats." },
    { "mp_no_radar",        "Hides the radar." },
    { "mp_turbo_mode",      "Everyone moves much faster." },
    { "mp_unlimited_ammo",  "Guns never run out of ammo." },

    /* MOUSE AND KEYBOARD */
    { "mouse",              "Look around with the mouse." },
    { "smart_mouse_lock",   "Holds the pointer only during play." },
    { "mouse_sens_x",       "Mouse look speed, left and right." },
    { "mouse_sens_y",       "Mouse look speed, up and down." },
    { "mouse_sens_aim",     "Mouse crosshair speed while aiming." },
    { "p1_hybrid",          "Keyboard, mouse and player 1's pad all control player 1." },

    /* CONTROLLER */
    { "deadzone",           "Move stick travel ignored around the centre." },
    { "deadzone_look",      "Look stick travel ignored around the centre." },
    { "trigger_threshold",  "How far a trigger must be pulled to count as pressed." },
    { "stick_scale",        "Multiplies the Move stick's sensitivity." },
    { "rumble",             "Controller vibration." },
    { "pad1_gadget_switch", "Press Z while holding B to switch gadget." },
    { "pad1_lr_classic",    "Hold L+R for half a second to toggle Classic Mode." },

    /* GENERAL CONTROL OPTIONS */
    { "detonate_use_reload", "Use and reload together detonate remote mines." },
    { "shoot_next_prev",    "Hold next weapon and tap fire to go back one." },
    { "no_aim_crouch",      "Aiming and pushing down no longer crouches." },
    { "background_input",   "Pads keep working in the background." },

    /* VIDEO: every row waits for Apply Changes */
    { "fullscreen",         "Windowed, Borderless, or exclusive Fullscreen." },
    { "@resolution",        "Window or Fullscreen size." },
    { "vsync",              "Waits for the screen's refresh." },
    { "widescreen",         "Wide widens the view; Stretch stretches; 4:3 adds side bars." },
    { "n64_aa",             "N64 edge blending alone, at your own resolution." },
    { "classic_mode",       "240p, hardware pacing, pillars, classic filter." },
    { "hfov_wide",          "Horizontal view angle." },
    { "weapon_fov",         "Keeps the gun at its normal size at any FOV." },
    { "low_res",            "3D render height." },
    { "filtering",          "Texture smoothing." },
    { "anisotropy",         "Sharper textures at glancing angles." },
    { "msaa",               "Smooths jagged edges." },
    { "filter_2d",          "Smooths HUD and menu graphics." },
    { "maxfps",             "Frame rate cap." },
    { "@apply",             "Applies the changes on this page." },

    /* CLASSIC: what Classic Mode forces while it is on */
    { "classic_res",        "Classic Mode renders 3D at the N64's 240 lines." },
    { "classic_filter",     "Classic Mode uses N64-style 3-point texture filtering." },
    { "classic_aniso",      "Classic Mode sets anisotropy to 8x." },
    { "classic_pacing",     "Classic Mode runs at the N64's own frame pacing." },
    { "classic_msaa",       "Classic Mode sets MSAA to 2x." },
    { "classic_mipmaps",    "Classic Mode turns mipmaps on." },
    { "classic_filter2d",   "Classic Mode smooths HUD and menu graphics." },
    { "classic_pillars",    "Classic Mode shows a 4:3 picture with side bars." },
    { "classic_hitches",    "Classic Mode adds the N64's loading pauses." },
    { "classic_n64aa",      "Classic Mode blends edges like the N64's video chip." },
    { "classic_composite",  "Classic Mode adds a composite TV cable look." },

    /* AUDIO */
    { "music_volume",       "How loud the music plays." },
    { "sfx_volume",         "How loud sound effects play." },
    { "stereo_pan",         "Effects pan left and right by position." },
    { "mute_unfocused",     "Mutes the game in the background." },

    /* CONTROL, the player pages and the rebind list */
    { "@player",            "Set up this player's device, binds and controls." },
    { "@input_device",      "Press, then press a button on the device this player uses." },
    { "@rebind",            "Change this device's buttons and keys." },
    { "@page_mousekb",      "Mouse look, sensitivity and keyboard options." },
    { "@page_pad",          "Deadzones, trigger point, stick speed and rumble." },
    { "@page_ctlgeneral",   "Detonate, weapon cycling, crouch and background input." },
    { "@reset_binds",       "Puts every bind on this device back to default." },
    { "@bind_mode",         "Port uses PC controls; N64 drives it as an N64 pad." },
    { "@control_style",     "Rare's N64 control style for this pad." },
    { "@bind_config",       "Loads a saved button layout for this pad." },
    { "@manage_config",     "Saves the current layout, or deletes one you made." },
    { "@bind",              "Press, then a key or button to add. Del or hold to unbind." },

    /* the GAMEPLAY and AUDIO VISUAL pickers */
    { "@page_general",      "Look, aim and view options." },
    { "@page_hud",          "What the in-game HUD shows and where." },
    { "@page_portopts",     "The port's own gameplay switches." },
    { "@page_video",        "Display mode, resolution and picture quality." },
    { "@page_audio",        "Volume and sound options." },
};

/** The help line for a row's key, or NULL (no box). padN_ keys read as pad1_. */
const char *portOptHelp(const char *key)
{
    char norm[48];
    size_t i;

    if (key == NULL)
    {
        return NULL;
    }
    strncpy(norm, key, sizeof(norm) - 1);
    norm[sizeof(norm) - 1] = '\0';
    if (strncmp(norm, "pad", 3) == 0 && norm[3] >= '1' && norm[3] <= '4' && norm[4] == '_')
    {
        norm[3] = '1';
    }
    for (i = 0; i < sizeof(sHelp) / sizeof(sHelp[0]); i++)
    {
        if (strcmp(sHelp[i].key, norm) == 0)
        {
            return sHelp[i].text;
        }
    }
    return NULL;
}
