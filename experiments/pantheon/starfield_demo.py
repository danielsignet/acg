"""Original deterministic star particles, formations, and their mean IBL snapshot."""
import copy
import json
import math
from pathlib import Path
import struct
import xml.etree.ElementTree as ET
import numpy as np

HERE=Path(__file__).resolve().parent
PREFIX="experiments/pantheon/"
CAMERAS={
    "particles":((-43,1,14),(-58,8.5,0)),
    "galaxies":((-58,4,0),(-52,14,-3)),
    "milky-way":((-58,4,0),(-63,15,1)),
}
SEED=180209

def unit(a):
    a=np.asarray(a,dtype=float)
    return a/np.linalg.norm(a,axis=-1,keepdims=True)

def bake():
    rng=np.random.default_rng(SEED)
    formations=[];speeds=[];particles=[];kinds=[]
    def add(name,p,size,color,matrix,speed=0):
        group=len(formations);n=len(p)
        data=np.empty((n,8),dtype='<f4');data[:,:3]=p;data[:,3]=size;data[:,4:7]=color
        data[:,7]=group*8+rng.uniform(0,2*math.pi,n)
        speed=round(speed*4096/(2*math.pi))*2*math.pi/4096
        particles.append(data);formations.append(matrix);speeds.append(speed)
        kinds.append({"type":name,"count":n,"angular_speed_rad_s":speed})
    palette=np.array([[.74,.84,1],[1,.83,.56],[1,.48,.28],[.6,.76,1],[.96,.96,1]])
    n=16384;y=rng.uniform(-1,1,n);a=rng.uniform(0,2*math.pi,n);s=np.sqrt(1-y*y)
    p=np.stack((s*np.cos(a),y,s*np.sin(a)),axis=1)
    magnitude=np.clip(rng.pareto(2.3,n)*.55+.12,.12,7)
    add("field stars: blue, white, yellow and red",p,rng.uniform(.65,1.65,n)+np.minimum(magnitude*.22,.9),
        palette[rng.integers(0,len(palette),n)]*magnitude[:,None],np.eye(4))
    normal=unit([.38,.15,.91]);forward=unit(np.array([-.6,.8,0])-normal*np.dot(normal,[-.6,.8,0]));side=np.cross(normal,forward)
    n=65344;phi=rng.uniform(-math.pi,math.pi,n);core=rng.random(n)<.33
    phi[core]=rng.normal(0,.48,np.count_nonzero(core))
    latitude=rng.normal(0,.031+ .032*np.exp(-phi*phi/1.5),n)+.012*np.sin(phi*3)
    p=(np.cos(phi)[:,None]*forward+np.sin(phi)[:,None]*side)*np.cos(latitude)[:,None]+np.sin(latitude)[:,None]*normal
    dust=np.abs(latitude-.012*np.sin(phi*3))<(.006+.003*np.sin(phi*7)**2)
    brightness=rng.uniform(.055,.225,n)*np.where(dust,.045,1)*(.6+.4*np.sin(phi*4+.5)**2)
    colors=np.tile([.60,.69,.92],(n,1));colors[core]=[1,.71,.40]
    add("Milky Way star clouds, central bulge and dust lanes",p,rng.uniform(1.3,2.4,n),colors*brightness[:,None],np.eye(4))
    def formation(center,radius,flatten,roll):
        center=unit(center);right=unit(np.cross([0,1,0],center));up=np.cross(center,right)
        c,s=math.cos(roll),math.sin(roll)
        matrix=np.eye(4);matrix[:3,0]=(right*c+up*s)*radius
        matrix[:3,1]=(-right*s+up*c)*radius*flatten;matrix[:3,2]=center*radius*.1;matrix[:3,3]=center
        return matrix
    for name,center,radius,flat,roll,speed,style in [
        ("two-arm spiral galaxy",[-.45,.62,-.64],.16,.62,-.5,.007,"spiral"),
        ("elliptical galaxy",[.58,.72,-.30],.075,.7,.4,0,"elliptical"),
        ("edge-on disc galaxy",[.38,.83,.40],.14,.13,-.5,.004,"spiral"),
        ("barred spiral galaxy",[.04,.85,-.53],.10,.86,.6,-.005,"barred")]:
        n=10240;r=rng.gamma(2,.23,n);r=r/(1+r*.35)
        arm=rng.integers(0,2,n)*math.pi
        theta=arm+np.log(r+.10)*2.7+rng.normal(0,.33,n)
        if style=="elliptical":theta=rng.uniform(0,2*math.pi,n)
        core=rng.random(n)<.2;r[core]=rng.gamma(2,.035,np.count_nonzero(core))
        diffuse=(rng.random(n)<.25) & ~core
        theta[core|diffuse]=rng.uniform(0,2*math.pi,np.count_nonzero(core|diffuse))
        p=np.stack((r*np.cos(theta),r*np.sin(theta),rng.normal(0,.15,n)),axis=1)
        if style=="barred":p[r<.28,1]*=.35
        colors=np.tile([.42,.62,1.0],(n,1));colors[core]=[1,.75,.42]
        if style=="elliptical":colors[:]=[1,.72,.43]
        energy=rng.uniform(.018,.065,n);energy[core]*=1.6;energy[diffuse]*=.5
        add(name,p,rng.uniform(2.6,4.0,n),colors*energy[:,None],formation(center,radius,flat,roll),speed)
    n=8192;r=np.minimum(np.abs(rng.standard_t(2.8,n))*.14,1.5);a=rng.uniform(0,2*math.pi,n)
    p=np.stack((r*np.cos(a),r*np.sin(a),rng.normal(0,.1,n)),axis=1)
    add("globular star cluster",p,rng.uniform(.8,2,n),np.tile([1,.8,.55],(n,1))*rng.uniform(.018,.12,(n,1)),
        formation([-.55,.82,.11],.055,1,0),.002)
    # Eight persistent meteor streams, each represented by 24 moving trail particles.
    seed=np.repeat(rng.uniform(0,1,8),24);trail=np.tile(np.linspace(0,1,24),8)
    p=np.stack((seed,trail,np.repeat(rng.uniform(0,1,8),24)),axis=1)
    add("eight moving meteor trails, 24 particles each",p,1.6+2.9*(1-trail),
        np.tile([2.3,3.3,4.8],(192,1)),np.eye(4))
    data=np.concatenate(particles);assert data.shape==(131072,8)
    target=HERE/"textures/astra-particles.bin"
    with target.open('wb') as out:
        out.write(struct.pack('<8sIII',b'ACGSTARS',1,len(data),len(formations)))
        for matrix,speed in zip(formations,speeds):out.write(matrix.T.astype('<f4').tobytes());out.write(struct.pack('<f',speed))
        out.write(data.tobytes())
    # Static mean radiance is used only by IBL and static environment reflection.
    # The visible sky is drawn from the actual point buffer every frame.
    w,h=4096,2048;image=np.zeros((h,w,3),dtype=np.float32)+[.00002,.00003,.00008]
    directions=[]
    for chunk,matrix in zip(particles[:7],formations[:7]):directions.append(unit(chunk[:,:3]@matrix[:3,:3].T+matrix[:3,3]))
    d=np.concatenate(directions);x=(np.arctan2(d[:,2],d[:,0])/(2*math.pi)+.5)*w
    y=(.5-np.arcsin(d[:,1])/math.pi)*h
    colors=data[:len(d),4:7]*.8
    # Pixel-size approximation for static reflections, not a replacement for live points.
    for dy in range(-2,3):
        for dx in range(-2,3):
            xx=(np.floor(x).astype(int)+dx)%w;yy=np.clip(np.floor(y).astype(int)+dy,0,h-1)
            weight=np.exp(-((dx+.5-(x%1))**2+(dy+.5-(y%1))**2)/.7)
            for channel in range(3):np.add.at(image[:,:,channel],(yy,xx),colors[:,channel]*weight)
    from wings import write_hdr
    write_hdr(HERE/"textures/astra-stars.hdr",image.astype(np.float32))
    metadata={"seed":SEED,"particles":len(data),"stride_bytes":32,"gpu_buffer_bytes":data.nbytes,
              "formations":kinds,"source":"Original procedural particle catalog, starfield_demo.py",
              "snapshot":"astra-stars.hdr: static mean IBL only; displayed sky uses live GL_POINTS",
              "toggle":"F12","views":list(CAMERAS),"max_point_size_reference_px":4.5,
              "meteor_streams":8,"meteor_trail_particles":192,"ibl_excludes_transient_meteors":True}
    (HERE/"starfield-material.json").write_text(json.dumps(metadata,indent=2)+"\n",encoding='utf-8')
    return metadata

def add_demonstration(scene):
    sky=next(s for s in scene.find('environments').findall('sky') if s.get('id')=='astra-stars')
    sky.set('particles',PREFIX+'textures/astra-particles.bin')

def main():
    metadata=bake()
    for path in [HERE/'scene.xml',HERE/'portal-scene.xml',*sorted((HERE/'views').glob('*.xml'))]:
        tree=ET.parse(path);add_demonstration(tree.getroot());ET.indent(tree,space='  ')
        tree.write(path,encoding='utf-8',xml_declaration=True)
    base=ET.parse(HERE/'scene.xml').getroot()
    for name,(eye,target) in CAMERAS.items():
        scene=copy.deepcopy(base);scene.find('camera').set('eye',' '.join(map(str,eye)));scene.find('camera').set('target',' '.join(map(str,target)))
        ET.indent(scene,space='  ');ET.ElementTree(scene).write(HERE/'views'/(name+'.xml'),encoding='utf-8',xml_declaration=True)
    path=HERE/'scene-manifest.json';manifest=json.loads(path.read_text(encoding='utf-8'));manifest['star_particles']=metadata
    path.write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
    print(f"Star particles: {metadata['particles']}, {metadata['gpu_buffer_bytes']} bytes")

if __name__=='__main__':main()
