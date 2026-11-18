#!/usr/bin/env python3
"""Verifica conflitos de registradores e de binding nos shaders, sem GPU.

O que checa:
  1) HLSL: dois recursos com o mesmo tipo e numero de registrador no mesmo shader (ex.: t15 usado por
     IblBrdfLut e Instances). O D3D11/D3D12 rejeita isso, e o GLSL gerado recebe um binding repetido.
     Cada variante com #define (ex.: PBR_LEGACY_OBJECT) e checada separadamente.
  2) GLSL (assets/shaders/glsl/*.glsl): dois layout(binding = N) iguais no mesmo arquivo.
  3) Limite de UBO: no pbr, VS + PS nao podem passar de 8 cbuffers (limite do Vulkan, ver pbr.hlsl).

Uso: python3 tools/shaders/check_bindings.py   (sai com 1 se achar conflito)
"""
import os, re, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
SHADERS = os.path.join(ROOT, "assets", "shaders")
INCLUDE = re.compile(r'^\s*#include\s+"([^"]+)"', re.M)
DIRECTIVE = re.compile(r'^\s*#\s*(ifdef|ifndef|else|endif|define)\b\s*(\w+)?', re.M)
DECL = re.compile(r'(\w+)\s*(?:\[[^\]]*\])?\s*:\s*register\(\s*([a-zA-Z])(\d+)\s*(?:,[^)]*)?\)')
# Recursos que o pipeline monta: tipo 'cbuffer' e 'Texture'/'StructuredBuffer' etc. so importam pelo registrador.

# Variantes do pbr.hlsl: a principal e a de objeto legado (usada no emissivo e no transparente).
VARIANTS = {
    "pbr.hlsl": [set(), {"PBR_LEGACY_OBJECT"}],
    "pbr_object.hlsl": [set()],
}
# Arquivos de passe com entradas diferentes; checamos todos os .hlsl de primeiro nivel.
UBO_LIMIT = 8


def flatten(path, seen):
    path = os.path.normpath(path)
    if path in seen:
        return ""
    seen.add(path)
    text = open(path, encoding="utf-8").read()
    base = os.path.dirname(path)
    return INCLUDE.sub(lambda m: flatten(os.path.join(base, m.group(1)), seen), text)


def active_lines(text, defines):
    """Mantem so as linhas de ramos ativos (#ifdef/#ifndef/#else/#endif), com 'defines' definidos."""
    out, stack = [], []  # stack: [parent_active, cond, taken]
    active = True
    for line in text.splitlines():
        m = DIRECTIVE.match(line)
        if m:
            kind, name = m.group(1), m.group(2)
            if kind in ("ifdef", "ifndef"):
                cond = (name in defines) if kind == "ifdef" else (name not in defines)
                stack.append([active, cond, cond])
                active = active and cond
                continue
            if kind == "else" and stack:
                parent, cond, _ = stack[-1]
                stack[-1][2] = True
                active = parent and not cond
                continue
            if kind == "endif" and stack:
                parent, _, _ = stack.pop()
                active = parent
                continue
            if kind == "define" and active and name:
                defines.add(name)
                continue
        if active:
            out.append(line)
    return "\n".join(out)


def check_hlsl(errors):
    names = sorted(f for f in os.listdir(SHADERS) if f.endswith(".hlsl"))
    for name in names:
        variants = VARIANTS.get(name, [set()])
        for defs in variants:
            src = active_lines(flatten(os.path.join(SHADERS, name), set()), set(defs))
            seen = {}
            for m in DECL.finditer(src):
                key = (m.group(2).lower(), int(m.group(3)))
                seen.setdefault(key, []).append(m.group(1))
            tag = name + ("" if not defs else " [" + ",".join(sorted(defs)) + "]")
            for key, owners in sorted(seen.items()):
                if len(owners) > 1:
                    errors.append(f"HLSL {tag}: registrador {key[0]}{key[1]} usado por {', '.join(owners)}")
            if name == "pbr.hlsl":
                ubo = {}
                for m in re.finditer(r'cbuffer\s+(\w+)\s*:\s*register\(\s*b(\d+)', src):
                    ubo.setdefault(m.group(1), int(m.group(2)))
                if len(ubo) > UBO_LIMIT:
                    errors.append(f"HLSL {tag}: {len(ubo)} cbuffers (limite {UBO_LIMIT})")


def check_glsl(errors):
    """No OpenGL, UBO, SSBO e textura/imagem tem namespaces de binding separados: so comparamos dentro do tipo."""
    d = os.path.join(SHADERS, "glsl")
    if not os.path.isdir(d):
        return
    decl = re.compile(r'layout\s*\(\s*binding\s*=\s*(\d+)[^)]*\)\s*(uniform|buffer)\s+(\w+)?\s*(\w+)?')
    for name in sorted(os.listdir(d)):
        if not name.endswith(".glsl"):
            continue
        text = open(os.path.join(d, name), encoding="utf-8").read()
        seen = {}
        for m in decl.finditer(text):
            b, kw, a, c = int(m.group(1)), m.group(2), m.group(3) or "", m.group(4) or ""
            if kw == "buffer":
                kind = "ssbo"
            elif a.startswith("sampler") or a.startswith("image") or c.startswith("sampler") or c.startswith("image"):
                kind = "textura"
            else:
                kind = "ubo"
            seen.setdefault((kind, b), []).append(c or a)
        for (kind, b), owners in sorted(seen.items()):
            if len(owners) > 1:
                errors.append(f"GLSL {name}: {kind} binding {b} usado por {', '.join(owners)}")


def main():
    errors = []
    check_hlsl(errors)
    check_glsl(errors)
    if errors:
        print("Conflitos de binding:")
        for e in errors:
            print("  " + e)
        return 1
    print("Bindings sem conflito (HLSL por variante e GLSL por arquivo).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
