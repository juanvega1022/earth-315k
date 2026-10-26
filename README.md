# Earth 315k

A simulation of Earth starting 315,000 years ago, around the time of the oldest known *Homo sapiens* fossils. The planet is built from real elevation data, with seeded procedural generation (the No Man's Sky idea) adding detail finer than the data. AI NPCs with early-human traits come later.

This first version shows a 3D globe of the whole planet, with a higher-detail patch over Africa, Arabia and the Levant.

## What you need (Windows)

- **Unreal Engine 5.4 or newer** from the Epic Games Launcher
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

   This downloads NOAA's ETOPO 2022 global elevation model (about 1 GB, once) and writes `global` and `region` files into `Content/Data/`. If the download fails, download the 60 arc-second "surface" GeoTIFF by hand from <https://www.ncei.noaa.gov/products/etopo-global-relief-model> and run `python tools/prepare_elevation.py --source path\to\file.tif`.

3. **Open the project.** Right-click `Earth315k.uproject` and pick "Generate Visual Studio project files", then double-click `Earth315k.uproject`. Say yes when Unreal asks to build the `Earth315k` module. The first build takes a few minutes.

4. **Press Play.** In the editor, File > New Level > Empty Level, then press Play. The planet, a sun and a flying camera appear automatically. Fly with W A S D, Q and E for down and up, and the mouse to look.

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
- `Source/Earth315k/Public/PlanetCore/` is the planet maths in plain C++ with no Unreal code: elevation sampling, seeded noise, cube-sphere geometry and terrain colouring. It is unit tested on its own (`tests/core_tests.cpp`).
- `APlanetGlobe` builds the mesh: six cube faces for the globe, plus a latitude/longitude patch for the region where the noise adds detail. Real data decides where land and sea are, and the noise is never allowed to move a coastline.
- `AEarthGameMode` sets up the planet, sun and camera when you press Play.

## Known limits of this first version

- Colours are a placeholder by height and latitude, not a real climate or vegetation model.
- Sea level is the only change from today's map. Ice sheets, the Green Sahara and lakes of 315,000 years ago are not modelled yet.
- The planet is not to real scale; it's a model globe you fly around.
- The engine's built-in vertex colour material is used, so lighting is basic.

## Running the tests

```
g++ -std=c++17 -O2 -I Source/Earth315k/Public tests/core_tests.cpp -o tests/build/core_tests && tests/build/core_tests
python tests/test_prepare_elevation.py
```
