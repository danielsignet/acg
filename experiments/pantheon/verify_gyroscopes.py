"""Verify model topology, hierarchy, numerical rotation tests and live poses.

All images come from our own hidden OpenGL context, never the user's screen.
"""
from collections import Counter
import json
import shutil
import subprocess
import xml.etree.ElementTree as ET
import numpy as np
from PIL import Image
from verify import HERE, ROOT, OUT, CAPTURE, build_capture


def topology():
    result={}
    for path in sorted((HERE/"meshes").glob("gyro-*.obj")):
        vertices=[];edges=Counter();triangles=0
        for line in path.read_text().splitlines():
            if line.startswith("v "):
                vertices.append(tuple(round(float(x),6) for x in line[2:].split()))
            elif line.startswith("f "):
                ids=[int(x.split('/')[0])-1 for x in line[2:].split()]
                for a,b in zip(ids,ids[1:]+ids[:1]):
                    edge=tuple(sorted((vertices[a],vertices[b])))
                    if edge[0]!=edge[1]: edges[edge]+=1
                triangles+=1
        open_edges=sum(count!=2 for count in edges.values())
        assert open_edges==0,(path.name,open_edges)
        result[path.name]={"triangles":triangles,"open_or_nonmanifold_edges":open_edges}
    return result


def main():
    OUT.mkdir(exist_ok=True)
    build_capture()
    scene=ET.parse(HERE/"scene.xml").getroot()
    names=[e.get("name","").lower() for e in scene.iter()]
    assert not any(any(word in name for word in ("shelving","oak shelf","bound volume","armillary")) for name in names)
    manifest=json.loads((HERE/"scene-manifest.json").read_text())["gyroscopes"]
    assert [r["method"] for r in manifest["rigs"]]==["euler","axis-angle","quaternion"]
    parent={child:node for node in scene.iter() for child in node}
    transforms={e.get("name"):e for e in scene.iter("transform")}
    for rig in manifest["rigs"]:
        h,p,r=[transforms[name] for name in rig["joints"]]
        assert parent[r] is p and parent[p] is h
        assert [j.get("joint") for j in (h,p,r)]==["heading","pitch","roll"]
        assert all(j.get("animation")=="gyroscope" and j.get("rotation-method")==rig["method"] for j in (h,p,r))
        assert len(list(r.iter("instance")))==3 # carrier, shaft collars, flywheel
    mesh_checks=topology()
    tests=CAPTURE.with_name("gyroscope_tests.exe")
    result=subprocess.run([str(tests),str(ROOT)],cwd=ROOT,capture_output=True,text=True,check=True)
    (OUT/"gyroscope-math.log").write_text(result.stdout+result.stderr)
    print(result.stdout.strip(),flush=True)
    # Three close-up samples on either side of the singular pose, a locked
    # Euler sample, and an overview. Each includes depth/lit culling comparison.
    cases=[("all", "gyroscopes",7), ("euler-lock","euler",7),
           ("quat-before","quaternion",1.98),("quat-pole","quaternion",2),
           ("quat-after","quaternion",2.02),("axis-turn","axis-angle",13)]
    pixels=[]
    for name,view,time in cases:
        prefix=OUT/("gyro-"+name)
        result=subprocess.run([str(CAPTURE),str(ROOT),str(HERE/"views"/(view+".xml")),str(prefix),"",str(time)],
                              cwd=ROOT,capture_output=True,text=True,timeout=90,
                              creationflags=getattr(subprocess,"CREATE_NO_WINDOW",0))
        (OUT/("gyro-"+name+".log")).write_text(result.stdout+result.stderr,encoding="utf-8")
        assert result.returncode==0,name
        for channel in ("depth","lit"):
            a=np.asarray(Image.open(str(prefix)+f"-{channel}-on.png").convert("RGB"),dtype=np.int16)
            b=np.asarray(Image.open(str(prefix)+f"-{channel}-unculled.png").convert("RGB"),dtype=np.int16)
            delta=np.abs(a-b)
            changed=int(np.count_nonzero(np.any(delta,axis=2)))
            pixels.append({"view":name,"pose_time":time,"channel":channel,"changed_pixels":changed,
                           "max_channel_difference":int(delta.max())})
            assert changed==0,(name,channel,changed)
        print(f"Pose {name} at {time}s: depth/lit culling identical",flush=True)
    shutil.copyfile(OUT/"gyro-all-lit-on.png",HERE/"gyroscopes-preview.png")
    shutil.copyfile(OUT/"gyro-quat-after-lit-on.png",HERE/"quaternion-preview.png")
    report={"date":"2026-10-29","books_and_shelves":"removed","rig_count":3,
            "rig_hierarchies":"three nested bearing transforms each", "mesh_checks":mesh_checks,
            "rotation_checks":(OUT/"gyroscope-math.log").read_text().strip(),
            "physical_note":manifest["quaternion_note"],"live_pose_culling_checks":pixels}
    (HERE/"gyroscope-verification.json").write_text(json.dumps(report,indent=2)+"\n",encoding="utf-8")


if __name__=="__main__": main()
