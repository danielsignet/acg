"""Offline CC0 bear preparation and the gallery's shell-fur exhibit.

The original rigged GLB is preserved. The demonstration bakes its default pose,
refines the surface and smooths normals, so repeated shells share one mesh.
"""
import hashlib
import json
import math
from pathlib import Path
import struct
import xml.etree.ElementTree as ET
import numpy as np
from PIL import Image

HERE=Path(__file__).resolve().parent
PREFIX='experiments/pantheon/'
CAMERAS={'fur':((10,3.7,54.8),(5,2.5,61))}


def refine(points,triangles,smooth=True):
    # Weld the source's hard-normal splits before applying geometric Loop weights.
    unique={};ids=[];welded=[]
    for p in points:
        key=tuple(np.round(p,6))
        if key not in unique:unique[key]=len(welded);welded.append(p)
        ids.append(unique[key])
    p=np.asarray(welded,dtype=float);tri=np.asarray(ids)[triangles]
    neighbours=[set() for _ in p];edges={}
    for a,b,c in tri:
        for x,y,opposite in ((a,b,c),(b,c,a),(c,a,b)):
            neighbours[x].add(y);neighbours[y].add(x)
            edges.setdefault(tuple(sorted((x,y))),[]).append(opposite)
    boundary=[set() for _ in p]
    for (a,b),opposite in edges.items():
        if len(opposite)==1:boundary[a].add(b);boundary[b].add(a)
    output=[]
    for i,near in enumerate(neighbours):
        if not smooth:output.append(p[i]);continue
        if len(boundary[i])==2:output.append(.75*p[i]+.125*sum(p[j] for j in boundary[i]))
        else:
            beta=3/16 if len(near)==3 else 3/(8*len(near))
            output.append((1-len(near)*beta)*p[i]+beta*sum(p[j] for j in near))
    midpoint={}
    for edge,opposite in edges.items():
        a,b=edge;midpoint[edge]=len(output)
        output.append(.375*(p[a]+p[b])+.125*(p[opposite[0]]+p[opposite[1]])
                      if smooth and len(opposite)==2 else .5*(p[a]+p[b]))
    result=[]
    for a,b,c in tri:
        ab=midpoint[tuple(sorted((a,b)))];bc=midpoint[tuple(sorted((b,c)))];ca=midpoint[tuple(sorted((c,a)))]
        result.extend(((a,ab,ca),(ab,b,bc),(ca,bc,c),(ab,bc,ca)))
    return np.asarray(output),np.asarray(result)


