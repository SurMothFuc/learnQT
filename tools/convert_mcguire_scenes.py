#!/usr/bin/env python3
"""Convert the downloaded McGuire archive to learnQT scenes (numpy, Pillow).

Run with --prepare, --convert, --validate, --capture and optional --entry ID.
Original ZIPs and extracted assets are never modified. Derived MTL/texture fixes,
scene JSONs and conversion_manifest.json carry the adaptations and provenance.
"""
from __future__ import annotations
import argparse, copy, hashlib, json, math, os, re, shutil, struct, subprocess, time, zipfile
from pathlib import Path
import numpy as np
from PIL import Image
# The verified Gallery archive contains a genuine 16384 x 16384 texture.
Image.MAX_IMAGE_PIXELS=300_000_000

REPO=Path(__file__).resolve().parents[1]
ROOT=REPO/'resources/imported/mcguire'
ASSETS=ROOT/'assets'; DERIVED=ROOT/'derived'; SCENES=REPO/'resources/scenes'
WORK=REPO/'build/mcguire-conversion'
HDR=REPO/'resources/imported/glslpt/assets/HDR/white_cliff_top_1k.hdr'
IDENTITY=[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1]
BLOCK={'lost_empire','rungholt','vokselia_spawn'}
INDOOR={'breakfast_room','conference','fireplace_room','gallery','holodeck','living_room','salle_de_bain','sibenik','lost_empire'}
NOTES={
 'powerplant':['Legacy Power Plant MTL Tr values interpreted as dissolve/opacity (1 is opaque); emitted as d in the derived MTL. Original source retained.'],
 'cube':['Source default.png has a CRC mismatch. Derived neutral checker replaces this unreadable image; archive unchanged.'],
 'white_oak':['Missing leaf opacity texture derived from the original diffuse PNG alpha channel.'],
 'bistro':['Missing ceiling-fan emissive map omitted in derived MTL. Other emissive maps retained.','512px texture derivatives used to limit the shared texture-array allocation. Original full-resolution images retained.','OBJ object/group boundaries consolidated by material for practical loading; positions, UVs, normals and faces retained.'],
 'gallery':['2048px texture derivatives used to avoid loading the original 16384px scan texture into the editor. Original full-resolution image retained.'],
 'San_Miguel':['Both full and low-poly source models are provided as separate scenes. 512px texture derivatives used to limit memory.'],
 'cloud':['Five surface cloud meshes arranged as a gallery. These are opaque surfaces, not participating-volume clouds.'],
 'geodesic':['All 86 source geodesic meshes arranged as an individually editable gallery.'],
 'indonesian':['STL imported through Assimp; rotated from Z-up to Y-up for presentation.'],
 'serapis':['STL imported through Assimp; rotated from Z-up to Y-up for presentation.'],
}

