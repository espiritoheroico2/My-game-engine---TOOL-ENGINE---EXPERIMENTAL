# Sistema de texturas e materiais — plano (v0.6)

Hoje: o material tem só cor, rugosidade e metálico (`MeshRenderer`). O renderer não amostra texturas.

## Recursos que um jogo costuma precisar (checklist)
Albedo/Base color · **Normal map** · Rugosidade/Metálico (ORM empacotado) · Oclusão ambiente (AO) · Emissivo · Opacidade/Alpha cutout ·
Tiling/Offset por material · Mipmaps + filtragem anisotrópica · Sprite/atlas (2D e partículas) · Skybox/HDRI · Lightmaps (futuro) · Decals (futuro).

## Arquitetura proposta (simples e modular)
1. **Material (.tmat, JSON)** por arquivo, referenciado pelo `MeshRenderer`: `{ albedo, normal, orm, emissive, tiling, tint, shader }`. O inspector edita e salva o `.tmat`.
2. **TextureLibrary** (já existe `lib` para malhas/sons): carrega PNG/JPG (stb_image), gera mipmaps, cache por caminho, formato sRGB p/ albedo e linear p/ normal/ORM.
3. **GPU primeiro:** texturas entram no `pbr.hlsl` (+ `.glsl` mantido à mão): `Texture2D` albedo/normal/orm; normal map via TBN (a malha precisa de tangentes — importer calcula com MikkTSpace-like).
4. **CPU (SoftRenderer):** amostragem bilinear opcional, normal map no sombreamento. Mantém o fallback funcionando.
5. **Compressão:** BC7/BC5 offline (basisu/ktx2) no export; o botão "Compress as NTC" (NVIDIA) fica para depois (`NTC.md`: viável, mas só em GPUs com Tensor Cores).
6. **Shader por objeto:** campo `shader` no `.tmat` aponta para um par HLSL/GLSL customizado (validado/compilado pelo Diligent); vazio = PBR padrão.

## Ordem de implementação sugerida
(1) TextureLibrary + albedo na GPU → (2) normal map + tangentes → (3) ORM/AO/emissivo → (4) `.tmat` + inspector → (5) tiling/alpha → (6) CPU renderer → (7) shader por material.
**Status:** não implementado nesta versão — exige mudar vértices (UV/tangente), shaders e o RHI; preferi não entregar pela metade.
