"""Turn WorldClim's present-day climate into the grids the simulation reads.

Writes to Content/Data/:
  temperature.bin / .json   yearly mean temperature, tenths of a degree C (int16)
  rain.bin / .json          yearly rain, mm (int16)

Same layout as the elevation grids: little-endian int16, rows north to south. Sea squares take
the value of the nearest land, so sampling along a coast never mixes in "no data".
The simulation moves this climate back in time itself (colder and drier in the ice ages).

Usage:
  python tools/prepare_climate.py                       # download WorldClim 2.1 (10 arc-min) and build both
  python tools/prepare_climate.py --source path\\to.zip  # use the zip you downloaded yourself
"""

import argparse
import json
import zipfile
from pathlib import Path

import numpy as np

from prepare_elevation import DEFAULT_CACHE, DEFAULT_OUT, download, downsample, read_geotiff

WORLDCLIM_URL = "https://geodata.ucdavis.edu/climate/worldclim/2_1/base/wc2.1_10m_bio.zip"
WORLDCLIM_PAGE = "https://www.worldclim.org/data/worldclim21.html"


def fill_sea(data, valid):
    """Give every invalid square the value of a neighbouring valid one, spreading out from land."""
    data = data.copy()
    valid = valid.copy()
    for _ in range(2000):
        if valid.all():
            break
        total = np.zeros_like(data)
        count = np.zeros_like(data)
        for dr, dc in ((-1, 0), (1, 0), (0, -1), (0, 1)):
            shifted_valid = np.roll(valid, (dr, dc), axis=(0, 1))
            shifted = np.roll(np.where(valid, data, 0.0), (dr, dc), axis=(0, 1))
            total += shifted
            count += shifted_valid
        grow = ~valid & (count > 0)
        data[grow] = total[grow] / count[grow]
        valid |= grow
    return data


def read_member(archive, suffix, cache_dir):
    name = next((n for n in archive.namelist() if n.endswith(suffix)), None)
    if name is None:
        raise SystemExit(f"No file ending in {suffix} in the WorldClim zip")
    dest = cache_dir / Path(name).name
    if not dest.exists():
        dest.write_bytes(archive.read(name))
    return read_geotiff(dest)


def write_grid(out_dir, name, data, bounds, units, note):
    out_dir.mkdir(parents=True, exist_ok=True)
    values = np.clip(np.round(data), -32768, 32767).astype("<i2")
    values.tofile(out_dir / f"{name}.bin")
    meta = {
        "width": int(values.shape[1]),
        "height": int(values.shape[0]),
        "north": bounds["north"],
        "south": bounds["south"],
        "west": bounds["west"],
        "east": bounds["east"],
        "units": units,
        "source": note,
    }
    (out_dir / f"{name}.json").write_text(json.dumps(meta, indent=2))
    print(f"Wrote {name}: {meta['width']} x {meta['height']}, {units}, min {int(values.min())}, max {int(values.max())}")


def build(source, out_dir, factor):
    cache_dir = DEFAULT_CACHE
    cache_dir.mkdir(parents=True, exist_ok=True)
    note = f"WorldClim 2.1 ({Path(source).name})"
    with zipfile.ZipFile(source) as archive:
        for suffix, name, scale, units in (
            ("_bio_1.tif", "temperature", 10.0, "tenths of a degree C, yearly mean"),
            ("_bio_12.tif", "rain", 1.0, "mm per year"),
        ):
            data, bounds = read_member(archive, suffix, cache_dir)
            valid = np.isfinite(data) & (data > -1e30)
            data = fill_sea(np.where(valid, data, 0.0), valid)
            small, small_bounds = downsample(data, bounds, factor)
            write_grid(out_dir, name, small * scale, small_bounds, units, note)


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--source", type=Path, help="WorldClim bio zip already on disk (skips the download)")
    p.add_argument("--out", type=Path, default=DEFAULT_OUT, help="output folder (default Content/Data)")
    p.add_argument("--factor", type=int, default=3, help="downsample factor (3 = 30 arc-minutes from the 10 arc-minute source)")
    args = p.parse_args(argv)
    source = args.source or download(WORLDCLIM_URL, DEFAULT_CACHE / Path(WORLDCLIM_URL).name, WORLDCLIM_PAGE, "the 10 minutes 'bio' zip")
    build(source, args.out, args.factor)


if __name__ == "__main__":
    main()