def dump(p,obj):
 p.parent.mkdir(parents=True,exist_ok=True);p.write_text(json.dumps(obj,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
def rel(p,base=SCENES):return Path(os.path.relpath(p,base)).as_posix()
def load(p):return json.loads(p.read_text(encoding='utf-8'))
def image_info(p):
 with Image.open(p) as im:
  alpha=('A' in im.getbands() or 'transparency' in im.info)
  return im.size, alpha and im.convert('RGBA').getchannel('A').getextrema()[0]<250

def extract(entries):
 for e in entries:
  for f in e['files']:
   dest=ASSETS/e['id']
   if e['id']=='bistro':dest/=Path(f['path']).stem
   dest.mkdir(parents=True,exist_ok=True)
   with zipfile.ZipFile(ROOT/f['path']) as z:
    for info in z.infolist():
     p=(dest/info.filename.replace('\\','/')).resolve()
     if not p.is_relative_to(dest.resolve()):raise ValueError('Unsafe archive member')
     if info.is_dir():p.mkdir(parents=True,exist_ok=True);continue
     if e['id']=='cube' and info.filename=='default.png':continue
     if p.exists() and p.stat().st_size==info.file_size:continue
     p.parent.mkdir(parents=True,exist_ok=True)
     with z.open(info) as a,p.open('wb') as b:shutil.copyfileobj(a,b,1024*1024)

def map_path(line):
 a=line.split();i=1
 while i<len(a) and a[i].startswith('-'):
  opt=a[i];i+=1;i+=3 if opt in ('-o','-s','-t') else 2 if opt=='-mm' else 1
 return ' '.join(a[i:]).strip('"').replace('\\','/')

def prepare_materials(key):
 aliases={};target=DERIVED/key;target.mkdir(parents=True,exist_ok=True)
 if key=='cube':
  im=Image.new('RGB',(64,64));im.putdata([(155,163,178) if (x//8+y//8)%2 else (225,228,235) for y in range(64) for x in range(64)]);im.save(target/'checker.png')
 if key=='white_oak':
  with Image.open(ASSETS/key/'T_White_Oak_Leaves_Hero_3_D.png') as im:im.getchannel('A').save(target/'leaf-opacity.png')
 for mtl in (ASSETS/key).rglob('*.mtl'):
  lines=[]
  for line in mtl.read_text(encoding='utf-8',errors='replace').splitlines():
   stripped=line.split('#')[0].strip()
   # This legacy asset uses Tr=1 for its solid structures. Assimp's standard
   # transparency interpretation would make the solid structures invisible.
   # Preserve fractional values and scope the compatibility fix to Power Plant.
   if key=='powerplant' and re.match(r'Tr\s+',stripped):
    line=re.sub(r'^(\s*)Tr(\s+)',r'\1d\2',line)
   if stripped.startswith(('map_','bump ','norm ')):
    name=map_path(stripped);p=(mtl.parent/name).resolve()
    if not p.exists():
     if key=='cube':p=target/'checker.png'
     elif key=='white_oak' and name.endswith('_A.png'):p=target/'leaf-opacity.png'
     elif key=='bistro' and stripped.startswith('map_Ke'):continue
     else:raise FileNotFoundError(f'{mtl}: {name}')
    if key in ('bistro','San_Miguel','gallery') and stripped.split()[0] in ('map_Kd','map_d','map_Ke'):
     size,alpha=image_info(p)
     if max(size)>512:
      stamp=str(p.relative_to(REPO))+('|gallery2048' if key=='gallery' else '')
      out=target/'textures'/(hashlib.sha256(stamp.encode()).hexdigest()[:12]+'.png')
      if not out.exists():
       out.parent.mkdir(parents=True,exist_ok=True)
       with Image.open(p) as im:
        im=im.convert('RGBA' if alpha else 'RGB');limit=2048 if key=='gallery' else 512;im.thumbnail((limit,limit),Image.Resampling.LANCZOS);im.save(out)
      p=out
    # Assimp resolves texture references against the original OBJ directory.
    directive=stripped.split()[0]
    line=directive+' '+rel(p,mtl.parent)
   elif stripped.startswith('Ni ') and float(stripped.split()[1])<=0:line='Ni 1.5'
   lines.append(line)
  out=target/'mtl'/mtl.relative_to(ASSETS/key);out.parent.mkdir(parents=True,exist_ok=True)
  out.write_text('\n'.join(lines)+'\n',encoding='utf-8')
  aliases[str(mtl.resolve())]=str(out.resolve())
 return aliases

def bounds(p):
 cache=WORK/'bounds.json';allcache=load(cache) if cache.exists() else {}
 key=rel(p,REPO)+('|stl-ascii-v2' if p.suffix.lower()=='.stl' else '');stamp=[p.stat().st_size,p.stat().st_mtime_ns]
 if key in allcache and allcache[key]['stamp']==stamp:return allcache[key]
 lo=np.full(3,np.inf);hi=-lo;triangles=0;vertices=0
 if p.suffix.lower()=='.stl':
  # STL is commonly binary, but the Serapis archive contains an ASCII STL
  # whose header also begins with ``solid``.  Only accept binary when its
  # declared record count fits the file; otherwise stream ASCII vertices.
  size=p.stat().st_size
  with p.open('rb') as f:
   header=f.read(80);raw=f.read(4)
   count=struct.unpack('<I',raw)[0] if len(raw)==4 else 0
  if len(raw)==4 and 84+50*count<=size and not header[:5].lower()==b'solid':
   with p.open('rb') as f:
    f.seek(84)
    data=np.fromfile(f,dtype=np.dtype([('n','<f4',(3,)),('v','<f4',(3,3)),('a','<u2')]),count=count)
   vs=data['v'].reshape(-1,3);lo=vs.min(axis=0).astype(float);hi=vs.max(axis=0).astype(float);triangles=count;vertices=count*3
  else:
   chunk=[]
   with p.open(encoding='utf-8',errors='replace') as f:
    for line in f:
     if line.lstrip().startswith('vertex '):
      chunk.append(line.split()[1:4])
   a=np.asarray(chunk,dtype=np.float64);lo=a.min(axis=0);hi=a.max(axis=0);vertices=len(a);triangles=vertices//3
 else:
  chunk=[]
  def accumulate():
   nonlocal lo,hi,vertices
   if chunk:
    a=np.asarray(chunk,dtype=np.float64);lo=np.minimum(lo,a.min(axis=0));hi=np.maximum(hi,a.max(axis=0));vertices+=len(chunk);chunk.clear()
  with p.open(encoding='utf-8',errors='replace') as f:
   for line in f:
    if line.startswith('v '):
     chunk.append(line.split()[1:4])
     if len(chunk)>=100000:accumulate()
    elif line.startswith('f '):triangles+=len(line.split())-3
  accumulate()
 if not np.isfinite(lo).all() or max(hi-lo)<=0:raise ValueError('Invalid bounds: '+str(p))
 result={'stamp':stamp,'min':lo.tolist(),'max':hi.tolist(),'triangles':triangles,'vertices':vertices}
 allcache[key]=result;dump(cache,allcache);return result

def specs(entry):
 key=entry['id'];ps=sorted(p for p in (ASSETS/key).rglob('*') if p.suffix.lower() in ('.obj','.stl'))
 if key in ('cloud','geodesic'):return [(key,entry['title'],ps)]
 if key=='CornellBox':ps=[p for p in ps if p.name!='water.obj']
 output=[]
 for p in ps:
  suffix='' if len(ps)==1 else '_'+re.sub('[^a-z0-9]+','_',p.stem.lower()).strip('_')
  output.append((key+suffix,entry['title']+(' · '+p.stem if suffix else ''),[p]))
 return output

def transform(b,key,offset=None):
 lo=np.array(b['min']);hi=np.array(b['max']);center=(lo+hi)/2
 rotation=np.eye(3)
 if key in ('indonesian','serapis'):rotation=np.array([[1,0,0],[0,0,1],[0,-1,0]])
 # The editor rejects transforms whose determinant is close to zero.  A few
 # archives (notably Power Plant) use very large world units, so keep a
 # numerically stable minimum scale and fit the camera to the resulting size.
 scale=max(4/max(hi-lo),1e-3);m=np.eye(4);m[:3,:3]=rotation*scale;m[:3,3]=-(rotation@center)*scale
 if offset is not None:m[:3,3]+=np.asarray(offset)
 dims=np.abs(rotation)@(hi-lo)*scale
 return m.reshape(-1).tolist(),dims.tolist()

def geometry_path(p,key):
 if key!='bistro':return p
 out=DERIVED/key/'geometry'/(p.stem+'_by_material.obj')
 if not out.exists():
  out.parent.mkdir(parents=True,exist_ok=True)
  bucketdir=WORK/'face-buckets'/p.stem;bucketdir.mkdir(parents=True,exist_ok=True)
  buckets={};current='default';counts=[0,0,0]
  with p.open(encoding='utf-8',errors='replace') as src,out.open('w',encoding='utf-8') as dst:
   dst.write('# Derived: faces regrouped by material, geometry/UV/normal values retained.\n')
   try:
    for line in src:
     if line.startswith('usemtl '):current=line.strip()[7:].strip()
     elif line.startswith('f '):
      if current not in buckets:
       bp=bucketdir/(str(len(buckets))+'.faces');buckets[current]=(bp,bp.open('w',encoding='utf-8'))
      # Resolve relative indices before moving faces past all vertex declarations.
      if '-' in line:
       tokens=[]
       for v in line.split()[1:]:
        idx=v.split('/')
        tokens.append('/'.join(str(counts[i]+int(x)+1) if x and int(x)<0 else x for i,x in enumerate(idx)))
       line='f '+' '.join(tokens)+'\n'
      buckets[current][1].write(line)
     elif line.startswith(('v ','vt ','vn ')):
      counts[{'v':0,'vt':1,'vn':2}[line.split()[0]]]+=1;dst.write(line)
     elif line.startswith('mtllib '):dst.write(line)
   finally:
    for bp,stream in buckets.values():stream.close()
   for material,(bp,_) in buckets.items():
    dst.write('usemtl '+material+'\n')
    with bp.open(encoding='utf-8') as faces:shutil.copyfileobj(faces,dst)
 return out

def scene_seed(entry,scene_id,title,paths,aliases):
 key=entry['id'];models=[];maxdims=np.zeros(3);notes=list(NOTES.get(key,[]))
 collection=key in ('cloud','geodesic');cols=math.ceil(math.sqrt(len(paths)))
 for i,p in enumerate(paths):
  b=bounds(p);offset=[(i%cols-(cols-1)/2)*5,0,(i//cols-(math.ceil(len(paths)/cols)-1)/2)*5] if collection else None
  matrix,dims=transform(b,key,offset);maxdims=np.maximum(maxdims,dims)
  deps={}
  if p.suffix.lower()=='.obj':
   with p.open(encoding='utf-8',errors='replace') as stream:
    for line in stream:
     if line.startswith('mtllib '):
      name=line.strip()[7:].strip();mtl=(p.parent/name.replace('\\','/')).resolve()
      if str(mtl) in aliases:
       deps[name]=aliases[str(mtl)]
       for ml in Path(aliases[str(mtl)]).read_text(encoding='utf-8').splitlines():
        if ml.strip().startswith(('map_','bump ','norm ')):
         request=map_path(ml);deps[request]=str((mtl.parent/request).resolve())
  models.append({'id':f'model-{i:03}','name':p.stem,'source':str(geometry_path(p,key).resolve()),'transform':matrix,'normalize':False,'smoothNormals':key not in BLOCK,'dependencies':deps})
 d=maxdims
 camera={'position':[4.3,2.7,5.7],'target':[0,0,0],'up':[0,1,0],'fov':45}
 if collection:
  extent=cols*5;camera.update(position=[extent*.6,extent*.95,extent*1.05],fov=48)
 elif key=='CornellBox':camera.update(position=[0,0,6.8],target=[0,0,-.4],fov=42)
 elif key in INDOOR:
  camera.update(position=[d[0]*.27,-d[1]*.08,d[2]*.35],target=[-d[0]*.12,-d[1]*.13,-d[2]*.35],fov=65)
 elif key=='crytek_sponza':
  camera.update(position=[0,-0.55,0],target=[1.0,-0.35,0],fov=68)
 elif key=='dabrovic_sponza':
  camera.update(position=[0,-0.55,0],target=[1.0,-0.35,0],fov=68)
 elif key=='powerplant':
  camera.update(position=[d[0]*.55,d[1]*.5,d[2]*.7],target=[0,0,0],fov=52)
 elif key=='bistro' and paths[0].stem=='interior':
  camera.update(position=[d[0]*.18,-d[1]*.08,d[2]*.32],target=[-d[0]*.12,-d[1]*.12,-d[2]*.32],fov=65)
 elif key=='lpshead':camera.update(position=[0,0,6],fov=38)
 elif key=='serapis':camera.update(position=[-8,1,0],target=[0,0,0],fov=45)
 materials=[]
 if key in ('buddha','dragon','indonesian','serapis','bunny','hairball','geodesic','cloud'):
  color=[.58,.32,.075] if key=='buddha' else [.65,.69,.75]
  materials=[{'id':'presentation-material','baseColor':color,'metallic':.7 if key=='buddha' else 0,'roughness':.35 if key=='buddha' else .65}]
  for model in models:model['material']='presentation-material'
 lights=[]
 if key in INDOOR or (key=='bistro' and paths[0].stem=='interior'):
  # Bounded interior fill for source OBJ files without a complete light rig.
  lights=[{'id':'interior-fill','type':'sphere','position':[0,d[1]*.22,0],'radius':max(.025,min(d)*.035),'radiance':[65,60,52]}]
  notes.append('Added interior fill light and fitted camera; original renderer lighting is not reproduced exactly.')
 if key=='CornellBox':notes.append('Authored emissive ceiling retained; camera fitted toward the open front.')
 s={'version':2,'name':'McGuire · '+title,'models':models,'objects':[],'groups':[{'id':'root','name':title,'parent':'','order':0}],'materials':materials,'textures':[],'lights':lights,'camera':camera,'hdr':str(HDR.resolve()),'environment':{'intensity':.7,'rotation':0},'display':{'exposure':0,'tonemap':1},'render':{'denoise':True,'maxBounces':5,'maxRenderFrames':64,'renderLow':True,'tileSize':128,'useTileRendering':True,'useEnvironmentMap':True},'output':{'width':1600,'height':1000,'samples':128,'bounces':8,'tileSize':128,'denoise':True},'credits':[{'asset':entry['title'],'author':entry['copyright'],'license':entry['license'],'source':entry['source'],'adaptations':notes},{'asset':'White Cliff Top HDR','author':'Greg Zaal / Poly Haven','license':'CC0','source':'https://polyhaven.com/a/white_cliff_top'}]}
 return s,notes

def rebase(doc,old,new):
 def path(v):return rel((old/v).resolve(),new) if v else v
 if doc.get('hdr'):doc['hdr']=path(doc['hdr'])
 for section in ('models','textures'):
  for o in doc.get(section,[]):
   if o.get('source'):o['source']=path(o['source'])
   o['dependencies']={k:path(v) for k,v in (o.get('dependencies') or {}).items() if not Path(k).is_absolute()}
 return doc

def adapt(doc,key,source_dir):
 notes=[];tex_alpha={};source_by_id={};cap=512 if key in ('bistro','San_Miguel') else 2048
 for t in doc['textures']:
  if not t.get('source'):continue
  p=(source_dir/t['source']).resolve();size,alpha=image_info(p);tex_alpha[t['id']]=alpha;source_by_id[t['id']]=p
  if max(size)>cap:
   out=DERIVED/key/'textures'/(hashlib.sha256(str(p.relative_to(REPO)).encode()).hexdigest()[:12]+'.png')
   if not out.exists():
    out.parent.mkdir(parents=True,exist_ok=True)
    with Image.open(p) as im:
     im=im.convert('RGBA' if alpha else 'RGB');im.thumbnail((cap,cap),Image.Resampling.LANCZOS);im.save(out)
   t['source']=rel(out,source_dir)
  if key in BLOCK:t.update(magFilter=9728,minFilter=9728)
 # Mineways exports map_d as a separate copy of map_Kd's alpha. Multiplying
 # both makes water's 136/255 coverage fall below the Mask cutoff.
 names={o['material']:o.get('name','') for o in doc['objects']}
 duplicate_alpha={}
 for m in doc['materials']:
  slots=m.get('textures',{});base=slots.get('baseColor')
  opacity=slots.get('opacity')
  if key in BLOCK and tex_alpha.get(base,False) and opacity:
   pair=(base,opacity)
   if pair not in duplicate_alpha:
    with Image.open(source_by_id[base]) as color, Image.open(source_by_id[opacity]) as mask:
     a=np.asarray(color.convert('RGBA').getchannel('A'))
     b=np.asarray(mask.convert('RGB').getchannel('R'))
     duplicate_alpha[pair]=a.shape==b.shape and np.array_equal(a,b)
   if duplicate_alpha[pair]:
    del slots['opacity']
  if m.get('IOR',1.5)<=0:m['IOR']=1.5
  if base and max(m.get('baseColor',[1,1,1]))<.01:m['baseColor']=[1,1,1]
  if m.get('opacity',1)>=.999 and (tex_alpha.get(base,False) or slots.get('opacity')):m.update(alphaMode=2,alphaCutoff=.5,opacity=1)
  if key in BLOCK:m.update(baseColor=[1,1,1],roughness=.95,metallic=0,transmission=0,IOR=1.5,alphaMode=2,alphaCutoff=.5,opacity=1)
  if key in BLOCK and 'water' in names.get(m['id'],'').lower():
   m['alphaMode']=3 # Partial atlas alpha is coverage, not a cutout hole.
 # Mineways emitters are otherwise just ordinary diffuse blocks.
 for m in doc['materials']:
  name=names.get(m['id'],'').lower()
  if key in BLOCK and any(x in name for x in ('torch','glowstone','lava')):m['emissive']=[4,2.5,1]
 if key=='vokselia_spawn':
  # Keep the already curated original scene as a separate existing preset.
  notes.append('The earlier minecraft_vokselia preset is preserved separately.')
 return notes

def run_app(exe,args,log,timeout=600):
 env=os.environ.copy();env['PATH']='C:/Qt/5.15.2/msvc2019_64/bin;'+env['PATH'];env['QT_SCALE_FACTOR']='1';env['QT_AUTO_SCREEN_SCALE_FACTOR']='0'
 cmd=[str(exe),'--background-test','--background-timeout-ms',str(timeout*1000)]+args
 start=time.time();log.parent.mkdir(parents=True,exist_ok=True)
 with log.open('w',encoding='utf-8') as out:
  p=subprocess.run(cmd,cwd=REPO,env=env,stdout=out,stderr=subprocess.STDOUT,timeout=timeout+30,creationflags=0x08000000)
 text=log.read_text(encoding='utf-8',errors='replace')
 if p.returncode:raise RuntimeError(f'Exit {p.returncode}: {text[-800:]}')
 if 'inputDesktopWindows=0' not in text:raise RuntimeError('Missing background isolation audit')
 return {'command':cmd,'seconds':round(time.time()-start,2),'exitCode':p.returncode,'log':rel(log,REPO),'inputDesktopWindows':0},text

def main():
 ap=argparse.ArgumentParser();ap.add_argument('--prepare',action='store_true');ap.add_argument('--convert',action='store_true');ap.add_argument('--validate',action='store_true');ap.add_argument('--capture',action='store_true');ap.add_argument('--entry',action='append');ap.add_argument('--scene',action='append');ap.add_argument('--force',action='store_true');ap.add_argument('--exe',type=Path,default=REPO/'build/mcguire-build/Release/learnQT.exe');a=ap.parse_args()
 WORK.mkdir(parents=True,exist_ok=True)
 entries=[e for e in load(ROOT/'manifest.json')['entries'] if not a.entry or e['id'] in a.entry]
 rp=ROOT/'conversion_manifest.json';report=load(rp) if rp.exists() else {'source':'https://casual-effects.com/data/','scenes':{},'notes':['Original archives and extracted assets preserved.','Only UV0 and supported PBR slots are imported; OBJ height/specular maps are not a complete PBR translation.','Camera/light setup is an adaptation, not a reference-render reproduction.']}
 if a.prepare:extract(entries)
 failures=[]
 for entry in entries:
  key=entry['id'];aliases=prepare_materials(key) if a.prepare or a.convert else {}
  for sid,title,ps in specs(entry):
   if a.scene and sid not in a.scene:continue
   target=SCENES/('mcguire_'+sid+'.scene.json');rec=report['scenes'].setdefault(sid,{'entry':key,'title':title,'scene':rel(target,REPO)})
   try:
    if a.convert and (a.force or not target.exists()):
     print('CONVERT',sid,flush=True);seed,notes=scene_seed(entry,sid,title,ps,aliases)
     input_path=WORK/(sid+'.input.json');raw=WORK/(sid+'.imported.json');dump(input_path,seed)
     run,text=run_app(a.exe,['--scene',str(input_path),'--save-scene',str(raw)],WORK/(sid+'.import.log'))
     doc=load(raw);notes+=adapt(doc,key,raw.parent);rebase(doc,raw.parent,target.parent)
     dump(target,doc)
     rec.update(conversion=run,notes=notes,objects=len(doc['objects']),textures=len(doc['textures']),materials=len(doc['materials']),sourceModels=[rel(p,REPO) for p in ps])
     print('SAVED',sid,rec['objects'],'objects',rec['textures'],'textures',flush=True)
    if a.validate and (a.force or not rec.get('validation') or rec.get('sceneSha256')!=hashlib.sha256(target.read_bytes()).hexdigest()):
     print('VALIDATE',sid,flush=True);run,text=run_app(a.exe,['--scene',str(target),'--validate-scene'],WORK/(sid+'.validate.log'))
     if re.search(r'failed to load texture|failed to import|Cannot|Scene initialization failed',text,re.I):raise RuntimeError('Import warnings: '+text[-1200:])
     rec['validation']=run;rec['sceneSha256']=hashlib.sha256(target.read_bytes()).hexdigest();rec.pop('error',None)
    if a.capture:
     out=WORK/'captures'/sid
     run,text=run_app(a.exe,['--scene',str(target),'--capture-ui',str(out),'--capture-pages','scene','--capture-raster','--capture-width','1280','--capture-height','800','--capture-warmup','1000'],out/'session.log')
     result=load(out/'capture-report.json')
     if not result['passed']:raise RuntimeError('Capture failed')
     rec['capture']={'run':run,'report':rel(out/'capture-report.json',REPO),'visuallyReviewed':False}
    dump(rp,report)
   except Exception as ex:
    rec['error']=str(ex);failures.append(sid);dump(rp,report);print('FAILED',sid,str(ex),flush=True)
 print('COMPLETE',len(report['scenes']),'scene records; failures',failures,flush=True)
 if failures:raise SystemExit(1)
if __name__=='__main__':main()
