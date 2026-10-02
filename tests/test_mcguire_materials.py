"""Mineways atlas alpha adaptation; run with numpy and Pillow installed."""
import copy
import importlib.util
from pathlib import Path
import unittest
from unittest.mock import patch
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('mcguire', ROOT / 'tools/convert_mcguire_scenes.py')
c = importlib.util.module_from_spec(spec)
spec.loader.exec_module(c)


class MinewaysAlphaTests(unittest.TestCase):
    def test_duplicate_alpha_and_distinct_masks(self):
        # Use an ordinary repository directory for Windows sandbox ACLs.
        directory = ROOT / 'build' / 'mcguire-material-tests'
        directory.mkdir(parents=True, exist_ok=True)
        Image.new('RGBA', (2, 2), (76, 115, 220, 136)).save(directory / 'color.png')
        Image.new('RGB', (2, 2), (136, 136, 136)).save(directory / 'duplicate.png')
        Image.new('RGB', (2, 2), (200, 200, 200)).save(directory / 'distinct.png')
        doc = {
            'textures': [{'id': x, 'source': x + '.png'} for x in ('color', 'duplicate', 'distinct')],
            'objects': [{'material': 'water', 'name': 'Stationary_Water'},
                        {'material': 'leaves', 'name': 'Leaves'},
                        {'material': 'other', 'name': 'Other'}],
            'materials': [{'id': x, 'textures': {'baseColor': 'color', 'opacity': mask}}
                          for x, mask in [('water', 'duplicate'), ('leaves', 'duplicate'), ('other', 'distinct')]],
        }
        independent = copy.deepcopy(doc)
        c.adapt(doc, 'rungholt', directory)
        water, leaves, other = doc['materials']
        self.assertEqual(water['alphaMode'], 3)
        self.assertNotIn('opacity', water['textures'])
        self.assertEqual(leaves['alphaMode'], 2)
        self.assertNotIn('opacity', leaves['textures'])
        self.assertEqual(other['textures']['opacity'], 'distinct')
        c.adapt(independent, 'unrelated', directory)
        self.assertEqual(independent['materials'][0]['textures']['opacity'], 'duplicate')

    def test_existing_rungholt_presets_and_real_atlas(self):
        for path in (ROOT / 'resources/scenes').glob('mcguire_rungholt_*.scene.json'):
            doc = c.load(path)
            materials = {m['id']: m for m in doc['materials']}
            water = [materials[o['material']] for o in doc['objects'] if 'water' in o['name'].lower()]
            if path.name == 'mcguire_rungholt_rungholt.scene.json':
                self.assertTrue(water, path.name)
            for material in water:
                self.assertEqual(material['alphaMode'], 3, path.name)
                self.assertNotIn('opacity', material['textures'], path.name)
        atlas = ROOT / 'resources/imported/mcguire/assets/rungholt/rungholt-RGBA.png'
        with Image.open(atlas) as image:
            alpha = image.convert('RGBA').getpixel((1908, 900))[3] / 255
        self.assertGreater(alpha, .5)
        self.assertLess(alpha * alpha, .5)


class PowerPlantMaterialTests(unittest.TestCase):
    def test_legacy_dissolve_conversion_is_scoped_and_preserves_fractional_opacity(self):
        directory = ROOT / 'build' / 'mcguire-material-tests' / 'legacy-opacity'
        original = 'newmtl solid\nTr 1.000000\nKd 0.3 0.4 0.5\nnewmtl glass\nTr 0.247059\n'
        with patch.object(c, 'ASSETS', directory / 'assets'), patch.object(c, 'DERIVED', directory / 'derived'):
            for key in ('powerplant', 'unrelated'):
                source = c.ASSETS / key / 'powerplant.mtl'
                source.parent.mkdir(parents=True, exist_ok=True)
                source.write_text(original, encoding='utf-8')
                aliases = c.prepare_materials(key)
                converted = Path(aliases[str(source.resolve())]).read_text(encoding='utf-8')
                self.assertEqual(source.read_text(encoding='utf-8'), original)
                if key == 'powerplant':
                    self.assertIn('d 1.000000', converted)
                    self.assertIn('d 0.247059', converted)
                    self.assertNotIn('Tr ', converted)
                    self.assertIn('Kd 0.3 0.4 0.5', converted)
                else:
                    self.assertEqual(converted, original)

    def test_powerplant_structures_remain_visible_to_path_tracing(self):
        doc = c.load(ROOT / 'resources/scenes/mcguire_powerplant.scene.json')
        materials = {m['id']: m for m in doc['materials']}
        used = {o['material'] for o in doc['objects']}
        source_opacity = [float(line.split()[1]) for line in
                          (ROOT / 'resources/imported/mcguire/assets/powerplant/powerplant.mtl')
                          .read_text(encoding='utf-8').splitlines() if line.startswith('Tr ')]
        self.assertTrue(used)
        for identity in used:
            material = materials[identity]
            source_index = int(identity.rsplit('/', 1)[1]) - 1
            expected = source_opacity[source_index]
            self.assertGreater(material['opacity'], 0, identity)
            self.assertAlmostEqual(material['opacity'], expected, places=5, msg=identity)
            self.assertEqual(material['alphaMode'], 0 if expected >= .999 else 3, identity)


if __name__ == '__main__':
    unittest.main()
