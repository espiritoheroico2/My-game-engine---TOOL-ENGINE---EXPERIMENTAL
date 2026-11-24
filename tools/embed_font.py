#!/usr/bin/env python3
"""Gera apps/editor/embedded_font.hpp a partir de assets/fonts/Xmind-Regular.ttf.

A fonte (OFL, ver assets/fonts/OFL-Xmind.txt) fica dentro do executavel: o editor abre mesmo sem a pasta assets/fonts.
Rode de novo sempre que a fonte mudar: python3 tools/embed_font.py
"""
import pathlib
root = pathlib.Path(__file__).resolve().parent.parent
src = root / "assets/fonts/Xmind-Regular.ttf"
dst = root / "apps/editor/embedded_font.hpp"
data = src.read_bytes()
import base64
b64 = base64.b64encode(data).decode("ascii")
chunks = [b64[i:i + 100] for i in range(0, len(b64), 100)]
body = "\n".join(f'    "{c}"' for c in chunks)
text = (
    "// GERADO por tools/embed_font.py a partir de assets/fonts/Xmind-Regular.ttf (licenca OFL). Nao edite a mao.\n"
    "// Base64 para caber menor no codigo-fonte; o carregamento decodifica (TextAtlasSet::load).\n"
    "#pragma once\n"
    "#include <cstddef>\n\n"
    "namespace editor::embedded {\n"
    f"inline constexpr std::size_t kXmindFontSize = {len(data)};\n"
    "inline constexpr char kXmindFontBase64[] =\n" + body + ";\n"
    "}  // namespace editor::embedded\n"
)
dst.write_text(text, encoding="utf-8")
print("wrote", dst, len(data), "bytes")
