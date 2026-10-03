"""Write small, self-contained rendering fixtures; paths are resolved for the chosen output."""
import argparse,json,math,pathlib
from PIL import Image

def main():
    parser=argparse.ArgumentParser();parser.add_argument('output',type=pathlib.Path);args=parser.parse_args()
    out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    def obj(name,faces,uv=(0,0,1,1)):
        lines=[];index=1
        for points in faces:
            a,b,c=points[:3]
            ab=[b[i]-a[i] for i in range(3)];ac=[c[i]-a[i] for i in range(3)]
            normal=[ab[1]*ac[2]-ab[2]*ac[1],ab[2]*ac[0]-ab[0]*ac[2],ab[0]*ac[1]-ab[1]*ac[0]]
            length=math.sqrt(sum(v*v for v in normal));normal=[v/length for v in normal]
            coords=[(uv[0],uv[1]),(uv[2],uv[1]),(uv[2],uv[3]),(uv[0],uv[3])]
            for v,t in zip(points,coords):
                lines+=['v '+' '.join(map(str,v)),'vt '+' '.join(map(str,t)),'vn '+' '.join(map(str,normal))]
            for tri in ((0,1,2),(0,2,3)):
                lines.append('f '+' '.join(f'{index+i}/{index+i}/{index+i}' for i in tri))
            index+=4
        (out/name).write_text('\n'.join(lines)+'\n',encoding='utf-8')
    obj('receiver.obj',[[(-3,-3,0),(3,-3,0),(3,3,0),(-3,3,0)]])
    obj('slab.obj',[
        [(-1.8,-1.8,0),(1.8,-1.8,0),(1.8,1.8,0),(-1.8,1.8,0)],
        [(-1.8,1.8,-.4),(1.8,1.8,-.4),(1.8,-1.8,-.4),(-1.8,-1.8,-.4)],
        [(-1.8,-1.8,-.4),(1.8,-1.8,-.4),(1.8,-1.8,0),(-1.8,-1.8,0)],
        [(1.8,1.8,-.4),(-1.8,1.8,-.4),(-1.8,1.8,0),(1.8,1.8,0)],
        [(-1.8,1.8,-.4),(-1.8,-1.8,-.4),(-1.8,-1.8,0),(-1.8,1.8,0)],
        [(1.8,-1.8,-.4),(1.8,1.8,-.4),(1.8,1.8,0),(1.8,-1.8,0)]])
    def hdr(name,values,w=8,h=4):
        data=bytearray(f'#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y {h} +X {w}\n'.encode())
        for color in values:
            maximum=max(color)
            if maximum<1e-32:data+=bytes(4);continue
            m,e=math.frexp(maximum);scale=m*256/maximum
            data+=bytes([min(255,int(c*scale)) for c in color]+[e+128])
        (out/name).write_bytes(data)
    colors=[(.001,)*3]*32;hdr('weak.hdr',colors)
    colors=[(0,)*3]*32;colors[2*8+6]=(3,3,3);hdr('hot.hdr',colors)
    def transform(z=0,x=0,y=0):return [1,0,0,x,0,1,0,y,0,0,1,z,0,0,0,1]
    def model(name,source,material,z=0,x=0,y=0):
        return {'id':name,'source':str(out/source),'material':material,'normalize':False,'smoothNormals':False,'transform':transform(z,x,y)}
    def scene(name,models,materials,hdrfile='',lights=(),textures=()):
        d={'version':1,'name':name,'hdr':str(out/hdrfile) if hdrfile else '',
           'models':models,'materials':materials,'textures':list(textures),'lights':list(lights),
           'camera':{'position':[0,0,3],'target':[0,0,-.5],'up':[0,1,0],'fov':45},
           'render':{'denoise':False,'denoiseMode':'none','antialiasing':True,'useEnvironmentMap':True,'maxBounces':12,
                     'tileSize':128,'useTileRendering':True}}
        (out/(name+'.scene.json')).write_text(json.dumps(d,indent=2),encoding='utf-8')
    glass={'id':'glass','baseColor':[1,1,1],'transmission':1,'roughness':0,'IOR':2.5}
    scene('glass-rr',[model('first','slab.obj','glass'),model('second','slab.obj','glass',-1)], [glass])
    rough=dict(glass,roughness=.12,IOR=1.5)
    scene('rough-glass-rr',[model('first','slab.obj','glass'),model('second','slab.obj','glass',-1)],[rough])
    white={'id':'white','baseColor':[.7,.7,.7],'roughness':1,'IOR':1}
    for name,hdrfile,radiance in [('strong-hdr','hot.hdr',.001),('strong-light','weak.hdr',40)]:
        scene(name,[model('receiver','receiver.obj','white')],[white],hdrfile,
              [{'id':'sun','type':'sun','direction':[0,0,-1],'radius':.15,'radiance':[radiance]*3}])
    # Two same-sized emitting patches sample different halves of the same image.
    image=Image.new('RGBA',(64,32),(0,0,0,255))
    for y in range(32):
        for x in range(32,64):image.putpixel((x,y),(255,255,255,255))
    image.save(out/'emission.png')
    obj('emitter-bright.obj',[[(-.6,-.6,0),(.6,-.6,0),(.6,.6,0),(-.6,.6,0)]],(.6,.1,.9,.9))
    obj('emitter-dark.obj',[[(-.6,-.6,0),(.6,-.6,0),(.6,.6,0),(-.6,.6,0)]],(.1,.1,.4,.9))
    emitter={'id':'emitter','baseColor':[0,0,0],'emissive':[3,3,3],'textures':{'emissive':'emit'}}
    texture={'id':'emit','source':str(out/'emission.png'),'wrapS':1,'wrapT':1,'minFilter':9729,'magFilter':9729}
    scene('uv-emission',[model('receiver','receiver.obj','white'),model('bright','emitter-bright.obj','emitter',1.5,-1.4),
                         model('dark','emitter-dark.obj','emitter',1.5,1.4)],[white,emitter],'weak.hdr',textures=[texture])
    print(out)
if __name__=='__main__':main()
