# 18. Revisao da janela Settings do editor

Auditoria somente leitura (nenhum codigo foi editado ou compilado). Foco: cada opcao da janela Configuracoes
existe, onde o valor e gravado e se alguem le esse valor de forma que mude o resultado na tela.

Snapshot: 27/11/2026, aprox. 17:08. Os arquivos `apps/editor/editor_state.hpp` e `apps/editor/project.hpp` estavam
sendo alterados durante a auditoria (campos de FSR mudaram entre duas leituras). Refazer a checagem da linha
"Sharpness" antes de mexer nela.

## Legenda

- FUNCIONA: gravado e lido por um caminho de renderizacao/simulacao com efeito visivel.
- PARCIAL: lido so em um caminho (so GPU, so CPU/software, so Play, ou so sob uma condicao).
- SEM EFEITO: nenhum leitor que mude o resultado.
- A CONFIRMAR: nao deu para determinar com a leitura feita.

Arquivos de referencia: `apps/editor/settings_form.hpp` (linhas genericas e dicas), `apps/editor/settings_panel.hpp`
(controles fixos), `apps/editor/settings_model.hpp` (abas e limites), `apps/editor/settings_changes.hpp` (pedidos e
ApplyChanges), `apps/editor/main.cpp` (funcoes `add*Rows` = montagem; `applyFormRequest` e `struct` de setters =
aplicacao; enum `FormId` linha ~1739).

## Resumo

- Controles visiveis: cerca de 139 (contando cada slider, caixa, combo e o R/G/B/A separados; faces da skybox e
  presets de gravidade contados como grupo quando identicos).
- SEM EFEITO: 4 (Idle rendering, Light color A, Skybox solid color A, Sharpness do FSR, que hoje nem compila/persiste; ver nota).
- PARCIAL: 8 (Physics enabled, Sharp pixels, Enable ray tracing x2, Ray-traced AO x2, RT AO rays, RT AO radius).
- Campos de FSR sem controle na tela: `fsrQuality`, `fsrCustomScale`, `fsr4`, `frameGen` (existem em `project.hpp`,
  nenhum controle na janela; `fsr4` e `frameGen` so guardam a escolha, segundo os comentarios).

## Tabela de opcoes

Campo: caminho do dado. `L.` = `sceneInfo.render.lighting`; `R.` = `sceneInfo.render`; `P.` = `sceneInfo.physics`;
`ed.` = Editor (`editor_state.hpp`).

### Presets de configuracao (topo da aba Appearance, id 50-53)

| Opcao | Categoria atual | Campo | Consumido em | Status |
|---|---|---|---|---|
| Edit config (abre seletor de .json) | Appearance (topo) | arquivo | `openConfigPicker`, `applyPendingConfigPick` (main.cpp) | FUNCIONA |
| Config Name | Appearance (topo) | `gCfgForm.name` | `cfgPathForName`, `config_preset` | FUNCIONA |
| Save | Appearance (topo) | `R.` inteiro para arquivo | `config_preset::save` (main.cpp ~2192) | FUNCIONA |
| Delete (2 cliques) | Appearance (topo) | arquivo | `config_preset::remove` | FUNCIONA |

### Appearance (aba 0, controles fixos)

| Opcao | Categoria atual | Campo | Consumido em | Status |
|---|---|---|---|---|
| Lista de temas (Palette) | Appearance | `palette::activeIndex` | `setPalette`; cores do editor | FUNCIONA |
| User themes (arquivo de tema) | Appearance | `ed.theme` / `ThemeFile` | `setThemeFile` | FUNCIONA |
| Base colors (5 cores, RGB) | Appearance | `palette::custom().base[0..4]` | `setCustomColor`, `deriveTokens` | FUNCIONA |
| Advanced colors (8 tokens, RGB) | Appearance | `palette::custom().token[0..7]` | `setCustomColor` | FUNCIONA |
| Button corner radius | Appearance | `ui_style::buttonRadius()` | widgets SpriteUI (`assets_spriteui.hpp`, `settings_panel.hpp` etc.) | FUNCIONA |
| Window corner radius | Appearance | `ui_style::windowRadius()` | `window_frame.hpp`, `editor_info_spriteui.hpp` | FUNCIONA |
| Interface text size | Appearance | `ui_style::textSetting()` | `refreshScale()` | FUNCIONA |

