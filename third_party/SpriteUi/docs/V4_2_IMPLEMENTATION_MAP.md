# Mapeamento da especificação V4.2 para o código-fonte

Este mapa correlaciona cada contrato normativo do documento V4.2 com os componentes e arquivos do projeto.

| Contrato V4.2 | Código / Estado |
|---|---|
| ABI `SpriteInstanceGPU` 80 bytes | `include/SpriteUi/Rendering/SpriteInstanceGPU.hpp` — implementado e validado por `static_assert` |
| Screen/World + Unlit/ClusterLit | `Rendering/CanvasConfig.hpp` — configuração e validação lógica implementadas |
| Input snapshot, foco e captura | `Input/InputRouter.hpp`, `src/Input/InputRouter.cpp` — implementado para ponteiro primário e cancelamento seguro |
| Eventos semânticos e bubbling | `UIEventRouter` (bubbling pula ancestrais não interagíveis no bridge) — bubbling recursivo pelos `parent` e consumo respeitando `blocksInput` |
| Navegação Activate/Cancel | `Input/SpatialNavigation.hpp` — cone direcional de 90° e roteador semântico implementados |
| Slider e ScrollView | `Widgets/Widgets.hpp` — domínio numérico com step e `ClampScroll` por `contentSize - viewportSize` |
| Window & 8-Edge Resizing | `Widgets/Widgets.hpp`, `src/Widgets/Widgets.cpp` — detecção de 8 bordas, arrasto de titlebar e respeito a `minSize` |
| TextInput & Toggle | `Widgets/Widgets.hpp` — cursor por cluster, seleção, blink timer e alternância de toggle |
| Virtualização de Listas | `Widgets/VirtualList.hpp`, `src/Widgets/VirtualList.cpp` — cálculo de faixa visível O(1) e overscan |
| Abas e DockSpace | `Widgets/Docking.hpp`, `src/Widgets/Docking.cpp` — agrupamento de abas e divisões binárias de docking |
| Gráficos e Osciloscópio | `Widgets/Graphs.hpp`, `src/Widgets/Graphs.cpp` — buffer circular de 256 amostras, ordenação e normalização |
| Gerenciador de Popups | `Widgets/Popup.hpp`, `src/Widgets/Popup.cpp` — gestão de foco e fechamento em clique externo |
| Data Binding Reativo MVVM | `Binding/ValueBinding.hpp` — dirty check por threshold para float, int, bool e string |
| GlyphInstance & Tipografia | `Text/GlyphInstance.hpp`, `Text/FontMetrics.hpp`, `Text/TextLayout.hpp`, `src/Text/TextLayout.cpp` — layout geométrico de quads, caret e seleção por cluster |
| ToonMotion V2 | `Animation/ToonMotion.hpp`, `src/Animation/ToonMotion.cpp` — interpolações POD, políticas de conflito e shake analítico não-cumulativo |
| Dirty flags | `Core/DirtyFlags.hpp` — flags de invalidação por subsistema |
| Extração GPU e batching | `Rendering/DrawList.hpp`, `src/Rendering/DrawList.cpp` — extração CPU estável e batching adjacente |
| LayoutTree -> Scene Bridge | `Layout/LayoutBridge.hpp`, `src/Layout/LayoutBridge.cpp` — conversão automática de árvore de layout em `HitTarget`, `UIRenderItem` e `ClipNode` |
| Persistência JSON Transacional | `Config/UIConfig.hpp`, `src/Config/UIConfig.cpp` — validação de `schemaVersion`, `ReadFiniteFloat` e staging atômico (Seção 7) |
| Shader Unlit & MSDF | `shaders/SpriteUI.hlsl` — compilado por DXC para DXIL (`SpriteUI.vs.dxil` e `SpriteUI.ps.dxil`) com amostragem MSDF por `fwidth` |
| Flecs Adapter | Pendente |
| Diligent Backend | `IRenderBackend` e `NullRenderBackend` funcionais; backend RHI Diligent pendente |
| HarfBuzz/FreeType C++ wrapper | Pendente; pipeline de layout geométrico de glifos shaped já concluído |
| Light Prism, Bloom e Blur | Pendente |
| QuickJS FFI | Pendente |
