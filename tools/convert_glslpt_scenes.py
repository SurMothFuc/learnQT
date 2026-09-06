#!/usr/bin/env python3
"""Convert local GLSL-PathTracer assets to learnQT v1 (requires numpy, Pillow).

Examples:
  python -m pip install -r tools/requirements-glslpt.txt
  python tools/convert_glslpt_scenes.py --source D:/program/GLSL-PathTracer-master --check
  python tools/convert_glslpt_scenes.py --source D:/program/GLSL-PathTracer-master
  python tools/convert_glslpt_scenes.py --source D:/program/GLSL-PathTracer-master --entry volume_cube.scene

Only generated glslpt_* scenes and resources/imported/glslpt are written. The
source tree is read-only. --check performs parsing and dependency checks without
writing files or launching learnQT. --no-validate is available for conversion
development; glTF normalization still requires the learnQT executable.
"""
from __future__ import annotations

import argparse
import copy
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
from urllib.parse import unquote

REPO = Path(__file__).resolve().parents[1]
IDENTITY = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]
MATERIAL_DEFAULTS = dict(baseColor=[1, 1, 1], emissive=[0, 0, 0], metallic=0,
    roughness=.5, subsurface=0, specularTint=0, anisotropic=0, sheen=0,
    sheenTint=0, clearcoat=0, clearcoatGloss=0, transmission=0, IOR=1.5,
    mediumtype=0, mediumDensity=0, mediumColor=[1, 1, 1], mediumAnisotropy=0,
    alphaMode=0, opacity=1, alphaCutoff=0, normalScale=1, normalMapFlipY=False,
    metallicChannel=2, roughnessChannel=1)
SCALARS = dict(metallic='metallic', roughness='roughness', subsurface='subsurface',
    speculartint='specularTint', anisotropic='anisotropic', sheen='sheen',
    sheentint='sheenTint', clearcoat='clearcoat', clearcoatgloss='clearcoatGloss',
    spectrans='transmission', ior='IOR', opacity='opacity', alphacutoff='alphaCutoff',
    mediumdensity='mediumDensity', mediumanisotropy='mediumAnisotropy')
VECTORS = dict(color='baseColor', emission='emissive', mediumcolor='mediumColor')
TEXTURE_KEYS = ('albedotexture', 'metallicroughnesstexture', 'normaltexture', 'emissiontexture')
FIELDS = {
    'material': set(SCALARS) | set(VECTORS) | set(TEXTURE_KEYS) | {'alphamode', 'mediumtype'},
    'mesh': {'name', 'file', 'material', 'matrix', 'position', 'scale', 'rotation'},
    'gltf': {'file', 'matrix', 'position', 'scale', 'rotation'},
    'camera': {'matrix', 'position', 'lookat', 'fov', 'aperture', 'focaldist'},
    'light': {'type', 'position', 'emission', 'radius', 'v1', 'v2'},
    'renderer': {'envmapfile', 'envmapintensity', 'resolution', 'windowresolution',
        'maxdepth', 'maxspp', 'tilewidth', 'tileheight', 'enablerr', 'rrdepth',
        'enabletonemap', 'enableaces', 'texarraywidth', 'texarrayheight',
        'openglnormalmap', 'hideemitters', 'enablebackground', 'transparentbackground',
        'backgroundcolor', 'independentrendersize', 'envmaprotation',
        'enableroughnessmollification', 'roughnessmollificationamt', 'enablevolumemis',
        'enableuniformlight', 'uniformlightcolor'},
}


def numbers(value, size=None):
    values = [float(x) for x in value.split()]
    if (size is not None and len(values) != size) or not all(map(math.isfinite, values)):
        raise ValueError(f'Invalid numeric vector: {value!r}')
    return values


def parse_scene(path):
    """Parse active blocks, including disabled '#light\n{...}' blocks as inert.

    Source Loader.cpp ignores '#' lines and unrecognized outer lines. Inside a
    recognized block, the last occurrence of a field wins (as with sscanf).
    """
    result = []
    active = None
    for lineno, raw in enumerate(path.read_text(encoding='utf-8-sig').splitlines(), 1):
        line = raw.split('#', 1)[0].strip()
        if not line:
            continue
        if active is not None:
            if '}' in line:
                result.append(active)
                active = None
            elif line != '{':
                # Real files contain both tabs and spaces.
                parts = line.split(None, 1)
                if len(parts) == 2:
                    active['fields'][parts[0]] = parts[1]
            continue
        match = re.fullmatch(r'(material\s+(\S+)|(renderer|camera|mesh|gltf|light))\s*\{?', line)
        if match:
            kind = 'material' if match[2] else match[3]
            active = dict(kind=kind, name=match[2] or '', line=lineno, fields={})
    if active is not None:
        raise ValueError(f'{path}:{active["line"]}: unterminated block')
    for kind in ('camera', 'renderer'):
        if len([x for x in result if x['kind'] == kind]) != 1:
            raise ValueError(f'{path}: expected exactly one {kind} block')
    return result


