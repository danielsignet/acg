"""Verify native color/depth with hall room culling on and off (hidden windows)."""
import subprocess,os,json
from pathlib import Path
import numpy as np
from PIL import Image
p=Path(__file__).resolve().parent;r=p.parent.parent;o=p/'output/hub';o.mkdir(exist_ok=True);exe=p/'output/capture-build/RelWithDebInfo/pantheon_capture.exe';results=[]
import xml.etree.ElementTree as ET
scenes={'default':p/'scene.xml','dragon-exit':p/'views/astra-exit.xml'}
for name,target in [('north',(0,4,-60)),('east',(60,4,0)),('west',(-60,4,0))]:
 tree=ET.parse(p/'scene.xml');camera=tree.getroot().find('camera');camera.set('eye','0 4 0');camera.set('target',' '.join(map(str,target)));scene=o/f'quality-center-{name}.xml';tree.write(scene,encoding='utf-8');scenes[name]=scene
for name,scene in scenes.items():
 env=os.environ.copy();env['PANTHEON_HUB_WORK']='visible';prefix=o/f'quality-{name}'
 with prefix.with_suffix('.log').open('w') as log:subprocess.run([str(exe),str(r),str(scene),str(prefix),'','0.5','cubemaps'],env=env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=120)
 for buffer in ['depth','lit']:
  a=np.asarray(Image.open(str(prefix)+f'-{buffer}-on.png')).astype(np.int16);b=np.asarray(Image.open(str(prefix)+f'-{buffer}-unculled.png')).astype(np.int16);d=abs(a-b)
  result={'view':name,'buffer':buffer,'changed_pixels':int(np.count_nonzero(np.any(d!=0,axis=2))),'maximum_difference':int(d.max())};results.append(result);print(result,flush=True)
  if np.any(d):raise RuntimeError('Culling changed visible pixels')
(o/'quality.json').write_text(json.dumps(results,indent=2)+'\n')
