#!/usr/bin/env python3
"""Gera os shaders GLSL 430 (OpenGL) a partir dos HLSL de assets/shaders, offline.

Pipeline (o mesmo caminho que os drivers de shader usam, sem depender do tradutor interno do Diligent):
  1) achata os #include (cada arquivo entra uma vez)
  2) glslangValidator -D -V: HLSL -> SPIR-V (-D = entrada HLSL)
  3) spirv-cross --version 430: SPIR-V -> GLSL 430 (core)
Saida: assets/shaders/glsl/<nome>.<estagio>.430.glsl (o Diligent carrega com ShaderLang::GLSL, entrada "main").

Uso: python3 tools/shaders/build_glsl.py [--check]
  --check so verifica se os GLSL commitados estao em dia (sai com 1 se algum falta ou esta antigo).
Requer: glslangValidator e spirv-cross no PATH (apt install glslang-tools spirv-cross).
"""
import os, re, subprocess, sys, tempfile, hashlib

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
SHADERS = os.path.join(ROOT, "assets", "shaders")
OUT = os.path.join(SHADERS, "glsl")

# (arquivo HLSL, estagio, entry HLSL, nome de saida)
JOBS = [
    ("pbr.hlsl", "vert", "VSMain", "pbr.vs"),
    ("pbr.hlsl", "frag", "PSMain", "pbr.ps"),
    ("pbr.hlsl", "frag", "EmissionPS", "pbr.emi"),
    ("shadow.hlsl", "vert", "VSMain", "shadow.vs"),
    ("shadow.hlsl", "frag", "PSMain", "shadow.ps"),
    ("shadow_point.hlsl", "vert", "VSMain", "shadowpoint.vs"),
    ("shadow_point.hlsl", "frag", "PSMain", "shadowpoint.ps"),
    ("prepass.hlsl", "vert", "VSMain", "prepass.vs"),
    ("prepass.hlsl", "frag", "PSMain", "prepass.ps"),
    ("ssao.hlsl", "vert", "VSMain", "ssao.vs"),
    ("ssao.hlsl", "frag", "SsaoMain", "ssao.ps"),
    ("ssao.hlsl", "frag", "SsaoBlur", "ssaoblur.ps"),
    ("bloom.hlsl", "vert", "VSMain", "bloom.vs"),
    ("bloom.hlsl", "frag", "BloomThreshold", "bloom.thr"),
    ("bloom.hlsl", "frag", "BloomBlur", "bloom.blur"),
    ("bloom.hlsl", "frag", "BloomComposite", "bloom.comp"),
]

# compute da Light Prism (cluster): saem em assets/shaders/lightprism/glsl/<arquivo>.<entry>.430.glsl
COMPUTE_JOBS = [
    ("lightprism/Cluster/ClusterLightPrep.compute.hlsl", "CSMain"),
    ("lightprism/Cluster/ClusterCullCount.compute.hlsl", "CSMain"),
    ("lightprism/Cluster/ClusterScan.compute.hlsl", "ScanLocal"),
    ("lightprism/Cluster/ClusterScan.compute.hlsl", "ScanGroupSums"),
    ("lightprism/Cluster/ClusterCullFill.compute.hlsl", "CSMain"),
]

INCLUDE = re.compile(r'^\s*#include\s+"([^"]+)"', re.M)


def flatten(path, seen=None):
    """Junta o arquivo e seus includes (relativos ao arquivo que inclui), cada um uma vez."""
    seen = set() if seen is None else seen
    path = os.path.normpath(path)
    if path in seen:
        return ""
    seen.add(path)
    text = open(path, encoding="utf-8").read()
    base = os.path.dirname(path)

    def repl(m):
        return flatten(os.path.join(base, m.group(1)), seen)

    return INCLUDE.sub(repl, text)


# O SPIRV-Cross junta textura + sampler fantasma e cria o nome "SPIRV_Cross_Combined<Nome>SPIRV_Cross_DummySampler".
# O Diligent (e o nosso RHI) procura a textura pelo nome do HLSL ("AlbedoMap"), entao voltamos ao nome original.
COMBINED = re.compile(r"SPIRV_Cross_Combined(\w+?)SPIRV_Cross_DummySampler")