def transform(fields):
    # Source's row-vector S*R*T is the same mapping as column-vector T*R*S.
    # Its matrix text is already row-major for the equivalent column mapping.
    if 'matrix' in fields:
        m = numbers(fields['matrix'], 16)
    else:
        p = numbers(fields.get('position', '0 0 0'), 3)
        s = numbers(fields.get('scale', '1 1 1'), 3)
        x, y, z, w = numbers(fields.get('rotation', '0 0 0 1'), 4)
        r = [[1-2*(y*y+z*z), 2*(x*y-z*w), 2*(x*z+y*w)],
             [2*(x*y+z*w), 1-2*(x*x+z*z), 2*(y*z-x*w)],
             [2*(x*z-y*w), 2*(y*z+x*w), 1-2*(x*x+y*y)]]
        m = [r[i][j]*s[j] if j < 3 else p[i] for i in range(3) for j in range(4)] + [0,0,0,1]
    determinant = (m[0]*(m[5]*m[10]-m[6]*m[9]) - m[1]*(m[4]*m[10]-m[6]*m[8])
                   + m[2]*(m[4]*m[9]-m[5]*m[8]))
    if m[12:] != [0,0,0,1] or abs(determinant) < 1e-12:
        raise ValueError('Non-affine or singular model transform')
    return m


def source_metadata(path, blocks):
    """Retain exact ignored values and authored camera fields for auditability."""
    ignored=[]
    for block in blocks:
        fields={k:v for k,v in block['fields'].items() if k not in FIELDS[block['kind']]}
        if fields:
            ignored.append(dict(kind=block['kind'],name=block['name'],line=block['line'],fields=fields))
    return dict(source_sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
        source_camera=next((x['fields'] for x in blocks if x['kind']=='camera'),None),
        ignored_source_fields=ignored)


def convert_camera(fields, resolution):
    if 'matrix' in fields:
        m = numbers(fields['matrix'], 16)
        p = [m[3], m[7], m[11]]
        target = [p[i] + m[i*4+2] for i in range(3)]
    else:
        p, target = numbers(fields['position'], 3), numbers(fields['lookat'], 3)
    fov = math.degrees(2*math.atan(math.tan(math.radians(float(fields['fov']))/2)
                                  * resolution[1]/resolution[0]))
    return dict(position=p, target=target, up=[0,1,0], fov=fov)


def convert_material(fields):
    m = copy.deepcopy(MATERIAL_DEFAULTS)
    for source, target in SCALARS.items():
        if source in fields:
            m[target] = float(fields[source])
    for source, target in VECTORS.items():
        if source in fields:
            m[target] = numbers(fields[source], 3)
    m['alphaMode'] = {'opaque':0, 'mask':2, 'blend':3}.get(fields.get('alphamode'), 0)
    m['mediumtype'] = {'absorb':1, 'scatter':2, 'emissive':3}.get(fields.get('mediumtype'), 0)
    m['roughness'] = max(.001, m['roughness'])  # Source GetMaterial's minimum.
    m['mediumAnisotropy'] = max(-.9, min(.9, m['mediumAnisotropy']))
    if m['mediumtype'] and m['alphaMode'] == 3 and m['opacity'] == 0:
        m['alphaMode'] = 1  # Preserve the boundary for learnQT's medium stack.
    return m


def quad_obj(fields):
    p, a, b = (numbers(fields[k], 3) for k in ('position', 'v1', 'v2'))
    u, v = [a[i]-p[i] for i in range(3)], [b[i]-p[i] for i in range(3)]
    n = [u[1]*v[2]-u[2]*v[1], u[2]*v[0]-u[0]*v[2], u[0]*v[1]-u[1]*v[0]]
    length = math.sqrt(sum(x*x for x in n))
    if length < 1e-12:
        raise ValueError('Degenerate quad light')
    vertices = [p, a, [a[i]+b[i]-p[i] for i in range(3)], b]
    return ('# Generated from GLSLPT quad light; two triangles, double-sided emission.\n'
        + ''.join('v ' + ' '.join(format(x, '.12g') for x in vertex) + '\n' for vertex in vertices)
        + 'vn ' + ' '.join(format(x/length, '.12g') for x in n) + '\n'
        + 'vt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\nf 1/1/1 2/2/1 3/3/1\nf 1/1/1 3/3/1 4/4/1\n')


