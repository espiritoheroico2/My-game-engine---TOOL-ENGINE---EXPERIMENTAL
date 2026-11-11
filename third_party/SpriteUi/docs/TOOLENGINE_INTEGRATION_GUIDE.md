# Guia de integração do SpriteUI na ToolEngine

Este guia define um caminho incremental para hospedar o SpriteUI na ToolEngine sem uma segunda
janela, sem um segundo event loop e sem tornar Dear ImGui uma dependência do núcleo. O repositório
atual contém os contratos do SpriteUI, mas não contém o código-fonte da ToolEngine; nomes concretos
de componentes, filas e comandos RHI precisam ser confirmados no checkout da engine antes de
implementar os adaptadores.

## Decisões de arquitetura

- A ToolEngine continua dona da janela SDL, do ciclo de eventos, do ECS e do RHI.
- O SpriteUI recebe um snapshot de input já coletado pelo host. Não chama `SDL_PollEvent` e não usa
  `SDL3Window` dentro da ToolEngine.
- O adaptador ECS traduz handles/componentes da engine para os dados do SpriteUI; o core não passa
  a depender de um ECS específico.
- O renderer recebe um `Rendering::FramePacket`, que contém instâncias de ABI fixa, batches e clip
  rects. Não cria um segundo dispositivo ou backend Diligent.
- ImGui permanece disponível como fallback por tela até a substituição e a regressão daquela tela
  serem validadas.
- O visualizador SDL3 é uma ferramenta independente de referência em CPU, não é o backend da engine.

## Estado e lacunas que precisam ser confirmadas no checkout da ToolEngine

| Área | Estado no SpriteUI | Trabalho na integração |
|---|---|---|
| Core C++20 | Layout, widgets lógicos, input, texto geométrico, `RenderExtractor` e `FramePacket` existem | Incluir o core como subdiretório/target ou pacote; manter opções de backend desligadas |
| ECS | Não existe adaptador para as entidades da ToolEngine | Definir ownership e mapeamento de handles; sincronizar componentes sem duplicar o estado de janela |
| Input | Adaptador SDL3 traduz mouse e fingers; `UIMultiPointerEventRouter` consome snapshots multi-pointer | Encaminhar eventos durante o pump do host ou montar snapshots (um por ponteiro); não fazer um segundo pump; definir política de foco de teclado entre ponteiros |
| RHI | `IRenderBackend` e shader de referência existem; não há backend RHI ToolEngine | Resolver upload de instâncias, binding de atlas/sampler, clips, desenho instanciado e fences |
| Texturas e ícones | O frame referencia `textureSlice`; o SpriteUI não decodifica arquivos de imagem | Usar o pipeline de assets da engine para carregar, converter e manter texturas |
| Fonts | Layout e contratos de glifos existem; o rasterizador GDI pertence somente ao visualizador Windows | Escolher FreeType/HarfBuzz ou o pipeline já usado pela engine e fornecer atlas persistente |
| Editor | Migração ainda não iniciada | Escolher uma tela pequena, manter fallback ImGui e comparar input/layout/render |

Os documentos de arquitetura podem mencionar recursos planejados que não estão entregues. Confira
`docs/IMPLEMENTATION_STATUS.md` antes de assumir uma API disponível.

## Ordem recomendada

### 1. Resolver contratos do host

No checkout atual da ToolEngine, localizar e registrar:

1. target/build system consumido pelo editor;
2. entidade e sistema de atualização do ECS;
3. dono do pump SDL e a representação dos eventos de mouse, teclado, gamepad, IME e DPI;
4. comandos RHI para criar buffers, copiar instâncias, criar/bindar textura e sampler, desenhar
   instâncias, submeter e liberar recursos por fence;
5. pass de composição UI, formato do framebuffer, espaço de cor, DPI e convenção de alpha.

Não feche lacunas do RHI por meio de um segundo backend concorrente. Se faltarem operações, desenhe
uma pequena extensão do RHI junto dos mantenedores da engine.

### 2. Integrar o core sem SDL ou ImGui

Comece com `SpriteUi::SpriteUI` apenas. Exponha os diretórios de include e o padrão C++20. Deixe
`SPRITEUI_BUILD_SDL3_ADAPTER` e `SPRITEUI_BUILD_VISUALIZER` desligados para o alvo do editor.
Construa um `UISystem` pertencente ao host, com fases explícitas:

```text
host events -> UIInputSnapshot -> ECS/UI state -> layout -> RenderExtractor
             -> FramePacket -> ToolEngine RHI -> UI composition pass
```

O exemplo é deliberadamente pseudocódigo até os nomes reais das APIs do host serem verificados:

```cpp
void UISystem::Update(float dt, const HostInput& input, const HostWindow& window) {
    UIInputSnapshot snapshot = BuildSnapshot(input, window); // sem SDL_PollEvent
    ApplyInput(snapshot);
    UpdateAnimations(dt);
    ResolveLayout(window.LogicalClientRect());
    FramePacket packet = extractor.Build(++frameId, renderItems, nullptr, clipRects);
    SubmitWithToolEngineRhi(packet);
}
```

