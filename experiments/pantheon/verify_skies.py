"""Real skybox, simultaneous doorway views, depth and renderer regression QA."""
import copy
import json
import shutil
import subprocess
import xml.etree.ElementTree as ET
import numpy as np
from PIL import Image,ImageFilter
from verify import ROOT,HERE,OUT,CAPTURE,build_capture
from generate import CAMERAS,HALF,vec


def capture(name,scene,mode="deferred"):
    result=subprocess.run([str(CAPTURE),str(ROOT),str(scene),str(OUT/name),"",".5",mode],
                          cwd=ROOT,capture_output=True,text=True,timeout=120,
                          creationflags=getattr(subprocess,"CREATE_NO_WINDOW",0))
    (OUT/(name+".log")).write_text(result.stdout+result.stderr,encoding="utf-8")
    if result.returncode:raise RuntimeError(name+": "+result.stderr[-2000:])
    comparisons=[]
    for channel in ("lit","depth"):
        a=np.asarray(Image.open(OUT/(name+"-"+channel+"-on.png")).convert("RGB"),dtype=np.int16)
        b=np.asarray(Image.open(OUT/(name+"-"+channel+"-unculled.png")).convert("RGB"),dtype=np.int16)
        delta=np.abs(a-b)
        comparisons.append({"view":name,"mode":mode,"channel":channel,
                            "changed_pixels":int(np.count_nonzero(np.any(delta,axis=2))),
                            "max_difference":int(delta.max())})
        tolerance=1 if mode=="msaa" and channel=="lit" else 0
        assert delta.max()<=tolerance,comparisons[-1]
    print(f"{name}: colour/depth culling passed ({mode}; max delta {max(c['max_difference'] for c in comparisons)})",flush=True)
    return comparisons


def main():
    build_capture()
    scene=ET.parse(HERE/"scene.xml").getroot()
    assert not any(e.get("ref") in {"star-ceiling","cloud-ceiling"} for e in scene.iter("instance"))
    layout=scene.find("environments")
    assert layout is not None and layout.find("sky").get("file").endswith(".hdr")
    math=subprocess.run([str(CAPTURE.with_name("sky_environment_tests.exe")),str(ROOT)],
                         cwd=ROOT,capture_output=True,text=True,check=True,timeout=90)
    (OUT/"sky-tests.log").write_text(math.stdout+math.stderr,encoding="utf-8")
    print(math.stdout.strip(),flush=True)
    comparisons=[]
    for view in ("astra","astra-exit","astra-sky","city-sky"):
        comparisons+=capture("sky-"+view,HERE/"views"/(view+".xml"))
    for mode in ("forward","msaa"):
        comparisons+=capture("sky-mixed-"+mode,HERE/"views/astra-exit.xml",mode)
    # Exercise both sides of the actual aperture, without the optional portal
    # culling code or its known near-plane clipping issue.
    mouth=float(layout.find("region/opening").get("min").split()[0])
    for side,x in (("inside",mouth-.03),("outside",mouth+.03)):
        doc=copy.deepcopy(scene)
        doc.find("camera").set("eye",vec((x,4,0)))
        doc.find("camera").set("target",vec((-58,8,0)))
        path=OUT/("sky-threshold-"+side+".xml")
        ET.ElementTree(doc).write(path,encoding="utf-8")
        comparisons+=capture("sky-threshold-"+side,path)
    # Turn off the room override. Only the star room's sky may change; the city
    # sky seen through the doorway must be pixel-identical in background areas.
    global_only=ET.parse(HERE/"views/astra-exit.xml").getroot()
    global_only.remove(global_only.find("environments"))
    path=OUT/"sky-all-global.xml";ET.ElementTree(global_only).write(path,encoding="utf-8")
    comparisons+=capture("sky-all-global",path)
    actual=np.asarray(Image.open(OUT/"sky-astra-exit-lit-on.png").convert("RGB"),dtype=np.int16)
    baseline=np.asarray(Image.open(OUT/"sky-all-global-lit-on.png").convert("RGB"),dtype=np.int16)
    bg=Image.open(OUT/"sky-astra-exit-background.pgm")
    # Exclude FXAA edges shared with surfaces whose regional lighting changed.
    background=np.asarray(bg.filter(ImageFilter.MinFilter(7)))==255
    eye,target=(np.array(v,dtype=float) for v in CAMERAS["astra-exit"])
    forward=target-eye;forward/=np.linalg.norm(forward)
    right=np.cross(forward,[0,1,0]);right/=np.linalg.norm(right)
    up=np.cross(right,forward)
    h,w=background.shape;y,x=np.mgrid[:h,:w]
    tangent=np.tan(np.radians(24))
    rays=forward+right*((2*(x+.5)/w-1)*w/h*tangent)[:,:,None]+up*((1-2*(y+.5)/h)*tangent)[:,:,None]
    distance=(mouth-eye[0])/rays[:,:,0]
    hit=eye+rays*distance[:,:,None]
    through=(distance>=0)&(rays[:,:,0]>0)&(np.abs(hit[:,:,2])<=HALF)&(hit[:,:,1]>=0)&(hit[:,:,1]<=10)
    world=background&through;stars=background&~through
    difference=np.abs(actual-baseline)
    assert world.sum()>100 and stars.sum()>100,"Both skies must be visible in the real scene"
    assert difference[world].max()==0,"City sky changed when toggling the star room"
    assert difference[stars].mean()>20,"Star room did not replace the actual skybox"
    simultaneous={"city_background_pixels":int(world.sum()),"star_background_pixels":int(stars.sum()),
                  "city_max_channel_difference_with_room_override":int(difference[world].max()),
                  "star_mean_channel_difference_with_room_override":float(difference[stars].mean())}
    for source,target in (("sky-astra-lit-on.png","astra-preview.png"),
                          ("sky-astra-exit-lit-on.png","skybox-doorway-preview.png")):
        shutil.copyfile(OUT/source,HERE/target)
    report={"date":"2026-10-29","method":"Per-ray cubemap array, scene-defined regions and openings; no sky geometry",
            "sky_resolution_per_face":1024,"regional_diffuse_ibl":True,"regional_reflection_refraction_fallback":True,
            "gpu_and_cpu_tests":math.stdout.strip(),"simultaneous_real_scene":simultaneous,
            "culling_comparisons":comparisons,"optional_portal_culling":False}
    (HERE/"skybox-verification.json").write_text(json.dumps(report,indent=2)+"\n",encoding="utf-8")
    print("Simultaneous skies:",simultaneous,flush=True)


if __name__=="__main__":main()
