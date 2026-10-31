#!/usr/bin/env python3
"""Gera os arquivos de exemplo em assets/samples/ (usados pelos testes e copiados para o projeto na 1a execucao):
   cubo.glb (GLB, no com translacao+escala), piramide.gltf (glTF com buffer base64), caixa.fbx (FBX binario v7400
   com arrays comprimidos), bip.wav, tom.mp3 (precisa de ffmpeg; se nao houver, so o wav).
   Uso: python3 tools/gen_samples.py"""
import base64, json, math, os, struct, subprocess, wave, zlib

OUT = os.path.join(os.path.dirname(__file__), '..', 'assets', 'samples')
os.makedirs(OUT, exist_ok=True)

def box(h=0.5):
    """24 vertices (4 por face), normais por face, 36 indices anti-horarios."""
    faces = [((0,0,1),(1,0,0),(0,1,0)), ((0,0,-1),(-1,0,0),(0,1,0)), ((1,0,0),(0,0,-1),(0,1,0)),
             ((-1,0,0),(0,0,1),(0,1,0)), ((0,1,0),(1,0,0),(0,0,-1)), ((0,-1,0),(1,0,0),(0,0,1))]
    pos, nor, idx = [], [], []
    for n, u, v in faces:
        b = len(pos)
        for su, sv in ((-1,-1),(1,-1),(1,1),(-1,1)):
            pos.append(tuple((n[i] + u[i]*su + v[i]*sv) * h for i in range(3))); nor.append(n)
        idx += [b, b+1, b+2, b, b+2, b+3]
    return pos, nor, idx

def glb_buffer(pos, nor, idx):
    pb = b''.join(struct.pack('<3f', *p) for p in pos); nb = b''.join(struct.pack('<3f', *n) for n in nor)
    ib = b''.join(struct.pack('<H', i) for i in idx)
    ib += b'\0' * (-len(ib) % 4)
    return pb + nb + ib, len(pb), len(nb), len(idx)

def gltf_json(pos, nor, idx, color, node, uri=None):
    data, pl, nl, ni = glb_buffer(pos, nor, idx)
    mn = [min(p[i] for p in pos) for i in range(3)]; mx = [max(p[i] for p in pos) for i in range(3)]
    j = {"asset": {"version": "2.0"}, "scene": 0, "scenes": [{"nodes": [0]}], "nodes": [node],
         "meshes": [{"name": "Mesh", "primitives": [{"attributes": {"POSITION": 0, "NORMAL": 1}, "indices": 2, "material": 0}]}],
         "materials": [{"name": "Mat", "pbrMetallicRoughness": {"baseColorFactor": color + [1.0], "metallicFactor": 0.1, "roughnessFactor": 0.6}}],
         "buffers": [{"byteLength": len(data)}],
         "bufferViews": [{"buffer": 0, "byteOffset": 0, "byteLength": pl, "target": 34962}, {"buffer": 0, "byteOffset": pl, "byteLength": nl, "target": 34962},
                         {"buffer": 0, "byteOffset": pl + nl, "byteLength": ni * 2, "target": 34963}],
         "accessors": [{"bufferView": 0, "componentType": 5126, "count": len(pos), "type": "VEC3", "min": mn, "max": mx},
                       {"bufferView": 1, "componentType": 5126, "count": len(nor), "type": "VEC3"},
                       {"bufferView": 2, "componentType": 5123, "count": ni, "type": "SCALAR"}]}
    if uri: j["buffers"][0]["uri"] = uri(data)
    return j, data

# --- cubo.glb: caixa 1x1x1 com no transladado e escalado (testa a "assadura" das transformacoes)
pos, nor, idx = box(0.5)
j, data = gltf_json(pos, nor, idx, [0.2, 0.55, 0.9], {"mesh": 0, "translation": [0, 0.5, 0], "scale": [1, 1.5, 1], "name": "Cubo"})
js = json.dumps(j, separators=(',', ':')).encode(); js += b' ' * (-len(js) % 4)
glb = struct.pack('<4sII', b'glTF', 2, 12 + 8 + len(js) + 8 + len(data)) + struct.pack('<I4s', len(js), b'JSON') + js + struct.pack('<I4s', len(data), b'BIN\0') + data
open(os.path.join(OUT, 'cubo.glb'), 'wb').write(glb)

# --- piramide.gltf: 4 faces + base, buffer embutido em base64
apex = (0, 1, 0); b0, b1, b2, b3 = (-0.6, 0, -0.6), (0.6, 0, -0.6), (0.6, 0, 0.6), (-0.6, 0, 0.6)
tris = [(b0, apex, b1), (b1, apex, b2), (b2, apex, b3), (b3, apex, b0), (b0, b1, b2), (b0, b2, b3)]
pos, nor, idx = [], [], []
for a, b, c in tris:
    u = [b[i]-a[i] for i in range(3)]; v = [c[i]-a[i] for i in range(3)]
    n = (u[1]*v[2]-u[2]*v[1], u[2]*v[0]-u[0]*v[2], u[0]*v[1]-u[1]*v[0]); l = math.sqrt(sum(x*x for x in n)) or 1
    n = tuple(x/l for x in n); k = len(pos); pos += [a, b, c]; nor += [n, n, n]; idx += [k, k+1, k+2]
