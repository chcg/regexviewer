#pragma once
// The docked Regex Viewer panel and everything behind it: extraction, the
// worker that matches, highlighting in the editor, navigation, replace.
#include <string>
#include "npp.h"

void panel_init(int show_func_index, int show_cmd_id);  // at NPPN_READY
void panel_show();
void panel_set_paste(const std::wstring& text);          // fills the paste box, shows the panel
void panel_next();
void panel_prev();
void panel_replace();
void panel_replace_all();
void panel_clear_highlights();
void panel_on_editor_notify(const SCNotification* n);   // SCN_MODIFIED / SCN_UPDATEUI from an editor
void panel_on_buffer_activated();
void panel_on_dark_mode();
void panel_shutdown();
