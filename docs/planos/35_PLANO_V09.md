# 35 - Plano da versão 0.9

Status: planejamento. Organizado por dificuldade (fácil, média, difícil), com as tarefas pendentes e em andamento incluídas.
Regra geral: tudo modular e configurável; a CPU trabalha em paralelo com o pool de threads; a GPU recebe o trabalho gráfico pesado; a oclusão por CPU fica disponível só para teste e medição.

## Fácil (interface e correções pontuais)

| # | Tarefa | Observação |
|---|---|---|
| F1 | Canal alfa do color picker não funciona (barra não se move) | investigar a causa antes de corrigir |
| F2 | Campos numéricos: arrastar com o mouse para mudar o valor (direita aumenta, esquerda diminui), contorno branco; clique comum edita como texto | vale para todo campo de texto numérico |
| F3 | A janela Scene abre e fica selecionada ao iniciar a engine | |
| F4 | Criar objeto "Particle" pelo Hierarchy | depende de P1 |
| F5 | Remover o sistema antigo de partículas (ParticleComp), migrando antes os recursos que só ele tem | depende de P1 |
| F6 | Uma única opção de qualidade de sombra, sob responsabilidade do Light Prism | |
| F7 | Versão 0.9: changelog, README, documentos da engine | |
| F8 | Documento do impostor (formalização da ideia) | |
| F9 | Capítulo novo no Engine Book | |

## Média

| # | Tarefa | Observação |
|---|---|---|
| M1 | Filtros de textura: bilinear e anisotrópico | |
| M2 | Antisserrilhado: SMAA, TAA e MSAA leve, além do FXAA | TAA precisa de vetores de movimento |
| M3 | Pasta Assets: criar objeto, material e imagem; editar no Inspector ao clicar | |
| M4 | Arrastar objeto da Hierarchy para Assets (salvar para reutilizar em outra cena) | |
| M5 | Partículas com imagem e recorte de fundo (alfa) | depende de P1 |
| M6 | Oclusão estática (objetos e luzes estáticos não recalculam a oclusão) | |
| M7 | Culling de áudio | |
| M8 | Entity Manager: objeto que não pode ser destruído, com EntityManagerScript editável na pasta do jogo | |
| M9 | Atributo [visible] para mostrar variáveis do script no Inspector | depende de M8 |
| M10 | Gerenciamento automático de malhas (agrupar malhas em fila de render) | |

## Difícil

| # | Tarefa | Observação |
|---|---|---|
| D1 | Oclusão em GPU (HiZ), com modo CPU só para teste | em andamento (agente interrompido por limite de uso; retomar) |
| D2 | LODs por objeto e AutoLOD (simplificação de malha e textura, presets) | |
| D3 | Impostores: sistema único, atlas por ângulo, grupo com objeto de referência ("Allie"), distância de troca | depende de F8 e D2 |
| D4 | Simplificar malhas atrás de outras (preservando sombra) | depende de D2 |
| D5 | Caminho de movimento e render na GPU (buffer persistente, desenho indireto) | em andamento no código, sem medição |
| D6 | Usar os 10 núcleos com carga útil (CPU ocupada sem atrapalhar a GPU) | depende de medição |
| D7 | Cache em disco (SSD) para recursos que não precisam ficar na RAM | |
| D8 | Mais pesquisa de otimização (pooling, particionamento espacial, alocadores) | |

## Pendências técnicas que já existem

- Contadores de tempo na GPU: pronto, desligado por padrão (`docs/planos/33_TEMPO_GPU.md`).
- Inventário de trabalho da CPU e da GPU: pronto (`docs/planos/32_INVENTARIO_CPU_GPU.md`).
- ECS rumo a 1 milhão: item 3 concluído; medição de 100 mil em movimento com a máquina ociosa: 155 a 157 ms.
- Física híbrida, física de esferas na GPU e biblioteca de partículas v2: concluídas e documentadas.
- FSR 1.0 e FSR 4 (consulta de suporte e upscale de teste): concluídos; motion vectors e frame generation pendentes.
- Ray tracing por GPU: F0 concluída; F1 pendente.

## Ordem de execução

1. Documentação e versão (F7, F8, F9), em paralelo com as correções fáceis (F1, F3).
2. Campos numéricos com arraste (F2) e interface de partículas (F4, F5, M5), depois de F1.
3. Assets e arrastar para Assets (M3, M4), e o Entity Manager (M8, M9).
4. Filtros e antisserrilhado (M1, M2), oclusão estática (M6) e áudio (M7).
5. LODs e AutoLOD (D2), gerenciamento de malhas (M10), impostores (D3, D4).
6. GPU: oclusão em HiZ (D1), movimento e render na GPU (D5), uso dos núcleos (D6), cache em disco (D7).

Cada item só é considerado concluído com build, selftest, ctest e, quando for desempenho, medição com 3 execuções e mediana.
