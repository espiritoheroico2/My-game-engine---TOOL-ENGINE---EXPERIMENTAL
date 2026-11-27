# Estudo: render de milhões de objetos

Resumo do estudo de render de milhões de objetos (instâncias, células, culling na GPU, draw indireto). Estudo completo: https://claude.ai/artifact/25qA87cxSKVrK6HbRAX12N

Documentos do repositório que tratam do mesmo assunto: `../planos/02_RENDER_DRAW_CALLS.md` (arquitetura-alvo e etapas P0 a P6), `../planos/00_ROADMAP.md` (seção "Pesquisa de milhões de objetos"), `../planos/12_OTIMIZACAO_FISICA_ECS.md`.

## Estado atual

Medido no Vulkan, AMD RX 6500 XT, 1280x720, 120 quadros, 5.000 esferas com física:

- Antes do filtro: quadro 45,7 ms; buffer de oclusão 25,3 ms.
- Depois do filtro de 8 px: quadro 19,1 ms; buffer de oclusão 0,8 ms.
- Instâncias estáticas (`--instances 5000`, câmera fixa): 9,1 ms -> 7,7 ms.
- Sem oclusão (`--no-occlusion`) com esferas: 18,4 ms.
- Cache de oclusores estáticos: testado, ganho pequeno (7,76 ms câmera fixa) e piora em câmera móvel (8,92 -> 9,30 ms). Foi removido.
- RHI: compute, UAV, draw indireto e readBuffer no backend Diligent, testados só em Vulkan.
- Culling na GPU: em andamento (fase 1: frustum por compute + draw indireto, sem HiZ).

## 1. Resumo

### Linha de base medida

GPU usada: AMD RX 6500 XT.

| Objetos | Resultado |
|---|---|
| 10 mil | 18 ms por quadro |
| 100 mil | 146 ms por quadro, dos quais 78 ms são oclusão por software |
| 1 milhão | estoura o heap dinâmico do Vulkan |

Origem: medição informada para este estudo. Data e comando exatos não estão registrados nos documentos do repositório; registrar na Etapa E0.

### O que o resultado diz

- Com 100 mil objetos mais da metade do quadro é culling de oclusão na CPU. Passar essa parte para a GPU é o ganho mais claro.
- O estouro com 1 milhão vem do heap dinâmico do Vulkan, que o Diligent aloca por quadro para dados que mudam. O repositório já subiu o padrão de 8 MB para 64 MB, configurável por `TOOL_DYNAMIC_HEAP_MB` (`../planos/03_DESEMPENHO_EDITOR.md`, `00_CHECKLIST.md`). Subir o heap adia o problema, não o resolve: 1 milhão de instâncias não pode ser reenviado inteiro a cada quadro.
- Caminho: dados de instância persistentes na GPU (só o que mudou é reenviado), culling na GPU e desenho indireto.

### O que existe hoje (conferido no código)

- `GpuRenderer::drawDynamic(slot, ...)`, `drawIndexed` e `drawIndexedRange`; o RHI tem draw indireto e compute no backend Diligent (testados só em Vulkan; ver "Estado atual").
- O shader PBR já lê um buffer de instâncias (`Instances` em `pbrStorageNames`, `gpu_renderer.hpp`).
- O helper `engine/render/include/tool/render/indirect_plan.hpp` existe e é coberto por `tests/test_render_helpers.cpp`, mas não tem chamador no renderer.
- Compute: o RHI prevê o estágio compute em `ShaderStage` e o backend Diligent já implementa compute e UAV (StorageRW), testados em Vulkan. D3D12 e GL ainda não foram testados.
- A apresentação da cena é lida de volta para a CPU a cada quadro (`../estudos/AUDITORIA_DESEMPENHO.md`); o zero-copy ainda não existe.
- Culling por frustum na CPU existe; o culling por oclusão por software é o custo de 78 ms acima.

### Ideias do estudo

