"""Bake a matched basketball material in the ball's spherical UV chart, offline.

The pattern is evaluated on a unit sphere rather than a flat UV grid. This keeps
the pebble size uniform and the longitude join periodic, including near poles.
The renderer expects linear albedo and OpenGL tangent-space normals.
"""
import hashlib
import json
from pathlib import Path
import numpy as np
from PIL import Image

HERE=Path(__file__).resolve().parent
DEPTH_M=.004
DISPLAY_RADIUS_M=1.15
SIZE=(2048,1024)
# Columns map the ball's local coordinates to the seam pattern's symmetry axes.
_pole=np.array([-2,.35,-1],dtype=np.float64);_pole/=np.linalg.norm(_pole)
_right=np.cross([0,1,0],_pole);_right/=np.linalg.norm(_right)
PATTERN_BASIS=np.stack([_right,_pole,np.cross(_right,_pole)],axis=1)


def smooth(a,b,x):
    t=np.clip((x-a)/(b-a),0,1)
    return t*t*(3-2*t)


def field(p):
    """Return physical relief and linear albedo at unit-sphere directions."""
    x,y,z=np.moveaxis(p,-1,0)
    distance=channel_distance(p)
    panel=smooth(.004,.016,distance)
    # 3D cellular grain: neighbouring texels see the same cells across the seam.
    cell=p*112
    base=np.floor(cell)
    nearest=np.full(x.shape,10.0,dtype=np.float32)
    for i in (-1,0,1):
        for j in (-1,0,1):
            for k in (-1,0,1):
                q=base+np.array([i,j,k],dtype=np.float32)
                seed=q[...,0]*127.1+q[...,1]*311.7+q[...,2]*74.7
                offset=np.stack([np.mod(np.sin(seed+phase)*43758.5453,1)
                    for phase in (0,19.19,47.73)],axis=-1)
                d=np.sum((q+.18+.64*offset-cell)**2,axis=-1)
                nearest=np.minimum(nearest,d)
    pebble=np.exp(-nearest*5.5)
    broad=.5+.5*np.sin(x*31+y*13+z*7)*np.sin(y*23-z*19)
    height=.12+panel*(.69+.14*pebble+.015*broad)
    orange=np.array([.49,.119,.024],dtype=np.float32)
    leather=orange*(.94+.055*broad[...,None]+.045*pebble[...,None])
    channel=np.array([.009,.007,.005],dtype=np.float32)
    color=channel+(leather-channel)*smooth(.006,.016,distance)[...,None]
    return height,color


def pattern_distance(q):
    """Angular channel distance in the pattern's reflection-symmetry frame.

    The great circles are x=0 and z=0. The third seam is the closed spherical
    curve y^3 = (3*sqrt(3)/2)*(z^2-x^2). It mirrors exactly under x -> -x and
    z -> -z, and passes clear of the great circles' shared poles. Each great
    circle meets the curved seam twice, giving eight panels without triple joins.

    Dividing the implicit residual by its surface gradient approximates angular
    distance near the curve and keeps groove widths consistent around the ball.
    """
    x,y,z=np.moveaxis(q,-1,0)
    k=3*np.sqrt(3)/2
    residual=y*y*y-k*(z*z-x*x)
    radial=2*k*(x*x-z*z)+3*y*y*y
    gradient2=4*k*k*(x*x+z*z)+9*y**4-radial*radial
    curved=np.abs(residual)/np.sqrt(np.maximum(gradient2,1e-12))
    straight=np.arcsin(np.clip(np.minimum(np.abs(x),np.abs(z)),0,1))
    return np.minimum(straight,curved)


def channel_distance(p):
    return pattern_distance(p@PATTERN_BASIS)


def verify_seams():
    # Check the symmetry the previous sinusoidal pattern did not preserve.
    q=np.random.default_rng(18022026).normal(size=(4096,3))
    q/=np.linalg.norm(q,axis=-1,keepdims=True)
    d=pattern_distance(q)
    for mirror in ([-1,1,1],[1,1,-1]):
        assert np.max(np.abs(d-pattern_distance(q*mirror)))<1e-10
    # An independent parameterization verifies the implicit curve lies on the
    # sphere, closes smoothly, and never reaches either straight-seam junction.
    t=np.linspace(0,2*np.pi,1025)
    a,b=np.cos(t)**3,np.sin(t)**3
    seam=np.stack([(a-b)/np.sqrt(2),np.sqrt(3)*np.sin(t)*np.cos(t),
                   (a+b)/np.sqrt(2)],axis=-1)
    assert np.max(np.abs(np.linalg.norm(seam,axis=-1)-1))<1e-12
    assert np.max(pattern_distance(seam))<1e-12
    assert np.linalg.norm(seam[0]-seam[-1])<1e-12
    assert np.max(np.abs(seam[:,1]))<.87


