"""Deterministic street oak and masonry on an existing city building. No network."""
import copy
import hashlib
import json
import math
from pathlib import Path
import subprocess
import xml.etree.ElementTree as ET
import numpy as np
from PIL import Image,ImageFilter
from basketball_material import bake as bake_basketball, DEPTH_M as BASKETBALL_DEPTH

HERE = Path(__file__).resolve().parent
PREFIX = "experiments/pantheon/"
CAMERAS = {
    "parallax": ((54.2, 1.45, 10.55), (55.5, 1.45, 11.303)),
    "bark": ((37.55, 1.9, 6.25), (39, 2.0, 7)),
    "brick": ((54.2, 1.45, 10.55), (55.5, 1.45, 11.303)),
    "trees": ((25, 6.7, 0), (82, 6.3, 0)),
    "bounce": ((-13, 4.2, -50), (-5.6, 2.1, -44)),
}
TREE_PLACEMENTS = [
    (-7,39,(.65,1.28,.82),(0,31,0)),
    (-7,70,(1.18,.78,1.25),(0,112,0)),
    (-7,104,(.9,1.07,.75),(0,204,0)),
    (7,39,(1.03,.95,1.1),(0,0,0)),
    (7,70,(.72,1.32,.8),(0,73,0)),
    (7,104,(1.12,.84,.92),(0,161,0)),
]
TREE_WARPS=[(-1.35,.35,.85,-.18),(.6,-.9,-.6,.4),(1.8,.75,1.15,-.1),
            (-.7,-.5,-.95,.2),(.35,1.45,.55,-.28),(-1.65,.2,1.35,.32)]


def unit(v):
    v = np.asarray(v, dtype=float)
    return v / max(np.linalg.norm(v), 1e-12)


def text(v):
    return " ".join(f"{x:.7g}" for x in v)


class Mesh:
    def __init__(self):
        self.vertices, self.uvs, self.normals, self.faces = [], [], [], {}

    def face(self, points, uv, material, normals=None):
        normal = unit(np.cross(np.asarray(points[1])-points[0], np.asarray(points[2])-points[0]))
        start = len(self.vertices) + 1
        self.vertices.extend(points)
        self.uvs.extend(uv)
        self.normals.extend(normals if normals is not None else [normal]*len(points))
        faces = self.faces.setdefault(material, [])
        for i in range(1, len(points)-1):
            faces.append((start, start+i, start+i+1))

    def tube(self, points, radii, sides):
        points = [np.array(p) for p in points]
        rings, normals, distance = [], [], [0.0]
        previous = None
        for i, (point, radius) in enumerate(zip(points, radii)):
            direction = unit(points[min(i+1,len(points)-1)] - points[max(i-1,0)])
            tangent = unit(np.cross(direction, [0,0,1])) if previous is None else unit(previous-direction*np.dot(previous,direction))
            if np.linalg.norm(tangent)<.5: tangent=unit(np.cross(direction,[1,0,0]))
            bitangent = unit(np.cross(direction,tangent))
            previous = tangent
            if i: distance.append(distance[-1]+np.linalg.norm(point-points[i-1]))
            ring, ns = [], []
            for j in range(sides+1):
                angle=j*2*math.pi/sides
                radial=tangent*math.cos(angle)+bitangent*math.sin(angle)
                # Low-amplitude geometric fluting keeps the silhouette organic.
                variation=1+.045*math.sin(angle*5+i*.23)+.022*math.sin(angle*9-i*.4)
                ring.append(point+radial*radius*variation)
                ns.append(radial)
            rings.append(ring);normals.append(ns)
        circumference=2*math.pi*radii[0]
        for i in range(len(points)-1):
            for j in range(sides):
                self.face([rings[i][j],rings[i][j+1],rings[i+1][j+1],rings[i+1][j]],
                          [(j/sides*circumference,distance[i]/2),((j+1)/sides*circumference,distance[i]/2),
                           ((j+1)/sides*circumference,distance[i+1]/2),(j/sides*circumference,distance[i+1]/2)],
                          "bark", [normals[i][j],normals[i][j+1],normals[i+1][j+1],normals[i+1][j]])

    def save(self, name, material_file):
        path=HERE/"meshes"/(name+".obj")
        with path.open("w",encoding="ascii") as out:
            out.write(f"# Deterministic geometry from relief_demo.py\nmtllib {material_file}\n")
            for p in self.vertices: out.write("v "+text(p)+"\n")
            for p in self.uvs: out.write("vt "+text(p)+"\n")
            for p in self.normals: out.write("vn "+text(p)+"\n")
            for material, faces in self.faces.items():
                out.write("usemtl "+material+"\n")
                for face in faces: out.write("f "+" ".join(f"{i}/{i}/{i}" for i in face)+"\n")
        return {"vertices":len(self.vertices),"triangles":sum(len(f) for f in self.faces.values())}


