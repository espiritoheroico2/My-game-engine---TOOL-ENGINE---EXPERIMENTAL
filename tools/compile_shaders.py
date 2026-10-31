#!/usr/bin/env python3
"""Compila HLSL -> SPIR-V -> (GLSL 330 desktop / GLSL ES 300 GLES+WebGL2).

Preferencia: dxc (producao). Se nao existir, usa glslangValidator (frontend HLSL) so para validar/gerar.
  tools/compile_shaders.py [--out out/shaders] [--dxc caminho] [--glslang caminho] [--spirv-cross caminho]
Saidas por shader/estagio: <nome>.<vs|ps>.spv, .330.glsl, .es300.glsl
"""
import argparse, os, shutil, subprocess, sys
from pathlib import Path

STAGES = {"VSMain": ("vs", "vert", "vs_6_0"), "PSMain": ("ps", "frag", "ps_6_0")}

def find(name, given):
    return given or shutil.which(name)

def run(cmd):
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        sys.stderr.write(f"FALHOU: {' '.join(map(str, cmd))}\n{r.stdout}{r.stderr}\n")
        sys.exit(1)

def main():
    ap = argparse.ArgumentParser()
    root = Path(__file__).resolve().parent.parent
    ap.add_argument("--src", default=str(root / "assets/shaders"))
    ap.add_argument("--out", default=str(root / "out/shaders"))
    ap.add_argument("--dxc"); ap.add_argument("--glslang"); ap.add_argument("--spirv-cross")
    a = ap.parse_args()
    dxc, gls, sc = find("dxc", a.dxc), find("glslangValidator", a.glslang), find("spirv-cross", a.spirv_cross)
    if not (dxc or gls):
        sys.exit("Precisa de dxc ou glslangValidator no PATH (ou use --dxc/--glslang).")
    out = Path(a.out); out.mkdir(parents=True, exist_ok=True)
    count = 0
    for src in sorted(Path(a.src).glob("*.hlsl")):
        for entry, (suffix, gstage, profile) in STAGES.items():
            if entry not in src.read_text():
                continue
            spv = out / f"{src.stem}.{suffix}.spv"
            if dxc:
                run([dxc, "-spirv", "-T", profile, "-E", entry, "-fspv-target-env=vulkan1.1", "-I", a.src, str(src), "-Fo", str(spv)])
            else:
                run([gls, "-D", "-V", "-S", gstage, "-e", entry, "--sep", "--auto-map-bindings", "--auto-map-locations",
                     "-I" + a.src, str(src), "-o", str(spv)])
            if sc:
                run([sc, str(spv), "--version", "330", "--no-es", "--output", str(out / f"{src.stem}.{suffix}.330.glsl")])
                run([sc, str(spv), "--version", "300", "--es", "--output", str(out / f"{src.stem}.{suffix}.es300.glsl")])
                # GLSL 430 para o backend OpenGL do Diligent (contexto 4.3 core): clip-space D3D->GL e Y invertido.
                run([sc, str(spv), "--version", "430", "--no-es", "--fixup-clipspace", "--flip-vert-y", "--output", str(out / f"{src.stem}.{suffix}.430.glsl")])
            count += 1
            print(f"ok  {src.name}:{entry} -> {spv.name}")
    print(f"{count} estagios compilados em {out}")

if __name__ == "__main__":
    main()
