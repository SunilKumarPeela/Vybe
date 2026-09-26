#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include "point_file_diff_ui.h"
// Compile-only Windows gate: check every inline UI body with MSVC /W4 /WX.
void check_file_diff_ui(HWND owner, const point::Engine& engine,
        const std::filesystem::path& root) {
    point::file_diff_ui::open(owner,[&engine] { return &engine; },root);
}

#include "point_startup_recovery.h"
