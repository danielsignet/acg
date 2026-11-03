"""Offline-authored grass cards for axis-constrained billboards."""
import copy
import json
import math
from pathlib import Path
import random
import xml.etree.ElementTree as ET
from PIL import Image, ImageDraw

HERE = Path(__file__).resolve().parent
PREFIX = "experiments/pantheon/"
CAMERAS = {
    "billboards": ((36, 1.8, 3.4), (39, 1.15, 7)),
    "billboards-side": ((41.8, 1.65, 5.3), (39, 1.15, 7)),
}
SEED = 180207


def bake():
    rng = random.Random(SEED)
    size = 1024
    image = Image.new("RGBA", (size, size), (34, 58, 13, 0))
    draw = ImageDraw.Draw(image)
    # Separate tapered blades, curved in the image plane. RGB is authored in
    # linear light, matching the engine's diffuse textures; alpha covers leaves.
    for i in range(145):
        root = rng.uniform(.18, .82)
        height = rng.uniform(.20, .94)
        lean = rng.uniform(-.23, .23)
        width = rng.uniform(.003, .009)
        shade = rng.uniform(.72, 1.18)
        color = tuple(round(c * shade) for c in (46, 85, 18)) + (255,)
        left, right, centres = [], [], []
        for step in range(25):
            t = step / 24
            x = root + lean * t * t
            y = .99 - height * t
            half = width * (1 - t) ** .65
            left.append(((x-half)*size, y*size))
            right.append(((x+half)*size, y*size))
            centres.append((x*size, y*size))
        draw.polygon(left + list(reversed(right)), fill=color)
        if i % 3 == 0:
            draw.line(centres[:20], fill=tuple(round(c*1.22) for c in color[:3])+(255,), width=1)
    image.save(HERE / "textures/billboard-grass.png")
    (HERE / "meshes/billboard-grass.mtl").write_text(
        "newmtl grass\nKd 1 1 1\nKa .22 .25 .15\nKs .015 .02 .005\nNs 8\n", encoding="ascii")
    lines=["# Crossed grass cards, billboard_demo.py","mtllib billboard-grass.mtl"]
    for angle in (-math.pi/4,math.pi/4):
        c,s=math.cos(angle),math.sin(angle)
        for x,y in ((-.5,0),(.5,0),(.5,1),(-.5,1)):
            lines.append(f"v {x*c:.8f} {y:.8f} {x*s:.8f}")
    lines += [f"vt {x} {y}" for _ in range(2) for x,y in ((0,0),(1,0),(1,1),(0,1))]
    for angle in (-math.pi/4,math.pi/4):lines.append(f"vn {-math.sin(angle):.8f} 0 {math.cos(angle):.8f}")
    lines.append("usemtl grass")
    for i in range(2):
        for tri in ((1,2,3),(1,3,4)):
            lines.append("f "+" ".join(f"{v+4*i}/{v+4*i}/{i+1}" for v in tri))
    (HERE/"meshes/billboard-grass.obj").write_text("\n".join(lines)+"\n",encoding="ascii")
    metadata = {"technique": "crossed camera-oriented grass cards, constrained to local Y; planes at +/-45 degrees to view",
                "source": "Original procedural geometry and RGBA grass texture; no external asset",
                "seed": SEED, "triangles_per_clump": 4, "texture_size": [size, size],
                "clumps_per_planter": 18, "planters": 6, "clumps": 108,
                "alpha_cutoff": .35, "views": list(CAMERAS)}
    (HERE / "billboard-material.json").write_text(json.dumps(metadata, indent=2)+"\n", encoding="utf-8")
    return {"vertices_before_import": 8, "triangles": 4}


def add_demonstration(scene):
    assets = scene.find("assets")
    for asset in list(assets):
        if asset.get("id") == "billboard-grass": assets.remove(asset)
    asset = ET.SubElement(assets, "model", id="billboard-grass", file=PREFIX+"meshes/billboard-grass.obj")
    ET.SubElement(asset, "material", name="grass", texture0=PREFIX+"textures/billboard-grass.png",
                  diffuse="1 1 1", ambient=".22 .25 .15", specular=".015 .02 .005", roughness=".96",
                  metallic="0", alphaCutoff=".35", cull="false", blend="false", environment="true")
    rng = random.Random(SEED)
    positions = [(side*.38, z*.33) for side in (-1, 1) for z in range(-3, 4)]
    positions += [(x*.16, side*1.12) for side in (-1, 1) for x in (-1, 1)]
    for planter in [node for node in scene.iter("transform") if node.get("name", "").startswith("Oak planting ")]:
        for child in list(planter):
            if child.get("name") == "Billboard grass": planter.remove(child)
        group = ET.SubElement(planter, "group", name="Billboard grass")
        for i, (x, z) in enumerate(positions, 1):
            width, height = rng.uniform(.43, .58), rng.uniform(.33, .56)
            card = ET.SubElement(group, "billboard", name=f"{planter.get('name')} grass {i:02}", mode="axial-y",
                                 translate=f"{x:.4f} .01 {z:.4f}", scale=f"{width:.4f} {height:.4f} {width:.4f}")
            ET.SubElement(card, "instance", ref="billboard-grass", selectable="false")


def main():
    stats = bake()
    for path in [HERE/"scene.xml", HERE/"portal-scene.xml", *sorted((HERE/"views").glob("*.xml"))]:
        tree = ET.parse(path)
        add_demonstration(tree.getroot())
        ET.indent(tree, space="  ")
        tree.write(path, encoding="utf-8", xml_declaration=True)
    base = ET.parse(HERE/"scene.xml").getroot()
    for name, (eye, target) in CAMERAS.items():
        scene = copy.deepcopy(base)
        scene.find("camera").set("eye", " ".join(map(str, eye)))
        scene.find("camera").set("target", " ".join(map(str, target)))
        ET.indent(scene, space="  ")
        ET.ElementTree(scene).write(HERE/"views"/(name+".xml"), encoding="utf-8", xml_declaration=True)
    path = HERE/"geometry.json"
    geometry = json.loads(path.read_text(encoding="utf-8"))
    geometry["mesh_resources"]["billboard-grass"] = stats
    geometry["unique_mesh_triangles"] = sum(v["triangles"] for v in geometry["mesh_resources"].values())
    geometry["authored_instances"] = len(list(base.iter("instance")))
    path.write_text(json.dumps(geometry, indent=2)+"\n", encoding="utf-8")
    path = HERE/"scene-manifest.json"
    manifest = json.loads(path.read_text(encoding="utf-8"))
    manifest["billboards"] = json.loads((HERE/"billboard-material.json").read_text(encoding="utf-8"))
    path.write_text(json.dumps(manifest, indent=2)+"\n", encoding="utf-8")
    print(f"Grass billboards: {geometry['authored_instances']} scene instances, 108 shared crossed four-triangle clumps")


if __name__ == "__main__":
    main()
