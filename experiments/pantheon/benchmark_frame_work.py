"""Compare frame-work optimizations using the same binary and normal HUD."""
import argparse
import csv
import json
import math
import os
from pathlib import Path
import statistics
import subprocess
import xml.etree.ElementTree as ET


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--frames", type=int, default=480)
    parser.add_argument("--repeats", type=int, default=2)
    parser.add_argument("--views", nargs="+", default=["north", "east", "south", "west", "up"])
    parser.add_argument("--vsync", action="store_true")
    parser.add_argument("--visible", action="store_true")
    parser.add_argument("--no-hud", action="store_true")
    args = parser.parse_args()
    if args.frames < 300 or args.repeats < 1:
        parser.error("Use at least 300 frames and one repeat")
    experiment = Path(__file__).resolve().parent
    root = experiment.parent.parent
    suffix = "vsync" if args.vsync else "uncapped"
    if args.visible:
        suffix += "-visible"
    output = experiment / "output" / ("frame-work-" + suffix)
    output.mkdir(parents=True, exist_ok=True)
    executable = experiment / "output/capture-build/RelWithDebInfo/pantheon_rooms_fixed_viewer.exe"
    scenes = {"rotunda": experiment / "scene.xml", "materials": experiment / "views/lapis.xml",
              "dragon": experiment / "views/astra.xml", "mirror": experiment / "views/mirror.xml"}
    for name, target in [("north", (0, 4, -60)), ("east", (60, 4, 0)), ("south", (0, 4, 60)),
                         ("west", (-60, 4, 0)), ("up", (0, 40, -.1))]:
        tree = ET.parse(experiment / "scene.xml")
        camera = tree.getroot().find("camera")
        camera.set("eye", "0 4 0")
        camera.set("target", " ".join(map(str, target)))
        scene = output / f"center-{name}.xml"
        tree.write(scene, encoding="utf-8")
        scenes[name] = scene
    if any(view not in scenes for view in args.views):
        parser.error("Views: " + ", ".join(scenes))
    report = {"frames": args.frames, "history": 120, "configuration": "RelWithDebInfo",
              "resolution": [1280, 800], "shading": "cook", "aa": "fxaa",
              "reflections": "cubemaps", "ao": True, "shadows": True, "vsync": args.vsync,
              "normal_hud": not args.no_hud, "buffer_previews": not args.no_hud, "visible": args.visible,
              "runs": [], "medians": {}}
    keys = ["frame_ms", "p95_ms", "peak_ms", "fps", "cpu_Render total_ms", "gpu_Render total_ms",
            "exclusive_cpu_Shadow maps_ms", "exclusive_gpu_Shadow maps_ms",
            "exclusive_cpu_Local cubemap capture / filter_ms", "exclusive_gpu_Local cubemap capture / filter_ms",
            "exclusive_gpu_Ambient occlusion_ms", "exclusive_cpu_Model raycast / BVH_ms",
            "exclusive_cpu_HUD / performance view_ms", "cpu_Swap / VSync wait_ms",
            "shadow_draws", "shadow_reused_faces", "gpu_skinned_vertices", "probe_draws", "color_draws",
            "p99_ms", "frames_over_16_67_percent", "frames_over_25_percent"]
    for view in args.views:
        for repeat in range(args.repeats):
            modes = ["reference", "optimized"] if repeat % 2 == 0 else ["optimized", "reference"]
            for mode in modes:
                prefix = output / f"{view}-{repeat}-{mode}"
                environment = os.environ.copy()
                environment["PANTHEON_HUB_WORK"] = "visible"
                environment["ACG_FRAME_WORK_REFERENCE"] = "1" if mode == "reference" else "0"
                environment.pop("ACG_AO_SAMPLES", None)
                environment.pop("ACG_FULL_SHADOWS", None)
                command = [str(executable), "--root", str(root), "--scene", str(scenes[view]),
                           "--shading", "cook", "--aa", "fxaa", "--reflections", "cubemaps",
                           "--frames", str(args.frames), "--benchmark", str(prefix.with_suffix(".csv"))]
                if not args.visible:
                    command += ["--hidden"]
                command += ["--no-menus"] if args.no_hud else ["--benchmark-hud"]
                if not args.vsync:
                    command += ["--no-vsync"]
                with prefix.with_suffix(".log").open("w", encoding="utf-8") as log:
                    subprocess.run(command, cwd=root, env=environment, stdout=log,
                                   stderr=subprocess.STDOUT, check=True, timeout=120)
                with prefix.with_suffix(".csv").open() as stream:
                    row = {key: float(value) for key, value in next(csv.DictReader(stream)).items()}
                with prefix.with_suffix(".frames.csv").open() as stream:
                    times = sorted(float(frame["frame_ms"]) for frame in csv.DictReader(stream))
                row["p99_ms"] = times[math.ceil(len(times) * .99) - 1]
                row["frames_over_16_67_percent"] = 100 * sum(t > 1000 / 60 for t in times) / len(times)
                row["frames_over_25_percent"] = 100 * sum(t > 25 for t in times) / len(times)
                run = {"view": view, "repeat": repeat, "mode": mode,
                       **{key: row[key] for key in keys}}
                report["runs"].append(run)
                print(f"{view} {repeat} {mode}: {row['frame_ms']:.3f} ms, P95 {row['p95_ms']:.3f}", flush=True)
                for label in modes:
                    rows = [item for item in report["runs"] if item["view"] == view and item["mode"] == label]
                    if rows:
                        report["medians"][f"{view}/{label}"] = {
                            key: statistics.median(item[key] for item in rows) for key in keys}
                (output / "results.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
