# OAR ESP — One Armed Robber

External-DLL ESP for **One Armed Robber** (UE 4.27 x64).  
No ImGui. No DirectX. GDI layered overlay. Survives game restarts.

---

## Features

| Toggle | Default | Key |
|---|---|---|
| Guards (with alert flash) | ON | menu |
| Civilians | ON | menu |
| Police (regular + shield) | ON | menu |
| Juggernaut | ON | menu |
| Interceptor | ON | menu |
| Health bars + HP numbers | ON | menu |
| Distance (metres) | ON | menu |
| Show dead NPCs | OFF | menu |

**Hotkeys**
- `INSERT` — open/close menu  
- `UP` / `DOWN` — navigate items  
- `ENTER` — toggle selected item  
- `DELETE` — unload DLL  

---

## Build (local, MSVC x64)

```bat
# Requires Visual Studio 2022 with C++ tools + CMake
cmake -B build -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build
:: output: build\oar_esp.dll
```

Or open a **Visual Studio x64 Developer Command Prompt** and run the same.

---

## Build (GitHub Actions)

Push to `main`. The `Build oar_esp DLL` workflow runs automatically.  
Download `oar_esp.dll` from the **Actions → Artifacts** section.  
`workflow_dispatch` also uploads the PDB for debugging.

---

## Injection

Any standard DLL injector works. Recommended: **Process Hacker 2** → right-click the game process → *Inject DLL*.

1. Launch the game.
2. Wait for the main menu / in-game to load fully (~5–10 s).
3. Inject `oar_esp.dll`.
4. Overlay appears automatically over the game window.

**Restart survival**: the DLL stays injected. When the game reloads a map,  
`World::get()` re-reads the live `GWorld` pointer every ESP frame — no re-inject needed.

---

## Per-build derivation notes

UE4.27 ships a fixed `GWorld` and `GEngine` ABI across most builds.  
If the game updates and patterns break, re-derive with:

```
x64dbg → Attach → search "48 8B 1D ? ? ? ? 48 85 DB" in .text section
→ the 4-byte RIP disp at +3 → add to (instruction start + 7) → GWorld address
```

`APlayerCameraManager` offset (`k_off_pc_camera_manager = 0x2B8`) and  
`FCachedCameraView` offsets (`0x1EB0 / 0x1EBC / 0x1EC8`) are standard 4.27 —  
re-derive in Ghidra if the game ships an unusual UE fork:  
search `APlayerController::GetPlayerViewPoint` → follow the CameraManager reference.

---

## File layout

```
oar_esp/
├── include/
│   ├── sdk.hpp        — minimal UE4 type stubs (offsets from CXXHeaderDump)
│   ├── pattern.hpp    — byte pattern scanner
│   ├── world.hpp      — GWorld resolution + world-to-screen math
│   ├── camera.hpp     — GEngine → PlayerCameraManager state reader
│   ├── actors.hpp     — UWorld actor array walker + NPC classifier
│   └── overlay.hpp    — GDI layered overlay + menu renderer
├── src/
│   └── dllmain.cpp    — wires everything; DLL entry point
├── CMakeLists.txt
├── .github/
│   └── workflows/
│       └── build.yml  — MSVC x64 Release CI
└── README.md
```