def oak():
    rng=np.random.default_rng(18022026)
    mesh=Mesh()
    leaf_count=0

    def leaf(origin, direction):
        nonlocal leaf_count
        length=float(rng.uniform(.105,.19))
        width=length*float(rng.uniform(.45,.66))
        axis=unit(direction+np.array([0,float(rng.uniform(-.8,.8)),0]))
        lateral=unit(np.cross(axis,unit(rng.normal(size=3))))
        normal=unit(np.cross(axis,lateral))
        # Folded, gently lobed blades: actual triangles, not camera-facing cards.
        outline=[(0,0),(.21,-.32),(.39,-.48),(.53,-.35),(.67,-.4),(.84,-.23),
                 (1,0),(.84,.23),(.67,.4),(.53,.35),(.39,.48),(.21,.32)]
        centre=origin+axis*length*.48+normal*length*.07
        material="leaf"+str(int(rng.integers(0,6)))
        for i in range(len(outline)):
            a,b=outline[i],outline[(i+1)%len(outline)]
            points=[centre, origin+axis*(a[0]*length)+lateral*(a[1]*width),
                    origin+axis*(b[0]*length)+lateral*(b[1]*width)]
            mesh.face(points,[(.5,.5),a,b],material)
        leaf_count+=1

    def branch(start, direction, length, radius, depth):
        direction=unit(direction)
        bend=unit(rng.normal(size=3)*[1,.3,1])*.12
        points=[start+direction*(length*t)+np.array([0,length*.24*t*t,0])+bend*(length*math.sin(math.pi*t))
                for t in np.linspace(0,1,6)]
        mesh.tube(points,[radius*(1-.85*t) for t in np.linspace(0,1,6)],max(5,12-depth*2))
        if depth>=3:
            for i in range(42):
                t=float(rng.uniform(.15,1.0))
                centre=points[0]*(1-t)+points[-1]*t
                radial=unit(rng.normal(size=3))
                offset=radial*float(rng.uniform(.05,.32))
                leaf(centre+offset,radial)
            return
        for i in range(3):
            t=.5+i*.23
            start_point=points[0]*(1-t)+points[-1]*t
            turn=unit(rng.normal(size=3)*[1,.55,1])
            child=unit(direction*.45+turn*.7+np.array([0,.24,0]))
            branch(start_point,child,length*float(rng.uniform(.49,.65)),radius*.48,depth+1)

    trunk=[np.array([.08*math.sin(y*.8),y,.055*math.sin(y*1.3)]) for y in np.linspace(0,6.5,24)]
    mesh.tube(trunk,[.29*(1-y/7.1)**1.2+.015 for y in np.linspace(0,6.5,24)],28)
    # Buttress roots merge into the tapering base rather than ending in a cylinder.
    for i in range(7):
        a=i*2*math.pi/7+.2
        direction=np.array([math.cos(a),0,math.sin(a)])
        mesh.tube([direction*.68+[0,.025,0],direction*.36+[0,.09,0],np.array([0,.58,0])],
                  [.025,.10,.15],10)
    for i in range(10):
        y=2.25+i*.365
        angle=i*2.399963+float(rng.uniform(-.22,.22))
        direction=unit([math.cos(angle),float(rng.uniform(.32,.58)),math.sin(angle)])
        branch(np.array([.08*math.sin(y*.8),y,.055*math.sin(y*1.3)]),direction,
               float(rng.uniform(1.9,2.6))*(1-.035*i),.12*(1-.065*i),0)
    mtl=["newmtl bark\nKd 1 1 1\nKa .18 .16 .13\nKs .035 .035 .035\nNs 12\n"]
    colors=[(.16,.34,.055),(.20,.39,.07),(.13,.29,.04),(.24,.42,.08),(.18,.36,.045),(.27,.40,.09)]
    for i,color in enumerate(colors):
        mtl.append(f"newmtl leaf{i}\nKd {text(color)}\nKa .09 .12 .055\nKs .04 .055 .025\nNs 18\n")
    (HERE/"meshes/relief-oak.mtl").write_text("\n".join(mtl),encoding="ascii")
    stats=mesh.save("relief-oak","relief-oak.mtl")
    stats["leaves"]=leaf_count
    return stats


