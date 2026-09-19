#pragma once
// language: C++17, file: overlay.hpp
// GDI layered transparent overlay — no ImGui, no DirectX dependency.
// Creates a topmost WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_TOOLWINDOW window,
// draws with GDI+ or raw GDI every frame. Covers the game window exactly.
// Menu is rendered here too — simple keyboard-toggled panel drawn with GDI.

#include <windows.h>
#include <wingdi.h>
#include <string>
#include <functional>
#include <vector>
#include <atomic>

#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")

namespace Overlay {

// ─── Menu item definition ─────────────────────────────────────────────────────

struct MenuItem {
    std::string  label;
    bool*        enabled;  // pointer to the toggle this item controls
};

// ─── Global overlay state ─────────────────────────────────────────────────────

inline HWND  g_hwnd       = nullptr;
inline HWND  g_game_hwnd  = nullptr;
inline int   g_width      = 1920;
inline int   g_height     = 1080;
inline bool  g_menu_open  = false;
inline int   g_menu_sel   = 0;

inline std::vector<MenuItem>           g_menu_items;
inline std::function<void(HDC, HWND)>  g_draw_callback; // called each frame with a drawing DC
inline std::atomic<bool>               g_running{ true };

// ─── Colours ─────────────────────────────────────────────────────────────────
inline COLORREF COL_GUARD    = RGB(255,  80,  80);  // red
inline COLORREF COL_CIVIL    = RGB(255, 220,  50);  // yellow
inline COLORREF COL_POLICE   = RGB( 80, 160, 255);  // blue
inline COLORREF COL_JUGGER   = RGB(180,  80, 255);  // purple
inline COLORREF COL_INTERCPT = RGB(255, 140,  20);  // orange
inline COLORREF COL_TEXT     = RGB(255, 255, 255);
inline COLORREF COL_MENU_BG  = RGB( 20,  20,  20);
inline COLORREF COL_MENU_SEL = RGB( 60, 120, 200);
inline COLORREF COL_MENU_TXT = RGB(220, 220, 220);
static const COLORREF TRANS_KEY = RGB(1, 1, 1); // transparent key colour (not 0,0,0)

// ─── GDI helpers ─────────────────────────────────────────────────────────────

inline void draw_line(HDC dc, int x1, int y1, int x2, int y2, COLORREF col, int thick = 1) {
    HPEN pen = CreatePen(PS_SOLID, thick, col);
    HPEN old = (HPEN)SelectObject(dc, pen);
    MoveToEx(dc, x1, y1, nullptr);
    LineTo(dc, x2, y2);
    SelectObject(dc, old);
    DeleteObject(pen);
}

inline void draw_rect(HDC dc, int x, int y, int w, int h, COLORREF col, int thick = 1) {
    draw_line(dc, x,   y,   x+w, y,   col, thick);
    draw_line(dc, x+w, y,   x+w, y+h, col, thick);
    draw_line(dc, x+w, y+h, x,   y+h, col, thick);
    draw_line(dc, x,   y+h, x,   y,   col, thick);
}

inline void draw_rect_filled(HDC dc, int x, int y, int w, int h, COLORREF col) {
    HBRUSH br = CreateSolidBrush(col);
    RECT   rc { x, y, x+w, y+h };
    FillRect(dc, &rc, br);
    DeleteObject(br);
}

inline void draw_text(HDC dc, int x, int y, const std::string& s, COLORREF col,
                      int font_size = 12, bool bold = false) {
    SetTextColor(dc, col);
    SetBkMode(dc, TRANSPARENT);
    HFONT font = CreateFontA(font_size, 0, 0, 0,
                             bold ? FW_BOLD : FW_NORMAL,
                             FALSE, FALSE, FALSE,
                             ANSI_CHARSET, OUT_DEFAULT_PRECIS,
                             CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                             DEFAULT_PITCH | FF_DONTCARE, "Arial");
    HFONT old_font = (HFONT)SelectObject(dc, font);
    TextOutA(dc, x, y, s.c_str(), (int)s.size());
    SelectObject(dc, old_font);
    DeleteObject(font);
}

// Bounding box ESP box from screen-space head + feet positions
inline void draw_esp_box(HDC dc, float sx, float sy_head, float sx2, float sy_feet,
                         COLORREF col) {
    float box_h = sy_feet - sy_head;
    float box_w = box_h * 0.4f;
    float bx = sx - box_w * 0.5f;
    draw_rect(dc, (int)bx, (int)sy_head, (int)box_w, (int)box_h, col, 2);
}

// ─── Menu rendering ───────────────────────────────────────────────────────────

inline void draw_menu(HDC dc) {
    if (!g_menu_open) return;

    int mx = 20, my = 80;
    int mw = 220, item_h = 24;
    int mh = 30 + (int)g_menu_items.size() * item_h + 10;

    // Background with alpha simulation (semi-opaque dark rect)
    draw_rect_filled(dc, mx, my, mw, mh, COL_MENU_BG);
    draw_rect(dc, mx, my, mw, mh, RGB(100,100,100), 1);

    // Title
    draw_text(dc, mx+8, my+6, "[ OAR ESP ]  INS=toggle", COL_TEXT, 11, true);

    for (int i = 0; i < (int)g_menu_items.size(); ++i) {
        int iy = my + 30 + i * item_h;
        bool sel = (i == g_menu_sel);
        if (sel)
            draw_rect_filled(dc, mx+2, iy, mw-4, item_h-2, COL_MENU_SEL);

        bool on = g_menu_items[i].enabled ? *g_menu_items[i].enabled : false;
        std::string line = (on ? "[X] " : "[ ] ") + g_menu_items[i].label;
        draw_text(dc, mx+8, iy+4, line, sel ? RGB(255,255,255) : COL_MENU_TXT, 12);
    }

    draw_text(dc, mx+8, my+mh-18, "UP/DOWN  ENTER=toggle", RGB(160,160,160), 10);
}

// ─── Window proc ─────────────────────────────────────────────────────────────

inline LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_DESTROY) { g_running = false; return 0; }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

