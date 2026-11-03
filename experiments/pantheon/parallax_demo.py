"""Two shared flat wall panels, without/with parallax, inside Pantheon."""
from pathlib import Path
import xml.etree.ElementTree as ET
import numpy as np
from PIL import Image,ImageDraw,ImageFont

HERE=Path(__file__).resolve().parent
PREFIX='experiments/pantheon/'
DEPTH=.28
CAMERA=((55.8,2.3,4.83),(60.5,1.4,10.83))


def bake():
    size=512
    y,x=np.meshgrid((np.arange(size)+.5)/size,(np.arange(size)+.5)/size,indexing='ij')
    u,v=x*4,(1-y)*4
    cells=np.array([[1,.28,.75,.5],[.5,.75,.28,1],[.28,1,.5,.75],[.75,.5,1,.28]])
    level=cells[np.floor(v).astype(int)%4,np.floor(u).astype(int)%4]
    edge=np.minimum.reduce([u%1,1-u%1,v%1,1-v%1])
    bevel=np.clip((edge-.035)/.075,0,1);bevel=bevel*bevel*(3-2*bevel)
    height=level*bevel
    Image.fromarray(np.uint8(np.rint(height*255))).save(HERE/'textures/parallax-reference-height.png')
    palette=np.array([[.82,.67,.40],[.36,.60,.66],[.64,.43,.31],[.65,.66,.62]])
    color=palette[(np.floor(u).astype(int)+np.floor(v).astype(int))%4]
    grain=.97+.03*np.sin(x*457+y*631)
    color=(color*bevel[...,None]+np.array([.10,.115,.12])*(1-bevel[...,None]))*grain[...,None]
    Image.fromarray(np.uint8(np.clip(color,0,1)*255)).save(HERE/'textures/parallax-reference-color.png')
    def mesh(name,resolution,displace):
        lines=['# Deterministic height-field reference; 2.4 metre square.']
        for row in range(resolution+1):
            for column in range(resolution+1):
                a,b=column/resolution,row/resolution
                # Bilinear height sampling matches the texture's V-flipped import.
                px,py=a*size-.5,(1-b)*size-.5
                ix,iy=int(np.floor(px)),int(np.floor(py));fx,fy=px-ix,py-iy
                sample=lambda dx,dy:height[min(size-1,max(0,iy+dy)),min(size-1,max(0,ix+dx))]
                h=(sample(0,0)*(1-fx)+sample(1,0)*fx)*(1-fy)+(sample(0,1)*(1-fx)+sample(1,1)*fx)*fy
                z=-(1-h)*DEPTH if displace else 0
                lines.extend([f'v {(a-.5)*2.4:.7f} {(b-.5)*2.4:.7f} {z:.7f}',f'vt {a:.7f} {b:.7f}'])
        for row in range(resolution):
            for col in range(resolution):
                a=row*(resolution+1)+col+1;b=a+1;d=a+resolution+1;c=d+1
                lines.extend([f'f {a}/{a} {b}/{b} {c}/{c}',f'f {a}/{a} {c}/{c} {d}/{d}'])
        (HERE/'meshes'/name).write_text('\n'.join(lines)+'\n',encoding='ascii')
    mesh('parallax-reference-flat.obj',1,False)
    # Labels are ordinary unlit textured quads, outside the compared surfaces.
    for title,key in [('FLAT / no parallax','off'),('PARALLAX / F9','on')]:
        im=Image.new('RGB',(1024,128),'#15232c');draw=ImageDraw.Draw(im)
        try:font=ImageFont.truetype('arial.ttf',48)
        except OSError:font=ImageFont.load_default(size=48)
        draw.text((30,35),title,font=font,fill='#e3ecdc')
        name=f'parallax-reference-label-{key}.png';im.save(HERE/'textures'/name)
    return {'height_levels':[0,.28,.5,.75,1],'depth_m':DEPTH,'triangles_per_panel':2,'shared_flat_geometry':True}


def add_demonstration(scene):
    assets=scene.find('assets')
    for item in list(assets):
        if item.get('id','').startswith('parallax-reference-'):assets.remove(item)
    m=ET.SubElement(assets,'model',id='parallax-reference-plane',file=PREFIX+'meshes/parallax-reference-flat.obj')
    ET.SubElement(m,'material',name='*',diffuse='1 1 1',lighting='false',
        texture0=PREFIX+'textures/parallax-reference-color.png')
    m=ET.SubElement(assets,'model',id='parallax-reference-mount',file='models/procedural-brick-plinth.obj')
    ET.SubElement(m,'material',name='*',diffuse='.025 .035 .045',roughness='.85',specular='.02 .02 .02')
    for key in ('off','on'):
        m=ET.SubElement(assets,'model',id='parallax-reference-label-'+key,file=PREFIX+'meshes/parallax-reference-flat.obj')
        ET.SubElement(m,'material',name='*',diffuse='1 1 1',lighting='false',castShadows='false',
            texture0=PREFIX+f'textures/parallax-reference-label-{key}.png')
    for item in list(scene):
        if item.get('name')=='Parallax wall comparison':scene.remove(item)
    group=ET.SubElement(scene,'transform',name='Parallax wall comparison',room='aes',
        translate='60.5 0 10.83',rotate='0 180 0')
    state=ET.SubElement(group,'state');ET.SubElement(state,'uniform',name='uFog',type='bool',value='false')
    for x,key,name in [(-1.4,'off','Flat panel - no parallax'),(1.4,'on','Parallax height field - F9')]:
        mount=ET.SubElement(group,'transform',translate=f'{x} 1.4 -.235',scale='2.5 2.5 .46')
        ET.SubElement(mount,'instance',ref='parallax-reference-mount',selectable='false')
        placement=ET.SubElement(group,'transform',translate=f'{x} 1.4 0')
        ET.SubElement(placement,'state',heightMap=PREFIX+'textures/parallax-reference-height.png',
            parallax='true' if key=='on' else 'false',parallaxScale=str(DEPTH))
        ET.SubElement(placement,'instance',ref='parallax-reference-plane',name=name,selectable='true')
        label=ET.SubElement(group,'transform',translate=f'{x} 2.85 .02',scale='1 .12 1')
        ET.SubElement(label,'instance',ref='parallax-reference-label-'+key,selectable='false')


if __name__=='__main__':
    import copy
    print(bake())
    for path in [HERE/'scene.xml',HERE/'portal-scene.xml',*sorted((HERE/'views').glob('*.xml'))]:
        if path.stem=='parallax-reference':continue
        scene=ET.parse(path).getroot();add_demonstration(scene);ET.indent(scene,space='  ')
        ET.ElementTree(scene).write(path,encoding='utf8',xml_declaration=True)
    scene=copy.deepcopy(ET.parse(HERE/'scene.xml').getroot())
    for key,value in zip(('eye','target'),CAMERA):scene.find('camera').set(key,' '.join(map(str,value)))
    ET.indent(scene,space='  ');ET.ElementTree(scene).write(HERE/'views/parallax-reference.xml',encoding='utf8',xml_declaration=True)
