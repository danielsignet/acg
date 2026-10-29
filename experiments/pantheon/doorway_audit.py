"""Find exposed coplanar triangle overlaps at the eight passage junctions.

Only generated architecture and the viewer's box primitive are needed. Native
model interiors are outside the doorway regions. Opposite one-sided contact
faces are valid solid joins. One-sided base faces facing below the floor are
not visible from a doorway. Remaining duplicate faces are reported by area.
"""
import argparse
from collections import defaultdict
import json
import math
from pathlib import Path
import xml.etree.ElementTree as ET
import numpy as np

HERE=Path(__file__).resolve().parent
ROOT=HERE.parent.parent
HALF=21.65*math.sin(math.radians(15))
DOOR=21.65*math.cos(math.radians(15))


def matrix(element):
    pos=np.fromstring(element.get("translate","0 0 0"),sep=" ")
    scale=np.fromstring(element.get("scale","1 1 1"),sep=" ")
    rx,ry,rz=np.radians(np.fromstring(element.get("rotate","0 0 0"),sep=" "))
    x=np.array([[1,0,0],[0,math.cos(rx),-math.sin(rx)],[0,math.sin(rx),math.cos(rx)]])
    y=np.array([[math.cos(ry),0,math.sin(ry)],[0,1,0],[-math.sin(ry),0,math.cos(ry)]])
    z=np.array([[math.cos(rz),-math.sin(rz),0],[math.sin(rz),math.cos(rz),0],[0,0,1]])
    m=np.eye(4);m[:3,:3]=z@y@x@np.diag(scale);m[:3,3]=pos
    return m


def mesh(path):
    vertices=[];triangles=[]
    for line in path.read_text().splitlines():
        if line.startswith("v "): vertices.append([float(x) for x in line[2:].split()])
        elif line.startswith("f "):
            ids=[int(x.split('/')[0])-1 for x in line[2:].split()]
            for i in range(1,len(ids)-1): triangles.append((ids[0],ids[i],ids[i+1]))
    return np.array(vertices,dtype=float)[np.array(triangles,dtype=int)]


def box():
    points=np.array([[-.5,-.5,-.5],[-.5,-.5,.5],[-.5,.5,-.5],[-.5,.5,.5],
                     [.5,-.5,-.5],[.5,-.5,.5],[.5,.5,-.5],[.5,.5,.5]])
    faces=((0,1,3,2),(5,4,6,7),(1,5,7,3),(4,0,2,6),(2,3,7,6),(0,4,5,1))
    return points[np.array([(f[0],f[1],f[2]) for f in faces]+[(f[0],f[2],f[3]) for f in faces])]


def cross2(a,b): return float(a[0]*b[1]-a[1]*b[0])


def overlap(a,b):
    polygon=list(a)
    sign=1 if cross2(b[1]-b[0],b[2]-b[0])>=0 else -1
    for q,r in zip(b,np.roll(b,-1,axis=0)):
        if not polygon: return 0.0
        result=[]
        for p,s in zip(polygon,polygon[1:]+polygon[:1]):
            dp=sign*cross2(r-q,p-q);ds=sign*cross2(r-q,s-q)
            if dp>=-1e-10: result.append(p)
            if (dp>=-1e-10)!=(ds>=-1e-10): result.append(p+(s-p)*(dp/(dp-ds)))
        polygon=result
    if len(polygon)<3:return 0.0
    return abs(sum(cross2(polygon[i]-polygon[0],polygon[i+1]-polygon[0]) for i in range(1,len(polygon)-1)))*.5


