# Revisao do sistema de iluminacao (0.7.10-beta)

Estado do codigo em `engine/render/include/tool/render/` (`lighting.hpp`, `shadow.hpp`), `soft_renderer.hpp`,
`gpu_renderer.hpp` e `apps/editor/editor_state.hpp`. Esta revisao e so de leitura: nenhuma sombra nova foi implementada.

## O que existe hoje

| Recurso | Estado | Onde |
|---|---|---|
| Sombra da luz global (sol) | Sim. Um mapa de sombra ortografico, cobrindo uma esfera de 40 m (padrao). | `shadow.hpp` (`makeSunShadow`) |
| Filtro de borda | PCF fixo: caixa 3x3 (padrao) ou 5x5. | `soft_renderer.hpp` (`sunVisibility`) |
| Bias | Constante de profundidade (0,0025) escalado pelo angulo, mais normal bias de 1,5 texel. | `shadow.hpp`, `sunVisibility` |
| Sombra de luz pontual / spot | **Nao existe.** | ver abaixo |
| Cascatas de sombra (CSM) | Funcao `cascadeSplits` escrita, **nao usada**. | `lighting.hpp` |
| Clusters de luz (Forward+) | Funcao `assignLights` e `ClusterAssignment` escritas, **nao usadas** pelo renderizador. | `lighting.hpp` |
| Atenuacao | 1/d^2 com janela suave (1-(d/r)^4)^2, igual no CPU e no HLSL. | `lighting.hpp` (`attenuation`) |
| Spot | Cone com angulo interno e externo. | `lighting.hpp` (`spotFactor`) |
| Paridade CPU / GPU | A mesma matriz de sombra e usada nos dois caminhos. | `shadow.hpp` |

## Erro encontrado e corrigido

- O Help (EN e PT) dizia que marcar "Casts shadows" numa luz pontual dava sombras reais. **Isso era falso**:
  o Inspector nao tem essa caixa, e `editor_state.hpp` forca `castShadow = false` para toda luz pontual
  (linha ~578). O texto foi corrigido nesta versao.

## Problemas, por prioridade

1. **Luz pontual e spot sem sombra.** Para a luz pontual, o caminho natural e o mapa de cubo: 6 passes de
   profundidade (uma por face, FOV de 90 graus), e o shader escolhe a face pela direcao da luz. Custo: 6 passes
   por luz com sombra. Deve ser limitado a poucas luzes ativas.
2. **Codigo morto que diz o contrario.** O cabecalho de `lighting.hpp` diz que as luzes sao atribuidas a clusters
   e que ha sombras em cascata. Nenhum dos dois esta ligado. Ou se liga, ou o comentario muda.
3. **Cobertura da sombra do sol.** Um unico mapa cobre uma esfera de 40 m em volta do centro da cena. Com 1024
   texels, cada texel mede cerca de 8 cm, e tudo fora da esfera nao tem sombra. Cascatas (CSM) resolveriam
   os dois problemas: texels finos perto da camera e cobertura longe.
4. **Qualidade do filtro.** PCF 3x3 tem 9 amostras, entao a borda cai em degraus de 1/9. PCF 5x5 suaviza mais, mas
   custa 25 leituras por pixel. Filtros de media (VSM, ESM) ou PCSS sao alternativas a estudar.
5. **Bias fixo.** O bias de profundidade e constante (com um ajuste pelo angulo). Bias por inclinacao da
   superficie (slope-scaled) e mais robusto, mas nao existe. Sem ele, ha risco de "acne" (listras) ou de a sombra
   descolar do objeto (peter-panning) em contatos.
6. **Sem custo de GPU medido.** O Profiler mede CPU, mas nao o tempo do passe de sombra na GPU. Sem
   timestamps de GPU nao da para saber quanto custa cada luz com sombra.
7. **Luzes sem culling.** O caminho GPU envia todas as luzes do quadro (`gpu_renderer.hpp`, laco de upload).
   Sem os clusters, nao ha corte por distancia das luzes que nao tocam o pixel. Ainda nao verifiquei o HLSL
   para ver se o pixel shader percorre todas as luzes.

## Proximos passos sugeridos (antes de implementar)

- Ler o documento de tecnicas de sombra que voce citou. **Nao consegui abri-lo** (o link do Chalmers devolveu erro
  e a busca nao achou o arquivo). Se voce anexar o PDF, eu uso os algoritmos dele como referencia.
- Medir o custo atual: adicionar zonas de tempo de GPU ao Profiler (timestamp queries) para o passe de sombra.
- Decidir a ordem: (a) sombra de ponto com mapa de cubo; (b) CSM para o sol; (c) filtro melhor.
- Escolher entre implementar ou remover `cascadeSplits` e `assignLights` antes de adicionar mais codigo em cima.