- **Instâncias:** uma malha com N transformações por chamada; buffer de instâncias persistente; atualização só das instâncias sujas.
- **Células:** o mundo dividido em células espaciais; cada célula tem bounds e uma lista de instâncias; culling por célula primeiro, por instância depois; células estáticas não são reenviadas.
- **Culling na GPU:** compute shader testa frustum e, depois, Hi-Z, e escreve a lista visível.
- **Draw indireto:** a GPU escreve os argumentos de desenho e a CPU emite uma chamada por malha, sem saber quantas instâncias passaram.

## 2. Etapas E0 a E7

Regra comum a todas: medir antes e depois na mesma máquina e cena; separar CPU e GPU; manter o caminho CPU-only funcionando (regra 7 de `../engineering/00_ROADMAP_EXECUCAO.md`); a etapa só fecha com o gate cumprido. Se um gate falhar, registrar como experimento rejeitado em vez de seguir.

| Etapa | Trabalho | Gate |
|---|---|---|
| E0 | Linha de base reproduzível: comando fixo para 10 mil, 100 mil e 1 milhão de objetos; registrar GPU, driver, CPU, data. Separar tempo de CPU e de GPU e a divisão do tempo de oclusão. Ligar contadores de bytes enviados por quadro e de uploads do `BatchCB`. | Tabela com as três cargas, repetível em 3 execuções com variação menor que 5%. Confirmar o que o RHI tem de compute e de indireto. |
| E1 | Buffer de instâncias persistente: transformações guardadas na GPU; reenviar só instâncias sujas. Remover o upload por lote (`firstInstance`). | 100 mil objetos estáticos: bytes enviados por quadro próximos de zero; 1 milhão não estoura o heap dinâmico (ou o limite aparece como erro claro, não crash). |
| E2 | Células espaciais: grade ou hierarquia, bounds por célula, culling em duas fases (célula, depois instância) na CPU. | Tempo de culling em 100 mil menor que a linha de base, com a mesma imagem final (comparação de imagem). |
| E3 | Substituir a oclusão por software por uma versão com custo limitado (por exemplo, só para células grandes) ou desligar por orçamento. | Os 78 ms de oclusão caem a no máximo uma fração definida na E0; sem objetos visíveis sumindo (teste de imagem). |
| E4 | RHI: buffers estruturados graváveis por compute e pipeline de compute, atrás de capability. Backends Vulkan e Direct3D 12 via Diligent. | Compute simples (preencher buffer) roda e é lido de volta em teste; backend sem suporte cai no caminho da E2. |
| E5 | Culling por frustum na GPU (compute), saída em lista de visíveis. | Mesma imagem que o culling da CPU em cena de teste; tempo de CPU em 100 mil menor que na E3. |
| E6 | Draw indireto: o RHI ganha `drawIndexedIndirect` (e multi-draw se a capability existir); usar `indirect_plan.hpp` já testado como base. | 100 mil e 1 milhão: número de chamadas por quadro é igual ao número de malhas, não de objetos. Quadro de 1 milhão com meta definida na E0 (a meta numérica só é fixada depois da E0). |
| E7 | Culling por oclusão Hi-Z na GPU, LOD e impostores para objetos distantes. | Imagem de referência igual dentro de tolerância; ganho medido contra E6. Parar aqui se o ganho for menor que o custo de manutenção. |

## 3. Riscos

- Compute e draw indireto já existem no backend Diligent, mas só foram testados em Vulkan; E4 e E6 ainda precisam de validação em Direct3D 12 e de GPU para medir. O ambiente de desenvolvimento citado nos documentos não tem GPU, então o gate de cada uma exige a máquina do usuário.
- Sombras, transparência e partículas passam pelo mesmo caminho de instâncias; a transparência exige ordenação, que o draw indireto sozinho não resolve.
- A física em milhares de corpos (`FISICA_OTIMIZACAO.md`) e as partículas (`../planos/13_FUNCIONALIDADES_FUTURAS.md`, item f) disputam o mesmo tempo de quadro; medir cada uma separada.
