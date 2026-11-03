"""Deterministic height-map terrain authored for the existing dragon island."""
import json
import xml.etree.ElementTree as ET
import numpy as np
from PIL import Image

CAMERAS={"terrain": ((-44,16,17),(-58,1.8,0))}

def bake(here):
    n=513
    z,x=np.mgrid[-1:1:complex(n),-1:1:complex(n)]
    r=np.sqrt(x*x+z*z)
    def smooth(a,b,v):
        t=np.clip((v-a)/(b-a),0,1);return t*t*(3-2*t)
    ridge=(.5+.5*np.sin(7*x+2*np.sin(5*z)))*(.65+.35*np.cos(9*z-3*x))
    detail=.11*np.sin(49*x+13*z)*np.sin(41*z-9*x)+.06*np.sin(97*x-53*z)
    # A flat central refuge keeps every original walking/idle support point stable.
    relief=(.1+2.9*ridge+detail)*smooth(.48,.72,r)*(.28+.72*smooth(-.5,.6,z))
    crater=np.exp(-((x+.57)**2+(z-.28)**2)/.013)
    rim=np.exp(-((np.sqrt((x+.57)**2+(z-.28)**2)-.17)/.045)**2)
    relief+=smooth(.48,.62,r)*(.35*rim-.25*crater)
    height=np.clip((.36+relief)*(1-smooth(.88,1,r)),0,3.8)
    gray=np.rint(height/3.8*255).astype(np.uint8)
    Image.fromarray(gray).save(here/'textures/dragon-terrain-height.png')
    q=512
    yy,xx=np.mgrid[0:q,0:q]/q
    grain=np.random.default_rng(1803).normal(0,1,(q,q))
    veins=np.sin(2*np.pi*(3*xx+2*yy+.2*np.sin(2*np.pi*yy)))
    rock=83+12*veins+5*grain+4*np.sin(2*np.pi*23*xx)*np.cos(2*np.pi*19*yy)
    rgb=np.stack((rock*1.06,rock,rock*.91),axis=-1).clip(0,255).astype(np.uint8)
    Image.fromarray(rgb).save(here/'textures/dragon-terrain-rock.png')
    qcoord=np.clip(np.arange(129)*n/128-.5,0,n-1)
    low=qcoord.astype(int);high=np.minimum(low+1,n-1);weight=qcoord-low
    grid=(gray[low[:,None],low]*(1-weight)+gray[low[:,None],high]*weight)*(1-weight[:,None])+\
         (gray[high[:,None],low]*(1-weight)+gray[high[:,None],high]*weight)*weight[:,None]
    a,b,c,d=grid[:-1,:-1],grid[:-1,1:],grid[1:,:-1],grid[1:,1:]
    triangle_count=int(np.count_nonzero((a>0)|(c>0)|(b>0))+np.count_nonzero((b>0)|(c>0)|(d>0)))
    data={"technique":"OpenGL 4.1 geometry shader; height lookup and triangle emission in color and depth passes",
          "heightmap":"textures/dragon-terrain-height.png","heightmap_size":[n,n],
          "grid_cells":[128,128],"triangles":triangle_count,"maximum_grid_triangles":32768,"clip_zero":True,"dimensions_m":[26,3.8,22],
          "height_range_m":[float(gray.min()/255*3.8),float(gray.max()/255*3.8)],
          "cpu":"Immutable matching triangles for conservative bounds and exact ray picking",
          "source":"Own deterministic procedural height map and rock texture; terrain_demo.py"}
    (here/'terrain-material.json').write_text(json.dumps(data,indent=2)+'\n',encoding='utf8')
    return data

def add_demonstration(scene,prefix):
    parent=next(n for n in scene.iter('group') if n.get('name')=='Dragon under the star sky')
    for island in list(parent):
        if island.get('name','').startswith('Dragon island'): parent.remove(island)
    placement=ET.SubElement(parent,'transform',name='Height-map rock island',translate='0 0 -58')
    terrain=ET.SubElement(placement,'terrain',name='Geometry-shader terrain',
        heightMap=prefix+'textures/dragon-terrain-height.png',dimensions='26 3.8 22',cells='128 128',clipZero='true')
    material=ET.SubElement(terrain,'state',diffuse='.24 .25 .27',ambient='.22 .23 .26',
        specular='.015 .015 .015',roughness='.85',environment='true',
        texture0=prefix+'textures/dragon-terrain-rock.png')
    ET.SubElement(material,'uniform',name='uProcedural',type='int',value='2')