def distant_light(fields):
    direction = numbers(fields['position'], 3)
    length = math.sqrt(sum(x*x for x in direction))
    if length == 0:
        raise ValueError('Distant light has zero direction')
    radius = math.radians(.1)
    # Native sun.direction is the direction light travels; source distant.position
    # points FROM the surface TO the light (SampleSunDiskLight negates it).
    return dict(type='sun', direction=[-x/length for x in direction], radius=radius,
        radiance=[x/(math.pi*math.sin(radius)**2) for x in numbers(fields['emission'], 3)])


def read_hdr(path):
    """Read Radiance RGBE, including old flat scanlines, without an image plugin."""
    import numpy as np
    with path.open('rb') as f:
        if f.readline().strip() not in (b'#?RADIANCE', b'#?RGBE'):
            raise ValueError(f'Not an RGBE HDR: {path}')
        while True:
            line = f.readline()
            if not line:
                raise ValueError('Truncated HDR header')
            if not line.strip():
                break
        match = re.fullmatch(rb'-Y (\d+) \+X (\d+)\s*', f.readline())
        if not match:
            raise ValueError('Unsupported HDR orientation')
        h, w = map(int, match.groups())
        pixels = np.empty((h, w, 4), dtype=np.uint8)
        for y in range(h):
            first = f.read(4)
            if first == bytes([2, 2, w >> 8, w & 255]) and 8 <= w <= 32767:
                for c in range(4):
                    x = 0
                    while x < w:
                        code = f.read(1)
                        if not code or code[0] == 0:
                            raise ValueError('Truncated HDR RLE')
                        count = code[0]-128 if code[0] > 128 else code[0]
                        if x+count > w:
                            raise ValueError('Invalid HDR run')
                        payload = f.read(1 if code[0] > 128 else count)
                        if len(payload) != (1 if code[0] > 128 else count):
                            raise ValueError('Truncated HDR payload')
                        pixels[y,x:x+count,c] = payload[0] if code[0] > 128 else np.frombuffer(payload, np.uint8)
                        x += count
            else:
                x, shift, value = 0, 0, first
                while x < w:
                    if len(value) != 4:
                        raise ValueError('Truncated flat HDR')
                    if value[:3] == b'\x01\x01\x01':
                        count = value[3] << shift
                        if x == 0 or count == 0 or x+count > w:
                            raise ValueError('Invalid old HDR run')
                        pixels[y,x:x+count] = pixels[y,x-1]
                        x += count
                        shift += 8
                    else:
                        pixels[y,x] = list(value)
                        x += 1
                        shift = 0
                    if x < w:
                        value = f.read(4)
    scale = np.ldexp(np.ones((h,w), np.float32), pixels[:,:,3].astype(np.int32)-136)
    scale[pixels[:,:,3] == 0] = 0
    return pixels[:,:,:3].astype(np.float32)*scale[:,:,None]


def write_hdr(path, rgb):
    import numpy as np
    rgb = np.maximum(rgb, 0)
    maximum = rgb.max(axis=2)
    mantissa, exponent = np.frexp(maximum)
    scale = np.divide(mantissa*256, maximum, out=np.zeros_like(maximum), where=maximum>1e-32)
    rgbe = np.zeros((*maximum.shape,4), np.uint8)
    rgbe[:,:,:3] = np.minimum(255, rgb*scale[:,:,None]).astype(np.uint8)
    rgbe[:,:,3] = np.where(maximum>1e-32, exponent+128, 0).astype(np.uint8)
    h,w = maximum.shape
    with path.open('wb') as f:
        f.write(f'#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y {h} +X {w}\n'.encode())
        for row in rgbe:
            if not 8 <= w <= 32767:
                f.write(row.tobytes())
                continue
            f.write(bytes([2,2,w>>8,w&255]))
            for c in range(4):
                data = row[:,c].tobytes()
                # Literal packets are valid Radiance RLE and deterministic.
                for x in range(0,w,128):
                    packet = data[x:x+128]
                    f.write(bytes([len(packet)])); f.write(packet)


def image_variant(source, destination, operation):
    from PIL import Image
    with Image.open(source) as original:
        image = original.convert('RGBA')
    r,g,b,a = image.split()
    if operation == 'roughness':
        # RGBA8 is the runtime's texture precision. Avoid accidental delta
        # surfaces from quantizing source's 0.001 roughness floor to zero.
        g = g.point([max(1, round(255*(i/255)**2)) for i in range(256)])
    elif operation == 'gamma22':
        def srgb(x):
            return 12.92*x if x <= .0031308 else 1.055*x**(1/2.4)-.055
        lut = [round(255*srgb((i/255)**2.2)) for i in range(256)]
        r,g,b = (c.point(lut) for c in (r,g,b))
    else:
        raise ValueError(operation)
    Image.merge('RGBA',(r,g,b,a)).save(destination)


