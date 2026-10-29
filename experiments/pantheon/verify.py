"""Exercise the scene with the same engine and the experiment's animation factories.

python experiments/pantheon/verify.py             # pixels, graph and materials
python experiments/pantheon/verify.py --benchmark # also benchmark live animation
"""
import argparse
import copy
import csv
import hashlib
import json
import math
import shutil
from collections import Counter
from pathlib import Path
import subprocess
import xml.etree.ElementTree as ET
from PIL import Image
import numpy as np
from generate import CAMERAS, DOOR, HERE, vec
from doorway_audit import audit as audit_doorways

ROOT = HERE.parent.parent
VIEWER = HERE / "output/capture-build/RelWithDebInfo/pantheon_rooms_fixed_viewer.exe"
OUT = HERE / "output"
CAPTURE = OUT / "capture-build/RelWithDebInfo/pantheon_capture.exe"


def fixed_capture(name, scene, dot=None):
    command=[str(CAPTURE),str(ROOT),str(scene),str(OUT/name)]
    if dot: command.append(str(dot))
    result=subprocess.run(command,cwd=ROOT,capture_output=True,text=True,timeout=90,
                          creationflags=getattr(subprocess,"CREATE_NO_WINDOW",0))
    (OUT/(name+".log")).write_text(result.stdout+result.stderr,encoding="utf-8")
    if result.returncode: raise RuntimeError(f"Fixed-pose capture failed: {name}; see output/{name}.log")


def build_capture():
    sources=list((HERE/"tools").glob("*"))
    sources.append(ROOT/"build/windows/applications/viewer/RelWithDebInfo/vrlib.lib")
    if CAPTURE.exists() and CAPTURE.stat().st_mtime>=max(p.stat().st_mtime for p in sources): return
    cmake=shutil.which("cmake") or str(ROOT.parent/".tools/cmake/cmake/data/bin/cmake.exe")
    for command in ([cmake,"-S",str(HERE/"tools"),"-B",str(OUT/"capture-build"),"-G","Visual Studio 17 2022","-A","x64"],
                    [cmake,"--build",str(OUT/"capture-build"),"--config","RelWithDebInfo","--parallel","4"]):
        subprocess.run(command,cwd=ROOT,check=True)


def semantic_checks(scene):
    manifest=json.loads((HERE/"scene-manifest.json").read_text(encoding="utf-8"))
    legacy=ET.parse(ROOT/"scenes/house-patio.xml").getroot()
    placements={e.get("name"):e for e in scene.iter("transform")}
    for name in manifest["legacy_material_spheres"]:
        old=next(e for e in legacy.iter("transform") if e.get("name")==name)
        assert old.find("state").attrib==placements[name].find("state").attrib,name
    assert len(manifest["legacy_material_spheres"])==9
    assert "materialMirror" in placements and "carExhibit" in placements and "dragonExhibit" in placements
    assets={e.get("id") for e in scene.find("assets")}
    assert set(manifest["legacy_models"])<=assets
    assert hashlib.sha256((ROOT/manifest["legacy_sky"]["source"]).read_bytes()).hexdigest()==manifest["legacy_sky"]["sha256"]
    # Check watertight added glass with welded positions. The native FBX's old
    # windows are separately documented as surface geometry.
    for label,count in (("short",5),("long",9)):
        positions=[];edges=Counter();faces=0;material=""
        for line in (HERE/"meshes"/("curtain-"+label+".obj")).read_text().splitlines():
            if line.startswith("v "): positions.append(tuple(round(float(x),6) for x in line[2:].split()))
            elif line.startswith("usemtl "): material=line[7:]
            elif line.startswith("f ") and material=="glazing":
                indices=[int(x.split('/')[0])-1 for x in line[2:].split()]
                for a,b in zip(indices,indices[1:]+indices[:1]): edges[tuple(sorted((positions[a],positions[b])))]+=1
                faces+=1
        assert faces==count*12 and set(edges.values())=={2},"Open added glass pane"
    actual_panes=sum((5 if e.get("ref")=="curtain-short" else 9) for e in scene.iter("instance") if e.get("ref") in {"curtain-short","curtain-long"})
    assert actual_panes==sum(e["added_pane_count"] for e in manifest["buildings"])
    for building in manifest["buildings"]:
        assert building["uniform_scale"]==.01
        assert .65<building["added_glass_to_total_facade_area"]<.8
        assert 45<building["dimensions_m"][1]<106
    assert not audit_doorways(HERE/"scene.xml"),"Coplanar doorway surfaces overlap"
    return {"nine_original_sphere_materials":"unchanged","legacy_model_assets":"all retained",
            "mirror_and_interaction_names":"present","closed_glass_panes":actual_panes,"tower_uniform_scale":.01,"legacy_hdr_checksum":"passed",
            "coplanar_doorway_overlaps":0}