def masonry():
    source=HERE.parent.parent/"models/ResidentialBuildings/ResidentialBuildings007.fbx"
    mesh=HERE/"meshes/city-007-masonry.obj"
    metadata=mesh.with_suffix(".json")
    tool=HERE/"output/capture-build/RelWithDebInfo/city_masonry.exe"
    digest=lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
    if tool.exists():
        subprocess.run([str(tool),str(source),str(mesh),str(metadata)],check=True)
        stats=json.loads(metadata.read_text(encoding="utf-8"))
        stats.update(source=str(source.relative_to(HERE.parent.parent)).replace("\\","/"),
                     source_sha256=digest(source),mesh_sha256=digest(mesh),
                     material_sha256=digest(mesh.with_suffix(".mtl")))
        metadata.write_text(json.dumps(stats,indent=2)+"\n",encoding="utf-8")
    stats=json.loads(metadata.read_text(encoding="utf-8"))
    for path,key in [(source,"source_sha256"),(mesh,"mesh_sha256"),(mesh.with_suffix(".mtl"),"material_sha256")]:
        if digest(path)!=stats[key]:
            raise ValueError(f"Stale masonry asset: {path}; build the city_masonry tool and regenerate")
    return stats


def ball():
    mesh=Mesh()
    sides,rings=64,32
    def point(u,v):
        a,b=u*2*math.pi,v*math.pi
        return np.array([math.sin(b)*math.cos(a),-math.cos(b),math.sin(b)*math.sin(a)])
    for j in range(rings):
        for i in range(sides):
            uv=[(i/sides,j/rings),((i+1)/sides,j/rings),
                ((i+1)/sides,(j+1)/rings),(i/sides,(j+1)/rings)]
            p=[point(*v) for v in uv]
            # Reverse the grid's winding to face outward; skip degenerate pole triangles.
            for tri in ((0,2,1),(0,3,2)):
                if (j==0 and tri==(0,2,1)) or (j==rings-1 and tri==(0,3,2)): continue
                mesh.face([p[k] for k in tri],[uv[k] for k in tri],"rubber",[p[k] for k in tri])
    (HERE/"meshes/bounce-ball.mtl").write_text(
        "newmtl rubber\nKd 1 1 1\nKa .15 .15 .15\nKs .04 .04 .04\nNs 80\n",encoding="ascii")
    bake_basketball()
    return mesh.save("bounce-ball","bounce-ball.mtl")


def prepare_brick_height():
    # The source is 16-bit displacement with a sub-range of the PNG's full range.
    # Keep it intact and make an explicitly normalized runtime height profile.
    source=HERE/'textures/red_brick-height.png'
    values=np.asarray(Image.open(source),dtype=np.float64)
    low,high=float(values.min()),float(values.max())
    if high<=low:raise ValueError('Brick height data has no relief')
    normalized=(values-low)/(high-low)
    # Anchor brick fronts to the mesh plane. Derive their individual footprints
    # from the matched colour image; mortar remains recessed between the bricks.
    colour=np.asarray(Image.open(HERE/'textures/red_brick-albedo.jpg').convert('RGB'),dtype=np.float64)/255
    mask=Image.fromarray(np.uint8((colour[...,0]-colour[...,1]>.075)*255))
    mask=mask.filter(ImageFilter.MaxFilter(5)).filter(ImageFilter.MinFilter(5)).filter(ImageFilter.GaussianBlur(2.0))
    profile=np.clip((np.asarray(mask,dtype=np.float64)/255-.12)/.76,0,1)
    profile=profile*profile*(3-2*profile)
    normalized=np.uint8(np.rint(profile*(.97+.03*normalized)*255))
    target=HERE/'textures/red_brick-parallax-height.png'
    Image.fromarray(normalized).save(target)
    metadata={'source':source.relative_to(HERE).as_posix(),'source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),
              'source_range_16bit':[int(low),int(high)],'runtime_range_8bit':[0,255],
              'operation':'Per-brick footprints from matched albedo, closed small holes, bevelled edge profile; brick fronts anchored near height 1, mortar at height 0; 3 percent source microrelief retained',
              'relief_depth_m':.035,'output_sha256':hashlib.sha256(target.read_bytes()).hexdigest()}
    (HERE/'brick-height-profile.json').write_text(json.dumps(metadata,indent=2)+'\n',encoding='utf8')
    return metadata


