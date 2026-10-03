"""Compare one feature with a reversible developer flag in serial offscreen runs."""
import argparse,json,os,pathlib,subprocess
import numpy as np
from PIL import Image

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--exe',type=pathlib.Path,required=True)
    p.add_argument('--scene',type=pathlib.Path,required=True)
    p.add_argument('--output',type=pathlib.Path,required=True)
    p.add_argument('--flag',required=True)
    p.add_argument('--width',type=int,default=256);p.add_argument('--height',type=int,default=192)
    p.add_argument('--spp',type=int,default=16);p.add_argument('--reference-spp',type=int,default=4096)
    p.add_argument('--bounces',type=int,default=8);p.add_argument('--seconds',type=float,default=.5)
    p.add_argument('--seeds',type=int,default=3)
    args=p.parse_args();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    if min(args.width,args.height,args.spp,args.reference_spp,args.seeds,args.bounces)<=0 or args.seconds<=0:
        p.error('Positive dimensions, samples and budgets required')
    exe=args.exe.resolve();scene=args.scene.resolve();records={}
    def run(tag,seed,spp,legacy=False,extras=()):
        env=os.environ.copy();env.pop(args.flag,None);env.pop('LEARNQT_TRACE_PROFILE',None)
        if legacy:env[args.flag]='1'
        command=[str(exe),'--benchmark',str(scene),str(out/(tag+'.json')),str(args.width),str(args.height),str(spp),
                 'none','--aa','--no-warmup','--seed',str(seed),'--bounces',str(args.bounces),*extras]
        with (out/(tag+'.log')).open('w',encoding='utf-8') as log:
            subprocess.run(command,env=env,stdout=log,stderr=log,check=True,timeout=600)
        result=json.loads((out/(tag+'.json')).read_text(encoding='utf-8-sig'))
        records[tag]={'command':command,'legacyFlag':args.flag if legacy else None,'result':result}
        image=np.fromfile(out/(tag+'.json.raw.png.linear'),dtype='<f4').reshape(args.height,args.width,4).astype('f8')
        if not np.isfinite(image).all():raise RuntimeError('Nonfinite image: '+tag)
        return image
    reference=run('reference',137,args.reference_spp)
    same_spp=[];same_time=[]
    def error(a):return float(np.mean((a[:,:,:3]-reference[:,:,:3])**2))
    for seed in range(args.seeds):
        images=[run(mode+'-'+str(seed),seed,args.spp,mode=='before') for mode in ('before','after')]
        same_spp.append({'seed':seed,'beforeMse':error(images[0]),'afterMse':error(images[1]),
                         'beforeMean':float(images[0][:,:,:3].mean()),'afterMean':float(images[1][:,:,:3].mean())})
        if seed==0:
            for mode,img in zip(('before','after'),images):
                e=np.sqrt(np.mean((img[:,:,:3]-reference[:,:,:3])**2,axis=2))
                # Common display scale; error PNG is a visualization, metrics use linear float data.
                intensity=np.clip(e/.2,0,1)
                rgb=np.stack((intensity,np.sqrt(intensity)*.3,np.zeros_like(intensity)),axis=2)
                Image.fromarray((rgb[::-1]*255).astype('uint8')).save(out/(mode+'-error.png'))
        for mode in ('before','after'):
            img=run('timed-'+mode+'-'+str(seed),seed,args.spp,mode=='before',['--seconds',str(args.seconds)])
            same_time.append({'mode':mode,'seed':seed,'mse':error(img),
                              'result':records['timed-'+mode+'-'+str(seed)]['result']})
    after=run('after-check',7,args.spp)
    full=run('after-full',7,args.spp,extras=['--full'])
    tiled=run('after-tile17',7,args.spp,extras=['--tile','17'])
    compute=run('after-compute',7,args.spp,extras=['--compute','--require-compute'])
    if not np.array_equal(after,full) or not np.array_equal(after,tiled):raise RuntimeError('Tile layout changed pixels')
    compute_error=float(np.max(np.abs(after-compute)))
    if compute_error>1e-5:raise RuntimeError('Compute/fragment mismatch: '+str(compute_error))
    summary={'scene':str(scene),'flag':args.flag,'sameSpp':args.spp,'referenceSpp':args.reference_spp,
             'sameSppRuns':same_spp,'sameTimeRuns':same_time,'tileBitIdentical':True,
             'computeMaxAbs':compute_error,'records':records,
             'scope':'Independent seed-137 finite-spp reference; same-spp and same-time comparisons, denoising off. PNG error scale is shared; numerical errors use raw linear floats. Finite-depth/reference error remains.'}
    (out/'report.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps({'sameSpp':same_spp,'computeMaxAbs':compute_error,'output':str(out)},ensure_ascii=False))
if __name__=='__main__':main()