def run(name, scene, extra):
    command = [str(VIEWER), "--root", str(ROOT), "--scene", str(scene), "--shading", "cook",
               "--aa", "fxaa", "--reflections", "environment", "--hidden", "--no-menus", *extra]
    result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, timeout=90,
                            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
    (OUT / (name+".log")).write_text(result.stdout+result.stderr, encoding="utf-8")
    if result.returncode:
        raise RuntimeError(f"{name}: viewer exited {result.returncode}; see output/{name}.log")
    return result.stdout


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("--benchmark",action="store_true")
    parser.add_argument("--audit-portals",action="store_true",help="Record the existing near-plane portal defect separately")
    parser.add_argument("--views",nargs="+",choices=list(CAMERAS),help="Recheck selected camera presets")
    args=parser.parse_args()
    OUT.mkdir(exist_ok=True)
    build_capture()
    scene=ET.parse(HERE/"scene.xml").getroot()
    materials=json.loads((HERE/"materials.json").read_text(encoding="utf-8"))
    for item in materials:
        assert hashlib.sha256((HERE/item["file"]).read_bytes()).hexdigest()==item["sha256"],item["file"]
    names=[e.get("name") for e in scene.iter("transform")]
    assert len(set(names))==len(names),"Duplicate transform registration"
    resources={"box","sphere-low","material-sphere","torus","ground","alphatest-panel"}|{e.get("id") for e in scene.find("assets")}
    for e in scene.iter("instance"):
        assert e.get("ref") in resources,e.get("ref")
    for e in scene.iter():
        for key in ("file","texture0","normalMap"):
            if e.get(key) and e.get(key)!="none": assert (ROOT/e.get(key)).is_file(),e.get(key)
    semantic=semantic_checks(scene)
    (OUT/"semantic-checks.json").write_text(json.dumps(semantic,indent=2)+"\n",encoding="utf-8")
    # The C++ capture tool advances the same Application to exactly 0.5 s, then
    # holds both skeletal and transform animation still across the four images.
    static=copy.deepcopy(scene)
    for e in static.iter("transform"):
        if e.get("animation")=="room-isolation": continue
        for key in ("animation","speed","axis"):
            e.attrib.pop(key,None)
    cameras=dict(CAMERAS)
    for i,name in enumerate(("lapis","aes","silva","astra")):
        a=math.radians(90*i)
        for suffix,depth in (("out",DOOR+6),("threshold",DOOR+.03)):
            cameras[name+"-"+suffix]=((depth*math.sin(a),2.2,-depth*math.cos(a)),(0,7,0))
    if args.views: cameras={name:cameras[name] for name in args.views}
    checks=[]
    for name,(eye,target) in cameras.items():
        doc=copy.deepcopy(static)
        camera=doc.find("camera");camera.set("eye",vec(eye));camera.set("target",vec(target))
        file=OUT/(name+"-static.xml")
        ET.ElementTree(doc).write(file,encoding="utf-8",xml_declaration=True)
        fixed_capture(name,file,OUT/"scenegraph.dot" if name=="rotunda" else None)
        for channel in ("lit","depth"):
            images={}
            for mode in ("on","unculled"):
                path=OUT/(name+"-"+channel+"-"+mode+".png")
                images[mode]=np.asarray(Image.open(path).convert("RGB"),dtype=np.int16)
            difference=np.abs(images["on"]-images["unculled"])
            checks.append({"view":name,"channel":channel,"changed_pixels":int(np.count_nonzero(np.any(difference,axis=2))),
                           "max_channel_difference":int(difference.max()),"mean_channel_difference":float(difference.mean())})
        print(f"Pixels {name}: "+", ".join(f"{r['channel']}={r['changed_pixels']} px" for r in checks[-2:]),flush=True)
    report=OUT/"pixel-checks.json"
    previous=json.loads(report.read_text()) if args.views and report.exists() else []
    combined=[row for row in previous if row["view"] not in cameras]+checks
    report.write_text(json.dumps(combined,indent=2)+"\n",encoding="utf-8")
    # Depth coverage must match. Lit results allow sparse rasterization changes
    # at shared module edges / dithered LODs after opaque batch order changes;
    # retain the exact counts and deltas, rather than calling this pixel equality.
    failures=[r for r in checks if (r["channel"]=="depth" and r["max_channel_difference"]>1) or
              (r["channel"]=="lit" and (r["changed_pixels"]>1024 or r["mean_channel_difference"]>.005))]
    if failures: raise RuntimeError(f"Culling changed pixels: {failures}")
    if args.benchmark:
        summary=[]
        for name in CAMERAS:
            scene_path=HERE/"scene.xml" if name=="rotunda" else HERE/"views"/(name+".xml")
            path=OUT/(name+".csv")
            run(name+"-benchmark",scene_path,["--frames","600","--benchmark",str(path)])
            with path.open(encoding="utf-8",newline="") as f: row=next(csv.DictReader(f))
            summary.append({"view":name,**row})
            print(f"Benchmark {name}: {row['frame_ms']} ms, P95 {row['p95_ms']} ms",flush=True)
        with (OUT/"benchmarks.csv").open("w",encoding="utf-8",newline="") as f:
            writer=csv.DictWriter(f,fieldnames=list(summary[0]));writer.writeheader();writer.writerows(summary)
    if args.audit_portals:
        portal=ET.parse(HERE/"portal-scene.xml").getroot()
        for e in portal.iter("transform"): e.attrib.pop("animation",None)
        audit=[]
        for name,(eye,target) in cameras.items():
            if not name.endswith("threshold"): continue
            doc=copy.deepcopy(portal)
            camera=doc.find("camera");camera.set("eye",vec(eye));camera.set("target",vec(target))
            path=OUT/(name+"-portal.xml");ET.ElementTree(doc).write(path,encoding="utf-8")
            prefix=name+"-portal"
            fixed_capture(prefix,path)
            images={}
            for mode,suffix in (("on","on"),("off","unculled")):
                screenshot=OUT/(prefix+"-lit-"+suffix+".png")
                images[mode]=np.asarray(Image.open(screenshot).convert("RGB"),dtype=np.int16)
            diff=np.abs(images["on"]-images["off"])
            audit.append({"view":name,"changed_pixels":int(np.count_nonzero(np.any(diff,axis=2))),"max_channel_difference":int(diff.max())})
            print(f"Portal audit {name}: {audit[-1]['changed_pixels']} changed pixels (not enabled in default scene)",flush=True)
        (OUT/"portal-audit.json").write_text(json.dumps(audit,indent=2)+"\n",encoding="utf-8")
    print(f"PASS: default scene, {len(checks)} frustum comparisons within documented tolerances; six texture checksums; graph exported.",flush=True)


if __name__=="__main__":
    main()