### API/Graphics (aba 1, controles fixos)

| Opcao | Categoria atual | Campo | Consumido em | Status |
|---|---|---|---|---|
| Graphics API (radio: Software, Vulkan, OpenGL, D3D12, D3D11) | API/Graphics | `ed.apiRequested` | `applyApi` -> `ed.requestApi` -> `editor_state.hpp` (`applyApi`, ~416) | FUNCIONA |
| Game resolution (combo: acompanha janela, 640x360, 720p, 1080p, 1440p, personalizada) | API/Graphics > Quality | `ed.resMode` | `fixedRes` (editor_state ~353), `updateSceneTexture`/`updateGameTexture` (main.cpp ~1476) | FUNCIONA |
| Width / Height (so resolucao personalizada) | API/Graphics > Quality | `ed.customW`, `ed.customH` | `FixedGameSize` | FUNCIONA |
| Render scale (slider) | API/Graphics > Quality | `ed.renderScale` | `updateSceneTexture` (main.cpp ~1476, ~1535) | FUNCIONA |
| Upscaling (software) (combo: Off, Bilinear, Bicubic, Lanczos2) | API/Graphics > Quality | `ed.upscaleMode` | `finishView` (editor_state ~331) | FUNCIONA (ver nota 2) |
| Anti-aliasing (software) (Off, FXAA) | API/Graphics > Quality | `ed.aaMode` | `finishView` (editor_state ~333) | FUNCIONA |
| Sharp pixels when enlarging (caixa) | API/Graphics > Quality | `ed.pixelFilter` | `SDL_SetTextureScaleMode` (main.cpp ~1491, ~1524) | PARCIAL (ver nota 1) |
| Frame rate limit (combo) | API/Graphics > Quality | `ed.fpsMode` | main.cpp ~3192, ~3759 | FUNCIONA (duplicada na Optimizing, id 300) |
| Enable shadows (caixa) | API/Graphics > GPU details | `L.shadows` | `collectLights` (editor_state ~817) | FUNCIONA (duplicada na Lighting, id 740) |
| Shadow quality (combo 512..4096) | API/Graphics > GPU details | `L.shadowMapResolution`, `L.custom = true` | `setShadowQuality`; `makeQuality` (lighting.hpp ~94) | FUNCIONA (ver nota 3) |
| Enable ray tracing (caixa) | API/Graphics > GPU details | `L.rayTracing` | editor_state ~1300 (so caminho CPU) | PARCIAL (CPU) |
| Ray-traced ambient occlusion (CPU) (caixa) | API/Graphics > GPU details | `L.rtAo` | editor_state ~1300-1320 (so CPU) | PARCIAL (CPU) |

Itens de Tags e Project tambem estao nesta aba (ver nota 4):

| Opcao | Categoria atual | Campo | Consumido em | Status |
|---|---|---|---|---|
| Built-in tags (lista, so leitura) | API/Graphics (Tags) | - | `ed_::kBuiltinTags` | FUNCIONA (informativo) |
| Links: Remove link (um por link) | API/Graphics (Tags) | `ed.projSettings.links` | `unlink` + `save` | FUNCIONA |
| your tag (campo de texto) | API/Graphics (Tags) | `gTagsForm.alias` | `kTagLink` | FUNCIONA |
| Link to (combo) | API/Graphics (Tags) | `gTagsForm.target` | `kTagLink` | FUNCIONA |
| Link (botao) | API/Graphics (Tags) | `projSettings.link` | `save` | FUNCIONA |
| Rename (um por tag do projeto) / New name / cancel | API/Graphics (Tags) | `scene.tags()` | `ed.renameTag` | FUNCIONA |
| Editor command (texto) | API/Graphics (Project) | `ed.projSettings.externalEditor` | `extra_ui.hpp` (~21) | FUNCIONA |

### Game (aba 2)