// ─── Sync overlay position to game window ────────────────────────────────────

inline void sync_to_game() {
    if (!g_game_hwnd || !IsWindow(g_game_hwnd)) {
        // try to re-find game window
        g_game_hwnd = FindWindowA(nullptr, "OneArmedRobber");
        if (!g_game_hwnd) return;
    }
    RECT rc{};
    GetClientRect(g_game_hwnd, &rc);
    POINT pt{};
    ClientToScreen(g_game_hwnd, &pt);
    int x = pt.x, y = pt.y;
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    if (w < 64 || h < 64) return;
    if (w != g_width || h != g_height) {
        g_width  = w;
        g_height = h;
    }
    SetWindowPos(g_hwnd, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE);
}

// ─── Per-frame render ────────────────────────────────────────────────────────
// Uses UpdateLayeredWindow with a memory DC — prevents flicker, allows real
// transparency via the TRANS_KEY colour key (background = TRANS_KEY = invisible).

inline void render_frame() {
    HDC screen_dc = GetDC(nullptr);
    HDC mem_dc    = CreateCompatibleDC(screen_dc);
    HBITMAP bmp   = CreateCompatibleBitmap(screen_dc, g_width, g_height);
    HBITMAP old   = (HBITMAP)SelectObject(mem_dc, bmp);

    // Fill transparent background
    draw_rect_filled(mem_dc, 0, 0, g_width, g_height, TRANS_KEY);

    // Invoke the ESP draw callback
    if (g_draw_callback)
        g_draw_callback(mem_dc, g_hwnd);

    // Draw menu on top
    draw_menu(mem_dc);

    // Compose onto overlay with colour key transparency
    BLENDFUNCTION blend{ AC_SRC_OVER, 0, 255, 0 };
    POINT zero{};
    SIZE  sz{ g_width, g_height };
    UpdateLayeredWindow(g_hwnd, screen_dc, nullptr, &sz,
                        mem_dc, &zero, TRANS_KEY,
                        &blend, ULW_COLORKEY);

    SelectObject(mem_dc, old);
    DeleteObject(bmp);
    DeleteDC(mem_dc);
    ReleaseDC(nullptr, screen_dc);
}

// ─── Public API ──────────────────────────────────────────────────────────────

// Register a menu item. Call before run().
inline void add_menu_item(const std::string& label, bool* flag) {
    g_menu_items.push_back({ label, flag });
}

// Set the per-frame draw function. Called with an HDC each frame.
inline void set_draw_callback(std::function<void(HDC, HWND)> cb) {
    g_draw_callback = std::move(cb);
}

// Create overlay window and enter render loop. Call from a dedicated thread.
inline void run() {
    WNDCLASSEXA wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = wnd_proc;
    wc.hInstance     = GetModuleHandleA(nullptr);
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH); // ULW owns the surface; WM_PAINT must not paint black
    wc.lpszClassName = "OAR_ESP_OVERLAY";
    RegisterClassExA(&wc);

    g_game_hwnd = FindWindowA(nullptr, "OneArmedRobber");

    int ox = 0, oy = 0;
    if (g_game_hwnd) {
        RECT rc{};
        GetClientRect(g_game_hwnd, &rc);
        POINT pt{};
        ClientToScreen(g_game_hwnd, &pt);
        ox = pt.x; oy = pt.y;
        g_width  = rc.right - rc.left;
        g_height = rc.bottom - rc.top;
    }
    if (g_width  < 64) g_width  = GetSystemMetrics(SM_CXSCREEN);
    if (g_height < 64) g_height = GetSystemMetrics(SM_CYSCREEN);

    g_hwnd = CreateWindowExA(
        WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW,
        "OAR_ESP_OVERLAY",
        "",
        WS_POPUP,
        ox, oy, g_width, g_height,
        nullptr, nullptr,
        GetModuleHandleA(nullptr),
        nullptr
    );

    // NOTE: do NOT call SetLayeredWindowAttributes here.
    // This window uses UpdateLayeredWindow (ULW_COLORKEY) in render_frame — the two are
    // mutually exclusive per MSDN. Mixing them causes undefined compositing behaviour
    // (manifests as black screen when INSERT opens the menu / large ULW area is written).
    ShowWindow(g_hwnd, SW_SHOW);
    UpdateWindow(g_hwnd);

    while (g_running) {
        MSG msg{};
        while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        sync_to_game();
        render_frame();
        Sleep(8); // ~120 fps cap
    }

    DestroyWindow(g_hwnd);
    UnregisterClassA("OAR_ESP_OVERLAY", GetModuleHandleA(nullptr));
}

} // namespace Overlay
