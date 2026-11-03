"""Audit and list the complete offline runtime resource closure (standard library only)."""
import argparse
import json
from pathlib import Path
import re
import shlex
import struct
import subprocess

IMAGE_TYPES = {'.png', '.jpg', '.jpeg', '.bmp', '.tga', '.dds', '.hdr'}
RESOURCE_ATTRIBUTES = {'file', 'normalMap', 'heightMap', 'particles', 'vertex', 'fragment', 'geometry',
                       'texture0', 'texture1', 'texture2', 'texture3'}


def entrypoints(root):
    return sorted([*root.glob('scenes/*.xml'), root/'experiments/pantheon/scene.xml',
                   root/'experiments/pantheon/portal-scene.xml',
                   *root.glob('experiments/pantheon/views/*.xml')])


def resource_closure(root):
    import xml.etree.ElementTree as ET
    root = root.resolve()
    resources, pending, errors = set(), [], []

    def add(path, source):
        path = path.resolve()
        if not path.is_relative_to(root):
            errors.append(f'{source}: resource leaves repository: {path}')
        elif not path.is_file():
            errors.append(f'{source}: missing {path.relative_to(root).as_posix()}')
        elif path not in resources:
            # Windows accepts wrong filename case; Linux does not.
            actual = path
            for parent in reversed(path.relative_to(root).parents):
                folder = root/parent
                component = path.relative_to(folder).parts[0]
                if component not in {p.name for p in folder.iterdir()}:
                    errors.append(f'{source}: filename case mismatch: {path.relative_to(root)}')
                    break
            resources.add(actual)
            pending.append(actual)

    def material_path(value, folder, source):
        value = value.strip().strip('"').replace('\\', '/')
        relative = Path(value)
        # Match the importer's portable, model-local and resource-root fallbacks.
        candidates = [folder/relative, folder/'textures'/relative.name, folder/relative.name, root/relative]
        for candidate in candidates:
            if candidate.is_file() and candidate.resolve().is_relative_to(root):
                add(candidate, source)
                return
        errors.append(f'{source}: missing material resource {value}')

    for scene in entrypoints(root):
        add(scene, 'scene entrypoint')
        if not scene.is_file():
            continue
        try:
            for element in ET.parse(scene).getroot().iter():
                for key, value in element.attrib.items():
                    if key in RESOURCE_ATTRIBUTES and value and value != 'none':
                        add(root/Path(value.replace('\\', '/')), scene.relative_to(root).as_posix())
        except ET.ParseError as error:
            errors.append(f'{scene}: {error}')
    for shader in root.glob('shaders/*'):
        if shader.suffix in {'.vs', '.fs', '.gs', '.glsl'}:
            add(shader, 'shader library')
    for resource in ['fonts/arial.ttf', 'textures/sky/kloofendal_48d_partly_cloudy_puresky_2k.hdr',
                     'LICENSE', 'models/dragon/readme.txt',
                     'experiments/pantheon/meshes/fur-bear-source-LICENSE.txt']:
        add(root/resource, 'runtime/asset notice')
    while pending:
        path = pending.pop()
        source = path.relative_to(root).as_posix()
        if path.suffix.lower() == '.obj':
            with path.open(encoding='utf-8-sig', errors='replace') as stream:
                for line in stream:
                    if line.lstrip().startswith('mtllib '):
                        value = line.strip()[7:].strip()
                        if (path.parent/value).is_file():
                            material_path(value, path.parent, source)
                        else:
                            for name in shlex.split(value, posix=False):
                                material_path(name, path.parent, source)
        elif path.suffix.lower() == '.mtl':
            for line in path.read_text(encoding='utf-8-sig', errors='replace').splitlines():
                words = shlex.split(line, comments=True, posix=False)
                if not words or words[0].lower() not in {
                    'map_kd', 'map_ka', 'map_ks', 'map_ns', 'map_d', 'map_bump', 'bump', 'norm', 'disp', 'decal'}:
                    continue
                # A path may contain spaces; options may precede it.
                for start in range(1, len(words)):
                    value = ' '.join(words[start:]).strip('"').replace('\\', '/')
                    name = Path(value)
                    if any(p.is_file() for p in [path.parent/name, path.parent/'textures'/name.name,
                                                path.parent/name.name, root/name]):
                        material_path(value, path.parent, source)
                        break
                else:
                    errors.append(f'{source}: unresolved texture: {line}')
        elif path.suffix.lower() == '.fbx':
            # Native Assimp loading below verifies actual references; retain all
            # companion images because FBX may encode those paths in binary form.
            for image in [*path.parent.glob('*'), *(path.parent/'textures').rglob('*')]:
                if image.is_file() and image.suffix.lower() in IMAGE_TYPES:
                    add(image, source)
        elif path.suffix.lower() in {'.gltf', '.glb'}:
            if path.suffix.lower() == '.glb':
                with path.open('rb') as stream:
                    header = stream.read(20)
                    if len(header) != 20 or header[:4] != b'glTF':
                        errors.append(f'{source}: invalid GLB header')
                        continue
                    size, kind = struct.unpack('<II', header[12:20])
                    if kind != 0x4E4F534A:
                        errors.append(f'{source}: GLB has no JSON chunk')
                        continue
                    model = json.loads(stream.read(size))
            else:
                model = json.loads(path.read_text(encoding='utf8'))
            for record in [*model.get('images', []), *model.get('buffers', [])]:
                uri = record.get('uri', '')
                if uri and not uri.startswith('data:'):
                    add(path.parent/uri, source)
        elif path.suffix in {'.vs', '.fs', '.gs', '.glsl'}:
            for include in re.findall(r'^\s*#include\s+"([^"]+)"', path.read_text(encoding='utf8'), re.M):
                add(path.parent/include, source)
    return sorted(p.relative_to(root).as_posix() for p in resources), sorted(set(errors))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument('--write-manifest', action='store_true')
    parser.add_argument('--tracked-list', type=Path,
                        help='JSON list from git ls-files, for a source archive without .git')
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    root = args.root.resolve()
    files, errors = resource_closure(root)
    if args.tracked_list:
        tracked = set(json.loads(args.tracked_list.read_text(encoding='utf8')))
    elif (root/'.git').exists():
        result = subprocess.run(['git', 'ls-files', '-z'], cwd=root, capture_output=True, check=True)
        tracked = {x.decode('utf8') for x in result.stdout.split(b'\0') if x}
    else:
        tracked = None
    if tracked is not None:
        errors.extend(f'Runtime file is not in Git: {name}' for name in files if name not in tracked)
    generated = '# Generated by tools/audit_assets.py --write-manifest; do not edit by hand.\n'
    generated += 'set(VIEWER_RESOURCES\n'+''.join(f'    "{name}"\n' for name in files)+')\n'
    manifest = root/'cmake/runtime-resources.cmake'
    if args.write_manifest and not errors:
        manifest.write_text(generated, encoding='utf8')
    elif manifest.is_file() and manifest.read_text(encoding='utf8') != generated:
        errors.append('Runtime resource manifest is stale; run tools/audit_assets.py --write-manifest')
    result = {'entrypoints': len(entrypoints(root)), 'runtime_files': len(files),
              'runtime_bytes': sum((root/name).stat().st_size for name in files),
              'models': [name for name in files if Path(name).suffix.lower() in {'.obj', '.fbx', '.glb', '.gltf'}],
              'files': files, 'errors': sorted(set(errors))}
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(result, indent=2)+'\n', encoding='utf8')
    print(f'{result["entrypoints"]} scene entrypoints, {len(result["models"])} models, '
          f'{len(files)} runtime resources, {result["runtime_bytes"]/1048576:.1f} MiB')
    for error in result['errors']:
        print('ERROR:', error)
    return bool(result['errors'])


if __name__ == '__main__':
    raise SystemExit(main())
