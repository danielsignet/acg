"""Compare doorway-scoped hall work against the full-room reference."""
import argparse
import csv
import json
import os
from pathlib import Path
import statistics
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--frames", type=int, default=420)
    parser.add_argument("--repeats", type=int, default=3)
    parser.add_argument("--views", nargs="+", default=["rotunda", "north", "east", "south", "west", "up"])
    args = parser.parse_args()
    if args.frames < 240 or args.repeats < 1:
        parser.error("Use at least 240 frames and one repeat")
    experiment = Path(__file__).resolve().parent
    root = experiment.parent.parent
    output = experiment / "output" / "hub"
    output.mkdir(parents=True, exist_ok=True)
    executable = experiment / "output/capture-build/RelWithDebInfo/pantheon_rooms_fixed_viewer.exe"
    import xml.etree.ElementTree as ET
    scenes = {"rotunda": experiment / "scene.xml"}
    for name, target in [("north", (0,4,-60)), ("east", (60,4,0)), ("south", (0,4,60)),
                         ("west", (-60,4,0)), ("up", (0,40,-.1))]:
        tree = ET.parse(experiment / "scene.xml")
        camera = tree.getroot().find("camera")
        camera.set("eye", "0 4 0")
        camera.set("target", " ".join(map(str,target)))
        scene = output / f"center-{name}.xml"
        tree.write(scene,encoding="utf-8")
        scenes[name] = scene
    if any(view not in scenes for view in args.views):
        parser.error("Views: " + ", ".join(scenes))
    report = {"frames": args.frames, "history": 120, "configuration": "RelWithDebInfo",
              "resolution": [1280, 800], "shading": "cook", "aa": "fxaa",
              "reflections": "cubemaps", "ao": True, "shadows": True, "vsync": False,
              "profiling": True, "buffer_previews": False, "runs": [], "medians": {}}
    for view in args.views:
        for repeat in range(args.repeats):
            policies = ["all", "visible"] if repeat % 2 == 0 else ["visible", "all"]
            for policy in policies:
                prefix = output / f"{view}-{repeat}-{policy}"
                environment = os.environ.copy()
                environment["PANTHEON_HUB_WORK"] = policy
                command = [str(executable), "--root", str(root), "--scene", str(scenes[view]),
                           "--shading", "cook", "--aa", "fxaa", "--reflections", "cubemaps",
                           "--hidden", "--no-menus", "--no-vsync", "--frames", str(args.frames),
                           "--benchmark", str(prefix.with_suffix(".csv"))]
                with prefix.with_suffix(".log").open("w", encoding="utf-8") as log:
                    # run() kills and waits for the child on timeout. Every normal run is bounded.
                    subprocess.run(command, cwd=root, env=environment, stdout=log,
                                   stderr=subprocess.STDOUT, check=True, timeout=120)
                with prefix.with_suffix(".csv").open() as stream:
                    row = {key: float(value) for key, value in next(csv.DictReader(stream)).items()}
                keys = ["frame_ms", "p95_ms", "peak_ms", "fps", "cpu_GPU skinning (once)_ms",
                        "gpu_Render total_ms", "exclusive_gpu_GPU skinning (once)_ms",
                        "gpu_Skinning transform feedback_ms", "shadow_draws", "gpu_skinned_vertices",
                        "shadow_reused_faces", "color_draws", "probe_draws", "mirror_draws", "visited_nodes"]
                run = {"view": view, "repeat": repeat, "policy": policy,
                       **{key: row[key] for key in keys}}
                report["runs"].append(run)
                print(f"{view} {repeat} {policy}: {row['frame_ms']:.3f} ms, "
                      f"P95 {row['p95_ms']:.3f}, skin CPU {row['cpu_GPU skinning (once)_ms']:.3f}",
                      flush=True)
                for policy_name in ["all", "visible"]:
                    rows = [item for item in report["runs"] if item["view"] == view
                            and item["policy"] == policy_name]
                    if rows:
                        report["medians"][f"{view}/{policy_name}"] = {
                            key: statistics.median(item[key] for item in rows) for key in keys}
                (output / "results.json").write_text(json.dumps(report, indent=2) + "\n",
                                                     encoding="utf-8")


if __name__ == "__main__":
    main()
