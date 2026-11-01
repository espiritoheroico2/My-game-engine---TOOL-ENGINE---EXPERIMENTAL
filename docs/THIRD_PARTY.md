# APIs e bibliotecas de terceiros

A ToolEngine é **livre e gratuita**. Só os jogos feitos nela (e software de pesquisa) podem gerar receita; a engine não é o produto.
Todas as dependências abaixo permitem uso comercial sem royalties. **Confira a licença exata no repositório antes de cada release** (as versões mudam).

| Componente | Dono / autor | Repositório | Licença | Uso na engine |
|---|---|---|---|---|
| Newtoon Physics | João Vitor Dias da Silva (dono da engine) | github.com/espiritoheroico2/Newtoon-Physics-for-engine | MIT | Física 3D (`engine/physics`) — completada e modificada aqui, com autorização do autor |
| SDL3 | Sam Lantinga / libsdl.org | github.com/libsdl-org/SDL | zlib | Janela, entrada, gamepad (XInput/WGI no Windows) |
| Dear ImGui (docking) | Omar Cornut | github.com/ocornut/imgui | MIT | Interface do editor |
| imnodes | Johann Muszynski | github.com/Nelarius/imnodes | MIT | Editor de nós do Narrative Controller |
| miniaudio | David Reid | github.com/mackron/miniaudio | MIT-0 / domínio público | Áudio |
| DiligentCore | Diligent Graphics | github.com/DiligentGraphics/DiligentCore | Apache-2.0 | Vulkan / OpenGL / D3D12 (v2.5.6) |
| stb (se usado) | Sean Barrett | github.com/nothings/stb | MIT / domínio público | — (não usado hoje) |

Dependências **do DiligentCore** (SPIRV-Cross, glslang, SPIRV-Tools, volk, GLEW, etc.) seguem as licenças dentro de `third_party/DiligentCore/ThirdParty/*/LICENSE`.

## Candidatos estudados (ainda NÃO incluídos)
| Componente | Dono | Repositório | Licença | Situação |
|---|---|---|---|---|
| RTX Neural Texture Compression | NVIDIA | github.com/NVIDIA-RTX/RTXNTC | "NVIDIA RTX SDKs LICENSE" (verificar termos antes de redistribuir) | Ver `docs/NTC.md` |
| AMD FidelityFX / FSR | AMD | gpuopen.com | MIT (SDK) | Ver `docs/FSR.md` |
| QuickJS-ng v0.17.0 | Fabrice Bellard / Charlie Gordon / quickjs-ng | github.com/quickjs-ng/quickjs | MIT | Runtime dos scripts ToolObject (`third_party/quickjs-ng`, vendorizado; `TOOL_WITH_QUICKJS`) |

## Regras
1. Só entra dependência com licença que permita uso comercial e redistribuição em binário.
2. Código copiado vai com o cabeçalho de licença original; este arquivo é atualizado no mesmo commit.
3. Nada de GPL/AGPL no núcleo (mods e jogos precisam poder ser fechados).