def fix_names(path):
    text = open(path, encoding="utf-8").read()
    open(path, "w", encoding="utf-8").write(COMBINED.sub(r"\1", text))


# Vertex shaders: o Diligent inverte o Y do clip ao traduzir HLSL para GL (ver diligent_device.cpp, FrontCounterClockwise
# do GLES). O GLSL gerado direto pelo SPIRV-Cross nao tem essa inversao, entao a aplicamos aqui: o mesmo resultado.
FULLSCREEN_VS = {"ssao.vs", "bloom.vs"}
MAIN = re.compile(r"void main\(\)")


def flip_clip_y(path):
    text = open(path, encoding="utf-8").read()
    if "gl_Position" not in text or "void main_inner()" in text:
        return
    text = MAIN.sub("void main_inner()", text, count=1)
    text += "\nvoid main() { main_inner(); gl_Position.y = -gl_Position.y; }\n"
    open(path, "w", encoding="utf-8").write(text)


def run(cmd):
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        raise RuntimeError(" ".join(cmd) + "\n" + r.stdout + r.stderr)
    return r.stdout + r.stderr


def build(hlsl, stage, entry, name, tmp):
    src = os.path.join(tmp, name + ".hlsl")
    with open(src, "w", encoding="utf-8") as f:
        f.write(flatten(os.path.join(SHADERS, hlsl)))
    spv = os.path.join(tmp, name + ".spv")
    run(["glslangValidator", "-D", "-V", "-S", stage, "-e", entry, "-o", spv, src])
    glsl = os.path.join(OUT, f"{name}.430.glsl")
    # saida: <nome>.430.glsl, com <nome> = pbr.vs, pbr.ps, bloom.thr, ... (como os arquivos ja existentes)
    run(["spirv-cross", "--version", "430", "--no-es", "--output", glsl, spv])
    fix_names(glsl)
    # guarda: um entry errado vira um main vazio sem erro (glslang aceita). Estagio fragment precisa de saida.
    body = open(glsl, encoding="utf-8").read()
    if stage == "frag" and "out " not in body and "layout(location" not in body:
        raise RuntimeError(f"{name}: entry '{entry}' nao gerou saida (entry errado?)")
    # triangulo de tela cheia (ssao.vs, bloom.vs) ja sai em coordenadas de tela: nao inverter o Y dele
    if stage == "vert" and name not in FULLSCREEN_VS: flip_clip_y(glsl)
    return glsl


def build_compute(hlsl, entry, tmp):
    stem = os.path.basename(hlsl).replace(".compute.hlsl", "")
    src = os.path.join(tmp, f"{stem}.{entry}.hlsl")
    with open(src, "w", encoding="utf-8") as f:
        f.write(flatten(os.path.join(SHADERS, hlsl)))
    spv = os.path.join(tmp, f"{stem}.{entry}.spv")
    run(["glslangValidator", "-D", "-V", "-S", "comp", "-e", entry, "-o", spv, src])
    out = os.path.join(SHADERS, "lightprism", "glsl", f"{stem}.{entry}.430.glsl")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    run(["spirv-cross", "--version", "430", "--no-es", "--output", out, spv])
    fix_names(out)
    return out


def main():
    check = "--check" in sys.argv
    os.makedirs(OUT, exist_ok=True)
    bad = []
    with tempfile.TemporaryDirectory() as tmp:
        for hlsl, stage, entry, name in JOBS:
            out = os.path.join(OUT, f"{name}.430.glsl")
            if check:
                if not os.path.exists(out):
                    bad.append(out)
                continue
            build(hlsl, stage, entry, name, tmp)
            print("ok", os.path.relpath(out, ROOT))
        if not check:
            for hlsl, entry in COMPUTE_JOBS:
                print("ok", os.path.relpath(build_compute(hlsl, entry, tmp), ROOT))
    if check and bad:
        print("GLSL faltando/desatualizado:", *bad, sep="\n  ")
        sys.exit(1)


if __name__ == "__main__":
    try:
        main()
    except RuntimeError as e:
        print(e)
        sys.exit(2)
