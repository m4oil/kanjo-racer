# KANJO RACER: BEAR PRODUCTS

Dedicated to Collin.

A top-down GBA racer: EF-style hatch, a night-time expressway loop, an AI rival,
3-lap races and a small tuning shop. Built with Butano.

## Getting the ROM (no installs needed)
1. Put these files in a GitHub repository (see the steps you were given).
2. Open the **Actions** tab and wait for "Build GBA ROM" to finish (about 3-6 minutes).
3. Download `kanjo_racer.gba` from the **Releases** section on the repo's main page
   (or from the run's "Artifacts" at the bottom, which is a zip).
4. Play it in an emulator (mGBA, Delta on iPhone, My Boy! on Android) or a flash cart.

## Controls
Title: START or A. In game: A gas, B brake, Left/Right steer, START = tune shop
(UP/DOWN pick part, LEFT/RIGHT change level, START back).
After a race: A restarts, B returns to the title screen.

## For editing later
- `src/main.cpp`   game code (physics in `compute_stats()` and `update_car()`)
- `graphics/`      the art (BMP files); regenerate with `python tools/make_assets.py`
- `.github/workflows/build.yml`   the cloud build recipe