# (b0,b1,b2) e (b0,b2,b3) ja saem com a normal para baixo: so ajusta as normais dos vertices da base
for t in (4, 5):
    for q in (0, 1, 2): nor[t*3+q] = (0, -1, 0)
j, _ = gltf_json(pos, nor, idx, [0.9, 0.6, 0.15], {"mesh": 0, "name": "Piramide"}, uri=lambda d: 'data:application/octet-stream;base64,' + base64.b64encode(d).decode())
open(os.path.join(OUT, 'piramide.gltf'), 'w').write(json.dumps(j, indent=1))

# --- caixa.fbx: FBX binario 7400 (arrays 'd' e 'i' comprimidos com zlib). Unidade em centimetros e Y-up.
def prop(v):
    if isinstance(v, bool): return b'C' + struct.pack('<B', v)
    if isinstance(v, int): return b'L' + struct.pack('<q', v)
    if isinstance(v, float): return b'D' + struct.pack('<d', v)
    if isinstance(v, str): s = v.encode(); return b'S' + struct.pack('<I', len(s)) + s
    kind, arr = v
    raw = b''.join(struct.pack('<d' if kind == 'd' else '<i', x) for x in arr); comp = zlib.compress(raw)
    return kind.encode() + struct.pack('<III', len(arr), 1, len(comp)) + comp
def node(name, props=(), kids=()):
    # o offset dos nos e absoluto no arquivo: montamos com um deslocamento base informado depois
    return (name, list(props), list(kids))
def ser(n, base):
    name, props, kids = n; nb = name.encode(); pb = b''.join(prop(p) for p in props)
    head_len = 13 + len(nb)
    body = b''; off = base + head_len + len(pb)
    for k in kids:
        kb = ser(k, off); body += kb; off += len(kb)
    if kids: body += b'\0' * 13; off += 13
    end = base + head_len + len(pb) + len(body)
    return struct.pack('<IIIB', end, len(props), len(pb), len(nb)) + nb + pb + body
def P(*a): return node('P', a)
pos, nor, idx = box(50.0)  # 100 cm de lado
polys = []
for q in range(6):  # FBX usa poligonos: 4 cantos, ultimo indice negativo (~i)
    b = q*4; polys += [b, b+1, b+2, -(b+3)-1]
vert = [c for p in pos for c in p]; norm = [c for q in range(6) for _ in range(4) for c in nor[q*4]]
# normais por canto de poligono, na ordem de PolygonVertexIndex (iguais as dos vertices)
geo = node('Geometry', [1001, 'Caixa\x00\x01Geometry', 'Mesh'], [
    node('Vertices', [('d', vert)]), node('PolygonVertexIndex', [('i', polys)]),
    node('LayerElementNormal', [0], [node('MappingInformationType', ['ByPolygonVertex']), node('ReferenceInformationType', ['Direct']), node('Normals', [('d', norm)])])])
mdl = node('Model', [1002, 'Caixa\x00\x01Model', 'Mesh'], [node('Properties70', [], [P('Lcl Translation', 'Lcl Translation', '', 'A', 200.0, 0.0, 0.0)])])
gs = node('GlobalSettings', [], [node('Properties70', [], [P('UpAxis', 'int', 'Integer', '', 1), P('UpAxisSign', 'int', 'Integer', '', 1), P('UnitScaleFactor', 'double', 'Number', '', 1.0)])])
top = [node('FBXHeaderExtension', [], [node('FBXVersion', [7400])]), gs, node('Objects', [], [geo, mdl]),
       node('Connections', [], [node('C', ['OO', 1001, 1002]), node('C', ['OO', 1002, 0])])]
hdr = b'Kaydara FBX Binary  \x00\x1a\x00' + struct.pack('<I', 7400)
body = b''; off = len(hdr)
for t in top: b = ser(t, off); body += b; off += len(b)
open(os.path.join(OUT, 'caixa.fbx'), 'wb').write(hdr + body + b'\0' * 13 + b'\0' * 160)

# --- audio: bip.wav (440 Hz, 0.4 s, mono 16-bit) e tom.mp3 (660 Hz, 0.5 s, estereo)
with wave.open(os.path.join(OUT, 'bip.wav'), 'wb') as w:
    w.setnchannels(1); w.setsampwidth(2); w.setframerate(22050)
    w.writeframes(b''.join(struct.pack('<h', int(9000 * math.sin(2 * math.pi * 440 * i / 22050) * min(1, (8820 - i) / 2000))) for i in range(8820)))
try:
    subprocess.run(['ffmpeg', '-y', '-loglevel', 'error', '-f', 'lavfi', '-i', 'sine=frequency=660:duration=0.5', '-ac', '2', '-ar', '44100', '-b:a', '64k', os.path.join(OUT, 'tom.mp3')], check=True)
except Exception as e:
    print('sem ffmpeg: tom.mp3 nao gerado', e)
print('amostras em', os.path.abspath(OUT))
