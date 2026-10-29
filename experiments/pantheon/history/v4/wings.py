"""Large, furnished side wings. Reuses the original scene's assets/materials.

The small API is supplied by generate.py, so there is one transform registry.
All new authoring and derived resources remain inside this experiment.
"""
import copy
import hashlib
import json
import math
import xml.etree.ElementTree as ET
import numpy as np
from PIL import Image, ImageDraw, ImageFont
from gyroscopes import add_gyroscopes

START = 34.0
ROOMS = {
    "lapis": {"width":48, "depth":48, "height":26, "center_depth":58, "kind":"materials / original cloudy sky"},
    "aes": {"width":64, "depth":78, "height":106, "center_depth":73, "kind":"four-tower city courtyard"},
    "silva": {"width":42, "depth":44, "height":14, "center_depth":56, "kind":"legacy object gallery / three rotation demonstrations"},
    "astra": {"width":48, "depth":48, "height":30, "center_depth":58, "kind":"dragon planetarium / star sky"},
}


def read_rgbe(path):
    """Decode the bundled Radiance HDR's planar scanline RLE, without plugins."""
    with path.open("rb") as f:
        assert f.readline().startswith(b"#?"), "Not a Radiance file"
        while f.readline().strip(): pass
        axis,h,axis2,w=f.readline().split()
        assert axis==b"-Y" and axis2==b"+X", "Unsupported HDR orientation"
        h,w=int(h),int(w)
        data=np.empty((h,w,4),dtype=np.uint8)
        for y in range(h):
            assert f.read(4)==bytes((2,2,w>>8,w&255)), "Unsupported RGBE scanline"
            for channel in range(4):
                x=0
                while x<w:
                    count=f.read(1)[0]
                    if count>128:
                        count-=128
                        data[y,x:x+count,channel]=f.read(1)[0]
                    else:
                        assert count>0
                        data[y,x:x+count,channel]=np.frombuffer(f.read(count),dtype=np.uint8)
                    x+=count
                assert x==w, "RGBE scanline overrun"
        pixels=np.ldexp(data[:,:,:3].astype(np.float32),data[:,:,3,None].astype(np.int32)-136)
        pixels[data[:,:,3]==0]=0
        return pixels


def sky_textures(here, repo):
    source=repo/"textures/sky/kloofendal_48d_partly_cloudy_puresky_2k.hdr"
    pixels=read_rgbe(source)
    height,width=pixels.shape[:2]
    weight=np.cos(((np.arange(height)+.5)/height-.5)*np.pi)
    luminance=pixels@np.array([.2126,.7152,.0722])
    exposure=.35*width*weight.sum()/(luminance*weight[:,None]).sum()
    # Linear 8-bit room backdrop; the viewer applies its existing ACES display
    # transform. Specular IBL still uses the original full HDR, unchanged.
    Image.fromarray(np.round(np.clip(pixels*exposure,0,1)*255).astype(np.uint8)).save(here/"textures/legacy-cloud-sky.png")
    rng=np.random.default_rng(180)
    w,h=2048,1024
    image=Image.new("RGB",(w,h),(0,0,1))
    draw=ImageDraw.Draw(image)
    for i in range(2800):
        u=int(rng.integers(0,w)); v=int(rng.integers(0,h))
        intensity=float(rng.uniform(.07,.42))
        color=tuple(int(c*intensity*255) for c in ((.8,.9,1) if i%3 else (1,.87,.7)))
        if i%85==0:
            draw.ellipse((u-1,v-1,u+1,v+1),fill=color)
        else: draw.point((u,v),fill=color)
    image.save(here/"textures/astra-stars.png")
    asphalt=rng.normal(40,3,(512,512,1)).clip(0,255)
    Image.fromarray(np.repeat(asphalt,3,axis=2).astype(np.uint8)).save(here/"textures/asphalt.png")
    return {"source":str(source.relative_to(repo)).replace("\\","/"),"sha256":hashlib.sha256(source.read_bytes()).hexdigest(),
            "room_backdrop":"textures/legacy-cloud-sky.png","exposure":float(exposure),
            "note":"Derived linear LDR backdrop only. Original HDR remains the global reflection/lighting source."}