| Opcao | Categoria atual | Campo | Consumido em | Status |
|---|---|---|---|---|
| Show grid in the Scene window | Game > Grid and gizmos | `ed.showGridScene` | main.cpp ~1292, `vo::buildGrid` | FUNCIONA |
| Show grid in the Game window | Game > Grid and gizmos | `ed.showGridGame` | editor_state ~1024 | FUNCIONA |
| Show gizmos (cameras, lights, sounds) | Game > Grid and gizmos | `ed.showGizmos` | main.cpp ~1294 | FUNCIONA |
| Split screen (combo: lado a lado, em cima/embaixo) | Game > Split | `sceneInfo.splitVertical` | main.cpp ~1580 (divisao da tela) | FUNCIONA (duplicada no Inspector, inspector_panel ~945) |
| Fill each half (no black bars) | Game > Split | `sceneInfo.splitFill` | main.cpp ~1583 (so com 2 cameras) | FUNCIONA |
| Skybox (6 images) (caixa) | Game > Skybox | `sceneInfo.sky.enabled` | editor_state ~970, ~1016 | FUNCIONA |
| Lados 1-6 (combo de imagem, ids 110-115) | Game > Skybox | `sceneInfo.sky.face[0..5]` | `assignAsset`, `lib.texture` (editor_state ~974) | FUNCIONA |
| Solid color R, G, B (ids 120-122) | Game > Skybox | `sceneInfo.sky.solid[0..2]` | `skydetail` (skybox.hpp ~73) | FUNCIONA |
| Solid color A (id 123) | Game > Skybox | `sceneInfo.sky.solid[3]` | nenhum (skybox.hpp escreve so RGB; fundo sai opaco) | SEM EFEITO |

### Physics (aba 3)

| Opcao | Categoria atual | Campo | Consumido em | Status |
|---|---|---|---|---|
| Physics enabled (caixa) | Physics | `P.enabled` | editor_play.hpp ~110 (so ao montar o Play) | PARCIAL (so Play) |
| Gravity X, Y, Z (sliders) | Physics > Gravity | `P.gravity[0..2]` | editor_play.hpp ~51 | FUNCIONA |
| Presets de gravidade (7 botoes, ids 210-216) | Physics > Gravity | `P.gravity` | main.cpp (kPresetG) | FUNCIONA |
| Solver iterations | Physics > Solver | `P.solverIterations` | editor_play.hpp ~52, `world.solverIterations` | FUNCIONA |
| Steps per second | Physics > Solver | `P.stepHz` | editor_play.hpp ~52 (`fixedDt`) | FUNCIONA |
| Draw colliders in the Scene view | Physics > Debug | `P.drawColliders` | view_overlays.hpp ~395 | FUNCIONA |
| Simulation radius (0 = everything) | Physics > Performance | `P.simRadius` | editor_play.hpp ~183 | FUNCIONA |
| Soft Step contacts (caixa) | Physics > Solver | `P.softStep` | editor_play.hpp ~55 -> `enableSoftConstraints` | FUNCIONA |
| Warm starting (caixa) | Physics > Solver | `P.warmStarting` | editor_play.hpp ~55 -> `enableWarmStarting` | FUNCIONA |
| Simulation islands (caixa) | Physics > Performance | `P.islands` | editor_play.hpp ~56 -> `enableIslands` | FUNCIONA |
| Render interpolation (caixa) | Physics > Performance | `P.interpolation` | editor_play.hpp ~56 -> `enableInterpolation` (world ~67) | FUNCIONA |
| Contact stiffness (Hz) | Physics > Contacts | `P.contactHertz` | editor_play.hpp ~57 -> `contactHertz` | FUNCIONA |
| Contact damping ratio | Physics > Contacts | `P.contactDampingRatio` | editor_play.hpp ~57 | FUNCIONA |

### Optimizing (aba 5)

