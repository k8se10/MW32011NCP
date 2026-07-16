#pragma once

// mod_config — user-facing tuning values, loaded from an INI file next to the DLL
// (task #14). Ships with sane defaults matching what was previously hardcoded
// throughout analog_input_hooks.cpp; this file replaces those constants with runtime
// values so a player can retune without recompiling. A real in-game options screen
// (sliders, live preview) is future work once controller menu/UI navigation exists --
// this INI is the interim, and the file this mod ships with today.

struct ModConfig
{
    // [Look]
    float lookDegreesPerSecond = 250.0f;   // right-stick turn rate at full deflection
    float adsSlowdownStrength = 1.0f;      // 0 = off, 1 = fully proportional to live zoom
    bool invertLook = false;               // OG console "Invert Look" -- flips vertical look

    // [Stance]
    unsigned long proneHoldThresholdMs = 400; // B: hold vs. tap threshold

    // [Interact]
    unsigned long interactHoldThresholdMs = 740; // X: hold-to-interact threshold

    // [Survival]
    unsigned long readyUpHoldThresholdMs = 740; // Y: hold-to-ready-up threshold (Survival only)

    // [Sprint]
    float sprintMaxStaminaSeconds = 4.0f; // continuous sprint time before depleting
    float sprintRegenSeconds = 2.0f;      // time NOT sprinting to fully recover from empty
};

// The loaded config, populated once by LoadModConfig(). Read-only after startup --
// nothing in this mod hot-reloads the INI mid-session.
extern ModConfig g_modConfig;

// Reads mw3ncp_config.ini from the same directory as this DLL, filling in any
// missing/malformed key with its default. Writes a fresh, fully-commented default file
// if none exists yet, so the file is discoverable and self-documenting on first run.
// Call once, early in DllMain (before any hook that reads g_modConfig runs).
void LoadModConfig();
