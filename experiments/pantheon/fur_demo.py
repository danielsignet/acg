"""Deterministic, self-contained dancing monsters and instanced shell fur."""
import json
import math
from pathlib import Path
import xml.etree.ElementTree as ET
import numpy as np
from PIL import Image

HERE=Path(__file__).resolve().parent
PREFIX='experiments/pantheon/'
CAMERAS={'fur':((12,4.7,52),(5,2.9,61))}


class Mesh:
    def __init__(self):self.points=[];self.faces=[];self.materials=[]
    def ellipsoid(self,center,radius,material='coat',segments=32,rings=22,pear=False):
        start=len(self.points)
        for row in range(rings+1):
            p=math.pi*row/rings
            for column in range(segments):
                t=2*math.pi*column/segments
                waist=1-.12*math.cos(p) if pear else 1
                self.points.append(np.array(center)+np.array(radius)*[math.sin(p)*math.cos(t)*waist,math.cos(p),math.sin(p)*math.sin(t)*waist])
        for row in range(rings):
            for col in range(segments):
                a=start+row*segments+col;b=start+row*segments+(col+1)%segments
                c=b+segments;d=a+segments
                for tri in ((a,b,c),(a,c,d)):
                    points=np.asarray([self.points[i] for i in tri]);normal=np.cross(points[1]-points[0],points[2]-points[0])
                    if np.linalg.norm(normal)<1e-10:continue
                    if np.dot(normal,points.mean(0)-center)<0:tri=tri[::-1]
                    self.faces.append(tri);self.materials.append(material)
    def horn(self):
        segments=20;rings=12
        for row in range(rings+1):
            t=row/rings;radius=.14*(1-t)+.005
            for col in range(segments):
                a=col/segments*2*math.pi
                self.points.append(np.array([.17*t*t+radius*math.cos(a),.55*t,radius*math.sin(a)]))
        for row in range(rings):
            for col in range(segments):
                a=row*segments+col;b=row*segments+(col+1)%segments;c=b+segments;d=a+segments
                self.faces.extend(((a,c,b),(a,d,c)));self.materials.extend(('ivory','ivory'))
    def save(self,name):
        points=np.asarray(self.points);normals=np.zeros_like(points)
        for face in self.faces:
            a,b,c=points[list(face)];n=np.cross(b-a,c-a)
            for i in face:normals[i]+=n
        normals/=np.maximum(np.linalg.norm(normals,axis=1,keepdims=True),1e-12)
        lines=['# Original procedural monster geometry.','mtllib fur-monster.mtl']
        lines.extend('v '+' '.join(f'{x:.7f}' for x in p) for p in points)
        lines.extend('vn '+' '.join(f'{x:.7f}' for x in n) for n in normals)
        for material in dict.fromkeys(self.materials):
            lines.extend(['g '+material,'usemtl '+material])
            lines.extend('f '+' '.join(f'{i+1}//{i+1}' for i in face) for face,m in zip(self.faces,self.materials) if m==material)
        (HERE/'meshes'/name).write_text('\n'.join(lines)+'\n',encoding='ascii')
        return {'vertices':len(points),'triangles':len(self.faces)}


def bake():
    body=Mesh();body.ellipsoid((0,1.32,0),(.81,1.12,.60),segments=40,rings=28,pear=True)
    arm=Mesh();arm.ellipsoid((0,-.39,0),(.21,.61,.23),segments=24,rings=18)
    foot=Mesh();foot.ellipsoid((0,0,.07),(.30,.22,.39),segments=24,rings=16)
    face=Mesh()
    for x in (-.28,.28):
        face.ellipsoid((x,1.70,.55),(.225,.275,.13),'white',24,18)
        face.ellipsoid((x+.025,1.69,.669),(.093,.135,.041),'dark',24,16)
        face.ellipsoid((x-.008,1.74,.707),(.025,.033,.012),'white',12,8)
    face.ellipsoid((0,1.10,.568),(.39,.20,.095),'dark',32,18)
    for x in (-.18,0,.18):face.ellipsoid((x,1.205,.647),(.064,.087,.035),'ivory',16,12)
    horn=Mesh();horn.horn()
    meshes={name:mesh.save(f'fur-monster-{name}.obj') for name,mesh in
            [('body',body),('arm',arm),('foot',foot),('face',face),('horn',horn)]}
    colors={'coat':(.55,.55,.55),'white':(.92,.94,.91),'dark':(.008,.012,.022),'ivory':(.95,.79,.53)}
    (HERE/'meshes/fur-monster.mtl').write_text('\n'.join('newmtl '+name+'\nKd '+' '.join(map(str,color))+
        '\nKa .12 .12 .12\nKs .025 .025 .025\nNs 32\n' for name,color in colors.items()),encoding='ascii')
    # Every shell samples the same periodic root identities and tapered profile.
    size,cells=1024,64;rng=np.random.default_rng(18022026)
    roots=rng.uniform(.15,.85,(cells,cells,2));height=rng.uniform(.72,1,(cells,cells))
    y,x=np.meshgrid((np.arange(size)+.5)/size*cells,(np.arange(size)+.5)/size*cells,indexing='ij')
    bx,by=np.floor(x).astype(int),np.floor(y).astype(int);field=np.zeros_like(x)
    for dx in (-1,0,1):
        for dy in (-1,0,1):
            rx,ry=(bx+dx)%cells,(by+dy)%cells;root=roots[ry,rx]
            d2=(x-bx-dx-root[...,0])**2+(y-by-dy-root[...,1])**2
            field=np.maximum(field,height[ry,rx]*np.exp(-d2/(2*.165**2)))
    Image.fromarray(np.uint8(np.clip(field,0,1)*255+.5)).save(HERE/'textures/fur-roots.png')
    triangles=meshes['body']['triangles']+2*meshes['arm']['triangles']+2*meshes['foot']['triangles']+meshes['face']['triangles']+2*meshes['horn']['triangles']
    stats={'title':'Dancing fur monsters','author':'Daniel Nilsson','source':'Original procedural geometry',
        'triangles':triangles,'vertices':sum(m['vertices'] for m in meshes.values()),'meshes':meshes,
        'instances':1,'layers':32,'blend_rgb':'FUNC_ADD, SRC_ALPHA, ONE',
        'simulation':'Damped springs per articulated patch, 1/240 s steps, 0.12 local-unit motion envelope',
        'technique':'Shared instanced shells, tapered triplanar roots, darker roots, inertial tip bending',
        'room':'silva','view':'fur'}
    (HERE/'fur-material.json').write_text(json.dumps(stats,indent=2)+'\n',encoding='utf8')
    return stats


