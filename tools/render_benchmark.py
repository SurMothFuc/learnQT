"""Run serial offscreen production timing, a reference, and a separate trace profile.

Example: python tools/render_benchmark.py --exe build/render-foundation/Release/aa_denoise_tests.exe
  --scene resources/scenes/lantern.scene.json --output build/benchmark-lantern --spp 32 --reference-spp 512
The executable uses QOffscreenSurface; this script never sends desktop input.
"""
import argparse, hashlib, json, os, pathlib, subprocess, time
import numpy as np

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe',required=True,type=pathlib.Path)
    parser.add_argument('--scene',required=True,type=pathlib.Path)
    parser.add_argument('--output',required=True,type=pathlib.Path)
    parser.add_argument('--width',type=int,default=320)
    parser.add_argument('--height',type=int,default=240)
    parser.add_argument('--spp',type=int,default=32)
    parser.add_argument('--reference-spp',type=int,default=1024)
    parser.add_argument('--seconds',type=float,default=1)
    parser.add_argument('--runs',type=int,default=3)
    parser.add_argument('--bounces',type=int,default=8)
    parser.add_argument('--seed',type=int,default=0)
    parser.add_argument('--compute',action='store_true')
    args=parser.parse_args()
    if min(args.width,args.height,args.spp,args.reference_spp,args.runs,args.bounces)<=0 or args.seconds<=0:
        parser.error('Dimensions, samples, time and repeats must be positive')
    output=args.output.resolve();output.mkdir(parents=True,exist_ok=True)
    exe=args.exe.resolve();scene=args.scene.resolve()
    environment=os.environ.copy();environment.pop('LEARNQT_TRACE_PROFILE',None)
    records={}
    def run(tag,spp,extras=()):
        target=output/(tag+'.json')
        command=[str(exe),'--benchmark',str(scene),str(target),str(args.width),str(args.height),str(spp),
                 'none','--aa','--no-warmup','--bounces',str(args.bounces),'--seed',str(args.seed),'--diagnostics',*extras]
        if args.compute:command+=['--compute','--require-compute']
        start=time.monotonic()
        with (output/(tag+'.log')).open('w',encoding='utf-8') as log:
            subprocess.run(command,env=environment,stdout=log,stderr=log,check=True,timeout=600)
        value=json.loads(target.read_text(encoding='utf-8-sig'))
        records[tag]={'command':command,'processSeconds':time.monotonic()-start,'result':value}
        pixels=np.fromfile(str(target)+'.raw.png.linear',dtype='<f4').reshape(args.height,args.width,4)
        if not np.isfinite(pixels).all():raise RuntimeError('Nonfinite pixels: '+tag)
        return pixels
    fixed=run('fixed',args.spp)
    profiled=run('profile',args.spp,['--profile'])
    if not np.array_equal(fixed,profiled):raise RuntimeError('Instrumentation changed beauty or its second moment')
    reference=run('reference',args.reference_spp)
    timings=[]
    for i in range(args.runs):
        image=run('timed-'+str(i+1),args.spp,['--seconds',str(args.seconds)])
        timings.append({'mse':float(np.mean((image[:,:,:3].astype('f8')-reference[:,:,:3])**2)),
                        'result':records['timed-'+str(i+1)]['result']})
    errors=np.mean((fixed[:,:,:3].astype('f8')-reference[:,:,:3])**2,axis=2)
    np.save(output/'fixed-error.npy',errors)
    profile=json.loads((output/'profile.json.profile.json').read_text(encoding='utf-8-sig'))
    report={'scene':str(scene),'sceneSha256':hashlib.sha256(scene.read_bytes()).hexdigest(),
            'exeSha256':hashlib.sha256(exe.read_bytes()).hexdigest(),'width':args.width,'height':args.height,
            'sameSpp':args.spp,'referenceSpp':args.reference_spp,'fixedMse':float(errors.mean()),
            'meanBias':float(fixed[:,:,:3].mean()-reference[:,:,:3].mean()),'timed':timings,'profile':profile,
            'records':records,'instrumentationBeautyIdentical':True,
            'scope':'Wall trace timings include completed rounds and synchronization; profiling is separate. Reference is a finite-spp estimate, not exact truth. Process times include setup and readback; no driver VRAM or hardware occupancy claim.'}
    (output/'report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps({'fixedMse':report['fixedMse'],'profile':profile,'output':str(output)},ensure_ascii=False))

if __name__=='__main__':main()
