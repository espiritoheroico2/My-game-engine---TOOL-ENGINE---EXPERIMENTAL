# Próximos passos (ordem sugerida)

0. **Pendências (29–30/10):** sombras de point light (projeto em `RENDER_PERF.md`); texturas/normal map + Material (.tmat) + shader por objeto (`TEXTURES.md`); "Compress as NTC" (`NTC.md`); DXR (`DX12_DXR.md`); pós-fx e partículas com profundidade na GPU; gizmo em espaço local; grafo no jogo exportado; interface 2D/juntas da física.

1. **Executar scripts ToolObject** — embutir uma VM (QuickJS, MIT) atrás de `IScriptHost`; ligar `start/update/OnDelete/DeleteAndReuse`
   ao ciclo de vida do `ToolObject`; API segura (sem acesso a arquivos/rede fora do projeto); orçamento de tempo por quadro no Profiler.
2. ~~Desfazer/refazer~~ — **feito na 0.4.1** (snapshot da cena). Melhoria futura: comandos incrementais se as cenas ficarem grandes.
3. **Apresentar direto na GPU** (hoje: render → readback para CPU → textura SDL). Elimina o readback (maior custo do caminho GPU) e é pré-requisito do FSR (docs/FSR.md).
4. **Sombras**: cascatas (CSM) para cenas grandes, sombras de spot/ponto, PCSS opcional.
5. **Texturas e materiais**: carregar PNG/JPG nos modelos importados, normal map; hoje só cor/rugosidade/metálico.
6. **Pós-processamento na GPU** (hoje CPU): bloom, tonemap configurável, FXAA/TAA.
7. **Exportação** — ligar `kExportEnabled` (project_settings.hpp) quando houver exportadores: Windows x64 primeiro, depois Linux, Android, Web.
8. ~~Seleção por raio~~ — **feita na 0.4.1**. Falta: gizmo em espaço local/global.
9. **OpenGL**: investigar a instabilidade relatada (pedir ao usuário a mensagem de erro exata); a proteção que volta para CPU após 5 quadros com erro já existe.
10. **Direct3D 12**: testar no Windows (compila, nunca foi executado).
