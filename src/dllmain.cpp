// language: C++17, file: dllmain.cpp, target: One Armed Robber (UE4.27 x64), Windows 11, MSVC
// Inject this DLL into the game process. Overlay survives game map restarts
// because World::get() is re-queried every ESP frame from the live GWorld pointer.
// Menu: INSERT = open/close │ UP/DOWN = navigate │ ENTER = toggle item │ DEL = unload

#include "../include/sdk.hpp"
#include "../include/pattern.hpp"
#include "../include/world.hpp"
#include "../include/camera.hpp"
#include "../include/actors.hpp"
#include "../include/overlay.hpp"
#include <thread>
#include <atomic>
#include <sstream>

// ─── Toggle flags (all off by default) ───────────────────────────────────────

bool g_esp_guards     = true;
bool g_esp_civilians  = true;
bool g_esp_police     = true;
bool g_esp_jugger     = true;
bool g_esp_intercept  = true;
bool g_esp_health     = true;  // show HP numbers
bool g_esp_distance   = true;  // show distance in units
bool g_esp_dead       = false; // show dead NPCs

// ─── ESP draw ────────────────────────────────────────────────────────────────

static void esp_draw(HDC dc, HWND /*wnd*/) {
    // Keyboard input — must poll here since overlay is transparent (no input focus)
    // We read async key state from the render thread; the actual key-processing
    // for menu navigation is done in a separate input thread below.
    // (draw is called from overlay render thread, input from input thread)

    CameraState cam{};
    if (!Camera::get_camera_state(cam)) {
        Overlay::draw_text(dc, 10, 10, "waiting for game...", RGB(200,80,80), 11);
        return;
    }

    // Re-acquire world actors every frame — survives map/session restart
    auto actors = Actors::scan_actors();

    for (const auto& a : actors) {
        if (a.dead && !g_esp_dead) continue;

        // Filter by category toggles
        switch (a.kind) {
            case Actors::NpcKind::Guard:       if (!g_esp_guards)    continue; break;
            case Actors::NpcKind::Civilian:    if (!g_esp_civilians) continue; break;
            case Actors::NpcKind::Police:      if (!g_esp_police)    continue; break;
            case Actors::NpcKind::Juggernaut:  if (!g_esp_jugger)    continue; break;
            case Actors::NpcKind::Interceptor: if (!g_esp_intercept) continue; break;
            case Actors::NpcKind::Shield:      if (!g_esp_police)    continue; break;
            default: continue;
        }

        // Head = location + (0, 0, +90cm), feet = location - (0, 0, -10cm)
        FVector feet_pos = a.location;
        FVector head_pos = { a.location.X, a.location.Y, a.location.Z + 90.f };

        FVector2D screen_head{}, screen_feet{};
        if (!world_to_screen(cam, head_pos, screen_head)) continue;
        if (!world_to_screen(cam, feet_pos, screen_feet)) continue;

        // Pick colour by type
        COLORREF col;
        const char* label;
        switch (a.kind) {
            case Actors::NpcKind::Guard:
                col = a.alert ? RGB(255,40,40) : Overlay::COL_GUARD;
                label = a.alert ? "GUARD!" : "Guard";
                break;
            case Actors::NpcKind::Civilian:
                col = Overlay::COL_CIVIL;
                label = "Civilian";
                break;
            case Actors::NpcKind::Police:
                col = Overlay::COL_POLICE;
                label = "Police";
                break;
            case Actors::NpcKind::Juggernaut:
                col = Overlay::COL_JUGGER;
                label = "Juggernaut";
                break;
            case Actors::NpcKind::Interceptor:
                col = Overlay::COL_INTERCPT;
                label = "Interceptor";
                break;
            case Actors::NpcKind::Shield:
                col = RGB(100, 200, 255);
                label = "Shield";
                break;
            default: continue;
        }

        // Dim dead actors
        if (a.dead) {
            col = RGB(120, 120, 120);
            label = "Dead";
        }

        // Box
        float bh = screen_feet.Y - screen_head.Y;
        float bw = bh * 0.4f;
        float bx = screen_head.X - bw * 0.5f;
        Overlay::draw_rect(dc, (int)bx, (int)screen_head.Y,
                               (int)bw, (int)bh, col, a.alert ? 3 : 2);

        // Label above box
        int text_x = (int)(bx);
        int text_y = (int)(screen_head.Y - 16);
        Overlay::draw_text(dc, text_x, text_y, label, col, 12, a.alert);

        // HP bar on left side of box
        if (g_esp_health && a.health > 0 && !a.dead) {
            int max_hp = (a.kind == Actors::NpcKind::Juggernaut) ? 500 : 100;
            float frac = (float)a.health / (float)max_hp;
            if (frac > 1.f) frac = 1.f;
            int bar_h = (int)bh;
            int bar_y = (int)screen_head.Y;
            int bar_x = (int)bx - 6;
            // background
            Overlay::draw_rect_filled(dc, bar_x, bar_y, 3, bar_h, RGB(40,40,40));
            // fill
            int fill_h = (int)(bar_h * frac);
            COLORREF hp_col = frac > 0.5f ? RGB(40,220,40) : frac > 0.25f ? RGB(220,220,40) : RGB(220,40,40);
            Overlay::draw_rect_filled(dc, bar_x, bar_y + (bar_h - fill_h), 3, fill_h, hp_col);

            // HP number
            std::string hp_str = std::to_string(a.health);
            Overlay::draw_text(dc, bar_x - 24, bar_y + bar_h/2 - 6, hp_str, Overlay::COL_TEXT, 10);
        }

        // Distance
        if (g_esp_distance) {
            float dx = a.location.X - cam.location.X;
            float dy = a.location.Y - cam.location.Y;
            float dz = a.location.Z - cam.location.Z;
            float dist_uu = sqrtf(dx*dx + dy*dy + dz*dz);
            float dist_m  = dist_uu / 100.f; // UU → meters
            char dist_buf[32];
            snprintf(dist_buf, sizeof(dist_buf), "%.0fm", dist_m);
            Overlay::draw_text(dc, (int)bx, (int)screen_feet.Y + 2, dist_buf,
                               RGB(180,180,180), 10);
        }
    }

    // HUD info bar
    char info[128];
    snprintf(info, sizeof(info), "OAR ESP  |  INS=menu  |  actors: %d", (int)actors.size());
    Overlay::draw_text(dc, 10, 10, info, RGB(160, 160, 160), 11);
}

