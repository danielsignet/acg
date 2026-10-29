"""Connected, shared museum gyroscopes. Dimensions are metres.

Each hinge owns its ring, bearing housings and its child's bearing seats.
The next hinge's trunnions end in those seats, on the invariant hinge axis.
"""
import math
from PIL import Image, ImageDraw, ImageFont

RADII = (2.4, 1.95, 1.5)
CENTRE_Y = 4.05


def add_gyroscopes(parent, assets, colors, api):
    Mesh, transform, asset = api.Mesh, api.transform, api.asset
    # A surface of revolution with an explicit profile gives machined bevels
    # and smooth circular normals; no scaled primitive torus intersections.
    def lathe(mesh, profile, axis="z", offset=(0,0,0), material="stone", segments=80):
        def position(radius, depth, angle):
            a,b=radius*math.cos(angle),radius*math.sin(angle)
            p=(a,b,depth) if axis=="z" else (a,depth,b) if axis=="y" else (depth,a,b)
            return tuple(v+d for v,d in zip(p,offset))
        for (r0,d0),(r1,d1) in zip(profile,profile[1:]):
            dr,dd=r1-r0,d1-d0
            for i in range(segments):
                a,b=i*math.tau/segments,(i+1)*math.tau/segments
                pts=[position(r,d,t) for r,d,t in ((r0,d0,a),(r0,d0,b),(r1,d1,b),(r1,d1,a))]
                # For a counter-clockwise (radius,depth) cross section, the
                # outward normal is (+dd,-dr), transformed with the axis.
                norms=[]
                for t in (a,b,b,a):
                    radial=(dd*math.cos(t),dd*math.sin(t))
                    norms.append((radial[0],radial[1],-dr) if axis=="z" else
                                 (radial[0],-dr,radial[1]) if axis=="y" else (-dr,radial[0],radial[1]))
                normal=tuple(sum(n[j] for n in norms)/4 for j in range(3))
                if r0==0:
                    pts=[pts[j] for j in (0,2,3)];norms=[norms[j] for j in (0,2,3)]
                elif r1==0:
                    pts=[pts[j] for j in (0,1,2)];norms=[norms[j] for j in (0,1,2)]
                mesh.poly(pts,normal,material=material,vertex_normals=norms)

    def ring(mesh,radius,axis,material="stone",width=.085,depth=.12):
        bevel=.025
        profile=[(radius-width,-depth+bevel),(radius-width+bevel,-depth),
                 (radius+width-bevel,-depth),(radius+width,-depth+bevel),
                 (radius+width,depth-bevel),(radius+width-bevel,depth),
                 (radius-width+bevel,depth),(radius-width,depth-bevel),
                 (radius-width,-depth+bevel)]
        lathe(mesh,profile,axis,material=material)

    def cylinder(mesh,radius,start,end,axis,offset=(0,0,0),material="stone",segments=32):
        # Closed bevelled cylinder. Axial end faces meet the spindle or cap;
        # bearing bores below are true holes rather than overlapping end disks.
        bevel=min(.025,radius*.2,(end-start)*.2)
        profile=[(0,start),(radius-bevel,start),(radius,start+bevel),
                 (radius,end-bevel),(radius-bevel,end),(0,end)]
        lathe(mesh,profile,axis,offset,material,segments)

    def bearing(mesh,centre,axis,material="bronze",inner=.095,outer=.19):
        # Hollow sleeve, inner radius .095, outer .19, axial length .24.
        r0,r1=inner,outer;d0,d1=centre-.12,centre+.12
        p=[(r0,d0),(r1-.02,d0),(r1,d0+.02),(r1,d1-.02),
           (r1-.02,d1),(r0,d1),(r0,d0)]
        lathe(mesh,p,axis,material=material,segments=32)

    steel=dict(diffuse=".42 .47 .52",specular=".65 .7 .75",ambient=".035 .04 .05",
               metallic=".95",roughness=".2",environment=True)
    brass=dict(colors["bronze"],diffuse=".67 .44 .16",roughness=".19",metallic=".92")
    dark=dict(steel,diffuse=".075 .095 .12",roughness=".25")
    accents={"heading":dict(steel,diffuse=".52 .09 .055",roughness=".24"),
             "pitch":dict(steel,diffuse=".055 .35 .19",roughness=".24"),
             "roll":dict(steel,diffuse=".055 .19 .5",roughness=".22")}

    fixed=Mesh()
    ring(fixed,2.9,"z",width=.1,depth=.16)
    # Turned pedestal and lower trunnion, all attached to the stationary yoke.
    lathe(fixed,[(0,.55),(.8,.55),(.87,.6),(.87,.69),(.65,.77),(.25,.82),
                 (.2,1.08),(.16,1.2),(0,1.2)],"y",offset=(0,-CENTRE_Y,0))
    for side in (-1,1):
        bearing(fixed,side*2.65,"y")
        cylinder(fixed,.14,2.77,2.9,"y",material="bronze") if side==1 else cylinder(fixed,.14,-2.9,-2.77,"y",material="bronze")
    fixed.save("gyro-yoke")
    asset(assets,"gyro-yoke","gyro-yoke",{"*":steel,"bronze":brass})

    # Heading: vertical XY ring on Y bearings; child pitch pivots lie on X.
    # Pitch: horizontal XZ ring on X bearings; child roll pivots lie on Z.
    # Roll: vertical YZ carrier on Z bearings, supporting the rotor's Z shaft.
    for label,radius,plane,pivot,next_axis,next_radius in (
        ("heading",2.4,"z","y","x",1.95),
        ("pitch",1.95,"y","x","z",1.5),
        ("roll",1.5,"x","z","z",None)):
        m=Mesh();ring(m,radius,plane)
        # Each child owns its own spindle; the parent's sleeve is separate.
        parent_radius={"heading":2.65,"pitch":2.4,"roll":1.95}[label]
        for side in (-1,1):
            a,b=sorted((side*(radius-.04),side*(parent_radius+.1)))
            if label=="roll":
                # Hollow roll trunnion; the independently spinning flywheel
                # shaft passes through this joint instead of intersecting it.
                lathe(m,[(.055,a),(.085,a),(.085,b),(.055,b),(.055,a)],pivot,material="bronze",segments=32)
            else:
                cylinder(m,.085,a,b,pivot,material="bronze")
            if next_radius is not None:
                bearing(m,side*radius,next_axis)
            else:
                bearing(m,side*1.5,"z",inner=.05,outer=.085)
        m.save("gyro-"+label)
        asset(assets,"gyro-"+label,"gyro-"+label,{"*":accents[label],"bronze":brass})

    rotor=Mesh()
    ring(rotor,.99,"z",material="bronze",width=.16,depth=.14)
    cylinder(rotor,.24,-.23,.23,"z",material="bronze")
    cylinder(rotor,.045,-2.12,2.12,"z",material="stone")
    # Six machined tapered spokes with bevels, plus one index rivet so spin is
    # visible on an otherwise rotationally symmetric flywheel.
    for i in range(6):
        angle=i*math.tau/6
        outline=[(.2,-.075),(.82,-.065),(.9,-.035),(.9,.035),(.82,.065),(.2,.075)]
        xy=[(r*math.cos(angle)-s*math.sin(angle),r*math.sin(angle)+s*math.cos(angle)) for r,s in outline]
        for depth,normal in ((-.07,(0,0,-1)),(.07,(0,0,1))):
            rotor.poly([(x,y,depth) for x,y in xy],normal,material="bronze")
        for a,b in zip(xy,xy[1:]+xy[:1]):
            rotor.poly([(*a,-.07),(*b,-.07),(*b,.07),(*a,.07)],(b[1]-a[1],a[0]-b[0],0),material="bronze")
    cylinder(rotor,.06,.14,.18,"z",offset=(.99,0,0),material="stone",segments=16)
    rotor.save("gyro-rotor")
    asset(assets,"gyro-rotor","gyro-rotor",{"*":steel,"bronze":brass})

    # The rotor shaft slides through hollow carrier bearings. A tiny collar
    # outside either sleeve captures the shaft without an unattached flywheel.
    collars=Mesh()
    for side in (-1,1):
        a,b=sorted((side*2.08,side*2.11))
        cylinder(collars,.13,a,b,"z",material="bronze")
    collars.save("gyro-shaft-collars")
    asset(assets,"gyro-shaft-collars","gyro-shaft-collars",{"*":brass})

    font="C:/Windows/Fonts/segoeui.ttf"
    descriptions=(
        ("euler","01   EULER ANGLES","Y / X / Z   |   ORDERED HINGES","AT PITCH 90: HEADING + ROLL AXES ALIGN"),
        ("axis-angle","02   AXIS / ANGLE","UNIT AXIS + ANGLE","ONE COMPLETE TURN ABOUT AN OBLIQUE AXIS"),
        ("quaternion","03   QUATERNION","UNIT q   |   SHORTEST-ARC SLERP","ORIENTATION CONTINUES THROUGH 90 DEGREES"))
    for name,title,sub,detail in descriptions:
        im=Image.new("RGB",(1400,360),(15,21,27));draw=ImageDraw.Draw(im)
        draw.line((60,38,1340,38),fill=(181,145,86),width=3)
        draw.text((700,103),title,font=ImageFont.truetype(font,68),fill=(219,210,185),anchor="mm")
        draw.text((700,200),sub,font=ImageFont.truetype(font,38),fill=(165,188,206),anchor="mm")
        draw.text((700,282),detail,font=ImageFont.truetype(font,29),fill=(164,175,181),anchor="mm")
        im.save(api.here/f"textures/gyro-{name}.png")
        asset(assets,"gyro-label-"+name,"sign",{"*":dict(diffuse="1 1 1",lighting=False,
              texture0=api.prefix+f"textures/gyro-{name}.png",castShadows=False,cull=False)})

    rigs=[]
    for x,(method,*_) in zip((-12,0,12),descriptions):
        base=transform(parent,"Gyroscope "+method,(x,0,-71))
        transform(base,"Gyroscope stone pedestal",(0,.275,0),(5.8,.55,4.8),ref="polished-plinth")
        frame=transform(base,"Stationary yoke "+method,(0,CENTRE_Y,0),ref="gyro-yoke")
        heading=transform(frame,method+" heading bearing",ref="gyro-heading",animation="gyroscope",
                          **{"rotation-method":method,"joint":"heading"})
        pitch=transform(heading,method+" pitch bearing",ref="gyro-pitch",animation="gyroscope",
                        **{"rotation-method":method,"joint":"pitch"})
        roll=transform(pitch,method+" roll bearing",ref="gyro-roll",animation="gyroscope",
                       **{"rotation-method":method,"joint":"roll"})
        transform(roll,method+" shaft collars",ref="gyro-shaft-collars")
        transform(roll,method+" flywheel",ref="gyro-rotor",animation="spin",axis="0 0 1",speed="135")
        # Shared sign is 4.4 x .9 m; keep the explanation inside its backing.
        transform(base,method+" plaque backing",(0,1.3,3.25),(5.9,1.1,.12),ref="box",mat=dark)
        transform(base,method+" explanation",(0,1.3,3.32),(1.3,1.2,1),ref="gyro-label-"+method)
        rigs.append({"method":method,"transform":base.get("name"),"position_local_m":[x,0,-71],
                     "joints":[heading.get("name"),pitch.get("name"),roll.get("name")],
                     "hinge_axes":[[0,1,0],[1,0,0],[0,0,1]]})
    return {"rigs":rigs,"cycle_seconds":20,"euler_lock_interval_seconds":[4,12],
            "ring_radii_m":list(RADII),"centre_height_m":CENTRE_Y,
            "geometry":"Shared bevelled rings, hollow bronze bearings, coaxial trunnions, captured rotor shaft and flywheel",
            "lecture":"lectures/5dv180_lecture2_acceleration_visibility_processing.txt, lines 910-1410",
            "quaternion_note":"Unit-quaternion SLERP drives orientation. Euler factorization is only display-joint inverse kinematics; mechanical gimbals can still align."}
