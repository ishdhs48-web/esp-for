#pragma once
// language: C++17, file: world.hpp
// GWorld resolution + world-to-screen projection for UE4 shipped builds.

#include "sdk.hpp"
#include "pattern.hpp"
#include <cmath>
#include <optional>

// ─── GWorld pointer ──────────────────────────────────────────────────────────
// UE 4.27 shipping signature for the GWorld assignment in FWorldContext::SetCurrentWorld:
//   48 8B 1D ? ? ? ?   ; mov rbx, [rip + GWorld]
// The 3-byte prefix is unique enough for one-armed robber's build.
// Alternate: 48 89 ? ? ? ? ? 48 8B  (store-to-GWorld pattern)

namespace World {

inline UWorld** g_world_ptr = nullptr;

// Call once from DllMain after game is loaded.
inline bool init() {
    // Primary sig — read from GWorld store in UGameEngine::LoadMap
    // "48 8B 1D ? ? ? ? 48 85 DB" — mov rbx,[GWorld]; test rbx,rbx
    uint8_t* hit = Pattern::find("48 8B 1D ? ? ? ? 48 85 DB");
    if (hit) {
        g_world_ptr = reinterpret_cast<UWorld**>(Pattern::resolve_rip(hit, 3, 7));
        if (g_world_ptr) return true;
    }
    // Fallback — GWorld write pattern "48 89 05 ? ? ? ?"
    hit = Pattern::find("48 89 05 ? ? ? ? 48 8B ? ? ? ? ? 48 85");
    if (hit) {
        g_world_ptr = reinterpret_cast<UWorld**>(Pattern::resolve_rip(hit, 3, 7));
        return g_world_ptr != nullptr;
    }
    return false;
}

inline UWorld* get() {
    if (!g_world_ptr) return nullptr;
    return *g_world_ptr;
}

inline ULevel* persistent_level() {
    UWorld* w = get();
    if (!w) return nullptr;
    return *reinterpret_cast<ULevel**>((uint8_t*)w + 0x30);
}

inline TArray<void*>* actor_array() {
    ULevel* lv = persistent_level();
    if (!lv) return nullptr;
    return reinterpret_cast<TArray<void*>*>((uint8_t*)lv + 0x98);
}

} // namespace World

// ─── Camera / Projection ─────────────────────────────────────────────────────
// FMinimalViewInfo is retrieved via APlayerCameraManager::GetCameraView
// We'll reach it through PlayerController → CameraManager, or cache per-frame
// by hooking/reading from the FSceneView pattern.
//
// Simpler reliable approach for external DLL:
//   GEngine → GameViewport → Viewport (FViewport*) → GetSizeXY
//   Camera transform: APlayerController → ControlRotation + PlayerPawn location
//
// Full matrix approach — build a view-projection matrix every frame from:
//   - camera location   (from APlayerCameraManager @ PC+0x298 → GetCameraLocation)
//   - camera rotation   (FRotator)
//   - FOV               (float, default 90°)
//   - viewport size     (GetSystemMetrics(SM_CXSCREEN) as fallback; proper via GEngine)

struct FMatrix {
    float M[4][4];
};

inline FMatrix make_view_matrix(FVector loc, FRotator rot) {
    float pitch = rot.Pitch * (3.14159265f / 180.f);
    float yaw   = rot.Yaw   * (3.14159265f / 180.f);
    float roll  = rot.Roll  * (3.14159265f / 180.f);

    float sp = sinf(pitch), cp = cosf(pitch);
    float sy = sinf(yaw),   cy = cosf(yaw);
    float sr = sinf(roll),  cr = cosf(roll);

    // UE4 axis: X=forward, Y=right, Z=up  (left-handed, Z-up)
    FMatrix m{};
    m.M[0][0] = cp * cy;
    m.M[0][1] = cp * sy;
    m.M[0][2] = sp;
    m.M[0][3] = 0.f;

    m.M[1][0] = sr * sp * cy - cr * sy;
    m.M[1][1] = sr * sp * sy + cr * cy;
    m.M[1][2] = -sr * cp;
    m.M[1][3] = 0.f;

    m.M[2][0] = -(cr * sp * cy + sr * sy);
    m.M[2][1] = cy * sr - cr * sp * sy;
    m.M[2][2] = cr * cp;
    m.M[2][3] = 0.f;

    m.M[3][0] = -(m.M[0][0]*loc.X + m.M[1][0]*loc.Y + m.M[2][0]*loc.Z);
    m.M[3][1] = -(m.M[0][1]*loc.X + m.M[1][1]*loc.Y + m.M[2][1]*loc.Z);
    m.M[3][2] = -(m.M[0][2]*loc.X + m.M[1][2]*loc.Y + m.M[2][2]*loc.Z);
    m.M[3][3] = 1.f;
    return m;
}

inline FMatrix make_proj_matrix(float fov_deg, float aspect, float near_z = 10.f) {
    float half = tanf((fov_deg * 3.14159265f / 180.f) * 0.5f);
    FMatrix m{};
    m.M[0][0] = 1.f / (aspect * half);
    m.M[1][1] = 1.f / half;
    m.M[2][2] = 0.f;    // UE4 reverse-Z
    m.M[2][3] = 1.f;
    m.M[3][2] = near_z;
    m.M[3][3] = 0.f;
    return m;
}

inline FMatrix mul(const FMatrix& a, const FMatrix& b) {
    FMatrix r{};
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            for (int k = 0; k < 4; ++k)
                r.M[i][j] += a.M[i][k] * b.M[k][j];
    return r;
}

struct CameraState {
    FVector  location;
    FRotator rotation;
    float    fov;       // horizontal degrees
    float    vp_w, vp_h;
    FMatrix  vp_matrix; // view * proj cached
};

// world_to_screen: returns false if behind camera.
inline bool world_to_screen(const CameraState& cam, FVector world, FVector2D& out) {
    // Clip-space transform
    float x = world.X - cam.location.X;
    float y = world.Y - cam.location.Y;
    float z = world.Z - cam.location.Z;

    const auto& m = cam.vp_matrix;
    float cx = x*m.M[0][0] + y*m.M[1][0] + z*m.M[2][0] + m.M[3][0];
    float cy = x*m.M[0][1] + y*m.M[1][1] + z*m.M[2][1] + m.M[3][1];
    float cz = x*m.M[0][2] + y*m.M[1][2] + z*m.M[2][2] + m.M[3][2];
    float cw = x*m.M[0][3] + y*m.M[1][3] + z*m.M[2][3] + m.M[3][3];

    if (cw < 0.001f) return false; // behind camera

    float ndc_x = cx / cw;
    float ndc_y = cy / cw;

    out.X = (1.f + ndc_x) * 0.5f * cam.vp_w;
    out.Y = (1.f - ndc_y) * 0.5f * cam.vp_h;
    return true;
}