def atomic_json(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    temp = path.with_name(path.name+'.tmp')
    temp.write_text(json.dumps(data, ensure_ascii=False, indent=2, allow_nan=False)+'\n', encoding='utf-8')
    temp.replace(path)


def relocate(document, source_dir, destination_dir):
    d = copy.deepcopy(document)
    def path(p):
        return os.path.relpath((source_dir/p).resolve(), destination_dir).replace('\\','/')
    if d.get('hdr'):
        d['hdr'] = path(d['hdr'])
    for group in ('models','textures'):
        for entry in d[group]:
            if entry.get('source'):
                entry['source'] = path(entry['source'])
            if entry.get('dependencies'):
                entry['dependencies'] = {k:path(v) for k,v in entry['dependencies'].items()}
    return d


def reference_triangles(source, cache=None):
    """Independent topology count, including glTF node instances (no Assimp)."""
    cache = {} if cache is None else cache
    source = source.resolve()
    if source in cache:
        return cache[source]
    if source.suffix.lower() == '.obj':
        with source.open(encoding='utf-8-sig',errors='replace') as stream:
            count = sum(max(0,len(line.split('#',1)[0].split())-3)
                        for line in stream if line.lstrip().startswith('f '))
    elif source.suffix.lower() == '.gltf':
        d = json.loads(source.read_text(encoding='utf-8-sig'))
        def node_count(index):
            node = d['nodes'][index]
            total = 0
            if 'mesh' in node:
                for primitive in d['meshes'][node['mesh']]['primitives']:
                    if primitive.get('mode',4) != 4:
                        raise ValueError('Expected triangle glTF primitives')
                    accessor = primitive.get('indices',primitive['attributes']['POSITION'])
                    total += d['accessors'][accessor]['count']//3
            return total + sum(node_count(child) for child in node.get('children',[]))
        count = sum(node_count(i) for i in d['scenes'][d.get('scene',0)]['nodes'])
    else:
        raise ValueError(f'Unsupported topology source: {source}')
    cache[source] = count
    return count


def runtime_env(exe, qt_bin):
    env = os.environ.copy()
    env['PATH'] = str(exe.parent)+os.pathsep+str(qt_bin)+os.pathsep+env.get('PATH','')
    return env


class Converter:
    def __init__(self, args):
        self.args = args
        self.source = args.source.resolve()
        self.assets = self.source/'assets'
        self.root = args.repo.resolve()
        self.destination = self.root/'resources/imported/glslpt'
        self.scene_dir = self.root/'resources/scenes'
        self.work = self.root/'build/glslpt-conversion'
        self.cache = {}
        self.dependencies = set()
        self.generated = set()
        self.env = runtime_env(args.exe.resolve(), args.qt_bin)

    def relative(self, path):
        return os.path.relpath(path, self.scene_dir).replace('\\','/')

    def resource(self, source):
        source = source.resolve()
        relative = source.relative_to(self.assets)  # reject external dependencies
        if not source.is_file():
            raise FileNotFoundError(source)
        self.dependencies.add(relative.as_posix())
        target = self.destination/'assets'/relative
        if not self.args.check and source not in self.cache:
            target.parent.mkdir(parents=True, exist_ok=True)
            with source.open('rb') as stream:
                digest = hashlib.file_digest(stream, 'sha256').hexdigest()
            same = target.is_file() and target.stat().st_size == source.stat().st_size
            if same:
                with target.open('rb') as stream:
                    same = hashlib.file_digest(stream, 'sha256').hexdigest() == digest
            if not same:
                shutil.copy2(source, target)
            self.cache[source] = digest
        return target

    def texture(self, source, operation=None):
        copied = self.resource(source)
        if operation is None:
            return copied
        relative = source.resolve().relative_to(self.assets)
        target = self.destination/'derived'/operation/relative.with_suffix(relative.suffix+'.png')
        self.generated.add(target.relative_to(self.destination).as_posix())
        key = (source.resolve(), operation)
        if not self.args.check and key not in self.cache:
            target.parent.mkdir(parents=True, exist_ok=True)
            image_variant(source, target, operation)
            self.cache[key] = True
        return target

    def hdr(self, filename, intensity):
        source = self.assets/filename
        copied = self.resource(source)
        if intensity == 1:
            return copied
        target = self.destination/'derived/hdr'/f'{source.stem}_x{intensity:g}.hdr'
        self.generated.add(target.relative_to(self.destination).as_posix())
        key = (source.resolve(), intensity)
        if not self.args.check and key not in self.cache:
            target.parent.mkdir(parents=True, exist_ok=True)
            write_hdr(target, read_hdr(source)*intensity)
            self.cache[key] = True
        return target

    def gltf(self, source):
        self.resource(source)
        original_data = json.loads(source.read_text(encoding='utf-8-sig'))
        data = original_data
        for group in ('images','buffers'):
            for item in data.get(group,[]):
                uri = item.get('uri','')
                if uri and not uri.startswith('data:'):
                    self.resource(source.parent/unquote(uri))
        derived = self.destination/'derived/gltf'/source.name
        self.generated.add(derived.relative_to(self.destination).as_posix())
        data = copy.deepcopy(data)
        aliases = {}
        for group in ('images','buffers'):
            for index,item in enumerate(data.get(group,[])):
                uri = item.get('uri','')
                if uri and not uri.startswith('data:'):
                    copied = self.resource(source.parent/unquote(uri))
                    item['uri'] = f'{group}/original/{index}/{copied.name}'
                    aliases[item['uri']] = self.relative(copied)
        variants = {}
        def variant_texture(info, operation):
            if not info or 'index' not in info:
                return
            index = info['index']
            key = (index, operation)
            if key not in variants:
                texture = copy.deepcopy(data['textures'][index])
                image = data['images'][texture['source']]
                uri = image.get('uri','')
                if not uri or uri.startswith('data:'):
                    raise ValueError('This converter expects external glTF images')
                original_image = original_data['images'][texture['source']]
                target = self.texture(source.parent/unquote(original_image['uri']), operation)
                alias = f'images/{operation}/{index}/{target.name}'
                aliases[alias] = self.relative(target)
                data['images'].append({'uri':alias})
                texture['source'] = len(data['images'])-1
                data['textures'].append(texture)
                variants[key] = len(data['textures'])-1
            info['index'] = variants[key]
        for mat in data.get('materials',[]):
            pbr = mat.setdefault('pbrMetallicRoughness',{})
            variant_texture(pbr.get('baseColorTexture'), 'gamma22')
            mr = pbr.get('metallicRoughnessTexture')
            if mr:
                variant_texture(mr, 'roughness')
                pbr['metallicFactor'] = pbr['roughnessFactor'] = 1
            else:
                pbr['roughnessFactor'] = max(.001, math.sqrt(pbr.get('roughnessFactor',1)))
            if mat.get('emissiveTexture'):
                variant_texture(mat['emissiveTexture'], 'gamma22')
                mat['emissiveFactor'] = [1,1,1]
            if mat.get('normalTexture'):
                mat['normalTexture']['scale'] = 1
        # Reference renderer samples all its texture array layers as repeat/linear.
        for sampler in data.get('samplers',[]):
            sampler.update(wrapS=10497, wrapT=10497, magFilter=9729, minFilter=9729)
        if not self.args.check:
            atomic_json(derived, data)
        # Parent-free logical aliases survive Assimp's normalization after the
        # portable exporter places models in assets/<hash>/. The physical data
        # remains shared; no duplicate bin/image files are needed here.
        return derived, aliases

    def invoke(self, scene, *options, log_name):
        self.work.mkdir(parents=True, exist_ok=True)
        command = [str(self.args.exe.resolve()), '--scene', str(scene), *map(str,options)]
        result = subprocess.run(command, env=self.env, cwd=self.root, capture_output=True, timeout=600)
        log = result.stdout + result.stderr
        (self.work/(log_name+'.log')).write_bytes(log)
        if result.returncode:
            raise RuntimeError(f'learnQT exit {result.returncode}; see build/glslpt-conversion/{log_name}.log')
        return log.decode('utf-8',errors='replace')

    def convert(self, entry):
        self.dependencies = set()
        self.generated = set()
        standalone = entry.suffix.lower() == '.gltf'
        stem = f'glslpt_{entry.stem}' + ('_gltf' if standalone else '')
        output = self.scene_dir/(stem+'.scene.json')
        blocks = [] if standalone else parse_scene(entry)
        renderer = next((x['fields'] for x in blocks if x['kind']=='renderer'), {})
        resolution = numbers(renderer.get('resolution','1280 720'),2)
        notes = [
            'Native learnQT v1 renderer: pixel-center rays, current tone mapping, RR after depth 3, existing BSDF/medium implementation.',
            'Source gamma 2.2 color textures converted to sRGB storage; RGBA8 quantization and filtering can cause small differences.',
            'Packed roughness G squared, metallic B overrides constants; RGBA8 roughness floor is 1/255 instead of source 0.001.',
            'Normal-map convention uses the source UV inversion / target image inversion equivalence; no extra green flip for OpenGL maps.',
            'Original output size is reference framing metadata only; native rendering follows the viewport size.',
        ]
        record = dict(source=entry.name, output=output.relative_to(self.root).as_posix(),
                      reference_resolution=resolution, source_renderer=renderer, notes=notes,
                      validation=dict(conversion='not_run', load='not_run', render='not_run'))
        record.update(source_metadata(entry,blocks))
        scene = dict(version=1, name=f'GLSLPT · {entry.stem}'+(' (glTF)' if standalone else ''),
            portable=False, models=[], materials=[], textures=[], lights=[], hdr='',
            camera=dict(position=[0,.35,4.5],target=[0,0,0],up=[0,1,0],fov=53.130102),
            render=dict(denoise=False,renderLow=False,useTileRendering=True,
                tileSize=int(min(float(renderer.get('tilewidth',100)),float(renderer.get('tileheight',100)))),
                maxBounces=int(renderer.get('maxdepth',2)),maxRenderFrames=0,useEnvironmentMap=False),
            credits=[dict(source='GLSL-PathTracer local asset collection',asset=entry.name,
                attribution='See resources/imported/glslpt/Model Credits.txt, HDR Credits.txt and LICENSE; asset licenses differ.')],
            conversion=dict(source=entry.name, referenceResolution=resolution, notes=notes))
        if not standalone:
            camera = next(x['fields'] for x in blocks if x['kind']=='camera')
            scene['camera'] = convert_camera(camera,resolution)
            if float(camera.get('aperture',0)):
                notes.append(f'Pinhole camera; source aperture={camera["aperture"]}, focaldist={camera.get("focaldist","1")} is not reproduced.')
        for key in ('hideemitters','enablebackground','transparentbackground','enableaces',
                    'enableroughnessmollification','enabletonemap','enablerr','rrdepth','enablevolumemis'):
            if key in renderer:
                notes.append(f'Source renderer {key}={renderer[key]}; native renderer behavior retained.')
        for block in blocks:
            ignored = set(block['fields'])-FIELDS[block['kind']]
            if ignored:
                notes.append(f'Source-loader ignored fields at line {block["line"]}: '+', '.join(sorted(ignored)))
        # Source default material is allocated before any authored material.
        default = convert_material({}); default['id']='default'; scene['materials'].append(default)
        texture_ids = {}
        def tex(path):
            relative = self.relative(path)
            if relative not in texture_ids:
                texture_ids[relative] = f'texture-{len(texture_ids):03d}'
                scene['textures'].append(dict(id=texture_ids[relative],source=relative,
                    uvScale=[1,1],uvOffset=[0,0],uvRotation=0,wrapS=0,wrapT=0,minFilter=9729,magFilter=9729))
            return texture_ids[relative]
        material_ids = {}
        for block in (x for x in blocks if x['kind']=='material'):
            fields, name = block['fields'], block['name']
            if name in material_ids:
                notes.append(f'Duplicate material {name}: kept first definition as source loader does.')
                continue
            m = convert_material(fields); m['id']='material-'+name
            material_ids[name] = m['id']; m['textures']={}
            for field,slot,operation in [('albedotexture','baseColor','gamma22'),
                    ('normaltexture','normal',None),('emissiontexture','emissive','gamma22')]:
                if fields.get(field,'none') != 'none':
                    m['textures'][slot] = tex(self.texture(entry.parent/fields[field],operation))
                    if slot=='emissive': m['emissive']=[1,1,1]
            if fields.get('metallicroughnesstexture','none') != 'none':
                id_ = tex(self.texture(entry.parent/fields['metallicroughnesstexture'],'roughness'))
                m['textures'].update(metallic=id_,roughness=id_); m['metallic']=m['roughness']=1
            m['normalMapFlipY'] = renderer.get('openglnormalmap','true') == 'false'
            if m['alphaMode']==1:
                notes.append(f'{name}: zero-opacity Blend medium converted to Transparent boundary.')
            if any(x>1 for x in m['mediumColor']) and m['mediumtype']==2:
                notes.append(f'{name}: native medium scatter albedo is clamped to [0,1].')
            scene['materials'].append(m)
        model_blocks = [x for x in blocks if x['kind'] in ('mesh','gltf')]
        if standalone:
            model_blocks = [dict(kind='gltf',fields={'file':entry.name})]
            scene['fitModelOnImport'] = True
            notes.append('Standalone glTF centered and scaled to maximum extent 3; fixed native default camera, no added key light.')
        has_gltf = False
        for index,block in enumerate(model_blocks):
            f = block['fields']; source=entry.parent/f['file']
            has_gltf |= block['kind']=='gltf'
            if block['kind']=='gltf':
                target,aliases=self.gltf(source)
            else:
                target=self.resource(source);aliases={}
            model = dict(id=f'model-{index:03d}',source=self.relative(target),transform=transform(f),
                         smoothNormals=True,normalize=False)
            if f.get('name'): model['name']=f['name']
            if aliases:model['dependencies']=aliases
            if block['kind']=='mesh':
                model['material'] = material_ids.get(f.get('material'),'default')
                if f.get('material') and f['material'] not in material_ids:
                    notes.append(f'Unknown material {f["material"]}: source fallback default used.')
            scene['models'].append(model)
        for index,block in enumerate(x for x in blocks if x['kind']=='light'):
            f=block['fields']; id_=f'light-{index:03d}'
            if f['type']=='quad':
                path=self.destination/'derived/lights'/f'{stem}_{id_}.obj'
                self.generated.add(path.relative_to(self.destination).as_posix())
                geometry=quad_obj(f)
                if not self.args.check:
                    path.parent.mkdir(parents=True,exist_ok=True);path.write_text(geometry,encoding='utf-8')
                material=convert_material({'emission':f['emission'],'color':'0 0 0','roughness':'1'})
                material['id']=id_;scene['materials'].append(material)
                scene['models'].append(dict(id=id_,source=self.relative(path),transform=IDENTITY,
                    smoothNormals=False,normalize=False,material=id_))
            else:
                if f['type']=='sphere':
                    light=dict(type='sphere',position=numbers(f['position'],3),radius=float(f['radius']),radiance=numbers(f['emission'],3))
                elif f['type']=='distant':
                    light=distant_light(f)
                    notes.append('Ideal distant light approximated by a visible 0.1-degree sun disk with matched integrated irradiance.')
                else: raise ValueError(f'Unknown light type: {f["type"]}')
                light['id']=id_;scene['lights'].append(light)
        if any(x['kind']=='light' and x['fields']['type']=='quad' for x in blocks):
            notes.append('Quad lights converted to two mesh triangles; native mesh emission is double-sided and has native surface scattering.')
        hdr_file=renderer.get('envmapfile','none')
        if hdr_file!='none':
            intensity=float(renderer.get('envmapintensity',1))
        elif not any(x['kind']=='light' for x in blocks):
            hdr_file='HDR/Background_05.hdr';intensity=1.5
            notes.append('Default environment from a fresh reference process: Background_05.hdr at 1.5.')
        if hdr_file!='none':
            scene['hdr']=self.relative(self.hdr(hdr_file,intensity))
            scene['render']['useEnvironmentMap']=True
            record['environment']=dict(source=hdr_file,intensity=intensity)
        record.update(source_models=len(model_blocks), models=len(scene['models']),
            authored_materials=len(material_ids), analytic_lights=len(scene['lights']),
            quad_lights=sum(x['kind']=='light' and x['fields']['type']=='quad' for x in blocks),
            dependencies=sorted(self.dependencies),generated=sorted(self.generated))
        if self.args.check:
            record['validation']['conversion']='check_passed'
            return record
        self.work.mkdir(parents=True,exist_ok=True)
        stage=self.work/(stem+'.scene.json')
        atomic_json(stage,relocate(scene,self.scene_dir,self.work))
        if has_gltf:
            saved=self.work/(stem+'.saved.scene.json')
            self.invoke(stage,'--save-scene',saved,log_name=stem+'-capture')
            scene=relocate(json.loads(saved.read_text(encoding='utf-8')),self.work,self.scene_dir)
            # Assimp's material fallback differs from the reference defaults;
            # explicitly restore source glTF scalar/alpha semantics by binding ID.
            defs={x['id']:x for x in scene['materials']}
            for index,block in enumerate(model_blocks):
                if block['kind']!='gltf':continue
                data=json.loads((entry.parent/block['fields']['file']).read_text(encoding='utf-8-sig'))
                bindings=scene['models'][index].get('materialBindings',{})
                for source_index,material_id in bindings.items():
                    source_mat=data.get('materials',[])[int(source_index)]
                    pbr=source_mat.get('pbrMetallicRoughness',{})
                    material=defs[material_id]; texture_bindings=material.get('textures',{})
                    material.update(copy.deepcopy(MATERIAL_DEFAULTS))
                    material['baseColor']=pbr.get('baseColorFactor',[1,1,1,1])[:3]
                    material['opacity']=pbr.get('baseColorFactor',[1,1,1,1])[3]
                    material['metallic']=pbr.get('metallicFactor',1)
                    material['roughness']=max(.001,math.sqrt(pbr.get('roughnessFactor',1)))
                    material['emissive']=source_mat.get('emissiveFactor',[0,0,0])
                    material['alphaMode']={'OPAQUE':0,'MASK':2,'BLEND':3}[source_mat.get('alphaMode','OPAQUE')]
                    material['alphaCutoff']=source_mat.get('alphaCutoff',.5)
                    material['transmission']=source_mat.get('extensions',{}).get('KHR_materials_transmission',{}).get('transmissionFactor',0)
                    if pbr.get('metallicRoughnessTexture'):material['metallic']=material['roughness']=1
                    if source_mat.get('emissiveTexture'):material['emissive']=[1,1,1]
                    material['textures']=texture_bindings
            atomic_json(stage,relocate(scene,self.scene_dir,self.work))
        if not self.args.no_validate:
            log=self.invoke(stage,'--validate-scene',log_name=stem+'-validate')
            match=re.search(r'Model loading completed: total (\d+) triangles',log)
            if not match:raise RuntimeError('Missing native import statistics')
            lights=re.search(r'Light encoding completed: total (\d+) lights',log)
            record['runtime']=dict(triangles=int(match[1]),textures=len(re.findall(r'^Texture \d+:',log,re.M)),
                encoded_lights=int(lights[1]) if lights else None)
            expected=sum(reference_triangles(entry.parent/x['fields']['file']) for x in model_blocks)+2*record['quad_lights']
            record['reference_triangles']=expected
            if record['runtime']['triangles']!=expected:
                raise RuntimeError(f'Topology count mismatch: source {expected}, loaded {record["runtime"]["triangles"]}')
            zero_normals=len(re.findall('zero normal Tri id:',log))
            if zero_normals:
                record['runtime']['zero_normal_warnings']=zero_normals
                notes.append(f'Native importer reported {zero_normals} zero-normal triangles in source geometry; see import log.')
            record['validation']['load']='passed'
        atomic_json(output,scene)
        record['validation']['conversion']='passed'
        return record


def main(argv=None):
    parser=argparse.ArgumentParser(description=__doc__,formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--source',type=Path,required=True)
    parser.add_argument('--repo',type=Path,default=REPO)
    parser.add_argument('--exe',type=Path,default=REPO/'build/Release/learnQT.exe')
    parser.add_argument('--qt-bin',type=Path,default=Path('C:/Qt/5.15.2/msvc2019_64/bin'))
    parser.add_argument('--entry',action='append',help='Exact source filename; repeatable')
    parser.add_argument('--check',action='store_true')
    parser.add_argument('--no-validate',action='store_true')
    args=parser.parse_args(argv)
    converter=Converter(args)
    entries=sorted((p for p in converter.assets.iterdir() if p.suffix in ('.scene','.gltf')),key=lambda p:p.name.casefold())
    if args.entry:
        unknown=set(args.entry)-{p.name for p in entries}
        if unknown:parser.error('Unknown entries: '+', '.join(sorted(unknown)))
        entries=[p for p in entries if p.name in args.entry]
    if not args.check:
        import numpy  # noqa: F401
        import PIL  # noqa: F401
        if not args.exe.is_file():parser.error('Build learnQT Release first or supply --exe')
        converter.destination.mkdir(parents=True,exist_ok=True)
        for source,name in [(args.source/'LICENSE','LICENSE'),
                (converter.assets/'Model Credits.txt','Model Credits.txt'),
                (converter.assets/'HDR/Credits.txt','HDR Credits.txt')]:
            shutil.copy2(source,converter.destination/name)
    manifest_path=converter.destination/'conversion_manifest.json'
    previous=json.loads(manifest_path.read_text(encoding='utf-8')) if manifest_path.is_file() and args.entry else {}
    records={x['source']:x for x in previous.get('scenes',[])}
    failures=0
    for i,entry in enumerate(entries,1):
        print(f'[{i}/{len(entries)}] {entry.name}',flush=True)
        try:
            records[entry.name]=converter.convert(entry)
        except Exception as error:
            failures+=1
            records[entry.name]=dict(source=entry.name,validation=dict(conversion='failed'),error=str(error))
            print(f'  FAILED: {error}',flush=True)
        if not args.check:
            manifest=dict(format_version=1,source_collection='GLSL-PathTracer-master',
                reproduction='tools/convert_glslpt_scenes.py --source <local source directory>',
                scenes=sorted(records.values(),key=lambda x:x['source'].casefold()))
            atomic_json(manifest_path,manifest)
    print(f'{len(entries)-failures}/{len(entries)} entries '+('checked' if args.check else 'converted')+'.',flush=True)
    return 1 if failures else 0


if __name__=='__main__':
    sys.exit(main())