| Opcao | Categoria atual | Campo | Consumido em | Status |
|---|---|---|---|---|
| Frame rate limit (combo) | Optimizing > Frame and cache | `ed.fpsMode` | main.cpp ~3192, ~3759 | FUNCIONA |
| Mesh chunk cache (slider) | Optimizing > Frame and cache | `ed.chunkCacheMax` | editor_state ~160 (evicao) | FUNCIONA |
| UI scale (slider) | Optimizing > Interface | `ed.theme.scale` | `refreshScale`, `interfaceFactor` | FUNCIONA |
| Idle rendering (caixa) | Optimizing > Interface | `ed.idleRendering` | nenhum: `viewsIdle()` (editor_state ~383) nao le o campo. So serializa/grava | SEM EFEITO |
| Simplify distant meshes (caixa) | Optimizing > Mesh simplification | `ed.simplifyFar` | `pickMesh` (editor_state ~774) | FUNCIONA |
| Simplify beyond (m) | Optimizing > Mesh simplification | `ed.simplifyDistance` | `pickMesh` (~777) | FUNCIONA |
| Faces removed | Optimizing > Mesh simplification | `ed.simplifyAmount` | `simplifiedMesh` (~759) | FUNCIONA |
| Upscaling FSR 1 (caixa) | Optimizing > Upscaling (GPU) | `R.fsr1` | `fsr1Usable()` (editor_state ~321), `gpu.setFsr1` (~1195) | FUNCIONA (so GPU) |
| Sharpness (slider) | Optimizing > Upscaling (GPU) | `R.fsr1Sharpness` (main.cpp) | ver nota 5: campo atual e `R.fsrSharpness`; o main.cpp le e grava um nome que nao existe | SEM EFEITO (codigo inconsistente, ver nota 5) |

### Language (aba 6)

| Opcao | Categoria atual | Campo | Consumido em | Status |
|---|---|---|---|---|
| Language (radio de pacotes) | Language | `I18n.select` | `ed.langDirty` | FUNCIONA |
| Reload packs (botao) | Language | `I18n.scan` | main.cpp ~2296 | FUNCIONA |
| Export template (botao) | Language | `I18n.exportTemplate` | grava `modelo.json` | FUNCIONA |

### Lighting (aba 4)

Dados em `sceneInfo.render.lighting` (`L.`) salvo na cena (`project.hpp`), exceto onde indicado.

