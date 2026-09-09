#!/usr/bin/env python3
"""Audit converted topology/resources and merge the opt-in Qt render results.

python tools/validate_glslpt_collection.py --source D:/program/GLSL-PathTracer-master
build/Release/learnQT.exe --scene resources/scenes/glslpt_ajax.scene.json --imported-scene-regression build/glslpt-render
python tools/validate_glslpt_collection.py --source D:/program/GLSL-PathTracer-master --render-results build/glslpt-render/results.json

The first and third commands verify the collection and update its manifest.
"""
import argparse
import hashlib
import html
import json
import os
from pathlib import Path
import re
from urllib.parse import unquote

from convert_glslpt_scenes import REPO, atomic_json, parse_scene, reference_triangles, source_metadata


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source',type=Path,required=True)
    parser.add_argument('--render-results',type=Path)
    args=parser.parse_args()
    root=REPO.resolve();assets=(args.source/'assets').resolve()
    bundle=root/'resources/imported/glslpt'
    path=bundle/'conversion_manifest.json'
    manifest=json.loads(path.read_text(encoding='utf-8'))
    source_entries={p.name for p in assets.iterdir() if p.suffix in ('.scene','.gltf')}
    assert {x['source'] for x in manifest['scenes']}==source_entries
    assert len(manifest['scenes'])==55
    copied=set();topology={};failures=[]
    results=json.loads(args.render_results.read_text(encoding='utf-8')) if args.render_results else {}
    renders={x['file']:x for x in results.get('results',[]) if 'file' in x and not x.get('revisit')}
    for record in manifest['scenes']:
        try:
            record['notes']=[note for note in record['notes'] if not note.startswith('GPU capture is dark under the preserved source/default HDR')]
            assert record['validation']['conversion']=='passed'
            assert record['validation']['load']=='passed'
            source=assets/record['source'];output=root/record['output']
            if source.suffix=='.scene':
                blocks=parse_scene(source)
                expected=sum(reference_triangles(assets/x['fields']['file'],topology)
                             for x in blocks if x['kind'] in ('mesh','gltf'))
                expected+=sum(2 for x in blocks if x['kind']=='light' and x['fields']['type']=='quad')
            else:
                blocks=[]
                expected=reference_triangles(source,topology)
            record.update(source_metadata(source,blocks))
            assert expected==record['runtime']['triangles'],f'Triangle count {expected} != {record["runtime"]["triangles"]}'
            record['reference_triangles']=expected
            doc=json.loads(output.read_text(encoding='utf-8'))
            assert doc['version']==1 and not doc.get('fitModelOnImport',False)
            assert len(doc['models'])==record['models']
            assert len(doc['lights'])==record['analytic_lights']
            assert len(doc['textures'])==record['runtime']['textures']
            record['runtime']['materials']=len(doc['materials'])
            record['runtime']['models']=len(doc['models'])
            if source.suffix=='.scene' and not any(x['kind']=='gltf' for x in blocks):
                assert len(doc['materials'])==1+record['authored_materials']+record['quad_lights']
            resources=[]
            if doc.get('hdr'):resources.append((output.parent/doc['hdr']).resolve())
            for group in ('models','textures'):
                for item in doc[group]:
                    if item.get('source'):resources.append((output.parent/item['source']).resolve())
                    resources.extend((output.parent/p).resolve() for p in (item.get('dependencies') or {}).values())
            for model in doc['models']:
                resource=(output.parent/model['source']).resolve()
                if resource.suffix=='.gltf':
                    data=json.loads(resource.read_text(encoding='utf-8'))
                    bindings=model['materialBindings']
                    materials={x['id']:x for x in doc['materials']}
                    used={p['material'] for mesh in data['meshes'] for p in mesh['primitives'] if 'material' in p}
                    assert used <= {int(i) for i in bindings}
                    for index in used:
                        source_material=data['materials'][index]
                        target_material=materials[bindings[str(index)]]
                        pbr=source_material.get('pbrMetallicRoughness',{})
                        for field,slots in [(pbr.get('baseColorTexture'),('baseColor',)),
                                (pbr.get('metallicRoughnessTexture'),('metallic','roughness')),
                                (source_material.get('normalTexture'),('normal',)),
                                (source_material.get('emissiveTexture'),('emissive',))]:
                            if field:assert all(k in target_material['textures'] for k in slots)
                    for group in ('images','buffers'):
                        for item in data.get(group,[]):
                            uri=item.get('uri','')
                            if uri and not uri.startswith('data:'):
                                alias=(model.get('dependencies') or {}).get(uri)
                                resources.append((output.parent/alias).resolve() if alias else (resource.parent/unquote(uri)).resolve())
            for resource in resources:
                resource.relative_to(bundle)
                assert resource.is_file(),resource
            copied.update(record['dependencies'])
            stem=output.name.removesuffix('.scene.json')
            log=(root/'build/glslpt-conversion'/(stem+'-validate.log')).read_text(encoding='utf-8',errors='replace')
            zero_normals=len(re.findall('zero normal Tri id:',log))
            if zero_normals:
                record['runtime']['zero_normal_warnings']=zero_normals
                note=f'Native importer reported {zero_normals} zero-normal triangles in source geometry; see import log.'
                if note not in record['notes']:record['notes'].append(note)
            record['validation']['topology']='passed'
            record['validation']['object_counts']='passed'
            record['validation']['material_texture_bindings']='passed'
            record['validation']['project_local_resources']='passed'
            if args.render_results:
                result=renders[output.name]
                assert result['passed'],result
                assert result['triangles']==expected and result['textures']==record['runtime']['textures']
                assert result['materials']==len(doc['materials'])
                assert result['encoded_lights']==record['runtime']['encoded_lights']
                png=(args.render_results.parent/result['png']).resolve();assert png.is_file()
                record['validation']['render']='passed'
                record['validation']['screenshot']=png.relative_to(root).as_posix()
                record['validation']['render_metrics']={k:result[k] for k in ('width','height','mean','stddev')}
                for key in ('roundtrip','portable_moved','external_reference_rejected'):
                    if key in result:record['validation'][key]='passed' if result[key] else 'failed'
                if output.name=='glslpt_Camera_01_4k_gltf.scene.json':
                    assert result['gold_pixels']>result['width']*result['height']*.0005
                    assert result['brown_pixels']>result['width']*result['height']*.002
                    record['validation']['camera_color']='passed'
                    record['validation']['render_metrics'].update(gold_pixels=result['gold_pixels'],brown_pixels=result['brown_pixels'])
        except Exception as error:
            record['validation']['audit']='failed'
            failures.append(f'{record["source"]}: {error}')
        else:
            record['validation']['audit']='passed'
    total=0
    for relative in sorted(copied):
        original=assets/relative;copy=bundle/'assets'/relative
        assert copy.is_file(),copy
        with original.open('rb') as f:expected=hashlib.file_digest(f,'sha256').digest()
        with copy.open('rb') as f:actual=hashlib.file_digest(f,'sha256').digest()
        assert expected==actual,f'Copied bytes differ: {relative}'
        total+=copy.stat().st_size
    manifest['resources']=dict(source_files=len(copied),source_bytes=total,
        bundle_bytes=sum(p.stat().st_size for p in bundle.rglob('*') if p.is_file()))
    manifest['audit']=dict(topology_and_project_paths='passed' if not failures else 'failed',
        original_resource_hashes='passed',failures=failures)
    if args.render_results:
        manifest['audit']['scene_list_entries']=results['scene_list_entries']
        manifest['audit']['revisit']='passed' if any(x.get('revisit') and x.get('passed') for x in results['results']) else 'failed'
        manifest['audit']['render']='passed' if not failures else 'failed'
        manifest['audit']['capture']={k:results[k] for k in ('capture_presentations','full_frame_accumulation_limit')}
    atomic_json(path,manifest)
    if args.render_results and not failures:
        cards=[]
        for record in manifest['scenes']:
            validation=record['validation'];runtime=record['runtime']
            png=os.path.relpath(root/validation['screenshot'],args.render_results.parent).replace('\\','/')
            notes=''.join('<li>'+html.escape(note)+'</li>' for note in record['notes'])
            cards.append(f'<article><h2>{html.escape(record["source"])}</h2><a href="{html.escape(png,quote=True)}">'
                f'<img loading="lazy" src="{html.escape(png,quote=True)}"></a>'
                f'<p>加载 / 资源 / GPU：通过 · 三角形 {runtime["triangles"]:,} · '
                f'材质 {runtime["materials"]} · 纹理 {runtime["textures"]} · 灯光 {runtime["encoded_lights"]}</p>'
                f'<details><summary>转换差异与限制</summary><ul>{notes}</ul></details></article>')
        gallery='''<!doctype html><html lang="zh-CN"><meta charset="utf-8"><title>GLSLPT 场景转换验收</title>
<style>body{margin:32px;background:#171a20;color:#eef1f7;font:15px system-ui}main{display:grid;grid-template-columns:repeat(auto-fit,minmax(360px,1fr));gap:20px}article{background:#252a33;padding:18px;border-radius:12px}h2{font-size:17px}img{width:100%;height:280px;object-fit:contain;background:#000}li{margin:8px 0}summary{cursor:pointer}p{line-height:1.6}</style>
<h1>GLSLPT 场景转换验收：55 / 55</h1><p>43 个源场景 + 12 个独立 glTF。实际 GPU 截图；完整帧累积上限 64，等待 96 次画面提交。
独立 glTF 保留约定的默认 HDR 和相机。Camera 额外验证金属边缘与皮带的暖色像素，GPU 数值测试覆盖贴图缩放后的颜色通道与透明度。</p><main>'''+''.join(cards)+'</main></html>'
        (args.render_results.parent/'gallery.html').write_text(gallery,encoding='utf-8')
    print(json.dumps(manifest['resources'],indent=2));print(f'{55-len(failures)}/55 scene audits passed')
    for failure in failures:print(failure)
    return bool(failures)


if __name__=='__main__':raise SystemExit(main())
