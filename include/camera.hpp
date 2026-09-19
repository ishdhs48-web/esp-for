#pragma once
// language: C++17, file: camera.hpp
// Reads camera state from APlayerController → APlayerCameraManager each frame.
// Survives game restarts because we re-resolve the world actor array every tick.

#include "sdk.hpp"
#include "world.hpp"
#include "pattern.hpp"
#include <psapi.h>
#pragma comment(lib, "psapi.lib")

namespace Camera {

// APlayerController offsets (UE4.27 standard + confirmed against dump)
// AController (parent of APlayerController):
//   +0x220  Pawn   (APawn*)
//   +0x228  PlayerState (APlayerState*)
// APlayerController:
//   +0x298  PlayerCameraManager (APlayerCameraManager*)
//   +0x390  ControlRotation (FRotator) — local player rotation
static constexpr ptrdiff_t k_off_pc_camera_manager = 0x2B8;  // APlayerController::PlayerCameraManager (dump confirmed)
// APlayerCameraManager::CameraCache (FCameraCacheEntry) @ 0x0290
// FCameraCacheEntry::POV (FMinimalViewInfo)             @ +0x0010  → 0x02A0
// FMinimalViewInfo::Location                            @ +0x0000  → 0x02A0
// FMinimalViewInfo::Rotation                            @ +0x000C  → 0x02AC
// FMinimalViewInfo::FOV                                 @ +0x0018  → 0x02B8
static constexpr ptrdiff_t k_off_cam_loc            = 0x02A0;
static constexpr ptrdiff_t k_off_cam_rot            = 0x02AC;
static constexpr ptrdiff_t k_off_cam_fov            = 0x02B8;

// GEngine singleton — resolved once
inline uintptr_t* g_engine_ptr = nullptr;

inline bool init_engine() {
    // "48 8B 05 ? ? ? ? 48 8B 88 ? ? ? ? 48 85 C9 74 ? 48 8B 01 FF 90"
    // → GEngine global pointer
    uint8_t* hit = Pattern::find("48 8B 05 ? ? ? ? 48 8B 88 ? ? ? ? 48 85 C9 74");
    if (hit) {
        g_engine_ptr = reinterpret_cast<uintptr_t*>(Pattern::resolve_rip(hit, 3, 7));
        return g_engine_ptr != nullptr;
    }
    return false;
}

// Read local PlayerController from GEngine → GameViewport → GameInstance → LocalPlayers[0]
inline uintptr_t get_local_player_controller() {
    if (!g_engine_ptr) return 0;
    uintptr_t engine = *g_engine_ptr;
    if (!engine) return 0;

    // UEngine + 0x780 → GameViewport (UGameViewportClient*)  — dump: UEngine::GameViewport @ 0x0780
    uintptr_t viewport = *reinterpret_cast<uintptr_t*>(engine + 0x780);
    if (!viewport) return 0;
    // UGameViewportClient + 0x080 → GameInstance (UGameInstance*)  — dump: UGameViewportClient::GameInstance @ 0x0080
    uintptr_t game_instance = *reinterpret_cast<uintptr_t*>(viewport + 0x080);
    if (!game_instance) return 0;
    // UGameInstance + 0x38 → LocalPlayers (TArray<ULocalPlayer*>)
    auto* local_players = reinterpret_cast<TArray<uintptr_t>*>(game_instance + 0x38);
    if (!local_players || local_players->Num <= 0 || !local_players->Data) return 0;
    uintptr_t local_player = local_players->Data[0];
    if (!local_player) return 0;
    // ULocalPlayer + 0x30 → PlayerController (APlayerController*)  [4.27 standard]
    return *reinterpret_cast<uintptr_t*>(local_player + 0x30);
}

inline bool get_camera_state(CameraState& out) {
    uintptr_t pc = get_local_player_controller();
    if (!pc) return false;

    // APlayerController + k_off_pc_camera_manager → APlayerCameraManager*
    uintptr_t cam_mgr = *reinterpret_cast<uintptr_t*>(pc + k_off_pc_camera_manager);
    if (!cam_mgr) return false;

    out.location = *reinterpret_cast<FVector*>(cam_mgr + k_off_cam_loc);
    out.rotation = *reinterpret_cast<FRotator*>(cam_mgr + k_off_cam_rot);
    out.fov      = *reinterpret_cast<float*>   (cam_mgr + k_off_cam_fov);
    if (out.fov < 1.f || out.fov > 170.f) out.fov = 90.f;

    // Viewport size via GetSystemMetrics as reliable fallback
    // (works even in windowed mode since the game fills the client area)
    HWND hwnd = FindWindowA(nullptr, "OneArmedRobber"); // adjust if title differs
    if (hwnd) {
        RECT rc{};
        GetClientRect(hwnd, &rc);
        out.vp_w = static_cast<float>(rc.right  - rc.left);
        out.vp_h = static_cast<float>(rc.bottom - rc.top);
    } else {
        out.vp_w = static_cast<float>(GetSystemMetrics(SM_CXSCREEN));
        out.vp_h = static_cast<float>(GetSystemMetrics(SM_CYSCREEN));
    }
    if (out.vp_w < 1.f) out.vp_w = 1920.f;
    if (out.vp_h < 1.f) out.vp_h = 1080.f;

    float aspect = out.vp_w / out.vp_h;
    FMatrix view = make_view_matrix(out.location, out.rotation);
    FMatrix proj = make_proj_matrix(out.fov, aspect);
    out.vp_matrix = mul(view, proj);
    return true;
}

} // namespace Camera
