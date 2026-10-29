"""Architectural overlap regression and close views of both ends of each wing."""
import copy
import json
import math
import xml.etree.ElementTree as ET
import numpy as np
from PIL import Image
from doorway_audit import audit
from verify import HERE, OUT, build_capture, fixed_capture
from generate import DOOR, HALF, vec


def main():
    OUT.mkdir(exist_ok=True)
    overlaps=audit(HERE/"scene.xml")
    (OUT/"doorway-overlaps.json").write_text(json.dumps(overlaps,indent=2)+"\n",encoding="utf-8")
    if overlaps:raise RuntimeError(f"{len(overlaps)} coplanar overlaps remain in the doorway regions")
    build_capture()
    scene=ET.parse(HERE/"scene.xml").getroot()
    eyes=[("arch-front",(-2.4,1.7,-DOOR+2.5),(0,1.9,-DOOR-2)),
          ("arch-back",(2.4,1.8,-DOOR-3),(0,3.2,-DOOR+1)),
          ("room-front",(HALF-1,1.7,-32.5),(HALF-.3,2.2,-35.2)),
          ("room-back",(HALF-1,1.7,-36.5),(HALF-.3,3,-33))]
    results=[]
    for i,wing in enumerate(("lapis","aes","silva","astra")):
        a=math.radians(-90*i)
        def world(v):
            x,y,z=v
            return (x*math.cos(a)+z*math.sin(a),y,-x*math.sin(a)+z*math.cos(a))
        for label,eye,target in eyes:
            name=f"door-{wing}-{label}"
            doc=copy.deepcopy(scene)
            camera=doc.find("camera");camera.set("eye",vec(world(eye)));camera.set("target",vec(world(target)))
            path=OUT/(name+".xml");ET.ElementTree(doc).write(path,encoding="utf-8")
            fixed_capture(name,path)
            for channel in ("depth","lit"):
                first=np.asarray(Image.open(OUT/(name+"-"+channel+"-on.png")),dtype=np.int16)
                second=np.asarray(Image.open(OUT/(name+"-"+channel+"-unculled.png")),dtype=np.int16)
                diff=np.abs(first-second)
                item={"view":name,"channel":channel,"changed_pixels":int(np.count_nonzero(np.any(diff,axis=2))),"max_difference":int(diff.max())}
                results.append(item)
            print(name+": "+", ".join(f"{r['channel']}={r['changed_pixels']} px" for r in results[-2:]),flush=True)
    (OUT/"doorway-pixel-checks.json").write_text(json.dumps(results,indent=2)+"\n",encoding="utf-8")
    failures=[r for r in results if r['max_difference']>1]
    if failures:raise RuntimeError(f"Doorway culling changed the image: {failures}")
    print("PASS: zero exposed coplanar overlaps and 32 doorway colour/depth comparisons",flush=True)


if __name__=="__main__":main()
