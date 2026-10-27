# Earth 315k

A simulation of Earth starting 315,000 years ago, around the time of the oldest known *Homo sapiens* fossils. The planet is built from real elevation data, with seeded procedural generation (the No Man's Sky idea) adding detail finer than the data. AI NPCs with early-human traits come later.

The goal is to watch AI NPCs grow and develop from early-human starting traits. The work goes in steps:

1. **The planet and its life (now).** A 3D globe of the whole planet from real elevation data, with a higher-detail patch over Africa, Arabia and the Levant. Plants grow and die back with the seasons, and 25 animal species of the time (elephants, mammoths, cave lions, Smilodon, Diprotodon and more) graze, hunt, breed and die out.
2. **The first NPC band** in East Africa, with needs, a small brain that learns, and children who inherit from parents.
3. **Watching tools:** time speed, following one NPC, and a timeline of firsts.

## What you need (Windows)

- **Unreal Engine 5.8** (5.4 or newer should also work) from the Epic Games Launcher
- **Visual Studio 2022** with the "Game development with C++" workload (the Epic installer guide lists the exact options)
- **Python 3.10 or newer** from python.org (tick "Add Python to PATH" when installing)
- **Git**, or GitHub Desktop, to download this repository
- About 2 GB of free disk space for the elevation download

An RTX 4060 is plenty for this.

## First run

1. **Download this repository** to your PC (GitHub Desktop: File > Clone repository > `earth-315k`).

2. **Fetch the real elevation data.** Open a terminal in the repository folder and run:

   ```
   pip install -r tools/requirements.txt
   python tools/prepare_elevation.py
   ```

   This downloads NOAA's ETOPO 2022 global elevation model (about 450 MB, once) and writes `global` and `region` files into `Content/Data/`. If the download fails, download the 60 arc-second "surface" GeoTIFF by hand from <https://www.ncei.noaa.gov/products/etopo-global-relief-model> and run `python tools/prepare_elevation.py --source path\to\file.tif`.

3. **Open the project.** Right-click `Earth315k.uproject` and pick "Generate Visual Studio project files", then double-click `Earth315k.uproject`. Say yes when Unreal asks to build the `Earth315k` module. The first build takes a few minutes.

4. **Press Play.** In the editor, File > New Level > Empty Level, then press Play. The planet, its plants and animals, a sun and a flying camera appear automatically. Fly with W A S D, Q and E for down and up, and the mouse to look.

## Watching life

Land is coloured by plant cover: deep green where plants are thick, brown where they are thin. Each coloured ball is a group of animals of one species, bigger for bigger animals and bigger herds. The top left of the screen shows the year, the season, how many animals are alive and which species have died out.

| Key | What it does |
| --- | --- |
| `]` | Twice as fast (up to 100 years per second, if your PC keeps up) |
| `[` | Half as fast |
| `P` | Pause or carry on |

Drag a `LifeSimulation` into the level to change its `Seed`, starting speed or marker size.

## Running life without graphics

The simulation itself is plain C++ in `Simulation/` and can run on its own, much faster than with graphics. With CMake installed (Visual Studio includes it):

```
cmake -S Simulation -B Simulation/build
cmake --build Simulation/build --config Release
Simulation/build/Release/earth315k_sim --years 1000 --csv populations.csv
```

(On Linux or macOS the program is at `Simulation/build/earth315k_sim`.) It prints each decade's populations and any species that died out, and `--csv` saves the populations for charts. Run it with no data and it tells you to fetch the elevation first.

To change the planet while editing, drag `PlanetGlobe` from the Place Actors panel into a level, then change its settings in the Details panel. The planet rebuilds as you change them.

## Settings worth playing with

| Setting | What it does |
| --- | --- |
| `SeaLevelMeters` | Sea level compared with today. Try `-120` to see ice-age coastlines and land bridges. |
| `ElevationExaggeration` | How much mountains are stretched so they show at globe size. |
| `Seed` | Changes the procedural detail. The same seed always gives the same planet. |
| `FaceResolution` / `RegionResolution` | Mesh detail for the globe and the region. Higher is slower to build. |
| `DetailAmplitudeMeters` | Height of procedural detail in the region. `0` shows real data only. |

## How it works

- `tools/prepare_elevation.py` turns the ETOPO model into a coarse global grid and a detailed regional grid.
- `Simulation/include/PlanetCore/` is the simulation in plain C++ with no Unreal code, so it can be tested and run on its own: elevation sampling, seeded noise, cube-sphere geometry and terrain colouring, plus `Ecosystem.h` (plants and animals) and `Species.h` (the species list, with sizes, diets, ranges and speeds). The same seed always gives the same history.
- `ALifeSimulation` runs the ecosystem inside Unreal and draws it on the globe.
- `APlanetGlobe` builds the mesh: six cube faces for the globe, plus a latitude/longitude patch for the region where the noise adds detail. Real data decides where land and sea are, and the noise is never allowed to move a coastline.
- `AEarthGameMode` sets up the planet, sun and camera when you press Play.

## Known limits of this first version

- Plant growth comes from latitude, height and a desert belt, not a real climate model, and it is worked out in 2° squares.
- Animals move as herds and packs, not as individuals. Some species don't balance yet: wild horses multiply until they fill the group limit, and some hunters die out early.
- Sea level is the only change from today's map. Ice sheets, the Green Sahara and lakes of 315,000 years ago are not modelled yet.
- The planet is not to real scale; it's a model globe you fly around.
- The engine's built-in vertex colour material is used, so lighting is basic.

## Running the tests

```
cmake -S Simulation -B Simulation/build
cmake --build Simulation/build --config Release
ctest --test-dir Simulation/build -C Release
python tests/test_prepare_elevation.py
```
