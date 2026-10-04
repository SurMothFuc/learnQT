"""Serial fourth/fifth batch GPU comparisons, raw errors and EXR readback checks.

Requires numpy/Pillow; an independent OpenEXR Python reader is required for
format verification. No foreground UI or input is used by this tool.
"""
import argparse,hashlib,json,os,subprocess,time
from pathlib import Path
import numpy as np
from PIL import Image,ImageDraw,ImageFont

def main():
    p=argparse.ArgumentParser();p.add_argument('--exe',type=Path,required=True);p.add_argument('--fixtures',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--openexr-path',type=Path);args=p.parse_args()
    if args.openexr_path:
        import sys;sys.path.insert(0,str(args.openexr_path.resolve()))
    import OpenEXR
    out=args.output.resolve();out.mkdir(parents=True,exist_ok=True);fixtures=args.fixtures.resolve();exe=args.exe.resolve();records={};metrics={}
    width,height=192,144
    font=ImageFont.truetype('C:/Windows/Fonts/arial.ttf',16)
    flags=('LEARNQT_LEGACY_MEDIA','LEARNQT_LEGACY_CLEARCOAT','LEARNQT_LEGACY_GAMMA','LEARNQT_LEGACY_OIDN_GUIDES','LEARNQT_LEGACY_MOTION_FILTER','LEARNQT_TRACE_DIAGNOSTICS','LEARNQT_TRACE_PROFILE')
    def run(tag,scene,spp=64,mode='none',flag=None,extras=(),seed=0):
        env=os.environ.copy()
        for name in flags:env.pop(name,None)
        if flag:env[flag]='1'
        command=[str(exe),'--benchmark',str(scene),str(out/(tag+'.json')),str(width),str(height),str(spp),mode,
                 '--aa','--no-warmup','--bounces','24','--seed',str(seed),*extras]
        start=time.monotonic()
        print('Rendering',tag,flush=True)
        with (out/(tag+'.log')).open('w',encoding='utf-8') as log:subprocess.run(command,env=env,stdout=log,stderr=log,check=True,timeout=600)
        records[tag]={'command':command,'flag':flag,'wallSeconds':time.monotonic()-start,'result':json.loads((out/(tag+'.json')).read_text(encoding='utf-8-sig'))}
        data=np.fromfile(out/(tag+'.json.raw.png.linear'),dtype='<f4').reshape(height,width,4)
        assert np.isfinite(data).all()
        return data
    def panel(task,before,after,caption):
        before=before if isinstance(before,Image.Image) else Image.open(before).convert('RGB')
        after=after if isinstance(after,Image.Image) else Image.open(after).convert('RGB')
        assert before.size==after.size
        before.save(out/(task+'-before.png'));after.save(out/(task+'-after.png'))
        w,h=before.size;image=Image.new('RGB',(w*2,h+92),'#151b24');draw=ImageDraw.Draw(image)
        draw.text((8,6),'Before / baseline',font=font,fill='white');draw.text((w+8,6),'After / current',font=font,fill='white')
        image.paste(before,(0,30));image.paste(after,(w,30))
        words=caption.split();lines=[];line=''
        for word in words:
            candidate=(line+' '+word).strip()
            if draw.textlength(candidate,font=font)>w*2-16:lines.append(line);line=word
            else:line=candidate
        lines.append(line)
        for i,line in enumerate(lines[:3]):draw.text((8,h+38+i*18),line,font=font,fill='white')
        image.save(out/(task+'-comparison.png'))
    def display(rgb,exposure=0,tonemap=1,legacy=False):
        c=np.maximum(rgb,0)*2**exposure
        if tonemap==1:c=(c*(2.51*c+.03))/(c*(2.43*c+.59)+.14)
        elif tonemap==0:c=c/(1+np.sum(c*np.array([.212671,.715160,.072169]),axis=2,keepdims=True)/1.5)
        c=np.clip(c,0,1)
        c=c**(1/2.2) if legacy else np.where(c<=.0031308,12.92*c,1.055*c**(1/2.4)-.055)
        return Image.fromarray(np.rint(c*255).astype('uint8'))
    def raw_image(data):return display(data[::-1,:,:3])
    # Fourth batch: fault visibility and physical-state comparisons.
    overflow=run('overflow',fixtures/'medium-overflow.scene.json',8,extras=['--diagnostics','--capture-diagnostics'])
    diagnostic=json.loads((out/'overflow.json.diagnostics.json').read_text(encoding='utf-8-sig'))
    assert diagnostic['capture']['eventCounts']['mediumOverflow']>0
    assert diagnostic['capture']['firstEvent']['mediumCount']==8
    metrics['R-C05']=diagnostic
    panel('R-C05',out/'overflow.json.png',out/'overflow.json.diagnostics.png','Nested-shell overflow: before beauty hid failures; after the diagnostic view locates failures and records exact events/state.')
    for task,scene,flag in [('R-C07','initial-media','LEARNQT_LEGACY_MEDIA'),('R-C06','adjacent-ior','LEARNQT_LEGACY_MEDIA'),
                            ('R-C01-C04','clearcoat','LEARNQT_LEGACY_CLEARCOAT'),('R-C08','combined','LEARNQT_LEGACY_MEDIA')]:
        before=run(task+'-before',fixtures/(scene+'.scene.json'),flag=flag,extras=['--diagnostics'])
        after=run(task+'-after',fixtures/(scene+'.scene.json'),extras=['--diagnostics'])
        reference=run(task+'-reference',fixtures/(scene+'.scene.json'),1024,seed=137)
        ref=reference[:,:,:3].astype('f8')
        mse=lambda a:float(np.mean((a[:,:,:3].astype('f8')-ref)**2))
        metrics[task]={'beforeMse':mse(before),'afterMse':mse(after),'beforeMean':float(before[:,:,:3].mean()),'afterMean':float(after[:,:,:3].mean())}
        if task=='R-C01-C04':
            panel(task,display(before[::-1,:,:3],0,2),display(after[::-1,:,:3],0,2),
                  f'Clearcoat furnace, 64 spp: linear RGB mean {metrics[task]["beforeMean"]:.6f} -> {metrics[task]["afterMean"]:.6f}. Both at 0 EV; energy audit, not a noise-reduction claim.')
        else:
            panel(task,display(before[::-1,:,:3],-2,2),display(after[::-1,:,:3],-2,2),'64 spp GPU readback; both displayed at -2 EV with linear clipping. Same camera/seed/depth; independent 1024-spp reference.')
    contact_before=run('contact-before',fixtures/'contact-ior.scene.json',flag='LEARNQT_LEGACY_MEDIA',extras=['--diagnostics'])
    contact_after=run('contact-after',fixtures/'contact-ior.scene.json',extras=['--diagnostics'])
    metrics['contact-ior']={'beforeMean':float(contact_before[:,:,:3].mean()),'afterMean':float(contact_after[:,:,:3].mean()),
        'diagnostics':json.loads((out/'contact-after.json.diagnostics.json').read_text(encoding='utf-8-sig'))}
    assert metrics['contact-ior']['diagnostics']['boundaryMismatch']==0
    panel('R-C06-contact',out/'contact-before.json.png',out/'contact-after.json.png','Exact coincident glass/water triangles: explicit boundary transition and two-sided IOR.')
    # Fifth batch: film/exposure, exact transfer, EXR and guide policies.
    run('film',fixtures/'delta-guides.scene.json',64,extras=['--linear-result'])
    with OpenEXR.File(str(out/'film.json.float.exr'),separate_channels=True) as file:
        channels={name:c.pixels.copy() for name,c in file.channels().items()};header=dict(file.header())
    rgb=np.stack([channels[c] for c in 'RGB'],axis=-1)
    raw=np.fromfile(out/'film.json.raw.png.linear',dtype='<f4').reshape(height,width,4)[::-1,:,:3]
    assert np.array_equal(raw,rgb),'FLOAT EXR changed linear beauty'
    assert float(rgb.max())>1,'HDR fixture did not exceed display range'
    assert all(name in channels for name in ('normal.R','albedo.R','depth.center','variance','sampleCount'))
    with OpenEXR.File(str(out/'film.json.half.exr'),separate_channels=True) as file:
        half={name:c.pixels.copy() for name,c in file.channels().items()}
    for c in 'RGB':assert np.array_equal(half[c],channels[c].astype('f2')),'HALF rounding differs from independent reader/numpy'
    metrics['R-Q12']={'hdrMaximum':float(rgb.max()),'exposureChangesFilm':False}
    panel('R-Q12',out/'film.json.png',out/'film.json.reexposed.png','Same saved linear film: before original display; after -2 EV without rerendering or clipping stored HDR.')
    run('gamma-before',fixtures/'srgb.scene.json',flag='LEARNQT_LEGACY_GAMMA')
    run('gamma-after',fixtures/'srgb.scene.json')
    panel('R-Q14',out/'gamma-before.json.png',out/'gamma-after.json.png','Same linear render: old gamma 2.2 versus exact sRGB encoding; this is a display correction, not less sampling noise.')
    assert 'learnqtSettings' in header
    metrics['R-Q13']={'floatBeautyBitEqual':True,'halfMatchesNumpy':True,'channels':list(channels),'hdrMaximum':float(rgb.max()),'metadataPresent':'learnqtSettings' in header}
    normal=np.stack([channels['normal.'+c] for c in 'RGB'],axis=-1)*.5+.5
    depth=channels['depth.center'];depth=np.repeat((depth/max(float(depth.max()),1e-6))[:,:,None],3,axis=2)
    aovs=Image.new('RGB',(width,height));aovs.paste(display(normal,tonemap=2).resize((width//2,height)),(0,0));aovs.paste(display(depth,tonemap=2).resize((width//2,height)),(width//2,0))
    panel('R-Q13',out/'film.json.png',aovs,'Before display-only image; after actual EXR normal/depth channels. FLOAT beauty is bit-exact and HDR is preserved.')
    for name in ('delta-guides','rough-guides'):
        before=run(name+'-oidn-before',fixtures/(name+'.scene.json'),32,mode='oidn',flag='LEARNQT_LEGACY_OIDN_GUIDES')
        after=run(name+'-oidn-after',fixtures/(name+'.scene.json'),32,mode='oidn')
        reference=run(name+'-oidn-reference',fixtures/(name+'.scene.json'),1024,seed=137)
        read_filtered=lambda mode:np.fromfile(out/(name+'-oidn-'+mode+'.json.filtered.png.linear'),dtype='<f4').reshape(height,width,4)
        error=lambda a:float(np.mean((a[:,:,:3].astype('f8')-reference[:,:,:3])**2))
        assert np.array_equal(before,after),'Guide policy changed beauty/second moment'
        metrics[name]={'rawMse':error(before),'beforeFilteredMse':error(read_filtered('before')),'afterFilteredMse':error(read_filtered('after'))}
        if name=='delta-guides':panel('R-C09',raw_image(read_filtered('before')),raw_image(read_filtered('after')),'Same 32-spp beauty: old first-hit OIDN policy versus delta-path guides with variance-based detail protection.')
    scene=Path('resources/scenes/lantern.scene.json').resolve()
    for mode in ('before','after'):
        run('quality-'+mode,scene,8,mode='realtime',flag='LEARNQT_LEGACY_MOTION_FILTER' if mode=='before' else None,extras=['--preview','--quality-sequence'])
        metrics['quality-'+mode]=json.loads((out/('quality-'+mode+'.json.sequence.json')).read_text(encoding='utf-8-sig'))
        metrics['quality-'+mode+'-settle']=json.loads((out/('quality-'+mode+'.json.quality.json')).read_text(encoding='utf-8-sig'))
    before=np.fromfile(out/'quality-before.json.object-11.filtered.png.linear',dtype='<f4').reshape(height,width,4)
    after=np.fromfile(out/'quality-after.json.object-11.filtered.png.linear',dtype='<f4').reshape(height,width,4)
    panel('R-Q10',raw_image(before),raw_image(after),'Actual object-motion sequence: previous temporal clipping versus conservative stale-history rejection. References and settle errors are recorded.')
    sources={str(f):hashlib.sha256(f.read_bytes()).hexdigest() for directory in ('shaders','src','include') for f in Path(directory).rglob('*') if f.is_file()}
    report={'exeSha256':hashlib.sha256(exe.read_bytes()).hexdigest(),'fixtureSha256':{str(f):hashlib.sha256(f.read_bytes()).hexdigest() for f in fixtures.iterdir() if f.is_file()},'numpyVersion':np.__version__,'OpenEXRVersion':OpenEXR.__version__,'commands':records,'metrics':metrics,'sourceSha256':sources,'scope':'Actual offscreen GPU readbacks and independent OpenEXR decoding. Fixed finite-depth/sample references; no foreground input; no generated imagery. Diagnostic/AOV/exposure panels show capabilities, not universal quality gains.'}
    (out/'report.json').write_text(json.dumps(report,indent=2,ensure_ascii=False),encoding='utf-8')
    print(json.dumps(metrics,ensure_ascii=False,indent=2),flush=True)
if __name__=='__main__':main()
