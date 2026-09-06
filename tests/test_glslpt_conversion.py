"""Numerical tests for the asset converter. Run with numpy and Pillow installed."""
import importlib.util
import json
from contextlib import contextmanager
import math
from pathlib import Path
import shutil
import uuid
import unittest
from types import SimpleNamespace

SPEC = importlib.util.spec_from_file_location('converter', Path(__file__).resolve().parents[1]/'tools/convert_glslpt_scenes.py')
c = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(c)


@contextmanager
def temporary_directory():
    # Python's mode-0700 tempfile ACL is incompatible with Windows sandbox
    # impersonation. Use an ordinary inheriting directory under build instead.
    root=(c.REPO/'build/glslpt-unit-tmp').resolve()
    path=root/uuid.uuid4().hex
    path.mkdir(parents=True)
    try:
        yield path
    finally:
        path.resolve().relative_to(root)
        shutil.rmtree(path)


class ConversionTests(unittest.TestCase):
    def test_transform_translation_rotation_and_nonuniform_scale(self):
        m = c.transform(dict(position='10 20 30', scale='2 3 4', rotation=f'0 0 {math.sqrt(.5)} {math.sqrt(.5)}'))
        p = [sum(m[i*4+j]*[1,0,0,1][j] for j in range(4)) for i in range(3)]
        for actual, expected in zip(p, [10,22,30]): self.assertAlmostEqual(actual,expected)
        explicit = '1 0 0 12 0 2 0 34 0 0 3 56 0 0 0 1'
        self.assertEqual(c.transform({'matrix':explicit,'position':'99 99 99'}),c.numbers(explicit))
        with self.assertRaises(ValueError): c.transform({'scale':'0 1 1'})

    def test_camera_rays_agree_at_same_aspect(self):
        for w,h in [(1280,720),(700,900),(800,800)]:
            camera=c.convert_camera(dict(position='0 0 5',lookat='0 0 0',fov='60'),[w,h])
            self.assertAlmostEqual(math.tan(math.radians(camera['fov'])/2)*w/h, math.tan(math.pi/6))
        camera=c.convert_camera(dict(matrix='1 0 0 3 0 1 0 4 0 0 -1 5 0 0 0 1',fov='60'),[1280,720])
        self.assertEqual(camera['position'],[3,4,5]);self.assertEqual(camera['target'],[3,4,4])

    def test_quad_has_correct_corners_area_and_winding(self):
        text=c.quad_obj(dict(position='1 2 3',v1='4 2 3',v2='1 2 8'))
        vertices=[c.numbers(line[2:]) for line in text.splitlines() if line.startswith('v ')]
        self.assertEqual(vertices,[[1,2,3],[4,2,3],[4,2,8],[1,2,8]])
        self.assertIn('vn 0 -1 0',text)
        self.assertEqual(sum(line.startswith('f ') for line in text.splitlines()),2)

    def test_sun_matches_incident_irradiance(self):
        light=c.distant_light(dict(position='0 390 439',emission='1 2 3'))
        self.assertAlmostEqual(sum(x*x for x in light['direction']),1)
        self.assertLess(light['direction'][1],0)  # Native sun direction is incoming propagation.
        self.assertLess(light['direction'][2],0)
        for actual,expected in zip(light['radiance'],[1,2,3]):
            self.assertAlmostEqual(actual*math.pi*math.sin(light['radius'])**2,expected)

    def test_alpha_and_medium_boundary_are_not_confused(self):
        self.assertEqual(c.convert_material({'alphamode':'blend'})['alphaMode'],3)
        boundary=c.convert_material({'alphamode':'blend','opacity':'0','mediumtype':'scatter','mediumdensity':'2'})
        self.assertEqual(boundary['alphaMode'],1); self.assertEqual(boundary['mediumDensity'],2)
        self.assertEqual(c.convert_material({'alphamode':'mask'})['alphaMode'],2)
        self.assertEqual(c.convert_material({})['roughness'],.5)
        self.assertEqual(c.convert_material({'roughness':'0'})['roughness'],.001)

    def test_disabled_light_and_comments_are_ignored(self):
        with temporary_directory() as tmp:
            path=Path(tmp)/'a.scene'
            path.write_text('renderer\n{\n resolution 1280 720\n}\ncamera\n{\n fov 60\n}\n#light\n{\n type sphere\n}\nlight\n{\n type quad\n #radius 99\n}\n')
            blocks=c.parse_scene(path)
            self.assertEqual([x['kind'] for x in blocks],['renderer','camera','light'])
            self.assertEqual(blocks[-1]['fields'],{'type':'quad'})

    def test_image_channels_gamma_and_alpha(self):
        from PIL import Image
        with temporary_directory() as tmp:
            source=Path(tmp)/'source.png';out=Path(tmp)/'out.png'
            image=Image.new('RGBA',(3,1));image.putdata([(17,128,64,23),(0,0,255,128),(255,255,0,255)]);image.save(source)
            c.image_variant(source,out,'roughness')
            with Image.open(out) as converted:
                pixels=[converted.getpixel((x,0)) for x in range(3)]
            self.assertEqual(pixels,[(17,64,64,23),(0,1,255,128),(255,255,0,255)])
            c.image_variant(source,out,'gamma22')
            with Image.open(out) as converted:
                value=converted.getpixel((0,0))
            srgb=value[1]/255
            linear=((srgb+.055)/1.055)**2.4 if srgb>.04045 else srgb/12.92
            self.assertAlmostEqual(linear,(128/255)**2.2,delta=.004)
            self.assertEqual(value[3],23)

    def test_hdr_roundtrip_and_baked_energy(self):
        import numpy as np
        with temporary_directory() as tmp:
            path=Path(tmp)/'a.hdr'
            for width in (4,8,257):
                pixels=np.random.default_rng(5).random((7,width,3),dtype=np.float32)*100
                pixels[0,0]=0
                c.write_hdr(path,pixels*1.5)
                result=c.read_hdr(path)
                np.testing.assert_allclose(result,pixels*1.5,rtol=.015,atol=1)
                self.assertLess(abs(result.sum()/(pixels.sum()*1.5)-1),.01)
                np.testing.assert_array_equal(result[0,0],[0,0,0])

    def test_relocation_includes_dependencies(self):
        with temporary_directory() as tmp:
            a,b=Path(tmp)/'a',Path(tmp)/'b'
            doc=dict(hdr='sky.hdr',models=[dict(source='m.obj',dependencies={'old':'m.mtl'})],textures=[])
            result=c.relocate(doc,a,b)
            self.assertEqual((b/result['hdr']).resolve(),(a/'sky.hdr').resolve())
            self.assertEqual((b/result['models'][0]['dependencies']['old']).resolve(),(a/'m.mtl').resolve())
            self.assertEqual(doc['hdr'],'sky.hdr')

    def test_topology_counts_polygons_and_gltf_instances(self):
        with temporary_directory() as tmp:
            mesh=tmp/'mesh.obj'
            mesh.write_text('f 1 2 3 4\nf 1 2 3 # triangle\n')
            self.assertEqual(c.reference_triangles(mesh),3)
            gltf=tmp/'mesh.gltf'
            gltf.write_text(json.dumps(dict(accessors=[dict(count=6)],
                meshes=[dict(primitives=[dict(indices=0,attributes={'POSITION':0})])],
                nodes=[dict(mesh=0,children=[1]),dict(mesh=0)],scenes=[dict(nodes=[0])],scene=0)))
            self.assertEqual(c.reference_triangles(gltf),4)

    def test_check_does_not_write_or_launch_native_process(self):
        with temporary_directory() as tmp:
            source=tmp/'source';assets=source/'assets';assets.mkdir(parents=True)
            (assets/'mesh.obj').write_text('f 1 2 3\n')
            scene=assets/'test.scene'
            scene.write_text('renderer\n{\n}\ncamera\n{\nposition 0 0 5\nlookat 0 0 0\nfov 60\n}\nmesh\n{\nfile mesh.obj\n}\nlight\n{\ntype sphere\nposition 0 2 0\nradius .5\nemission 1 1 1\n}\n')
            before=sorted(str(p.relative_to(tmp)) for p in tmp.rglob('*'))
            converter=c.Converter(SimpleNamespace(source=source,repo=tmp/'repo',exe=tmp/'absent.exe',
                qt_bin=tmp/'absent-qt',check=True,no_validate=False))
            record=converter.convert(scene)
            self.assertEqual(record['validation']['conversion'],'check_passed')
            self.assertEqual(before,sorted(str(p.relative_to(tmp)) for p in tmp.rglob('*')))

    def test_gltf_aliases_survive_packaged_model_directory(self):
        from PIL import Image
        with temporary_directory() as tmp:
            source=tmp/'source';assets=source/'assets';assets.mkdir(parents=True)
            (assets/'mesh.bin').write_bytes(b'geometry')
            Image.new('RGBA',(2,2),(17,128,64,23)).save(assets/'color.png')
            original=dict(buffers=[dict(uri='mesh.bin',byteLength=8)],
                images=[dict(uri='color.png')],textures=[dict(source=0)],
                materials=[dict(pbrMetallicRoughness=dict(baseColorTexture=dict(index=0),
                    metallicRoughnessTexture=dict(index=0),metallicFactor=.2,roughnessFactor=.3))])
            gltf=assets/'mesh.gltf';gltf.write_text(json.dumps(original))
            converter=c.Converter(SimpleNamespace(source=source,repo=tmp/'repo',exe=tmp/'absent.exe',
                qt_bin=tmp/'absent-qt',check=False,no_validate=True))
            derived,aliases=converter.gltf(gltf)
            data=json.loads(derived.read_text())
            for item in data['images']+data['buffers']:
                uri=item['uri']
                self.assertNotIn('..',Path(uri).parts)
                self.assertIn(uri,aliases)
                self.assertTrue((converter.scene_dir/aliases[uri]).is_file())
                # Assimp can normalize assets/<hash>/<URI> without collapsing
                # out of the model directory and losing the dependency alias.
                packaged=Path('assets/hash')/uri
                self.assertEqual(packaged.relative_to('assets/hash').as_posix(),uri)
            pbr=data['materials'][0]['pbrMetallicRoughness']
            self.assertNotEqual(pbr['baseColorTexture']['index'],pbr['metallicRoughnessTexture']['index'])
            self.assertEqual(pbr['metallicFactor'],1)
            self.assertEqual(pbr['roughnessFactor'],1)
            self.assertEqual(json.loads(gltf.read_text()),original)


if __name__=='__main__':unittest.main()