def audit(scene_path):
    scene=ET.parse(scene_path).getroot()
    geometry={"box":box()};double={"box":False}
    for item in scene.find("assets"):
        path=ROOT/item.get("file")
        if path.suffix.lower()==".obj" and path.parent==HERE/"meshes":
            geometry[item.get("id")]=mesh(path)
            double[item.get("id")]=any(s.get("cull")=="false" for s in item.findall("material"))
    zones=[]
    for i,name in enumerate(("lapis","aes","silva","astra")):
        rotation=matrix(ET.Element("transform",rotate=f"0 {-90*i} 0"))
        for label,depth in (("rotunda",DOOR+.7),("room",34.5)):
            corners=np.array([[x,y,z,1] for x in (-HALF-1,HALF+1) for y in (-.1,12) for z in (-depth-2.5,-depth+1.5)])@rotation.T
            zones.append((name+"-"+label,corners[:,:3].min(axis=0),corners[:,:3].max(axis=0)))
    planes=defaultdict(list)
    def visit(e,parent,path,cull):
        tag=e.tag
        if tag in {"assets","camera","visibility","state","light"}:return
        if tag=="transform":parent=parent@matrix(e)
        local=e.find("state")
        if local is not None and local.get("cull") is not None:cull=local.get("cull")=="false"
        name=path+"/"+e.get("name",tag)
        if tag=="instance" and e.get("ref") in geometry:
            ref=e.get("ref");source=geometry[ref]
            tris=source@parent[:3,:3].T+parent[:3,3]
            lo,hi=tris.min(axis=(0,1)),tris.max(axis=(0,1))
            if not any(np.all(hi>=a) and np.all(lo<=b) for _,a,b in zones):return
            for tri in tris:
                lo,hi=tri.min(axis=0),tri.max(axis=0)
                zone=next((n for n,a,b in zones if np.all(hi>=a) and np.all(lo<=b)),None)
                if zone is None:continue
                axis=int(np.argmin(hi-lo))
                if hi[axis]-lo[axis]>1e-5:continue
                n=np.cross(tri[1]-tri[0],tri[2]-tri[0])
                if np.linalg.norm(n)<1e-8:continue
                two_sided=double.get(ref,cull) or cull
                if axis==1 and hi[1]<=1e-6 and n[1]<0 and not two_sided:continue
                axes=[a for a in range(3) if a!=axis]
                polygon=tri[:,axes]
                if abs(cross2(polygon[1]-polygon[0],polygon[2]-polygon[0]))<1e-7:continue
                key=(axis,round(float(tri[:,axis].mean())*1e4))
                planes[key].append((name,polygon,1 if n[axis]>0 else -1,two_sided,zone))
        if tag=="lod":
            level=e.find("level")
            if level is not None:visit(level,parent,name,cull)
            return
        for child in e:visit(child,parent,name,cull)
    visit(scene,np.eye(4),"",False)
    pairs=defaultdict(float);zones_by_pair={}
    for (axis,plane),items in planes.items():
        for i,(name,a,normal,two_sided,zone) in enumerate(items):
            for other,b,normal_b,two_sided_b,zone_b in items[i+1:]:
                if name==other or (normal!=normal_b and not (two_sided or two_sided_b)):continue
                if np.any(a.max(axis=0)<b.min(axis=0)) or np.any(b.max(axis=0)<a.min(axis=0)):continue
                area=overlap(a,b)
                if area>1e-5:
                    key=(*sorted((name,other)),axis,plane)
                    pairs[key]+=area;zones_by_pair[key]=zone
    return [{"first":a,"second":b,"axis":"xyz"[axis],"plane_m":plane/1e4,"overlap_m2":round(area,8),"zone":zones_by_pair[(a,b,axis,plane)]}
            for (a,b,axis,plane),area in sorted(pairs.items())]


if __name__=="__main__":
    parser=argparse.ArgumentParser()
    parser.add_argument("--scene",type=Path,default=HERE/"scene.xml")
    parser.add_argument("--output",type=Path,default=HERE/"output/doorway-overlaps.json")
    parser.add_argument("--require-clear",action="store_true")
    args=parser.parse_args();result=audit(args.scene)
    args.output.parent.mkdir(exist_ok=True,parents=True)
    args.output.write_text(json.dumps(result,indent=2)+"\n",encoding="utf-8")
    print(f"{len(result)} exposed coplanar surface pairs; total overlapping area {sum(p['overlap_m2'] for p in result):.6f} m2")
    for p in result[:15]:print(p['zone'],p['overlap_m2'],p['first'].split('/')[-2],'<->',p['second'].split('/')[-2])
    if args.require_clear and result:raise SystemExit(1)