def add_demonstration(scene):
    assets=scene.find('assets')
    for model in list(assets):
        if model.get('id','').startswith(('fur-bear','fur-monster-')):assets.remove(model)
    for part,length in [('body',.18),('arm',.16),('foot',.10),('face',0),('horn',0)]:
        model=ET.SubElement(assets,'model',id='fur-monster-'+part,file=PREFIX+f'meshes/fur-monster-{part}.obj')
        if length:
            ET.SubElement(model,'material',name='*',furLength=str(length),furLayers='32',furDensity='2.1',furSoft='true',
                furFace='true' if part=='body' else 'false',
                furBend=f'0 {-length*.35:.5f} 0',texture2=PREFIX+'textures/fur-roots.png',
                roughness='.92',specular='.015 .015 .015',environment='false')
        else:ET.SubElement(model,'material',name='*',roughness='.42',specular='.12 .12 .12',environment='false')
    south=next(n for n in scene.iter('transform') if n.get('name','').startswith('SILVA expanded wing'))
    for old in list(south):
        if old.get('name')=='Shell fur exhibit':south.remove(old)
    group=ET.SubElement(south,'group',name='Shell fur exhibit',room='silva')
    plinth=ET.SubElement(group,'transform',name='Fur monster stage',translate='-5 .35 -61',scale='13 .7 7')
    ET.SubElement(plinth,'instance',ref='polished-plinth',selectable='false')
    for index,(x,scale,color) in enumerate([(-5,1.7,'.64 .22 .94')]):
        root=ET.SubElement(group,'transform',name=f'Monster {index+1} placement',translate=f'{x} .72 -61',scale=f'{scale} {scale} {scale}')
        phase=str(index*.8)
        body=ET.SubElement(root,'transform',name=f'Monster {index+1} body',animation='fur-dance',dancePart='0',dancePhase=phase)
        coat=ET.SubElement(body,'group',name='Colored coat');ET.SubElement(coat,'state',tint=color)
        ET.SubElement(coat,'instance',ref='fur-monster-body',name=f'Dancing monster {index+1}',selectable='true')
        ET.SubElement(body,'instance',ref='fur-monster-face',selectable='false')
        for side in (-1,1):
            horn=ET.SubElement(body,'transform',translate=f'{side*.51} 2.22 -.03',rotate=f'0 0 {-side*18}')
            ET.SubElement(horn,'instance',ref='fur-monster-horn',selectable='false')
        for part in range(1,5):
            limb=ET.SubElement(body if part<=2 else root,'transform',name=f'Monster {index+1} limb {part}',
                animation='fur-dance',dancePart=str(part),dancePhase=phase)
            ET.SubElement(limb,'state',tint='.12 .85 .66' if part<=2 else '1 .40 .08')
            ET.SubElement(limb,'instance',ref='fur-monster-arm' if part<=2 else 'fur-monster-foot',selectable='false')


if __name__=='__main__':
    print(json.dumps(bake(),indent=2))
    for path in [HERE/'scene.xml',HERE/'portal-scene.xml',*sorted((HERE/'views').glob('*.xml'))]:
        scene=ET.parse(path).getroot();add_demonstration(scene)
        if path.name=='fur.xml':
            for key,value in zip(('eye','target'),CAMERAS['fur']):scene.find('camera').set(key,' '.join(map(str,value)))
        ET.indent(scene,space='  ');ET.ElementTree(scene).write(path,encoding='utf-8',xml_declaration=True)
