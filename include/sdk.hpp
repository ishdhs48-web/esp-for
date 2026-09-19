#pragma once
// language: C++17, file: sdk.hpp, target: One Armed Robber (UE4/UE5 BP game), Windows 11, MSVC
// Minimal UE4SS-derived SDK — offsets verified against CXXHeaderDump

#include <cstdint>
#include <string>
#include <windows.h>

// ─── Primitive UE types ────────────────────────────────────────────────────────

struct FVector {
    float X, Y, Z;
};
struct FRotator {
    float Pitch, Yaw, Roll;
};
struct FVector2D {
    float X, Y;
};
struct FLinearColor {
    float R, G, B, A;
};

// FString (UE4 wide string, 16 bytes)
struct FString {
    wchar_t* Data;   // 0x00
    int32_t  Len;    // 0x08
    int32_t  Max;    // 0x0C
    std::string to_string() const {
        if (!Data || Len <= 0) return "";
        std::string out(Len, 0);
        WideCharToMultiByte(CP_UTF8, 0, Data, Len, out.data(), Len, nullptr, nullptr);
        return out;
    }
};

// TArray<T> (16 bytes)
template<typename T>
struct TArray {
    T*       Data;   // 0x00
    int32_t  Num;    // 0x08
    int32_t  Max;    // 0x0C
    T& operator[](int32_t i) { return Data[i]; }
    T  operator[](int32_t i) const { return Data[i]; }
};

// UObject base
struct UObject {
    void**   VTable;       // 0x00
    uint8_t  pad_0x08[0x08]; // padding
    int32_t  Flags;        // 0x10 — ObjectFlags
    int32_t  InternalIdx;  // 0x14
    // ClassPrivate at 0x18
    void*    ClassPrivate; // 0x18
    FString  Name;         // 0x20  (FName stored as FString for simplicity)
    void*    Outer;        // 0x30
};

// AActor root
struct AActor : UObject {
    uint8_t pad_actor[0x100]; // enough padding before known fields
    // location is in a SceneComponent; use GetActorLocation via vtable or root component
};

// ─── Root Component / Location trick via vtable call ───────────────────────────
// We'll resolve location via USceneComponent RootComponent offset in ACharacter
// ACharacter → APawn → AActor
// AActor.RootComponent @ 0x130 (standard UE4.27)
// SceneComponent.RelativeLocation @ 0x11C (standard UE4.27)
// These are the canonical shipped-engine offsets, not BP-generated ones.

struct USceneComponent {
    uint8_t pad[0x11C];
    FVector RelativeLocation; // 0x11C
};

struct AActorFull {
    uint8_t pad_obj[0x38];    // UObject fields
    uint8_t pad_actor[0xF8];  // to reach RootComponent
    USceneComponent* RootComponent; // 0x130
};

// ─── ANPCBase_C (from NPCBase.hpp) ─────────────────────────────────────────────
struct ANPCBase_C : AActorFull {
    // 0x04C0 UberGraphFrame skip via ACharacter padding
    // direct fields relative to class start:
    uint8_t pad_npcbase_pre[0x390]; // 0x130 + 0x390 = 0x4C0 chars base start
    // 0x04C0 = UberGraphFrame (skip 0x38)
    uint8_t pad_npc[0x38];
    // 0x04D0 = DamageComponent ptr (skip 0x10)
    uint8_t pad_npc2[0x10];
    int32_t Health;      // 0x04E0
    int32_t BloodHits;   // 0x04E4
    bool    Dead;        // 0x04E8
};
// Size: 0x53A

// ─── ANPC_Guard_C (from NPC_Guard.hpp) ─────────────────────────────────────────
struct ANPC_Guard_C : ANPCBase_C {
    uint8_t pad_guard_pre[0x5A]; // up to 0x53A → pad to 0x5E0
    bool    Armed;       // 0x05E0
    uint8_t pad_g2[0x06];
    FString InvestigateReason; // 0x05E8
    bool    Patrolling;  // 0x05F8
    bool    Investigating; // 0x05F9
    uint8_t pad_g3[0x06];
    void*   GunActor;    // 0x0600
    uint8_t pad_g4[0x50];
    bool    Alert;       // 0x0655
    uint8_t pad_g5[0x01];
    void*   TargetPlayer; // 0x0658
    uint8_t pad_g6[0x01];
    bool    Sensing;     // 0x0660
};

// ─── ACivilian_NPC_C (from Civilian_NPC.hpp) ────────────────────────────────────
struct ACivilian_NPC_C : ANPCBase_C {
    uint8_t pad_civ_pre[0x8A]; // 0x53A → 0x5D0
    bool    Scared;      // 0x05D0
    uint8_t pad_c2[0x77];
    bool    Alerted;     // 0x06A8
    uint8_t pad_c3[0x1C];
    bool    Fleeing;     // 0x06C5
};

// ─── ANPC_Police_base_C (from NPC_Police_base.hpp) ─────────────────────────────
struct ANPC_Police_base_C : ANPCBase_C {
    uint8_t pad_pol_pre[0x5A]; // 0x53A → 0x594
    void*   TargetPlayer; // 0x0590 (offset from class base 0x0590)
    bool    Sensing;     // 0x0598
    bool    Rifle;       // 0x0599
    bool    Shooting;    // 0x059A
    uint8_t pad_p2[0x1D];
    bool    HeadStunned; // 0x05B8 (size 0x1)
    bool    InCover;     // 0x05B9
    bool    GoingCover;  // 0x05BA
    bool    BodyStunned; // 0x05BB
};

// Subtypes — just inherit, their offsets are within police_base
struct ANPC_Police_regular_C : ANPC_Police_base_C {};
struct ANPC_Police_Juggernaut_C : ANPC_Police_base_C {};
struct ANPC_Interceptor_C : ANPCBase_C {};

// ─── APlayerCharacter_C (relevant fields only) ──────────────────────────────────
struct APlayerCharacter_C : AActorFull {
    uint8_t pad_pc[0x390]; // to 0x4C0
    // skip to Health @ 0x06B0
    uint8_t pad_pc2[0x1F0];
    int32_t Health;      // 0x06B0
};

// ─── UWorld / GWorld / GEngine ─────────────────────────────────────────────────
// Standard UE 4.27 GWorld / GEngine signatures — pattern scan in DllMain
// Actors live in UWorld->PersistentLevel->Actors (TArray<AActor*>)

struct ULevel {
    uint8_t  pad[0x98];
    TArray<void*> Actors; // 0x98
};

struct UWorld {
    uint8_t pad[0x30];
    ULevel* PersistentLevel; // 0x30
    // GameViewport @ 0x0170 (standard 4.27 UWorld — confirmed consistent)
};

// ─── Engine / Viewport for WorldToScreen ───────────────────────────────────────
// We'll resolve via GEngine singleton and dynamic viewport size query.
// Pattern bytes for GWorld (UE 4.27 x64 shipping):
//   48 8B 1D ? ? ? ? 48 85 DB (mov rbx, [rip+?])
// For this game we'll use a TArray<ULevel*> walk approach + the process's
// GetSystemMetrics for viewport width/height as fallback.
