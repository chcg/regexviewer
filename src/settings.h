#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include "flavor.h"

// ─── enums ────────────────────────────────────────────────────────────────────
enum class MatchLike { NotepadPP, Source, Fixed };

// ─── settings struct (defaults = first-run behaviour) ────────────────────────
struct ViewerSettings {
    // Matching tab
    MatchLike  match_like = MatchLike::NotepadPP;
    rv::Flavor fixed      = rv::Flavor::Python;   // used when match_like == Fixed
    int        budget_s   = 2;                    // time budget per run
    int        large_mb   = 5;                    // above this: visible part first

    // Highlighting tab
    bool       match_box  = true;                 // box around the whole match
    int        max_list   = 10000;                // matches in the list

    // Behavior tab
    bool       rerun_on_edit = true;              // re-run when the document changes
};

extern ViewerSettings g_settings;

void load_settings();
void save_settings();
void show_settings_dialog(HWND parent);
void show_about_dialog(HWND parent);
