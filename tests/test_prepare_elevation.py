"""Checks the elevation script on a small synthetic GeoTIFF shaped like ETOPO.

Run: python tests/test_prepare_elevation.py
"""

import json
import sys
import tempfile
from pathlib import Path

import numpy as np
import tifffile

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools"))
import prepare_elevation  # noqa: E402


def make_tif(path, step_deg=1.0):
    w, h = int(360 / step_deg), int(180 / step_deg)
    lat = 90 - (np.arange(h) + 0.5) * step_deg
    lon = -180 + (np.arange(w) + 0.5) * step_deg
    # Height = latitude * 10 + longitude, so every cell is identifiable.
    data = (lat[:, None] * 10 + lon[None, :]).astype(np.float32)
    tifffile.imwrite(
        path,
        data,
        extratags=[
            (33550, "d", 3, (step_deg, step_deg, 0.0), False),
            (33922, "d", 6, (0.0, 0.0, 0.0, -180.0, 90.0, 0.0), False),
        ],
    )
    return data


def main():
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        src = tmp / "fake_etopo.tif"
        make_tif(src)
        out = tmp / "out"
        prepare_elevation.main(["--source", str(src), "--out", str(out), "--global-factor", "2", "--region-factor", "1",
                                "--region", "40", "-36", "-20", "60"])

        g = json.loads((out / "global.json").read_text())
        assert (g["width"], g["height"]) == (180, 90), g
        assert (g["north"], g["south"], g["west"], g["east"]) == (90, -90, -180, 180), g
        gh = np.fromfile(out / "global.bin", dtype="<i2").reshape(g["height"], g["width"])
        # First 2x2 block averages lat 89.5/88.5 and lon -179.5/-178.5.
        assert gh[0, 0] == round(89.0 * 10 - 179.0), gh[0, 0]

        r = json.loads((out / "region.json").read_text())
        assert (r["north"], r["south"], r["west"], r["east"]) == (40, -36, -20, 60), r
        assert (r["width"], r["height"]) == (80, 76), r
        rh = np.fromfile(out / "region.bin", dtype="<i2").reshape(r["height"], r["width"])
        assert rh[0, 0] == round(39.5 * 10 - 19.5), rh[0, 0]
    print("Elevation script test passed")


if __name__ == "__main__":
    main()
