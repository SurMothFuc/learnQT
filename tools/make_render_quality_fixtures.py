"""Closed boundaries and textured reflectors for fourth/fifth batch image tests."""
import argparse,json,math
from pathlib import Path
from PIL import Image

def main():
    parser=argparse.ArgumentParser();parser.add_argument('output',type=Path);out=parser.parse_args().output.resolve();out.mkdir(parents=True,exist_ok=True)
    def geometry(name,faces):
        lines=[];start=1
        for face in faces:
            a,b,c=face[:3];u=[b[i]-a[i] for i in range(3)];v=[c[i]-a[i] for i in range(3)]
            n=[u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0]];length=math.sqrt(sum(x*x for x in n));n=[x/length for x in n]
            for point,uv in zip(face,[(0,0),(1,0),(1,1),(0,1)]):lines+=['v '+' '.join(map(str,point)),'vt '+' '.join(map(str,uv)),'vn '+' '.join(map(str,n))]
            for ids in ((0,1,2),(0,2,3)):lines+=['f '+' '.join(f'{start+i}/{start+i}/{start+i}' for i in ids)]
            start+=4
        (out/name).write_text('\n'.join(lines)+'\n',encoding='utf-8')
    vertices=[(1 if i&1 else -1,1 if i&2 else -1,1 if i&4 else -1) for i in range(8)]
    faces=[(0,4,6,2),(1,3,7,5),(0,1,5,4),(2,6,7,3),(0,2,3,1),(4,5,7,6)]
    geometry('box.obj',[[vertices[i] for i in face] for face in faces])
    geometry('screen.obj',[[(-3,-3,0),(3,-3,0),(3,3,0),(-3,3,0)]])
    image=Image.new('RGBA',(128,128))
    for y in range(128):
        for x in range(128):image.putpixel((x,y),((240,180,35,255) if (x//8+y//8)%2 else (25,65,200,255)))
    image.save(out/'checker.png')
    def matrix(scale=1,z=0,x=0):return [scale,0,0,x,0,scale,0,0,0,0,scale,z,0,0,0,1]
    def model(name,source,material,scale=1,z=0,x=0):return {'id':name,'source':str(out/source),'material':material,'normalize':False,'smoothNormals':False,'transform':matrix(scale,z,x)}
    textures=[{'id':'checker','source':str(out/'checker.png'),'minFilter':9729,'magFilter':9729,'wrapS':1,'wrapT':1}]
    screen={'id':'screen','baseColor':[1,1,1],'emissive':[3,3,3],'textures':{'emissive':'checker'}}
    def scene(name,models,materials,eye=(.4,.15,5),lights=(),tex=textures,bounces=16):
        data={'version':1,'name':name,'models':models,'materials':materials,'textures':tex,'lights':list(lights),
            'camera':{'position':list(eye),'target':[0,0,-2],'up':[0,1,0],'fov':45},
            'render':{'denoise':False,'denoiseMode':'none','antialiasing':True,'useEnvironmentMap':True,'maxBounces':bounces,'rrMinDepth':8,'useTileRendering':True,'tileSize':128},
            'display':{'exposure':0,'tonemap':1}}
        (out/(name+'.scene.json')).write_text(json.dumps(data,indent=2),encoding='utf-8')
    fog1={'id':'outer','alphaMode':1,'mediumtype':1,'mediumDensity':.7,'mediumColor':[.3,.8,.95]}
    fog2={'id':'inner','alphaMode':1,'mediumtype':1,'mediumDensity':.5,'mediumColor':[.9,.5,.3]}
    scene('initial-media',[model('outer','box.obj','outer',1.8),model('inner','box.obj','inner',.7),model('screen','screen.obj','screen',1,-2.2)],[fog1,fog2,screen],eye=(0,0,.1))
    glass={'id':'glass','baseColor':[1,1,1],'transmission':1,'IOR':1.5,'roughness':0}
    water={'id':'water','baseColor':[1,1,1],'transmission':1,'IOR':1.33,'roughness':0}
    scene('adjacent-ior',[model('outer','box.obj','glass',1.8),model('inner','box.obj','water',1.1),model('screen','screen.obj','screen',1,-2.2)],[glass,water,screen])
    textured_screen=dict(screen,textures={'emissive':'checker','baseColor':'checker'})
    scene('contact-ior',[model('glass','box.obj','glass',1,1),model('water','box.obj','water',1,-1),model('screen','screen.obj','screen',1,-2.2)],[glass,water,screen])
    scene('delta-guides',[model('glass','box.obj','glass',1.4),model('screen','screen.obj','screen',1,-2.2)],[glass,textured_screen])
    scene('medium-overflow',[model('shell-'+str(i),'box.obj','outer',1.8-i*.1) for i in range(9)]+[model('screen','screen.obj','screen',1,-2.2)],[fog1,screen])
    coat={'id':'coat','baseColor':[.15,.3,.55],'IOR':1.5,'roughness':.3,'clearcoat':1,'clearcoatGloss':.2}
    scene('clearcoat',[model('coat','screen.obj','coat')],[coat],lights=[{'id':'sun','type':'sun','direction':[.6,0,-.8],'radius':.2,'radiance':[5,5,5]}],tex=[],bounces=8)
    rough=dict(glass,roughness=.2)
    scene('rough-guides',[model('glass','box.obj','glass',1.4),model('screen','screen.obj','screen',1,-2.2)],[rough,textured_screen])
    # Combined alpha/refraction/absorption with a shared texture.
    mask={'id':'mask','baseColor':[.7,.7,.7],'alphaMode':3,'opacity':.3}
    absorbing=dict(glass,mediumtype=1,mediumDensity=.25,mediumColor=[.85,.6,.4])
    scene('combined',[model('outer','box.obj','glass',1.4),model('alpha','screen.obj','mask',.5,1.6),model('screen','screen.obj','screen',1,-2.2)],[absorbing,mask,screen])
    gradient=Image.new('RGBA',(128,16))
    for y in range(16):
        for x in range(128):gradient.putpixel((x,y),(int(x*80/127),)*3+(255,))
    gradient.save(out/'gray.png')
    gray={'id':'gray','baseColor':[0,0,0],'emissive':[1,1,1],'IOR':1,'textures':{'emissive':'gray'}}
    scene('srgb',[model('gray','screen.obj','gray')],[gray],tex=[{'id':'gray','source':str(out/'gray.png'),'minFilter':9729,'magFilter':9729}])
    print(out)
if __name__=='__main__':main()