def bake():
    source=HERE/'meshes/fur-bear-source.glb';raw=source.read_bytes()
    size=struct.unpack_from('<I',raw,12)[0];doc=json.loads(raw[20:20+size]);binary=raw[28+size:]
    def read(index):
        a=doc['accessors'][index];view=doc['bufferViews'][a['bufferView']]
        components={'VEC3':3,'VEC4':4,'MAT4':16,'SCALAR':1}[a['type']]
        return np.frombuffer(binary,dtype={5126:'<f4',5123:'<u2',5121:'u1'}[a['componentType']],
            count=a['count']*components,offset=view.get('byteOffset',0)+a.get('byteOffset',0)).reshape(-1,components)
    worlds={}
    def world(index,parent):
        node=doc['nodes'][index];x,y,z,w=node.get('rotation',[0,0,0,1])
        rotation=np.array([[1-2*y*y-2*z*z,2*x*y-2*z*w,2*x*z+2*y*w],
                           [2*x*y+2*z*w,1-2*x*x-2*z*z,2*y*z-2*x*w],
                           [2*x*z-2*y*w,2*y*z+2*x*w,1-2*x*x-2*y*y]])
        local=np.eye(4);local[:3,:3]=rotation@np.diag(node.get('scale',[1,1,1]));local[:3,3]=node.get('translation',[0,0,0])
        worlds[index]=parent@local
        for child in node.get('children',[]):world(child,worlds[index])
    for index in doc['scenes'][doc.get('scene',0)]['nodes']:world(index,np.eye(4))
    lines=['# CC0 bear from Frakassets/Pixor; derived static demonstration mesh.','mtllib fur-bear.mtl']
    offset=0;counts={}
    for mesh_index,mesh in enumerate(doc['meshes']):
        primitive=mesh['primitives'][0];p=read(primitive['attributes']['POSITION']);tri=read(primitive['indices']).reshape(-1,3)
        node_index,node=next((i,n) for i,n in enumerate(doc['nodes']) if n.get('mesh')==mesh_index)
        if 'skin' in node:
            skin=doc['skins'][node['skin']];inverse=read(skin['inverseBindMatrices']).reshape(-1,4,4).transpose(0,2,1)
            matrices=np.array([worlds[j]@b for j,b in zip(skin['joints'],inverse)])
            joints=read(primitive['attributes']['JOINTS_0']);weights=read(primitive['attributes']['WEIGHTS_0'])
            pose=np.sum(matrices[joints]*weights[...,None,None],axis=1)
            p=np.einsum('nij,nj->ni',pose,np.column_stack([p,np.ones(len(p))]))[:,:3]
        else:p=(np.column_stack([p,np.ones(len(p))])@worlds[node_index].T)[:,:3]
        if mesh['name'] not in ('Bear eyes','Bear nose'):
            p,tri=refine(p,tri,smooth=mesh['name']!='Bear legs')
        normal=np.zeros_like(p)
        for face in tri:
            a,b,c=p[face];n=np.cross(b-a,c-a)
            for i in face:normal[i]+=n
        normal/=np.maximum(np.linalg.norm(normal,axis=1,keepdims=True),1e-12)
        for pos in p:lines.append('v '+' '.join(f'{v:.8f}' for v in pos))
        for n in normal:lines.append('vn '+' '.join(f'{v:.8f}' for v in n))
        groups={}
        for face in tri:
            center=p[face].mean(0)
            material='bare' if mesh['name'] in ('Bear eyes','Bear nose') else 'muzzle' if mesh['name']=='Bear muzzle' else 'legs' if mesh['name']=='Bear legs' else 'face' if center[2]>.58 and center[1]>.60 else 'coat'
            groups.setdefault(material,[]).append(face)
        for material,faces in groups.items():
            lines.extend(['g '+mesh['name'].replace(' ','-')+'-'+material,'usemtl '+material])
            for face in faces:lines.append('f '+' '.join(f'{i+1+offset}//{i+1+offset}' for i in face))
            counts[material]=counts.get(material,0)+len(faces)
        offset+=len(p)
    obj=HERE/'meshes/fur-bear.obj';obj.write_text('\n'.join(lines)+'\n',encoding='ascii')
    materials={'coat':(.198,.089,.037),'legs':(.198,.089,.037),'face':(.198,.089,.037),
               'muzzle':(.552,.332,.150),'bare':(.017,.014,.024)}
    (HERE/'meshes/fur-bear.mtl').write_text(''.join('newmtl '+name+'\nKd '+' '.join(map(str,color))+'\nKa .2 .2 .2\nKs .04 .04 .04\nNs 25\n\n' for name,color in materials.items()),encoding='ascii')
    # Periodic jittered strand roots. The radial profile stores each strand's
    # height, so successive shells share roots and taper towards their tips.
    size,cells=512,64;rng=np.random.default_rng(18022026)
    roots=rng.uniform(.15,.85,(cells,cells,2));height=rng.uniform(.6,1,(cells,cells))
    y,x=np.meshgrid((np.arange(size)+.5)/size*cells,(np.arange(size)+.5)/size*cells,indexing='ij')
    bx,by=np.floor(x).astype(int),np.floor(y).astype(int);field=np.zeros_like(x)
    for dx in (-1,0,1):
        for dy in (-1,0,1):
            rx,ry=(bx+dx)%cells,(by+dy)%cells;root=roots[ry,rx]
            d2=(x-bx-dx-root[...,0])**2+(y-by-dy-root[...,1])**2
            field=np.maximum(field,height[ry,rx]*np.exp(-d2/(2*.18**2)))
    texture=HERE/'textures/fur-roots.png';Image.fromarray(np.uint8(np.clip(field,0,1)*255+.5)).save(texture)
    stats={'title':'Bear','author':'Frakassets / Pixor','license':'CC0-1.0',
        'source_url':'https://pixor.art/library/creatures/bear/',
        'download_url':'https://pixor.art/library/files/bear/pixor-library-bear.glb',
        'source_sha256':hashlib.sha256(raw).hexdigest(),
        'modifications':'Baked default skeletal pose; Loop refinement of body/muzzle; linear leg refinement preserves paw contact; smoothed normals; separate face, leg and muzzle grooming',
        'triangles':sum(counts.values()),'materials':counts,'vertices':offset,
        'technique':'Shell fur with instanced layers and additive blending',
        'layers':14,'blend_rgb':'FUNC_ADD, SRC_ALPHA, ONE','room':'silva','view':'fur',
        'derived_sha256':hashlib.sha256(obj.read_bytes()).hexdigest(),
        'texture_sha256':hashlib.sha256(texture.read_bytes()).hexdigest()}
    (HERE/'fur-material.json').write_text(json.dumps(stats,indent=2)+'\n',encoding='utf-8')
    return stats


