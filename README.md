# HOLLOWMERE

Found-footage survival horror. You went camping, got lost, and a house with its lights on is the only way out of the woods.
C++17, Win32 + Direct3D 12 (HLSL SM5), no external dependencies, everything (geometry, textures, audio) is procedural.

Build: `build.bat` (needs Visual Studio with the C++ workload) -> `hollowmere.exe`

Controls: WASD move, Shift run, C crouch, mouse look, F flashlight, N night vision, E interact (hold E on a seal), Esc pause, F11 fullscreen.

Goal: reach the house, break the four seals (hold E), then run south to the car. The entity only moves while you are NOT
lighting it (torch on it, or very close, or night vision within 16 m). Batteries drain the torch and night vision.

Tools: `shot.ps1 -name x -a "--frames 20 --at X Z YAW PITCH [--level 1] [--stage 1] [--ent DIST] [--nv] [--menu|--dead|--win]"`
renders a still to shots\x.png. `test_sim.cpp` is a headless gameplay test (door, stairs, seals, AI, loop, win).

## Textures
Photoscanned CC0 materials from Poly Haven (https://polyhaven.com), 1k JPG, in `textures\` next to the exe
(forest_ground_04, bark_brown_02, wood_floor_worn, weathered_planks, decrepit_wallpaper, dark_wood, mossy_rock,
roof_slates_02, rusty_metal_02, dirty_carpet). Loaded at start-up with WIC; the game still runs (flat grey) if the folder is missing.
Keep the `textures` folder beside hollowmere.exe when moving the game.