// ─── Input thread ─────────────────────────────────────────────────────────────
// Polls hotkeys and controls menu navigation.
// INS     = toggle menu
// UP/DOWN = navigate
// ENTER   = toggle selected item
// DEL     = unload DLL

static std::atomic<bool> g_unload{ false };

static void input_thread() {
    auto key_pressed = [](int vk) -> bool {
        static int16_t prev[256]{};
        int16_t cur = GetAsyncKeyState(vk);
        bool fired = (cur & 0x8000) && !(prev[vk] & 0x8000);
        prev[vk] = cur;
        return fired;
    };

    while (!g_unload && Overlay::g_running) {
        if (key_pressed(VK_INSERT)) {
            Overlay::g_menu_open = !Overlay::g_menu_open;
        }
        if (Overlay::g_menu_open) {
            int n = (int)Overlay::g_menu_items.size();
            if (key_pressed(VK_UP))   Overlay::g_menu_sel = (Overlay::g_menu_sel - 1 + n) % n;
            if (key_pressed(VK_DOWN)) Overlay::g_menu_sel = (Overlay::g_menu_sel + 1) % n;
            if (key_pressed(VK_RETURN)) {
                auto& item = Overlay::g_menu_items[Overlay::g_menu_sel];
                if (item.enabled) *item.enabled = !*item.enabled;
            }
        }
        if (key_pressed(VK_DELETE)) {
            g_unload = true;
            Overlay::g_running = false;
        }
        Sleep(10);
    }
}

// ─── Initialization ───────────────────────────────────────────────────────────

static void main_thread(HMODULE hmod) {
    // Wait for game module to be fully loaded
    Sleep(2000);

    // Resolve singletons
    World::init();
    Camera::init_engine();
    Actors::init_names();

    // Register menu items
    Overlay::add_menu_item("Guards",             &g_esp_guards);
    Overlay::add_menu_item("Civilians",          &g_esp_civilians);
    Overlay::add_menu_item("Police",             &g_esp_police);
    Overlay::add_menu_item("Juggernaut",         &g_esp_jugger);
    Overlay::add_menu_item("Interceptor",        &g_esp_intercept);
    Overlay::add_menu_item("Health Bars + HP",   &g_esp_health);
    Overlay::add_menu_item("Distance",           &g_esp_distance);
    Overlay::add_menu_item("Show Dead",          &g_esp_dead);

    Overlay::set_draw_callback(esp_draw);

    // Launch input handler
    std::thread inp(input_thread);
    inp.detach();

    // Overlay render loop (blocking until DEL is pressed)
    Overlay::run();

    // Cleanup + free DLL
    FreeLibraryAndExitThread(hmod, 0);
}

// ─── DllMain ──────────────────────────────────────────────────────────────────

BOOL APIENTRY DllMain(HMODULE hmod, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hmod);
        std::thread(main_thread, hmod).detach();
    }
    return TRUE;
}
