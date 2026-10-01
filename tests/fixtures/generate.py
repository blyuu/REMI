"""Regenerate tiny self-contained glTF fixtures for AssetPipelineTests."""
import base64
import json
import pathlib
import struct
import zlib

ROOT = pathlib.Path(__file__).parent


def png_chunk(kind: bytes, payload: bytes) -> bytes:
    return struct.pack('>I', len(payload)) + kind + payload + struct.pack('>I', zlib.crc32(kind + payload))


png = (b'\x89PNG\r\n\x1a\n'
       + png_chunk(b'IHDR', struct.pack('>IIBBBBB', 1, 1, 8, 6, 0, 0, 0))
       + png_chunk(b'IDAT', zlib.compress(b'\x00\xff\x80\x20\xff'))
       + png_chunk(b'IEND', b''))
(ROOT / 'sample.png').write_bytes(png)
(ROOT / 'materials').mkdir(exist_ok=True)
(ROOT / 'materials' / 'static_triangle_0.remimat').write_text(json.dumps({
    'version': 1, 'shadingModel': 'Toon', 'tint': [0.6, 0.8, 1.0],
    'metallic': 0.0, 'roughness': 0.7, 'emissive': 0.0,
    'baseColorTexture': '../sample.png'
}, indent=2) + '\n', encoding='utf-8')


def make(skinned: bool) -> None:
    binary = bytearray()
    views = []
    accessors = []

    def add(values, fmt, components, component_type, type_name):
        while len(binary) % 4:
            binary.append(0)
        offset = len(binary)
        binary.extend(struct.pack('<' + fmt * len(values), *values))
        views.append({'buffer': 0, 'byteOffset': offset, 'byteLength': len(binary) - offset})
        accessor = {'bufferView': len(views) - 1, 'componentType': component_type,
                    'count': len(values) // components, 'type': type_name}
        accessors.append(accessor)
        return len(accessors) - 1

    pos = add([-0.5, 0, 0, 0.5, 0, 0, 0, 1, 0], 'f', 3, 5126, 'VEC3')
    normal = add([0, 0, -1] * 3, 'f', 3, 5126, 'VEC3')
    uv = add([0, 0, 1, 0, 0.5, 1], 'f', 2, 5126, 'VEC2')
    indices = add([0, 1, 2], 'H', 1, 5123, 'SCALAR')
    attributes = {'POSITION': pos, 'NORMAL': normal, 'TEXCOORD_0': uv}
    gltf = {'asset': {'version': '2.0'}, 'buffers': [], 'bufferViews': views, 'accessors': accessors,
            'images': [{'uri': 'sample.png'}], 'textures': [{'source': 0}],
            'materials': [{'pbrMetallicRoughness': {'baseColorTexture': {'index': 0},
                           'metallicFactor': 0, 'roughnessFactor': 0.7}}],
            'meshes': [{'primitives': [{'attributes': attributes, 'indices': indices, 'material': 0}]}],
            'nodes': [{'mesh': 0}], 'scenes': [{'nodes': [0]}], 'scene': 0}
    if skinned:
        gltf['images'] = [{'uri': 'data:image/png;base64,' + base64.b64encode(png).decode()}]
        joints = add([0, 0, 0, 0] * 3, 'B', 4, 5121, 'VEC4')
        weights = add([1, 0, 0, 0] * 3, 'f', 4, 5126, 'VEC4')
        bind = add([1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1], 'f', 16, 5126, 'MAT4')
        times = add([0, 1], 'f', 1, 5126, 'SCALAR')
        translation = add([0, 0, 0, 0, 0.5, 0], 'f', 3, 5126, 'VEC3')
        attributes.update({'JOINTS_0': joints, 'WEIGHTS_0': weights})
        gltf['nodes'] = [{'mesh': 0, 'skin': 0}, {'name': 'joint'}]
        gltf['scenes'] = [{'nodes': [0, 1]}]
        gltf['skins'] = [{'joints': [1], 'inverseBindMatrices': bind}]
        gltf['animations'] = [{'name': 'move', 'samplers': [{'input': times, 'output': translation}],
                               'channels': [{'sampler': 0, 'target': {'node': 1, 'path': 'translation'}}]}]
    gltf['buffers'] = [{'uri': 'data:application/octet-stream;base64,' + base64.b64encode(binary).decode(),
                        'byteLength': len(binary)}]
    (ROOT / ('skinned_triangle.gltf' if skinned else 'static_triangle.gltf')).write_text(
        json.dumps(gltf, separators=(',', ':')), encoding='utf-8')
    if not skinned:
        glb_json = dict(gltf)
        glb_json['buffers'] = [{'byteLength': len(binary)}]
        json_chunk = json.dumps(glb_json, separators=(',', ':')).encode('utf-8')
        json_chunk += b' ' * (-len(json_chunk) % 4)
        bin_chunk = bytes(binary) + b'\0' * (-len(binary) % 4)
        total = 12 + 8 + len(json_chunk) + 8 + len(bin_chunk)
        glb = (struct.pack('<III', 0x46546c67, 2, total)
               + struct.pack('<I4s', len(json_chunk), b'JSON') + json_chunk
               + struct.pack('<I4s', len(bin_chunk), b'BIN\0') + bin_chunk)
        (ROOT / 'static_triangle.glb').write_bytes(glb)


make(False)
make(True)