| Opcao | Categoria atual | Campo | Consumido em | Status |
|---|---|---|---|---|
| Quality preset (Mobile, Low, Medium, High) | Lighting > Quality | `L.tier` | `makeQuality` (lighting.hpp ~94) | FUNCIONA |
| Custom values (caixa) | Lighting > Quality | `L.custom` | `makeQuality` | FUNCIONA |
| Shadow cascades | Lighting > Quality | `L.csmCascades` | `makeQuality` (so com custom) | FUNCIONA (so custom) |
| Shadow map resolution (per cascade) | Lighting > Quality | `L.shadowMapResolution` | `makeQuality` (so com custom) | FUNCIONA (so custom) |
| Edge softness (PCF) | Lighting > Quality | `L.pcfTaps` | `makeQuality` (so com custom) | FUNCIONA (so custom) |
| Max lights per cluster | Lighting > Quality | `L.maxLightsPerCluster` | `makeQuality` (so com custom) | FUNCIONA (so custom) |
| Light indices per cluster | Lighting > Quality | `L.indicesPerCluster` | `makeQuality` (so com custom) | FUNCIONA (so custom) |
| Auto radius cutoff | Lighting > Quality | `L.autoRadiusCutoff` | `makeQuality` (so com custom) | FUNCIONA (so custom) |
| Max point lights | Lighting > Budget | `L.maxPointLights` | `LightRig::configure` (recria o sistema) | FUNCIONA |
| Max spot lights | Lighting > Budget | `L.maxSpotLights` | `LightRig::configure` | FUNCIONA |
| Enable shadows (caixa) | Lighting > Sun shadows | `L.shadows` | `collectLights` (editor_state ~817) | FUNCIONA (duplicada na API/Graphics) |
| Max shadow distance (m) | Lighting > Sun shadows | `L.shadowDistance` | `makeCsmConfig` (lighting.hpp ~121) | FUNCIONA |
| Cascade split (lambda) | Lighting > Sun shadows | `L.splitLambda` | `makeCsmConfig` | FUNCIONA |
| Cascade blend | Lighting > Sun shadows | `L.blendFraction` | `makeCsmConfig` | FUNCIONA |
| Caster extension (m) | Lighting > Sun shadows | `L.casterExtension` | `makeCsmConfig` | FUNCIONA |
| Depth bias (m) | Lighting > Sun shadows | `L.depthBias` | `makeCsmConfig` | FUNCIONA |
| Normal bias (texels) | Lighting > Sun shadows | `L.normalBias` | `makeCsmConfig` | FUNCIONA |
| Snap to texels (caixa) | Lighting > Sun shadows | `L.snapToTexels` | `makeCsmConfig` | FUNCIONA |
| Point and spot shadow lights | Lighting > Point and spot shadows | `L.pointShadowLights` | gpu_renderer.hpp, soft_renderer.hpp | FUNCIONA |
| Point/spot shadow resolution | Lighting > Point and spot shadows | `L.pointShadowResolution` | `LightFrame::pointShadowResolution` (lighting.hpp ~278), gpu/soft | FUNCIONA |
| Ambient occlusion (SSAO) (caixa) | Lighting > SSAO | `L.ssao` | `out_.ssaoOn` (lighting.hpp ~265), gpu/soft | FUNCIONA |
| AO kernel samples | Lighting > SSAO | `L.ssaoKernel` | `makeSsaoConfig` | FUNCIONA |
| AO radius (m) | Lighting > SSAO | `L.ssaoRadius` | `makeSsaoConfig` | FUNCIONA |
| AO bias (m) | Lighting > SSAO | `L.ssaoBias` | `makeSsaoConfig` | FUNCIONA |
| AO power (contrast) | Lighting > SSAO | `L.ssaoPower` | `makeSsaoConfig` | FUNCIONA |
| Enable ray tracing (caixa) | Lighting > Ray tracing | `L.rayTracing` | editor_state ~1300 (so CPU) | PARCIAL (CPU) |
| Ray-traced ambient occlusion (CPU) (caixa) | Lighting > Ray tracing | `L.rtAo` | editor_state ~1300 (so CPU) | PARCIAL (CPU) |
| RT AO rays per pixel | Lighting > Ray tracing | `L.rtAoRays` | editor_state ~1320 (`RtAoConfig`) | PARCIAL (CPU) |
| RT AO radius (m) | Lighting > Ray tracing | `L.rtAoRadius` | editor_state ~1320 | PARCIAL (CPU) |
| Bloom (caixa) | Lighting > Post | `L.bloom` | gpu `setBloom` (editor_state ~1218); CPU (editor_state ~940) | FUNCIONA |
| Bloom strength | Lighting > Post | `L.bloomStrength` | mesmo caminho do bloom | FUNCIONA |
| Ambient intensity | Lighting > Ambient | `L.ambientIntensity` | lighting.hpp ~284 | FUNCIONA |
| Pre-exposure | Lighting > Ambient | `L.preExposure` | lighting.hpp ~258 | FUNCIONA |
| Frustum culling (caixa) | Lighting > Scene | `R.frustumCull` | editor_state ~1055 | FUNCIONA |
| Occlusion culling (caixa) | Lighting > Scene | `R.occlusionCull` | editor_state ~1064 | FUNCIONA |
| Draw distance (0 = each camera's Far) | Lighting > Scene | `R.drawDistance` | `withDrawDistance` (editor_state ~713-715) | FUNCIONA |
| Sun light color R, G, B | Lighting > Sun | `GlobalLight.color[0..2]` | `sunLight` (components.hpp ~341) | FUNCIONA |
| Sun light color A | Lighting > Sun | `GlobalLight.color[3]` | nenhum (sunLight copia so RGB) | SEM EFEITO |
| Sun intensity | Lighting > Sun | `GlobalLight.intensity` | `sunLight` | FUNCIONA |
| Direction (azimuth) | Lighting > Sun | `GlobalLight.azimuth` | `sunLight` | FUNCIONA |
| Height (elevation) | Lighting > Sun | `GlobalLight.elevation` | `sunLight` | FUNCIONA |
| Ambient light R, G, B | Lighting > Sun | `GlobalLight.ambient[0..2]` | `collectLights` (ambiente) | FUNCIONA |
| Sun casts shadows (caixa) | Lighting > Sun | `GlobalLight.castShadow` | editor_state ~817 (com `L.shadows`) | FUNCIONA |
| Enable post-processing (caixa) | Lighting > Post | `R.post.enabled` | `applyPostFx` (postfx.hpp), main.cpp ~1486, ~3426 | FUNCIONA |
| Exposure, Contrast, Saturation | Lighting > Post | `R.post.*` | `applyPostFx` | FUNCIONA |
| Tint color R, G, B, Tint strength | Lighting > Post | `R.post.tint[0..2]`, `tintStrength` | `applyPostFx` | FUNCIONA |
| Vignette, Vignette size | Lighting > Post | `R.post.vignette`, `vignetteSize` | `applyPostFx` | FUNCIONA |
| Sharpen | Lighting > Post | `R.post.sharpen` | `applyPostFx` (postfx.hpp ~33) | FUNCIONA |
| Reset effect (botao) | Lighting > Post | `R.post` | main.cpp ~2369 | FUNCIONA |

## Checkboxes (caixas de selecao) hoje: 29

Lista para decidir a troca por toggle. Todas usam o mesmo tipo `Check` (ou `Checkbox` SpriteUI no painel fixo).

- Game: Show grid in the Scene window; Show grid in the Game window; Show gizmos; Fill each half; Skybox (6 images).
- Physics: Physics enabled; Draw colliders; Soft Step contacts; Warm starting; Simulation islands; Render interpolation.
- Optimizing: Idle rendering (SEM EFEITO); Simplify distant meshes; Upscaling FSR 1.
- Lighting: Frustum culling; Occlusion culling; Custom values; Sun casts shadows; Enable shadows; Snap to texels;
  Ambient occlusion (SSAO); Enable ray tracing (PARCIAL); Ray-traced AO (PARCIAL); Bloom; Enable post-processing.
- Painel fixo (API/Graphics e Quality): Sharp pixels (PARCIAL); Enable shadows (duplicada); Enable ray tracing (duplicada, PARCIAL); Ray-traced AO (duplicada, PARCIAL).

Observacao: 3 caixas tem o mesmo dado em duas telas (Enable shadows, Enable ray tracing, Ray-traced AO). Ao trocar por
toggle, vale manter uma so.

## Opcoes de FSR e upscale hoje

| Opcao | Onde fica | Dado | Estado |
|---|---|---|---|
| Upscaling FSR 1 | Optimizing > Upscaling (GPU) | `R.fsr1` | FUNCIONA (GPU, Vulkan/D3D) |
| Sharpness | Optimizing > Upscaling (GPU) | `R.fsrSharpness` (o main.cpp grava `R.fsr1Sharpness`) | Quebrado, ver nota 5 |
| Render scale | API/Graphics > Quality | `ed.renderScale` | FUNCIONA; FSR so age com escala abaixo de 1 |
| Upscaling (software) | API/Graphics > Quality | `ed.upscaleMode` | FUNCIONA (CPU, Bilinear/Bicubic/Lanczos2) |
| Anti-aliasing (software) | API/Graphics > Quality | `ed.aaMode` | FUNCIONA (FXAA) |
| Sem controle na tela | project.hpp | `R.fsrQuality`, `R.fsrCustomScale`, `R.fsr4`, `R.frameGen` | Dados existentes, sem controle; `fsr4`/`frameGen` so guardam escolha |

## Notas

1. Sharp pixels: o efeito so existe quando Upscaling (software) = Off, Anti-aliasing = Off e FSR desligado,
   porque `softScaling()` (editor_state ~322) fica verdadeiro no padrao (upscaleMode = 2). Com o padrao atual, a
   caixa nao muda nada na tela.
2. Upscaling (software) "Off": `finishView` (editor_state ~331) usa Bilinear quando outro filtro soft esta ativo
   (`filters[0] = Bilinear`). "Off" nao e exatamente "SDL linear". Comportamento a confirmar com o usuario.
3. Shadow quality (painel fixo) grava `L.shadowMapResolution` e liga `L.custom = true`. Ja a combo "Shadow map
   resolution" da aba Lighting so e editavel com custom ligado. Sao dois caminhos para o mesmo campo.
4. Tags e Project estao montados dentro da aba API/Graphics (main.cpp ~2453-2455). Para a reorganizacao, move-las
   para Projeto e Interface.
5. Sharpness (FSR): `main.cpp` linhas ~1946 e ~2257 usam `ed.sceneInfo.render.fsr1Sharpness`. O campo atual em
   `project.hpp` (linha ~253) e `fsrSharpness`, e `editor_state.hpp` (~1242) passou a ler `fsrSharpness`. No
   momento da leitura, o nome `fsr1Sharpness` nao existe em `RenderSettings`, entao o main.cpp nao compila e o slider
   nao chega ao renderer. Confirmar com o build antes de tratar como bug de runtime. Corrigir: trocar para
   `fsrSharpness` nos dois pontos do main.cpp.
6. Physics enabled: so age no Play (editor_play.hpp ~110). Na aba Scene nao muda nada, o que pode ser o esperado.
7. Itens de texto (Note, Dim, Heading, Gap) nao sao opcoes e nao entram na contagem. A nota "Audio output" da aba
   Game e so informativa.

## Proposta de categorias

Mapeamento das opcoes atuais para as 8 categorias pedidas. Itens fora da lista original (Presets e Skybox) estao
indicados.

- **Graficos/API**: Graphics API; Frame rate limit; Game resolution; Width; Height; Render scale (ou em Qualidade).
- **Qualidade**: Upscaling (software); Anti-aliasing (software); Sharp pixels (remover ou mover, ver nota 1); Quality
  preset; Custom values; cascades/PCF/cluster (so custom); Max point/spot lights; Mesh chunk cache; Simplify
  distant meshes e campos; Idle rendering (remover ou corrigir).
- **Sombras e luz**: Enable shadows; Shadow quality; Shadow cascades e demais sombras do sol; Point and spot shadows;
  SSAO; Bloom; Ambient e Pre-exposure; Sun (cor, intensidade, direcao, altura, ambiente, sombra); Ray tracing e RT AO
  (marcar como CPU); Draw distance; Frustum/Occlusion culling; Post-processing.
- **Upscaling (FSR)**: Upscaling FSR 1; Sharpness (corrigir, nota 5); Render scale (referencia); fsrQuality, fsr4,
  frameGen, fsrCustomScale (hoje sem tela).
- **Fisica**: Physics enabled; Gravity X/Y/Z e presets; Solver iterations; Steps per second; Draw colliders; Simulation
  radius; Soft Step; Warm starting; Islands; Render interpolation; Contact stiffness; Contact damping.
- **Idioma**: Language; Reload packs; Export template.
- **Projeto**: Presets de configuracao (Edit config, Config Name, Save, Delete); Tags (links, rename); Editor command.
- **Interface**: Tema (Palette, User themes, Base colors, Advanced colors); Button/Window corner radius; Interface text
  size; UI scale; Skybox e cores do ceu (Game); grades e gizmos da Game/Scene; Split screen (Game).

Pontos a decidir com o usuario: (a) Idle rendering, Light color A e Skybox solid color A sao candidatos a remocao;
(b) Sharp pixels so faz sentido com upscale Off; (c) manter ray tracing como "CPU" ou esconder ate existir na GPU;
(d) remover as duplicatas (Enable shadows, Frame rate limit, Ray tracing e RT AO, Split screen).

## Verificacao pendente

- Compilar (fora desta auditoria) para confirmar o problema do `fsr1Sharpness` (nota 5).
- Confirmar no app se Idle rendering realmente nao muda nada (esperado: nenhum efeito).
- Confirmar se o skybox tem efeito visivel com o alfa (esperado: nenhum).
