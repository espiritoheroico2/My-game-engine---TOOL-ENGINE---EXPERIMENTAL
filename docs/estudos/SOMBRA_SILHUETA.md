# Estudo: sombra por silhueta em vez de sombra calculada a cada quadro

Ideia proposta: desenhar a sombra em resolução maior, extrair a silhueta (o contorno geométrico) e desenhá-la como sombra, sem recalcular sombras reais o tempo todo. A silhueta poderia ser tingida e suavizada pela iluminação local que o Light Prism já entrega.

## 1. Estado atual (medido no código)

- Sombra do sol: mapas em cascatas (CSM). Sombras de luzes pontuais e spots: mapas por face (`apps/editor/editor_state.hpp`, `renderScene`).
- Já existe reuso ("sleeper"): se a assinatura da câmera, da luz e dos casters não muda, o mapa de sombra do quadro anterior é reaproveitado (`shadowSignature`, `pointShadowSignature`, `gpu.shadowPassSkipped()`). O Profiler conta esses reusos em `shadowReused`.
- Consequência: a sombra só é recalculada quando algo que a afeta muda. A ideia proposta cai em cima disso e precisa mostrar o que ganha além do reuso.
- Esferas do teste de ECS: voltaram a projetar sombra nesta sessão (`apps/editor/ecs_test.hpp`). Antes estavam com `castShadow = false` para medir só criação, atualização e física. Medir o custo com sombras ligadas é o primeiro passo deste estudo.

## 2. Faz sentido?

Sim, em partes. Separar os casos:

| Caso | Silhueta funciona? | Motivo |
|---|---|---|
| Chão plano, casters estáticos, luz do sol fixa | Sim, bem | Projeção planar da silhueta é barata e exata o bastante. É a técnica de sombras projetadas clássica. |
| Personagens ou objetos que se movem sobre chão plano | Sim, com custo | A silhueta muda a cada quadro; precisa atualizar só a região suja. |
| Sombra sobre geometria irregular (escadas, paredes, objetos empilhados) | Parcial | A projeção planar erra. Precisa de profundidade do receptor, ou volta a ser mapa de sombra. |
| Objeto sombreando outro objeto | Parcial | Exige ordem de profundidade. Silhueta sozinha não resolve. |
| Luz pontual ou spot móvel | Não diretamente | A silhueta muda com a luz; o custo volta ao de hoje. |
| Penumbra e cor da sombra pela luz local | Sim | Suavizar a máscara com desfoque proporcional à distância, e tingir com a cor da luz local do Light Prism, é viável e aproxima a penumbra física. |

Conclusão: a parte "tingir pela luz local e suavizar" é a mais valiosa e a mais barata. A parte "substituir a sombra real" só vale para receptores planos e casters estáticos ou de movimento lento. Para o resto, o mapa de sombra continua necessário.

Ponto a corrigir na ideia: a silhueta não é "mais realista que a sombra". Ela é uma sombra mais barata e controlável, com aparência própria. Isso pode ser melhor para o estilo do jogo, mas não é fisicamente mais correta em geral.

## 3. Quanto custa, e o que medir antes

Antes de implementar, medir se a sombra pesa o suficiente para valer o esforço:

1. Rodar `test ECS: 5000` com sombras ligadas (agora é o padrão) e exportar o Profiler. Ver quanto da zona "Scene preparation" e do "GPU render" é a passagem de sombra. Se for pouco, a ideia não compensa.
2. Repetir com a câmera parada e com a câmera em órbita (`--bench-camera orbit`). A câmera parada é onde o reuso ajuda; a órbita mostra o custo real.

Sem esses números, qualquer estimativa de ganho é chute.

## 4. Prazo estimado (julgamento, não medido)

| Etapa | Escopo | Prazo com agentes |
|---|---|---|
| E0 — medição | Números de sombra com `test ECS` e órbita | 1 dia |
| E1 — protótipo | Silhueta planar do sol para casters estáticos, máscara suavizada, cor pela luz local | 1 a 2 semanas |
| E2 — integração | Atualização por região suja, invalidação pelo `shadowSignature`, fallback para mapa de sombra | 2 a 3 semanas |
| E3 — luzes pontuais e receptores irregulares | Decidir caso a caso; provavelmente manter mapa de sombra | 3 a 4 semanas |
| E4 — qualidade | Testes visuais, comparação com a sombra real, ajustes | 1 a 2 semanas |

Total para uma primeira versão útil (E0 a E2, chão plano): 4 a 6 semanas. Para cobrir todos os casos: mais 4 a 6 semanas, e parte deles pode continuar com mapa de sombra.

## 5. Riscos

- Silhueta sem profundidade faz sombra aparecer através de paredes ou sobre objetos errados.
- Serrilhado e falta de estabilidade temporal ao mover a luz ou a câmera.
- Dois sistemas de sombra (silhueta e mapa) precisam concordar na mesma cena; isso aumenta a manutenção.
- Estilo: a sombra por silhueta tem uma aparência própria. Vale testar com o estilo do jogo antes de investir.

## 6. Recomendação

Medir primeiro (E0). Se a passagem de sombra for uma parte grande do quadro, fazer o protótipo E1 com a parte barata (máscara suavizada e tingida pela luz local). Não substituir o mapa de sombra das luzes pontuais nem das sombras em geometria irregular sem uma medição que justifique.