def add_demonstration(scene):
    assets=scene.find('assets')
    for model in list(assets):
        if model.get('id')=='fur-bear':assets.remove(model)
    model=ET.SubElement(assets,'model',id='fur-bear',file=PREFIX+'meshes/fur-bear.obj')
    for material,length in [('coat',.10),('legs',.055),('face',.008),('muzzle',.012)]:
        ET.SubElement(model,'material',name=material,furLength=str(length),furLayers='14',
            furDensity='7' if material in ('face','muzzle') else '2.5',
            furBend=f'0 {-length*.6} 0',texture2=PREFIX+'textures/fur-roots.png',
            roughness='.92',specular='.025 .025 .025',environment='true')
    ET.SubElement(model,'material',name='bare',roughness='.35')
    south=next(n for n in scene.iter('transform') if n.get('name','').startswith('SILVA expanded wing'))
    for old in list(south):
        if old.get('name')=='Shell fur exhibit':south.remove(old)
    group=ET.SubElement(south,'group',name='Shell fur exhibit',room='silva')
    plinth=ET.SubElement(group,'transform',name='Fur bear plinth',translate='-5 .4 -61',scale='6 .8 7')
    ET.SubElement(plinth,'instance',ref='polished-plinth',selectable='false')
    bear=ET.SubElement(group,'transform',name='Fur bear placement',translate='-5 .8 -61',rotate='0 -25 0',scale='4 4 4')
    ET.SubElement(bear,'instance',ref='fur-bear',name='Brown bear - shell fur',selectable='true')


if __name__=='__main__':
    print(json.dumps(bake(),indent=2))
    for path in [HERE/'scene.xml',HERE/'portal-scene.xml',*sorted((HERE/'views').glob('*.xml'))]:
        scene=ET.parse(path).getroot();add_demonstration(scene);ET.indent(scene,space='  ')
        ET.ElementTree(scene).write(path,encoding='utf-8',xml_declaration=True)
    scene=ET.parse(HERE/'scene.xml').getroot();eye,target=CAMERAS['fur']
    scene.find('camera').set('eye',' '.join(map(str,eye)));scene.find('camera').set('target',' '.join(map(str,target)))
    ET.indent(scene,space='  ');ET.ElementTree(scene).write(HERE/'views/fur.xml',encoding='utf-8',xml_declaration=True)
