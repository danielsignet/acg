"""Check fixed-pose shadows/reflections against the full-work reference, then AO quality."""
import argparse
import json
import os
from pathlib import Path
import subprocess

import numpy as np
from PIL import Image


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--views", nargs="+", default=["north", "south", "materials", "dragon-exit"])
    args = parser.parse_args()
    experiment = Path(__file__).resolve().parent
    root = experiment.parent.parent
    output = experiment / "output/frame-work-quality"
    output.mkdir(parents=True, exist_ok=True)
    executable = experiment / "output/capture-build/RelWithDebInfo/pantheon_capture.exe"
    scenes = {"north": experiment / "output/frame-work-uncapped/center-north.xml",
              "south": experiment / "output/frame-work-uncapped/center-south.xml",
              "east": experiment / "output/frame-work-uncapped/center-east.xml",
              "materials": experiment / "views/lapis.xml", "dragon-exit": experiment / "views/astra-exit.xml"}
    results = []
    for view in args.views:
        prefixes = {}
        for mode in ["reference", "optimized64", "optimized32"]:
            prefix = output / f"{view}-{mode}"
            prefixes[mode] = prefix
            environment = os.environ.copy()
            environment["PANTHEON_HUB_WORK"] = "visible"
            environment["ACG_FRAME_WORK_REFERENCE"] = "1" if mode == "reference" else "0"
            environment["ACG_AO_SAMPLES"] = "64" if mode != "optimized32" else "32"
            with prefix.with_suffix(".log").open("w", encoding="utf-8") as log:
                subprocess.run([str(executable), str(root), str(scenes[view]), str(prefix), "", "0.5", "cubemaps"],
                               cwd=root, env=environment, stdout=log, stderr=subprocess.STDOUT,
                               check=True, timeout=120)
        load = lambda mode, buffer: np.asarray(Image.open(str(prefixes[mode]) + f"-{buffer}-on.png"))[:, :, :3].astype(np.int16)
        result = {"view": view}
        for buffer in ["depth", "lit"]:
            difference = np.abs(load("reference", buffer) - load("optimized64", buffer))
            result[buffer + "_changed_pixels"] = int(np.count_nonzero(np.any(difference != 0, axis=2)))
            result[buffer + "_max_difference"] = int(difference.max())
            if np.any(difference):
                raise RuntimeError(f"{view}: full-work and optimized {buffer} differ: {result}")
        difference = np.abs(load("optimized64", "lit") - load("optimized32", "lit"))
        mask = np.asarray(Image.open(str(prefixes["optimized32"]) + "-background.pgm")) == 0
        result["ao_mean_absolute_rgb_difference"] = float(difference[mask].mean())
        result["ao_p95_rgb_difference"] = float(np.percentile(difference[mask], 95))
        result["ao_max_rgb_difference"] = int(difference.max())
        if result["ao_mean_absolute_rgb_difference"] > 2:
            raise RuntimeError(f"{view}: reduced-sample AO changed too much: {result}")
        results.append(result)
        (output / "results.json").write_text(json.dumps(results, indent=2) + "\n", encoding="utf-8")
        print(result, flush=True)


if __name__ == "__main__":
    main()