def add_wings(scene, root, assets, colors, api):
    here, repo, prefix=api.here,api.here.parent.parent,api.prefix
    Mesh,transform,group,state,asset,vec=api.Mesh,api.transform,api.group,api.state,api.asset,api.vec
    legacy=ET.parse(repo/"scenes/house-patio.xml").getroot()
    legacy_assets={e.get("id"):e for e in legacy.find("assets")}
    for item in legacy_assets.values(): assets.append(copy.deepcopy(item))
    # The current raster material is deliberately explicit: real glass polygons,
    # no baked reflected-photo texture masquerading as a reflective window.
    catalog=json.loads((here/"building_catalog.json").read_text(encoding="utf-8"))
    used_buildings=(6,7,9,10)
    for index in used_buildings:
        item=catalog[f"{index:03}"]
        source=repo/item["file"]
        assert hashlib.sha256(source.read_bytes()).hexdigest()==item["sha256"],"Reinspect changed building: "+str(source)
        e=ET.SubElement(assets,"model",id=f"city-{index}",file=item["file"])
        ET.SubElement(e,"material",name="ground",environment="true",metallic="0.65",roughness="0.26",diffuse="0.32 0.36 0.41",specular="0.2 0.22 0.25",texture0="none",normalMap="none")
        ET.SubElement(e,"material",name="hotel_glas",environment="true",metallic="0",roughness="0.055",ior="1.52",
                      diffuse="0.095 0.16 0.21",specular="0.04 0.04 0.04",texture0="none",normalMap="none",cull="false")
    # Real closed 12 mm panes and separate mullions, authored as reusable floor
    # strips. The source FBX remains each tower's structural core. This avoids
    # one graph node per window while keeping glass as actual traceable triangles.
    first=catalog["006"]
    tower_width=(first["max"][0]-first["min"][0])*.01
    tower_depth=(first["max"][2]-first["min"][2])*.01
    for label,span,columns in [("short",tower_width,5),("long",tower_depth,9)]:
        m=Mesh();step=span/columns
        for column in range(columns):
            x=-span/2+(column+.5)*step
            m.box((x,1.8,0),(step-.14,2.8,.012),"glazing")
        for column in range(columns+1):
            m.box((-span/2+column*step,1.8,.008),(.075,3.6,.1),"mullion")
        for y in (.17,3.43): m.box((0,y,.008),(span,.1,.1),"mullion")
        m.save("curtain-"+label)
        asset(assets,"curtain-"+label,"curtain-"+label,{"glazing":dict(diffuse=".065 .14 .21",specular=".04 .04 .04",environment=True,roughness=".055",metallic="0",ior="1.52",cull=False),
                                                       "mullion":dict(diffuse=".2 .24 .3",specular=".3 .34 .4",environment=True,roughness=".22",metallic=".82")})
    derived_sky=sky_textures(here,repo)
    # Rounded plinth with shared smooth edge normals, rather than sharp boxes.
    mesh=Mesh()
    grid=(-.5,-.42,.42,.5)
    for axis in range(3):
        others=[i for i in range(3) if i!=axis]
        for sign in (-1,1):
            for i in range(3):
                for j in range(3):
                    pts=[]; normals=[]
                    for u,v in [(grid[i],grid[j]),(grid[i+1],grid[j]),(grid[i+1],grid[j+1]),(grid[i],grid[j+1])]:
                        p=np.zeros(3);p[axis]=sign*.5;p[others[0]]=u;p[others[1]]=v
                        inner=np.clip(p,-.42,.42); n=p-inner;n/=np.linalg.norm(n)
                        pts.append(tuple(inner+n*.08));normals.append(tuple(n))
                    n=[0,0,0];n[axis]=sign
                    mesh.poly(pts,n,material="stone",vertex_normals=normals)
    mesh.save("polished-plinth")
    asset(assets,"polished-plinth","polished-plinth",{"*":dict(diffuse=".12 .16 .19",ambient=".12 .14 .16",metallic=".3",roughness=".23",environment=True)})
    asset(assets,"dragon-plinth","polished-plinth",{"*":dict(diffuse=".035 .05 .08",ambient=".18 .2 .24",specular=".25 .28 .32",roughness=".22",environment=False)})
    m=Mesh();api.annulus(m,.998,1,0,"stone",128);m.save("floor-inlay")
    asset(assets,"floor-inlay","floor-inlay",{"*":colors["bronze"]})
    # Unit floor with metre-based UVs when authored to physical dimensions.
    def box_asset(name,center,size,material):
        m=Mesh();m.box(center,size);m.save(name)
        asset(assets,name,name,{"*":material})
    brass=colors["bronze"]
    marble=dict(colors["marble"],environment=True,roughness=".24",normalScale=".2")
    floor_stone=dict(marble,tint=".39 .45 .48",roughness=".3")
    box_asset("gallery-floor",(0,-.16,-56),(42,.32,44),floor_stone)
    box_asset("city-floor",(0,-.16,-73),(64,.32,78),dict(diffuse=".55 .58 .63",ambient=".25 .26 .28",roughness=".94",texture0=prefix+"textures/asphalt.png"))
    def sky_cap(name,spring,rise):
        m=Mesh()
        for j in range(12):
            for i in range(64):
                pairs=[(i,j),(i+1,j),(i+1,j+1),(i,j+1)] if j<11 else [(i,j),(i+1,j),(i+.5,12)]
                pts=[];uv=[];normals=[]
                for u,v in pairs:
                    a=u*math.tau/64; elevation=v*math.pi/24
                    p=(24*math.cos(elevation)*math.cos(a),spring+rise*math.sin(elevation),-58+24*math.cos(elevation)*math.sin(a))
                    pts.append(p);uv.append((u/64+.5,.5+elevation/math.pi))
                    normals.append((-p[0]/24**2,-(p[1]-spring)/rise**2,-(p[2]+58)/24**2))
                m.poly(pts,normals[0],uv,vertex_normals=normals)
        m.save(name)
    sky_cap("cloud-ceiling",8,18)
    sky_cap("star-ceiling",6,24)
    for name,texture in [("cloud-ceiling","legacy-cloud-sky.png"),("star-ceiling","astra-stars.png")]:
        asset(assets,name,name,{"*":dict(lighting=False,diffuse="1 1 1",texture0=prefix+"textures/"+texture,cull=False,castShadows=False,receiveShadows=False)})
    m=Mesh()
    for a,b in ((-1,-.18),(.18,1)):
        m.poly([(a,0,0),(b,0,0),(b,2.8,0),(a,2.8,0)],(0,0,1),[(0,0),(1,0),(1,1),(0,1)])
    m.save("gallery-mirror")
    asset(assets,"gallery-mirror","gallery-mirror",{"*":dict(mirror=True,environment=False,lighting=False,castShadows=False,cull=True,diffuse="0 0 0")})
    # Floor and wall are separate meshes / bounds; do not leave a disk at the
    # rotunda origin under a distant wing.
    for name,height,mat in [("material-wall",8,dict(colors["marble"],tint=".32 .37 .39",roughness=".28")),
                            ("planetarium-wall",6,dict(colors["marble"],tint=".035 .055 .09",roughness=".24"))]:
        m=Mesh()
        opening=math.asin(api.half/24)
        for i in range(112):
            a,b=opening+i*(math.tau-2*opening)/112,opening+(i+1)*(math.tau-2*opening)/112
            pts=[(24*math.sin(t),y,-58+24*math.cos(t)) for t,y in [(a,0),(b,0),(b,height),(a,height)]]
            m.poly(pts,(-math.sin((a+b)/2),0,-math.cos((a+b)/2)),[(t*8,y/3) for t,y in [(a,0),(b,0),(b,height),(a,height)]])
        m.save(name);asset(assets,name,name,{"*":dict(mat,cull=False)})
    for name,mat in [("material-floor",marble),("planetarium-floor",dict(marble,tint=".055 .075 .105",roughness=".23",environment=False,ambient=".18 .22 .28",specular=".18 .22 .28"))]:
        m=Mesh()
        # Reuse the annulus writer but move the final disk's vertices explicitly
        # while authoring, rather than flattening the scene graph transforms.
        for i in range(128):
            a,b=i*math.tau/128,(i+1)*math.tau/128
            pts=[(0,-.01,-58),(24*math.sin(a),-.01,-58+24*math.cos(a)),(24*math.sin(b),-.01,-58+24*math.cos(b))]
            m.poly(pts,(0,1,0),[(p[0]/3,p[2]/3) for p in pts])
        m.save(name);asset(assets,name,name,{"*":mat})

    def inst(parent,ref,name=None,selectable=False):
        return ET.SubElement(parent,"instance",ref=ref,name=name or ref,selectable=str(selectable).lower())
    def podium(parent,name,x,z,width=3.2,height=.64,depth=3.2,ref="polished-plinth"):
        p=transform(parent,name,(x,height/2,z),(width,height,depth),ref=ref)
        transform(parent,name+" brass reveal",(x,height-.018,z),(width-.1,.025,depth-.1),ref="box",mat=brass)
        return p
    def lamp(parent,name,position,diffuse,range_=60,direction=(0,-1,0),inner=40,outer=68):
        ET.SubElement(parent,"light",name=name,position=vec((*position,1)),direction=vec(direction),inner=str(inner),outer=str(outer),range=str(range_),
                      diffuse=diffuse,specular="2 2 2",ambient=".015 .016 .02",attenuation="1 .005 .0015")
    def neck(parent,name,mat):
        start=api.door+1.1 # back face of the existing arch, not its front face
        rounded=name in {"lapis","astra"}
        end=58-math.sqrt(24**2-api.half**2) if rounded else START-.25 if name=="silva" else START
        length=end-start;center=-(start+end)/2
        half=api.half;outer=half+.4
        m=Mesh()
        # One U-shaped extrusion owns all interior and exterior shell faces.
        # No coincident wall-top/ceiling-bottom faces; the arch joint is open.
        profile=[(-half,0),(-half,10),(half,10),(half,0),(outer,0),(outer,10.3),(-outer,10.3),(-outer,0)]
        normals=[(1,0,0),(0,-1,0),(-1,0,0),(0,-1,0),(1,0,0),(0,1,0),(-1,0,0),(0,-1,0)]
        for i,normal in enumerate(normals):
            x0,y0=profile[i];x1,y1=profile[(i+1)%len(profile)]
            pts=[(x0,y0,-start),(x1,y1,-start),(x1,y1,-end),(x0,y0,-end)]
            m.poly(pts,normal,[(p[2]/3,(p[0] if normal[1] else p[1])/3) for p in pts])
        if name!="silva":
            for x0,x1,y0,y1 in [(-outer,-half,0,10.3),(half,outer,0,10.3),(-half,half,10,10.3)]:
                m.poly([(x0,y0,-end),(x1,y0,-end),(x1,y1,-end),(x0,y1,-end)],(0,0,-1))
        ref=name+"-passage-shell";m.save(ref);asset(assets,ref,ref,{"*":dict(mat,cull=True)})
        transform(parent,name+" passage shell",ref=ref)
        for side in (-1,1):
            transform(parent,name+" passage rail",(side*(api.half-.12),1.2,center),(.06,.1,length),ref="box",mat=brass)
        # Match the actual polygonal floor outlines, including their tessellation.
        # A 25/35 mm transition joins the old inset hall floor to the room floor.
        def edge(radius,steps,centre,direction):
            points=[(radius*math.sin(i*math.tau/steps),centre+direction*radius*math.cos(i*math.tau/steps)) for i in range(steps)]
            result=[]
            for a,b in zip(points,points[1:]+points[:1]):
                if direction*((a[1]+b[1])/2-centre)<=0:continue
                if -half<=a[0]<=half:result.append(a)
                for x in (-half,half,-3,0,3):
                    if min(a[0],b[0])<x<max(a[0],b[0]):
                        t=(x-a[0])/(b[0]-a[0]);result.append((x,a[1]+t*(b[1]-a[1])))
            return sorted(set(result))
        front=edge(api.radius,112,0,1)
        back=edge(24,128,58,-1) if rounded else [(-half,START),(half,START)]
        xs=sorted(set([p[0] for p in front+back]))
        def depth(points,x):return float(np.interp(x,[p[0] for p in points],[p[1] for p in points]))
        m=Mesh();height=-.01 if rounded else 0
        for x0,x1 in zip(xs,xs[1:]):
            pts=[(x0,-.035,-depth(front,x0)),(x1,-.035,-depth(front,x1)),
                 (x1,height,-depth(back,x1)),(x0,height,-depth(back,x0))]
            m.poly(pts,(0,1,0),[(p[0]/3,p[2]/3) for p in pts])
        ref=name+"-passage-floor";m.save(ref);asset(assets,ref,ref,{"*":dict(floor_stone,cull=True)})
        transform(parent,name+" passage floor",ref=ref)
    wings={}
    for i,name in enumerate(("lapis","aes","silva","astra")):
        wings[name]=transform(root,name.upper()+" expanded wing",rot=(0,-90*i,0))
        neck(wings[name],name,dict(colors["marble"],tint=".52 .55 .54"))
    # North: retain every original material ball and its actual optical state.
    north=wings["lapis"]
    transform(north,"Legacy cloudy sky",ref="cloud-ceiling")
    transform(north,"Marble rotunda gallery floor",ref="material-floor")
    transform(north,"Material gallery perimeter",ref="material-wall")
    gallery=group(north,"All nine original material spheres",room="lapis")
    old_gallery=next(e for e in legacy.iter("group") if e.get("name")=="materialGallery")
    gallery.append(copy.deepcopy(old_gallery.find("state")))
    sphere_names=[]
    for e in old_gallery:
        if e.tag!="transform" or e.get("name") in ("materialPatioFloor","materialMirror") or "Plinth" in e.get("name",""): continue
        placed=copy.deepcopy(e)
        gallery.append(placed)
        sphere_names.append(e.get("name"))
        x,y,z=map(float,e.get("translate").split())
        podium(gallery,e.get("name")+" display",x,z)
    # Existing PlanarReflection looks up this exact transform and expects a world
    # matrix. North's parent is identity, so the original contract remains valid.
    mirror=transform(north,"materialMirror",(0,.35,-43),(18,2.6,1),rot=(0,180,0),exact=True,
                     mat=dict(mirror=True,environment=False,lighting=False,castShadows=False,cull=True,diffuse="0 0 0"))
    inst(mirror,"gallery-mirror")
    frame=group(north,"Full size mirror frame")
    for x in (-18.16,-3.3,3.3,18.16):
        transform(frame,"Mirror upright",(x,4,-42.87),(.22,7.7,.25),ref="polished-plinth")
    transform(frame,"Mirror lintel",(0,7.78,-42.87),(36.4,.22,.25),ref="polished-plinth")
    for x in (-10.62,10.62):
        transform(frame,"Mirror lower rail",(x,.21,-42.87),(14.76,.22,.25),ref="polished-plinth")
    lamp(north,"Material gallery softbox",(-8,16,-51),"4.2 3.9 3.5",65)
    transform(north,"Gallery luminous canopy",(-8,17,-51),(8,.1,5),ref="box",mat=dict(lighting=False,diffuse=".7 .8 1",emissive=".5 .6 .8",castShadows=False))
    # South: the old monkey / cow / torus / crates / cutout / characters remain
    # selectable and retain their source materials, animations and LODs.
    south=wings["silva"]
    transform(south,"Archive stone floor",ref="gallery-floor")
    for side in (-1,1):
        transform(south,"Archive side wall",(side*21.2,7,-56),(.4,14,44),ref="box",mat=dict(colors["marble"],tint=".26 .31 .3"))
    transform(south,"Archive rear wall",(0,7,-78.2),(42.8,14,.4),ref="box",mat=dict(colors["marble"],tint=".28 .3 .28"))
    transform(south,"Archive ceiling",(0,14.2,-56),(42.8,.4,44.8),ref="box",mat=dict(diffuse=".16 .2 .23",ambient=".4 .4 .4",roughness=".6"))
    transform(south,"Archive front lintel",(0,12,-START),(api.half*2,4,.5),ref="box",mat=dict(diffuse=".3 .34 .33",ambient=".3 .3 .3",roughness=".3"))
    # Close the two shoulders around the narrower entry passage.
    for side in (-1,1):
        transform(south,"Archive entrance shoulder",(side*(21+api.half)/2,7,-START),(21-api.half,14,.5),ref="box",mat=dict(diffuse=".3 .34 .33",ambient=".3 .3 .3",roughness=".3"))
    contents=group(south,"Legacy exhibits and rotation demonstrations",room="silva")
    old_primary=next(e for e in legacy.iter("group") if e.get("name")=="gallery")
    for e in old_primary:
        if e.tag!="transform" or e.get("name") not in {"instanceA","instanceB","animated","cowDisplay","toon","crateA","crateB","alphatest"}: continue
        placed=copy.deepcopy(e)
        x,y,z=map(float,e.get("translate","0 0 0").split())
        placed.set("translate",vec((x,y,z-48)))
        contents.append(placed)
    for x,z in [(-6,-48),(0,-48),(6,-48),(12,-48),(-6,-53)]: podium(contents,"Legacy object plinth",x,z,3.8,.8,3.8)
    for ref,x in (("c3po",-17),("altair",17)):
        podium(contents,ref+" character pedestal",x,-61,3,.35,3)
        if ref=="c3po": pos=(x,.35+.928358,-61);scale=.001
        else: pos=(x-.058,.35006,-61);scale=1.8/126.775
        p=transform(contents,ref+" human scale exhibit",pos,(scale,scale,scale))
        inst(p,ref,ref,True)
    gyroscopes=add_gyroscopes(contents,assets,colors,api)
    lamp(south,"Archive warm softbox",(0,12,-57),"5 4.5 3.6",65)
    for z in (-43,-56,-69): transform(south,"Archive ceiling light",(0,13.8,z),(10,.08,1),ref="box",mat=dict(lighting=False,diffuse="1 .82 .6",emissive=".7 .55 .3",castShadows=False))
    # West: a large curved planetarium with the original animated dragon.
    west=wings["astra"]
    transform(west,"Planetarium star hemisphere",ref="star-ceiling")
    transform(west,"Obsidian planetarium floor",ref="planetarium-floor")
    transform(west,"Planetarium curved perimeter",ref="planetarium-wall")
    dragon=group(west,"Dragon under the star sky",room="astra")
    podium(dragon,"Dragon island",0,-58,21,.36,17,ref="dragon-plinth")
    pose=transform(dragon,"dragonExhibit",(0,.02363,-56.43),(.0065,.0065,.0065),exact=True)
    inst(pose,"dragon","Dragon",True)
    lamp(west,"Dragon warm key",(-10,17,-48),"6 4.6 3.4",65,direction=(.25,-1,-.2),inner=35,outer=62)
    lamp(west,"Dragon blue rim",(10,12,-67),"1.4 3.5 7",55,direction=(-.5,-.5,.6),inner=28,outer=65)
    # Thin concentric brass floor inlays: geometry useful to a later ray tracer.
    for radius in (12,18,22):
        transform(west,"Planetarium brass orbit",(0,.025,-58),(radius,1,radius),ref="floor-inlay")
    # East: four real existing facades, at metres, with actual separate windows.
    east=wings["aes"]
    transform(east,"Metropolis road surface",ref="city-floor")
    buildings=group(east,"Four individually culled tower blocks",room="aes")
    building_manifest=[]
    for index,(x,depth) in zip(used_buildings,[(-17,54),(17,54),(-17,88),(17,88)]):
        item=catalog[f"{index:03}"];lo,hi=item["min"],item["max"]
        block=transform(buildings,f"Tower {index:03}",(x,0,-depth))
        pos=(-(lo[0]+hi[0])*.005,.24-lo[1]*.01,-(lo[2]+hi[2])*.005)
        structure=transform(block,"Original structural FBX",pos,(.01,.01,.01))
        inst(structure,f"city-{index}",f"Tower {index:03}",True)
        height=(hi[1]-lo[1])*.01
        floors=math.floor(height/3.6)
        for side,p,rotation,ref in [("front",(0,0,tower_depth/2+.06),0,"curtain-short"),
                                     ("back",(0,0,-tower_depth/2-.06),180,"curtain-short"),
                                     ("right",(tower_width/2+.06,0,0),90,"curtain-long"),
                                     ("left",(-tower_width/2-.06,0,0),-90,"curtain-long")]:
            facade=transform(block,side+" glass facade",p,rot=(0,rotation,0))
            for floor in range(1,floors):
                transform(facade,f"Glazed floor {floor:02}",(0,.24+floor*3.6,0),ref=ref)
        podium(buildings,"Tower stone podium",x,-depth,14,.24,25)
        pane_count=(floors-1)*28
        pane_area=(floors-1)*2*2.8*(5*(tower_width/5-.14)+9*(tower_depth/9-.14))
        building_manifest.append({"asset":item["file"],"source_sha256":item["sha256"],"uniform_scale":.01,"native_bounds":{"min":lo,"max":hi},
                                  "dimensions_m":[round((b-a)*.01,4) for a,b in zip(lo,hi)],"parent":"METROPOLIS / tower blocks",
                                  "local_center_m":[x,0,-depth],"source_local_translate_m":pos,"source_window_material":"hotel_glas","source_window_triangles":item["window_triangles"],
                                  "added_pane_count":pane_count,"added_glass_area_m2":round(pane_area,2),
                                  "added_glass_to_total_facade_area":round(pane_area/(2*(tower_width+tower_depth)*height),4),
                                  "raytracer":{"glass_material":"glazing","kind":"dielectric glazing","ior":1.52,"roughness":.055,"thickness_m":.012,"closed_panes":True,
                                               "note":"Source hotel_glas surfaces remain separate. Original FBX cores have no authored room interiors behind the new curtain wall."}})
    for side in (-1,1):
        transform(east,"Continuous stone sidewalk",(side*8.2,.12,-73),(3.8,.24,78),ref="box",mat=dict(colors["marble"],tint=".32 .37 .4",roughness=".35"))
        for depth in (39,70,104):
            transform(east,"Street planter",(side*7,.48,-depth),(1.4,.72,3),ref="polished-plinth")
            # Leaf cutouts reuse the old alpha-tested model, rather than box foliage.
            for offset in (-.55,.55):
                p=transform(east,"Planter foliage",(side*7,.7,-depth+offset),(.65,.65,.65),rot=(0,side*25,0),
                            mat=dict(cull=False,diffuse=".6 .85 .6",ambient=".3 .3 .3",texture0="textures/alphatest.png",alphaCutoff=".5"))
                inst(p,"alphatest-panel")
    for depth in range(40,110,7):
        transform(east,"Road centre marking",(0,.006,-depth),(.12,.012,3.5),ref="box",mat=dict(diffuse=".75 .69 .5",ambient=".2 .2 .2",roughness=".7"))
    # Real-size car: source bounds are 1.98 x 1.30 x 4.55 m at scale 1.
    old_car=next(e for e in legacy.iter("transform") if e.get("name")=="carExhibit")
    car=copy.deepcopy(old_car);car.set("translate","-2.2 0.019 -48");car.set("scale","1 1 1");car.set("rotate","0 0 0")
    east.append(car)
    return {"rooms":ROOMS,"legacy_material_spheres":sphere_names,"legacy_models":sorted(legacy_assets),
            "legacy_gallery_objects":[e.get("name") for e in old_primary if e.tag=="transform" and e.get("name") in {"instanceA","instanceB","animated","cowDisplay","toon","crateA","crateB","alphatest"}],
            "mirror":{"name":"materialMirror","overall_width_m":36,"height_m":7.28,"plane_world_z_m":-43,"central_passage_width_m":6.48,"front_normal_world":[0,0,-1]},
            "car":{"scale":1,"dimensions_m":[1.978846,1.3007395,4.54811]},"dragon":{"scale":.0065,"source_animation_clips":["Fly","Idle","Run","Walk"]},
            "buildings":building_manifest,"legacy_sky":derived_sky,"gyroscopes":gyroscopes,
            "lighting":"One sun, gallery/archive softboxes, dragon key/rim, two switchable car headlights and camera flashlight: eight light slots total."}
