# 29 - Item 3: versao por API (assinatura de sombra O(1))

## O que foi feito
- `World` (engine/ecs/include/tool/ecs/world.hpp): versao (epoch) por componente. Sobe em todo acesso MUTAVEL (get/getByIndex nao-const,
  each/parallelEach/forEachDense/parallelEachDense, add/tryAdd, remove, destroy) e a versao ESTRUTURAL sobe em create/destroy/dormencia/
  renew/resetTo. Leitura (get const, eachRead, denseView) nao sobe. `epochOf<T>()` e `structuralEpoch()` sao O(1).
- `Editor::beginWorldMatFrame` (editor_hier.hpp): reaproveita as matrizes mundiais se Transform, Parent e estrutural nao mudaram; senao
  reconstroi em paralelo (bit-identico ao calculo sob demanda, coberto no selftest).
- `Editor::casterSignatureEpoch`: assinatura de sombra = versoes de Transform/Parent/MeshRenderer/Inactive/estrutura + isolamento + malhas
  internas + modelos + voxel chunks (poucos). O(1) em relacao ao numero de objetos.
- O hash por valor virou VERIFICADOR (`valueSumsNow`, `verifyVersionsByValue`): em Debug roda ao fim de renderScene (assert com mensagem) e o
  selftest o chama explicitamente em Release.
- Bench: linha "world-matrix cache: N rebuilds, M misses in F frames; versions ...".
- Comentario de thread-safety de worldMat em fx.hpp corrigido.
- Limite conhecido: o LOD da esfera por distancia da camera nao entra na assinatura por versao (para o sol a camera ja esta na assinatura).

## Medicao (RX 6500 XT, Vulkan, 1280x720, dt 0.016667, 120 quadros, 3 execucoes, mediana)
Colunas: quadro medio ms / "lights, shadow signature" ms. "Antes" = binario com o hash por valor (ToolEditor_valuehash.exe), 3 execucoes a 100k.

| N | Modo | Antes: quadro / assinatura | Depois: quadro / assinatura (execucoes) |
|---|---|---|---|
| 10k | parado (--instances) | - | 12,4 / 0,01 (12,04; 12,42; 12,44) |
| 10k | movers | - | 24,0 / 0,01 (24,02; 24,00; 24,94) |
| 50k | parado | - | 28,0 / 0,01 (27,26; 67,97*; 28,03) |
| 50k | movers | - | 84,3 / 0,01 (82,92; 161,92*; 84,33) |
| 100k | parado | 46,2 / 0,83 | 45,7 / 0,01 (45,67; 45,68; 44,86) |
| 100k | movers | 164,0 / 2,78 | 173,6 / 0,01 (213,69*; 173,60; 163,78) |

(*) execucoes com pico de ruido da maquina (uma execucao em cada degrau 50k/100k movers dobrou/subiu sem relacao com o codigo; a
assinatura ficou em 0,01-0,03 ms nelas tambem). A mediana de movers 100k fica 6% acima do "antes" por causa desse ruido; a melhor execucao
(163,8) iguala o antes (164,0). Nao ha prova de piora, mas tambem nao ha prova estatistica de igualdade: repetir em maquina quieta.

## Criterios
| Criterio | Resultado |
|---|---|
| Selftest 0 falhas | 297 checks, 0 falhas |
| ctest -R "Editor\." Release | 74/74 passaram |
| Assinatura 100k parado <= 0,5 ms | 0,01 ms: passou |
| Assinatura 100k movers <= 2 ms | 0,01 ms: passou (antes 2,78) |
| Quadro movers 100k sem piora | mediana 173,6 x 164,0 antes; melhor 163,8; ruido alto: inconclusivo (ver acima) |
| Imagem igual ao caminho anterior | parado 100k: hash do Game b3b6123584b50be5 identico antes e depois (e identico entre as 3 execucoes); cena (scene) igual em todas. Movers: o hash do Game varia entre execucoes ate no binario antigo (fase da oscilacao nao deterministica), entao nao e comparavel |
| Falhas/reconstrucoes visiveis no bench | parado 100k: 1 reconstrucao em 180 quadros; movers: 1 por vista/quadro (419 em 419); 0 falhas de cache |

## Pendente
- Em movers todo Transform muda por quadro: a reconstrucao das matrizes continua O(N) (paralela); so a assinatura virou O(1).
- Medir de novo o quadro de movers 100k/50k com a maquina ociosa para fechar o criterio "sem piora" com estatistica.