Use handles com geração estável e inválide os handles no teardown/reuso das entidades. A UI deve
usar coordenadas lógicas; aplique a conversão de DPI em um único ponto do caminho de renderização e
deixe input e clipping concordarem nessa convenção.

### 3. Implementar o backend RHI em um pass de UI

O backend deve:

1. validar o pacote com `IsFramePacketValid`;
2. copiar `SpriteInstanceGPU` para um buffer de instâncias respeitando alinhamento e lifetime;
3. bindar pipeline/shader, atlas de textura, sampler, buffer e clip rects;
4. preservar a ordem dos batches — blending alfa não permite ordenar materiais livremente;
5. fornecer corretamente o índice/base da instância. Se o RHI não tiver `base-instance`, ajustar o
   offset do binding por batch ou usar um buffer/offset equivalente, validando a compatibilidade com
   o shader;
6. converter clips e viewport para as unidades esperadas pelo framebuffer;
7. tratar falhas de alocação/submissão e aguardar fences antes de reciclar buffers/texturas;
8. validar blend premultiplicado, sRGB, MSDF e `FlagBitmapText` em GPU real.

`SpriteInstanceGPU` tem ABI de 80 bytes. Uma mudança no layout deve atualizar o contrato do shader e
os `static_assert`s; não altere o ABI apenas para adicionar um efeito que possa ser representado pela
tint pré-multiplicada. `UIRenderItem::opacity` escala RGB e alpha juntos durante a extração e não
acrescenta campos ao ABI GPU.

### 4. Importar assets, ícones e fontes

`Rendering::UIIcon` é uma referência a um retângulo UV e a um `textureSlice` de um atlas já carregado.
`MakeIconRenderItem` transforma essa referência em um quad normal do frame. O SpriteUI intencionalmente
não abre arquivos, não aloca recursos RHI e não implementa um parser SVG.

Para ícones personalizados:

- **SVG:** decodificar/rasterizar no importador de assets da engine ou no pipeline de build para uma
  textura RGBA em tamanho(s) de UI e inserir no atlas. SVG preserva a fonte vetorial no asset e
  permite gerar variantes para DPI sem adicionar um parser SVG ao core.
- **PNG/WebP/arquivos rasterizados:** decodificar com o loader já existente na ToolEngine; manter
  alpha, espaço de cor e mipmaps compatíveis com o RHI.
- **Ícones de glifo:** tratar como fonte/atlas e associar o glifo ao slice/retângulo UV.
- Após packing, guardar o slice e o UV normalizado em `UIIcon`; garantir padding/gutter no atlas
  para evitar bleeding de sampler, principalmente em mipmaps.
- Destruir ou substituir o atlas somente depois da fence que protege as instâncias que o referenciam.

Para produção de texto, não usar o GDI do exemplo. Conectar o shaping do host ou FreeType/HarfBuzz,
resolver glifos faltantes e bidi de acordo com o escopo do editor, manter atlas e métricas coerentes
e produzir quads com a flag de material correta.

### 5. Migrar uma tela mantendo fallback

Comece por uma tela de baixo risco, por exemplo uma ferramenta de diagnóstico isolada, antes do
Console. Encaminhe o mesmo estado de janela/input para as duas implementações e selecione uma delas
por feature flag. Compare geometria, foco/navegação, clipping, DPI, atalhos, estado persistido e
capturas. Só remova o fallback ImGui daquela tela depois de passar a matriz de regressão.

Não remova a dependência ImGui do editor inteiro enquanto existirem telas não migradas. A estratégia
é por tela, não por uma troca global de renderer.

## Critérios de aceite do primeiro MVP

- O editor cria e destrói o sistema/UI sem vazamento e sem janela/event loop paralelo.
- Uma tela simples recebe mouse e teclado/gamepad através do pump já existente.
- O `FramePacket` passa validação e chega ao RHI sem reordenar transparências.
- Texturas, clipping, resize, foco e DPI funcionam em pelo menos uma cena real do editor.
- O mesmo cenário pode voltar ao ImGui pelo fallback, sem perder estado persistente.
- Capturas e testes são executados com o backend de GPU da engine; o visualizador CPU não substitui
  essa verificação.

## Próximas tarefas executáveis

1. No checkout da ToolEngine, produzir uma tabela curta das interfaces reais de ECS, input e RHI.
2. Aprovar qualquer extensão mínima de RHI que falte para instâncias, textura/sampler e clipping.
3. Adicionar um alvo de integração que consome o core, sem incluir o wrapper SDL3 standalone.
4. Implementar o snapshot/event tap de input e um `UISystem` mínimo.
5. Renderizar uma tela de diagnóstico com um ícone importado e uma animação de fade.
6. Validar uma tela real e só então ampliar a migração do editor.

Este guia não declara essas integrações concluídas; elas só podem ser implementadas e verificadas no
checkout e na toolchain reais da ToolEngine.
