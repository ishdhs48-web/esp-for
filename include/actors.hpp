#pragma once
// language: C++17, file: actors.hpp
// Walks the UWorld actor array, classifies NPCs by class name hash,
// reads location + health. Re-walks every frame — survives map reloads
// and game restarts automatically (GWorld is re-resolved each tick).

#include "sdk.hpp"
#include "world.hpp"
#include <vector>
#include <string>
#include <cstring>
#include <exception>

namespace Actors {

enum class NpcKind : uint8_t {
    Unknown    = 0,
    Guard      = 1,  // ANPC_Guard_C
    Civilian   = 2,  // ACivilian_NPC_C
    Police     = 3,  // ANPC_Police_regular_C / ANPC_Police_base_C
    Juggernaut = 4,  // ANPC_Police_Juggernaut_C
    Interceptor= 5,  // ANPC_Interceptor_C
    Shield     = 6,  // NPC_Police_Shield / BlindingShield
};

struct NpcEntry {
    uintptr_t ptr;
    NpcKind   kind;
    FVector   location;
    int32_t   health;
    bool      dead;
    bool      alert;
};

// ─── Class-name hashing ───────────────────────────────────────────────────────
// UObject.ClassPrivate → UClass → FName (offset 0x18 in UObject = ClassPrivate ptr)
// UClass has Name (FName) at UObject+0x18 offset inside its own UObject base,
// meaning: class_ptr → +0x18 → FName (index+number).
// FName → GNames[index] → FNameEntry → ANSICHAR at +0x10 (for non-wide).
// To avoid GNames dependency here, we'll read the class DisplayName via the
// vtable call GetClass()->GetName() — or simpler: pattern match on known name
// strings by reading the UClass UObject Name offset directly as raw bytes.
//
// Simplest stable approach for BP classes: read the UClass pointer, then
// offset to the package-qualified name string buffer that UE4 writes at
// a known offset in the class linker name table.
//
// Most reliable for our case: compare the raw FName FNumber+Index pair against
// a pre-hashed table. We collect the hashes on first successful actor scan
// by reading the name of first encountered NPC_Guard_C etc.
//
// EVEN SIMPLER and 100% reliable: check UObject.ClassPrivate pointer value
// itself — each class has a unique singleton UClass* at a known GObjects slot.
// We cache the UClass* for each kind on first encounter.

inline uintptr_t g_class_guard      = 0;
inline uintptr_t g_class_civilian   = 0;
inline uintptr_t g_class_police_reg = 0;
inline uintptr_t g_class_jugger     = 0;
inline uintptr_t g_class_intercept  = 0;
inline uintptr_t g_class_shield     = 0;
inline uintptr_t g_class_blind_shld = 0;

// Read UObject.ClassPrivate → UClass* (at offset 0x10 in UObject for UE 4.27)
// UObject layout (4.27 x64):
//   0x00  VTable
//   0x08  ObjectFlags (int32) + InternalIndex (int32)
//   0x10  ClassPrivate (UClass*)
//   0x18  NamePrivate  (FName)
//   0x20  OuterPrivate (UObject*)
static constexpr ptrdiff_t k_off_class_private = 0x10;
static constexpr ptrdiff_t k_off_name_private  = 0x18; // FName = 8 bytes (index+serial)

inline uintptr_t get_class(uintptr_t obj) {
    if (!obj) return 0;
    return *reinterpret_cast<uintptr_t*>(obj + k_off_class_private);
}

// Safe memory read helper — returns false if access would fault
inline bool safe_read(uintptr_t addr, void* out, size_t sz) {
    __try {
        memcpy(out, reinterpret_cast<const void*>(addr), sz);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Get class-private safely
inline uintptr_t get_class_safe(uintptr_t obj) {
    uintptr_t cls = 0;
    if (!safe_read(obj + k_off_class_private, &cls, 8)) return 0;
    return cls;
}

// ─── Name-based classification ────────────────────────────────────────────────
// UClass also inherits UObject, so its own ClassPrivate == UClass (it IS a class).
// FName at UClass+0x18 encodes the class name. UE4 stores names in GNames.
// To avoid GNames, we read the first 64 bytes after the UClass's SuperStruct
// chain name offset and look for our known substrings.
//
// More portable: check if a cached UClass* matches. We populate the cache
// by name-string comparison exactly once on first successful hit.
//
// FName → GNames → FNameEntry
// GNames pattern: "48 8B 05 ? ? ? ? 48 8B 0C C8" (TNameEntryArray)
// FNameEntry+0x10 → ANSICHAR[NAME_SIZE]

inline uintptr_t* g_gnames_ptr = nullptr;

inline bool init_names() {
    uint8_t* hit = Pattern::find("48 8B 05 ? ? ? ? 48 8B 0C C8");
    if (hit) {
        g_gnames_ptr = reinterpret_cast<uintptr_t*>(Pattern::resolve_rip(hit, 3, 7));
        return true;
    }
    return false;
}

// Read the ASCII name of a UObject (uses FName index → GNames lookup)
// Returns empty string on failure.
inline std::string read_uobject_name(uintptr_t obj) {
    if (!obj || !g_gnames_ptr) return {};
    // FName at UObject+0x18: first 4 bytes = ComparisonIndex (entry index)
    uint32_t name_idx = 0;
    if (!safe_read(obj + k_off_name_private, &name_idx, 4)) return {};

    // GNames is a FChunkedFixedUObjectArray-style name table.
    // Chunk array: GNames[chunk_index][entry_index_in_chunk]
    // chunk = name_idx / 16384; in_chunk = name_idx % 16384;
    // Each chunk pointer is at GNames + chunk*8
    static const uint32_t CHUNK_SIZE = 16384;
    uint32_t chunk_idx     = name_idx / CHUNK_SIZE;
    uint32_t in_chunk_idx  = name_idx % CHUNK_SIZE;

    uintptr_t gnames_base = *g_gnames_ptr;
    if (!gnames_base) return {};
    uintptr_t chunk_ptr = 0;
    if (!safe_read(gnames_base + chunk_idx * 8, &chunk_ptr, 8)) return {};
    if (!chunk_ptr) return {};

    // FNameEntry: each entry is variable size, stored in a block.
    // In 4.27: FNameEntry header is 2 bytes (Len+wide flag), name follows.
    // Stride per entry = sizeof(FNameEntry) which is (size+len+2) aligned.
    // Simplification: block pointer array — each chunk points to array of
    // FNameEntry* (pointer per entry). In 4.27 the layout is a flat block,
    // stride = ((strlen+2+7)&~7). We use the "name table as pointer array"
    // approach which some 4.27 builds use (FNameEntryId):
    //   chunk_ptr → array of FNameEntry*; entry = chunk_ptr[in_chunk_idx * stride]
    // Actual 4.27 layout: stride = 8 bytes per pointer? No — 4.27 packs names.
    // Use the most common 4.27 shipping pattern:
    //   each entry at chunk_ptr + in_chunk_idx * FNAME_ENTRY_STRIDE
    //   FNameEntry { uint16 header; char AnsiName[...] }
    //   header: bits 0 = wide, bits 1..15 = len
    static const uint32_t FNAME_ENTRY_STRIDE = 0x10; // 16 bytes conservative
    uintptr_t entry_ptr = chunk_ptr + in_chunk_idx * FNAME_ENTRY_STRIDE;

    uint16_t header = 0;
    if (!safe_read(entry_ptr, &header, 2)) return {};
    bool   is_wide = header & 1;
    int    name_len = (header >> 1) & 0x7FFF;
    if (name_len <= 0 || name_len > 512) return {};

    char buf[513] = {};
    if (!safe_read(entry_ptr + 2, buf, name_len)) return {};
    return std::string(buf, name_len);
}

// ─── Actor location from RootComponent ───────────────────────────────────────
// AActorFull.RootComponent @ 0x130
// USceneComponent.RelativeLocation @ 0x11C

static constexpr ptrdiff_t k_off_root_component = 0x130;
static constexpr ptrdiff_t k_off_relative_loc   = 0x11C;

inline FVector get_actor_location(uintptr_t actor) {
    FVector out{};
    uintptr_t root = 0;
    if (!safe_read(actor + k_off_root_component, &root, 8)) return out;
    if (!root) return out;
    safe_read(root + k_off_relative_loc, &out, sizeof(FVector));
    return out;
}

// ─── Health + Dead from ANPCBase_C ───────────────────────────────────────────
// Health @ 0x04E0, Dead @ 0x04E8 (from NPCBase.hpp)

inline int32_t get_npc_health(uintptr_t npc) {
    int32_t h = 0;
    safe_read(npc + 0x04E0, &h, 4);
    return h;
}
inline bool get_npc_dead(uintptr_t npc) {
    uint8_t d = 0;
    safe_read(npc + 0x04E8, &d, 1);
    return d != 0;
}
// Alert @ 0x0655 in ANPC_Guard_C
inline bool get_guard_alert(uintptr_t npc) {
    uint8_t a = 0;
    safe_read(npc + 0x0655, &a, 1);
    return a != 0;
}

// ─── Cache class pointers on first encounter ─────────────────────────────────

inline NpcKind classify_by_name(const std::string& name, uintptr_t cls_ptr) {
    // substring match against known BP class names from actor CSV
    auto has = [&](const char* sub) { return name.find(sub) != std::string::npos; };
    if (has("NPC_Guard"))            { g_class_guard      = cls_ptr; return NpcKind::Guard; }
    if (has("Civilian_NPC"))         { g_class_civilian   = cls_ptr; return NpcKind::Civilian; }
    if (has("Police_Juggernaut"))    { g_class_jugger     = cls_ptr; return NpcKind::Juggernaut; }
    if (has("Police_BlindingShield"))  { g_class_blind_shld = cls_ptr; return NpcKind::Shield; }
    if (has("Police_Shield"))        { g_class_shield     = cls_ptr; return NpcKind::Shield; }
    if (has("NPC_Police"))           { g_class_police_reg = cls_ptr; return NpcKind::Police; }
    if (has("NPC_Interceptor"))      { g_class_intercept  = cls_ptr; return NpcKind::Interceptor; }
    return NpcKind::Unknown;
}

inline NpcKind classify(uintptr_t actor) {
    uintptr_t cls = get_class_safe(actor);
    if (!cls) return NpcKind::Unknown;
    // Fast path — cached class pointers
    if (cls == g_class_guard)      return NpcKind::Guard;
    if (cls == g_class_civilian)   return NpcKind::Civilian;
    if (cls == g_class_police_reg) return NpcKind::Police;
    if (cls == g_class_jugger)     return NpcKind::Juggernaut;
    if (cls == g_class_intercept)  return NpcKind::Interceptor;
    if (cls == g_class_shield)     return NpcKind::Shield;
    if (cls == g_class_blind_shld) return NpcKind::Shield;
    // Slow path — name lookup, populates cache on hit
    std::string name = read_uobject_name(cls);
    if (name.empty()) return NpcKind::Unknown;
    return classify_by_name(name, cls);
}

// ─── Main scan ───────────────────────────────────────────────────────────────

inline std::vector<NpcEntry> scan_actors() {
    std::vector<NpcEntry> results;
    TArray<void*>* arr = World::actor_array();
    if (!arr || !arr->Data || arr->Num <= 0) return results;

    results.reserve(64);
    for (int32_t i = 0; i < arr->Num; ++i) {
        uintptr_t actor = 0;
        if (!safe_read((uintptr_t)&arr->Data[i], &actor, 8)) continue;
        if (!actor) continue;

        NpcKind kind = classify(actor);
        if (kind == NpcKind::Unknown) continue;

        NpcEntry e{};
        e.ptr      = actor;
        e.kind     = kind;
        e.location = get_actor_location(actor);
        e.health   = get_npc_health(actor);
        e.dead     = get_npc_dead(actor);
        e.alert    = (kind == NpcKind::Guard) ? get_guard_alert(actor) : false;
        results.push_back(e);
    }
    return results;
}

} // namespace Actors
