"""Turn NOAA's ETOPO 2022 elevation model into the grids the Unreal globe reads.

Writes to Content/Data/:
  global.bin / global.json   whole planet, coarse (default 10 arc-minutes)
  region.bin / region.json   a detailed region, Africa by default (default 2 arc-minutes)

Each .bin is little-endian int16 metres relative to present-day sea level, rows
north to south, columns west to east. The .json next to it gives the size and bounds.

Usage:
  python tools/prepare_elevation.py                      # download ETOPO and build both grids
  python tools/prepare_elevation.py --source path\\to.tif # use a file you downloaded yourself
"""

import argparse
import json
import math
import sys
import urllib.request
from pathlib import Path

import numpy as np
import tifffile

ETOPO_URL = (
    "https://www.ngdc.noaa.gov/thredds/fileServer/global/ETOPO2022/60s/"
    "60s_surface_elev_gtif/ETOPO_2022_v1_60s_N90W180_surface.tif"
)
ETOPO_PAGE = "https://www.ncei.noaa.gov/products/etopo-global-relief-model"

REPO_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_OUT = REPO_ROOT / "Content" / "Data"
DEFAULT_CACHE = Path(__file__).resolve().parent / "cache"

# North and East Africa plus the Levant and Arabia: where the earliest Homo sapiens lived
# and the routes out of Africa.
AFRICA = {"north": 40.0, "south": -36.0, "west": -20.0, "east": 60.0}


def download(url, dest):
    dest.parent.mkdir(parents=True, exist_ok=True)
    if dest.exists():
        print(f"Using cached {dest}")
        return dest
    print(f"Downloading {url}")
    tmp = dest.with_suffix(".part")

    def progress(blocks, block_size, total):
        if total > 0:
            pct = min(100.0, blocks * block_size * 100.0 / total)
            sys.stdout.write(f"\r  {pct:5.1f}% of {total / 1e6:.0f} MB")
            sys.stdout.flush()

    try:
        urllib.request.urlretrieve(url, tmp, progress)
    except Exception as exc:  # noqa: BLE001
        print(f"\nDownload failed: {exc}")
        print(f"Download the 60 arc-second 'surface' GeoTIFF by hand from {ETOPO_PAGE}")
        print("then run again with --source <path to the .tif>")
        sys.exit(1)
    print()
    tmp.rename(dest)
    return dest


def read_geotiff(path):
    """Return (heights, bounds) with heights north-up and bounds as cell edges."""
    with tifffile.TiffFile(path) as tif:
        page = tif.pages[0]
        data = page.asarray()
        scale = page.tags.get("ModelPixelScaleTag")
        tie = page.tags.get("ModelTiepointTag")
        if scale is None or tie is None:
            raise SystemExit(f"{path} has no GeoTIFF position tags")
        sx, sy = float(scale.value[0]), float(scale.value[1])
        west, north = float(tie.value[3]), float(tie.value[4])
        geo = tif.geotiff_metadata or {}
        raster_type = geo.get("GTRasterTypeGeoKey")
        if raster_type is not None and int(raster_type) == 2:  # PixelIsPoint
            west -= sx / 2
            north += sy / 2

    data = np.asarray(data, dtype=np.float32)
    if data.ndim == 3:
        data = data[..., 0]
    h, w = data.shape

    # A grid-registered global file repeats the first column at the end; drop it.
    if w * sx > 360.0 + sx / 2:
        extra = int(round((w * sx - 360.0) / sx))
        data = data[:, : w - extra]
        w = data.shape[1]

    bounds = {"north": north, "south": north - h * sy, "west": west, "east": west + w * sx}
    return data, bounds


def downsample(data, bounds, factor):
    """Average factor x factor blocks, trimming leftover rows/columns, and adjust the bounds."""
    if factor <= 1:
        return data, dict(bounds)
    rows, cols = data.shape
    dx = (bounds["east"] - bounds["west"]) / cols
    dy = (bounds["north"] - bounds["south"]) / rows
    h = (rows // factor) * factor
    w = (cols // factor) * factor
    out = data[:h, :w].reshape(h // factor, factor, w // factor, factor).mean(axis=(1, 3))
    out_bounds = {
        "north": bounds["north"],
        "south": bounds["north"] - h * dy,
        "west": bounds["west"],
        "east": bounds["west"] + w * dx,
    }
    return out, out_bounds


def crop(data, bounds, box):
    h, w = data.shape
    dx = (bounds["east"] - bounds["west"]) / w
    dy = (bounds["north"] - bounds["south"]) / h
    c0 = max(0, math.floor((box["west"] - bounds["west"]) / dx))
    c1 = min(w, math.ceil((box["east"] - bounds["west"]) / dx))
    r0 = max(0, math.floor((bounds["north"] - box["north"]) / dy))
    r1 = min(h, math.ceil((bounds["north"] - box["south"]) / dy))
    if c1 <= c0 or r1 <= r0:
        raise SystemExit("Region does not overlap the source data")
    out = data[r0:r1, c0:c1]
    out_bounds = {
        "north": bounds["north"] - r0 * dy,
        "south": bounds["north"] - r1 * dy,
        "west": bounds["west"] + c0 * dx,
        "east": bounds["west"] + c1 * dx,
    }
    return out, out_bounds


def write_grid(out_dir, name, data, bounds, note):
    out_dir.mkdir(parents=True, exist_ok=True)
    heights = np.clip(np.round(data), -32768, 32767).astype("<i2")
    heights.tofile(out_dir / f"{name}.bin")
    meta = {
        "width": int(heights.shape[1]),
        "height": int(heights.shape[0]),
        "north": bounds["north"],
        "south": bounds["south"],
        "west": bounds["west"],
        "east": bounds["east"],
        "units": "metres relative to present-day sea level",
        "source": note,
    }
    (out_dir / f"{name}.json").write_text(json.dumps(meta, indent=2))
    print(
        f"Wrote {name}: {meta['width']} x {meta['height']}, "
        f"lat {meta['south']:.2f}..{meta['north']:.2f}, lon {meta['west']:.2f}..{meta['east']:.2f}, "
        f"min {int(heights.min())} m, max {int(heights.max())} m"
    )


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--source", type=Path, help="ETOPO GeoTIFF already on disk (skips the download)")
    p.add_argument("--out", type=Path, default=DEFAULT_OUT, help="output folder (default Content/Data)")
    p.add_argument("--global-factor", type=int, default=10, help="downsample factor for the globe (10 = 10 arc-min at 60s source)")
    p.add_argument("--region-factor", type=int, default=2, help="downsample factor for the region")
    p.add_argument("--region", type=float, nargs=4, metavar=("NORTH", "SOUTH", "WEST", "EAST"), help="region bounds in degrees (default: Africa)")
    args = p.parse_args(argv)

    source = args.source or download(ETOPO_URL, DEFAULT_CACHE / Path(ETOPO_URL).name)
    print(f"Reading {source}")
    data, bounds = read_geotiff(source)
    note = f"ETOPO 2022 ({Path(source).name})"

    small, small_bounds = downsample(data, bounds, args.global_factor)
    write_grid(args.out, "global", small, small_bounds, note)

    box = AFRICA if args.region is None else dict(zip(("north", "south", "west", "east"), args.region))
    region, region_bounds = crop(data, bounds, box)
    region, region_bounds = downsample(region, region_bounds, args.region_factor)
    write_grid(args.out, "region", region, region_bounds, note)


if __name__ == "__main__":
    main()