def add_demonstration(scene):
    assets=scene.find("assets")
    city=next(node for node in scene.iter("transform") if node.get("name","").startswith("AES expanded wing"))
    for child in list(city):
        if child.get("name")=="Parallax bark and brick" or child.get("name","").startswith("Planter foliage"):
            city.remove(child)
    for child in list(assets):
        if child.get("id") in {"relief-oak", "relief-brick-wall", "bounce-ball"}: assets.remove(child)
    building=next(asset for asset in assets if asset.get("id")=="city-7")
    building.set("file",PREFIX+"meshes/city-007-masonry.obj")
    for child in list(building):
        if child.get("name")=="brick": building.remove(child)
    for model,material,source,depth in [("relief-oak","bark","bark_brown_01",.018),
                                         ("city-7","brick","red_brick",.035)]:
        asset=building if model=="city-7" else ET.SubElement(assets,"model",id=model,file=PREFIX+f"meshes/{model}.obj")
        ET.SubElement(asset,"material",name=material,diffuse="1 1 1",ambient=".2 .2 .2",
            specular=".035 .035 .035",metallic="0",roughness=".88",environment="true",
            texture0=PREFIX+f"textures/{source}-albedo.jpg",
            normalMap=PREFIX+f"textures/{source}-normal.png",normalScale="1",
            heightMap=PREFIX+("textures/red_brick-parallax-height.png" if source=="red_brick" else f"textures/{source}-height.png"),
            parallax="true",parallaxScale=str(depth))
        if model=="relief-oak":
            for i in range(6):
                ET.SubElement(asset,"material",name=f"leaf{i}",cull="false",roughness=".72",environment="true")
    exhibit=ET.SubElement(city,"group",name="Parallax bark and brick",room="aes")
    for i,(x,depth,scale,rotation) in enumerate(TREE_PLACEMENTS,1):
        planter=ET.SubElement(exhibit,"transform",name=f"Oak planting {i:03}",translate=text((x,.875,-depth)))
        soil=ET.SubElement(planter,"transform",name=f"Oak soil bed {i:03}",translate="0 -.015 0",scale="1.2 .04 2.75")
        ET.SubElement(soil,"state",diffuse=".28 .18 .08",ambient=".12 .1 .07",roughness="1",
                      texture0=PREFIX+"textures/asphalt.png")
        ET.SubElement(soil,"instance",ref="box",selectable="false")
        tree=ET.SubElement(planter,"transform",name=f"Oak shape {i:03}",scale=text(scale),rotate=text(rotation))
        shape=ET.SubElement(tree,"group",name=f"Oak crown deformation {i:03}")
        parameters=ET.SubElement(shape,"state")
        ET.SubElement(parameters,"uniform",name="uTreeHeight",type="float",value="8")
        ET.SubElement(parameters,"uniform",name="uTreeWarp",type="vec4",value=text(TREE_WARPS[i-1]))
        ET.SubElement(shape,"instance",ref="relief-oak",name=f"Street oak {i:03}",selectable="true")
    north=next(node for node in scene.iter("transform") if node.get("name","").startswith("LAPIS expanded wing"))
    for child in list(north):
        if child.get("name")=="Vertex shader bouncing ball": north.remove(child)
    asset=ET.SubElement(assets,"model",id="bounce-ball",file=PREFIX+"meshes/bounce-ball.obj")
    ET.SubElement(asset,"material",name="rubber",texture0=PREFIX+"textures/basketball-albedo.png",
                  normalMap=PREFIX+"textures/basketball-normal.png",normalScale="1",
                  heightMap=PREFIX+"textures/basketball-height.png",parallax="true",parallaxScale=str(BASKETBALL_DEPTH),
                  diffuse="1 1 1",ambient=".16 .16 .16",specular=".04 .04 .04",shininess="28",
                  roughness=".76",metallic="0",environment="true")
    placed=ET.SubElement(north,"transform",name="Vertex shader bouncing ball",translate="-7 1.16 -47",scale="1.15 1.15 1.15")
    ET.SubElement(placed,"instance",ref="bounce-ball",name="Bouncing ball",selectable="true",bounceRadius="1")