def bake():
    verify_seams()
    width,height=SIZE
    u=(np.arange(width,dtype=np.float32)+.5)/width
    v=1-(np.arange(height,dtype=np.float32)+.5)/height
    longitude=u[None,:]*(2*np.pi); latitude=v[:,None]*np.pi
    sine=np.sin(latitude)
    p=np.stack(np.broadcast_arrays(sine*np.cos(longitude),-np.cos(latitude),
                                   sine*np.sin(longitude)),axis=-1)
    relief,color=field(p)
    # A tiny flat collar resolves the UV chart's singular point. It also keeps
    # POM from wrapping north/south into the opposite pole at the exact vertex.
    collar=smooth(0,.035,sine)
    relief=.82+(relief-.82)*collar
    for row in (0,-1):
        relief[row]=.82
        color[row]=np.mean(color[row],axis=0)
    du=(np.roll(relief,-1,axis=1)-np.roll(relief,1,axis=1))*width*.5
    dv=(np.roll(relief,1,axis=0)-np.roll(relief,-1,axis=0))*height*.5
    dv[[0,-1]]=0
    nx=-du*DEPTH_M/(2*np.pi*DISPLAY_RADIUS_M*np.maximum(sine,.005))
    ny=-dv*DEPTH_M/(np.pi*DISPLAY_RADIUS_M)
    normal=np.stack([nx,ny,np.ones_like(nx)],axis=-1)
    normal/=np.linalg.norm(normal,axis=-1,keepdims=True)
    textures=HERE/"textures"
    files={"albedo":textures/"basketball-albedo.png","normal":textures/"basketball-normal.png",
           "height":textures/"basketball-height.png"}
    Image.fromarray(np.uint8(np.clip(color,0,1)*255+.5)).save(files["albedo"])
    Image.fromarray(np.uint8(np.clip(normal*.5+.5,0,1)*255+.5)).save(files["normal"])
    Image.fromarray(np.uint8(np.clip(relief,0,1)*255+.5)).save(files["height"])
    # Validate the analytic wrap, finite normals, and unit-length bake before shipping.
    check=np.linspace(-.99,.99,101,dtype=np.float32)
    def at_longitude(angle):
        radius=np.sqrt(1-check*check)
        return np.stack([radius*np.cos(angle),check,radius*np.sin(angle)],axis=-1)
    a=field(at_longitude(0))
    b=field(at_longitude(2*np.pi))
    assert all(np.max(np.abs(x-y))<1e-5 for x,y in zip(a,b))
    assert np.isfinite(normal).all() and np.max(np.abs(np.linalg.norm(normal,axis=-1)-1))<1e-5
    stats={"generator":"basketball_material.py","dimensions":SIZE,"albedo_space":"linear",
        "normal_convention":"OpenGL tangent space, +V toward north pole",
        "uv_chart":"u=atan2(z,x)/(2*pi), v=acos(-y)/pi; image top is v=1",
        "parallax_depth_m":DEPTH_M,"display_radius_m":DISPLAY_RADIUS_M,
        "geometry":"Unchanged UV sphere, 3968 triangles",
        "panels":"Eight; two orthogonal great circles plus a symmetric closed spherical seam",
        "seam_equation":"In pattern coordinates: x=0, z=0, y^3=(3*sqrt(3)/2)*(z^2-x^2)",
        "seam_symmetry":"Reflection in pattern planes x=0 and z=0; no triple seam junctions",
        "reference":"https://www.wilson.com/en-us/products/evolution-game-basketball-wz10012",
        "source":"Original procedural bake; no downloaded product imagery or logos",
        "files":{name:{"file":str(path.relative_to(HERE)).replace("\\","/"),
            "sha256":hashlib.sha256(path.read_bytes()).hexdigest()} for name,path in files.items()}}
    (HERE/"basketball-material.json").write_text(json.dumps(stats,indent=2)+"\n",encoding="utf-8")
    return stats


if __name__=="__main__": print(json.dumps(bake(),indent=2))