def configure_parallax_view(scene,name):
    if name not in ('brick','parallax'):return
    # The close view uses the same physical depth as the building in the main scene.
    material=scene.find("assets/model[@id='city-7']/material[@name='brick']")
    material.set('parallaxScale','.035')


def build_meshes():
    prepare_brick_height()
    stats={"oak":oak(),"city_masonry":masonry(),"ball":ball(),"tree_instances":len(TREE_PLACEMENTS),
           "tree_placements":TREE_PLACEMENTS,"tree_warps":TREE_WARPS,"seed":18022026,"cameras":CAMERAS}
    (HERE/"relief-geometry.json").write_text(json.dumps(stats,indent=2)+"\n",encoding="utf-8")
    return stats


def main():
    from billboard_demo import add_demonstration as add_billboards
    stats=build_meshes()
    for path in [HERE/"scene.xml",HERE/"portal-scene.xml",*sorted((HERE/"views").glob("*.xml"))]:
        tree=ET.parse(path)
        add_demonstration(tree.getroot())
        add_billboards(tree.getroot())
        ET.indent(tree,space="  ")
        tree.write(path,encoding="utf-8",xml_declaration=True)
    base=ET.parse(HERE/"scene.xml").getroot()
    for name,(eye,target) in CAMERAS.items():
        scene=copy.deepcopy(base)
        scene.find("camera").set("eye",text(eye))
        scene.find("camera").set("target",text(target))
        configure_parallax_view(scene,name)
        ET.indent(scene,space="  ")
        ET.ElementTree(scene).write(HERE/"views"/(name+".xml"),encoding="utf-8",xml_declaration=True)
    geometry_path=HERE/"geometry.json"
    geometry=json.loads(geometry_path.read_text(encoding="utf-8"))
    resources=geometry["mesh_resources"]
    resources.pop("relief-brick-wall",None)
    for name,key in [("relief-oak","oak"),("city-007-masonry","city_masonry"),("bounce-ball","ball")]:
        item=stats[key]
        resources[name]={"vertices_before_import":item["vertices"],"triangles":item["triangles"]}
    geometry["unique_mesh_triangles"]=sum(v["triangles"] for v in resources.values())
    geometry["authored_instances"]=len(list(base.iter("instance")))
    geometry_path.write_text(json.dumps(geometry,indent=2)+"\n",encoding="utf-8")
    manifest_path=HERE/"scene-manifest.json"
    manifest=json.loads(manifest_path.read_text(encoding="utf-8"))
    manifest["parallax"].update(brick_location="Tower 007 ground-floor walls",brick_height_m=3.6)
    manifest["city_trees"]={"instances":len(TREE_PLACEMENTS),"shared_asset":"relief-oak","placements":TREE_PLACEMENTS,"vertex_warps":TREE_WARPS,"deformation_height":8}
    manifest["vertex_bounce"]={"instance":"Bouncing ball","room":"lapis","view":"bounce",
        "pause":"Aim, then click Pause or press P","shader":"shaders/bounce.glsl",
        "material":"basketball-material.json","parallax_depth_m":BASKETBALL_DEPTH}
    manifest_path.write_text(json.dumps(manifest,indent=2)+"\n",encoding="utf-8")
    print(json.dumps(stats,indent=2))


if __name__=="__main__": main()
