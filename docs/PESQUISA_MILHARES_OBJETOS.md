# Pesquisa: desenhar milhares (ou milhoes) de objetos com custo baixo

Data: 2026-11-18. Fontes externas citadas no fim de cada item. Numeros de fornecedores e de autores
independentes sao informados como estao; nenhum foi reproduzido aqui.

## 1. O que as engines fazem

### 1.1 Instanciamento com um desenho so (Unity, exemplo de um autor)
- A CPU envia as posicoes de todas as instancias uma vez para um buffer na GPU e emite um unico comando.
  O shader le cada instancia por `SV_InstanceID`. Um compute shader opcional faz o culling e escreve so as
  instancias visiveis no buffer de argumentos.
- Relato: 100.000 instancias, de 38 ms (26 FPS, 35 ms de CPU) para 0,4 ms (0,1 ms CPU + 0,3 ms GPU).
- Dado por instancia: um `float4` de 16 bytes. Os argumentos do desenho sao 5 `uint` no total, nao por instancia.
- Limites citados pelo autor: nao serve para objetos com callback por quadro na CPU, nem para malhas
  muito unicas que nao compartilham o desenho.
- Fonte: [How I render 100,000 Unity objects with one draw call](https://dev.to/michael_sacco_0d4c96f7eaf/how-i-render-100000-unity-objects-with-one-draw-call-38ms-to-04ms-2jhh)

### 1.2 Pipeline controlado pela GPU (vkguide)
- Os dados por instancia guardam um ObjectID e um BatchID (indice do comando de desenho).
- Culling em compute: frustum (esfera contra os planos) e oclusao por piramide de profundidade (Hi-Z),
  escolhendo o nivel de mip pelo tamanho da esfera na tela.
- A piramide usa o depth do quadro anterior: custa um quadro de latencia.
- O comando de desenho e preenchido com `atomicAdd` no `instanceCount`; alternativa: `DrawIndirectCount`.
- Transparentes nao podem ser ordenados com esse metodo (a ordem vira instavel).
- Fonte: [vkguide - compute culling](https://vkguide.dev/docs/gpudriven/compute_culling/)

### 1.3 GPU-controlled rendering em GPU movel (Imagination)
- Mover para a GPU: travessia de cena, ordenacao e culling. A CPU continua gerando os comandos iniciais
  e ordenando por material quando ha varios materiais.
- Relato em hardware TBDR (BXM-8-256): tempo do tiler de 8,73 ms para 4,55 ms por quadro com culling.
  O custo do culling no exemplo: 0,031 ms.
- Fonte: [Imagination - GPU Controlled Rendering](https://docs.imgtec.com/sdk-documentation/html/whitepapers/GPUControlledRendering.html)

### 1.4 GPU Resident Drawer (Unity 6)
- Usa o `BatchRendererGroup`: agrupa GameObjects em "Hybrid Batch Groups" e faz oclusao em GPU.
- Requisitos: Forward+, plataformas com compute shader (exceto OpenGL ES), SRP Batcher ligado.
- Limites: build mais lento (todas as variantes de shader), ganho depende da cena, geralmente menor na Scene
  e no Game view do que no build.
- Fonte: [Unity - GPU Resident Drawer](https://docs.unity.com/en-us/engine/6000.0/manual/analysis/graphics-performance-profiling/in-urp/reduce-draw-calls-urp/reduce-rendering-work-on-cpu/gpu-resident-drawer)

### 1.5 O caso de arvores (Toni Macaroni, motor autoral)
- Tecnicas citadas: LOD em clusters (CLOD) e geometria virtual no estilo Nanite; vegetacao distante em
  representacao de voxels no lugar de impostors; proxies para objetos muito distantes; iluminacao com
  espaco de tela e GI por voxels, sem ray tracing por hardware.
- Numeros autorreportados: RTX 3080, cerca de 130 FPS em 1440p; 200 FPS em visao aerea; 80 FPS em floresta densa.
- Atencao: a materia nao encontrou repositorio nem build publico; os numeros nao foram verificados; ha
  contradicao entre "3 milhoes de arvores" (legenda) e "cerca de 1 milhao de instancias" (texto).
- Fonte: [Wccftech - custom forest engine](https://wccftech.com/custom-forest-engine-delivers-stunning-visuals-at-130-fps-on-an-rtx-3080-at-1440p-without-hardware-ray-tracing/)

### 1.6 Arvores geradas inteiramente na GPU (pesquisa academica, HPG 2025)
- Geracao procedural de arvores em GPU com DirectX 12 work graphs; mais de 150 parametros; LOD automatico.
- Relato: cerca de 3 ms para gerar a geometria unica e enviar ao buffer, em RX 7900 XTX; "120 FPS estavel"
  sob carga pesada. Hardware suportado: RDNA 3+ e NVIDIA serie 30 em diante.
- Fonte: [TechPowerUp - pipeline GPU-only para arvores](https://www.techpowerup.com/338260/researchers-unveils-real-time-gpu-only-pipeline-for-fully-procedural-trees)

## 2. Estado do nosso renderizador (verificado no codigo)

Arquivo: `engine/render/include/tool/render/gpu_renderer.hpp`.

| Ponto | O que o codigo faz | Linha |
|---|---|---|
| Um desenho por objeto | `drawIndexed(count, 1)` dentro de um laco por `Draw` | 481-493 |
| Constante por objeto | `updateBuffer(objCb_, ...)` a cada objeto, com matriz e material | 482-483 |
| Estado por objeto | `setUniform`, `setTexture` e troca de malha por objeto | 486-490 |
| Sombras | o mesmo laco de objetos roda para cada cascata e cada face de luz pontual | 376-400 |
| Culling | nenhum: todos os objetos entram em `draws_` | 349-356 |
| Ordenacao | `stable_sort` por buffer de vertices (agrupa por malha, nao por material) | 419 |
| Instanciamento | nenhum | - |

Conclusao: o custo cresce com `objetos x (1 + cascatas + faces de luz)`, e cada objeto paga uma escrita
de buffer. Esse e o gargalo que as tecnicas acima atacam. As metricas ja existem em `DrawStats`:
`draws`, `triangles`, `pipelineSwitches` e `bufferUploads`.

## 3. Como aplicar (ordem de prioridade)

| Prioridade | Mudanca | Por que | Como validar |
|---|---|---|---|
| P1 | Instanciamento: agrupar `Draw` por malha e material; uma escrita de instancias (matriz e material) e um `drawIndexed(count, n)` por grupo | Remove a escrita de buffer e o estado por objeto | Pixels iguais aos atuais (teste visual) e `bufferUploads` cai para ~ numero de grupos |
| P2 | Culling por frustum na CPU antes de montar `draws_` e para cada cascata | Reduz objetos desenhados fora da tela | Teste unitario ja pronto (`frustum_cull.hpp`); teste visual com a camera girando (nao pode sumir objeto) |
| P3 | Culling em compute + comando indireto | Tira o trabalho de culling da CPU | Precisa adicionar desenho indireto no RHI (hoje nao existe); teste em GPU |
| P4 | Oclusao Hi-Z | Reduz desenho atras de paredes | Teste em GPU; aceitar um quadro de latencia |
| P5 | LOD por distancia e proxies | Reduz triangulos de objetos distantes | Medir `triangles` em cenas com muitas arvores |
| P6 | Arvores em voxels ou impostors para vegetacao distante | Custo fixo para floresta | Comparacao visual e de FPS, em GPU |

Observacoes:
- P1 e P2 podem ser feitos antes do GPU-driven completo e sao os de maior retorno por esforco.
- P2 ja tem o modulo puro e testado (commit `993d168`, `engine/render/include/tool/render/frustum_cull.hpp`).
  Nao esta ligado ao renderizador.
- Para o teste de tela: a Cena e o Jogo precisam estar visiveis para que `DrawStats` reflita o trabalho real
  (ver CODAR.MD, teste 9). Sem isso, as medidas do editor saem zeradas.

## 4. O que precisa de voce

1. Testar P2 na sua maquina: mover a camera e confirmar que nenhum objeto desaparece quando deveria aparecer.
2. Rodar o benchmark com `--ecs 1000` e `--play`, e me enviar `DrawStats` (draws, triangulos, bufferUploads).
   Isso mostra quanto P1 pode ganhar antes de eu mexer no renderizador.
3. Decidir se o alvo de cena e de milhoes de arvores (exige P3 a P6) ou de dezenas de milhares (P1 e P2 bastam).

## 5. Fontes

- [How I render 100,000 Unity objects with one draw call](https://dev.to/michael_sacco_0d4c96f7eaf/how-i-render-100000-unity-objects-with-one-draw-call-38ms-to-04ms-2jhh)
- [vkguide - GPU driven: compute culling](https://vkguide.dev/docs/gpudriven/compute_culling/)
- [Imagination - GPU Controlled Rendering](https://docs.imgtec.com/sdk-documentation/html/whitepapers/GPUControlledRendering.html)
- [Unity - GPU Resident Drawer](https://docs.unity.com/en-us/engine/6000.0/manual/analysis/graphics-performance-profiling/in-urp/reduce-draw-calls-urp/reduce-rendering-work-on-cpu/gpu-resident-drawer)
- [Wccftech - custom forest engine, 130 FPS RTX 3080](https://wccftech.com/custom-forest-engine-delivers-stunning-visuals-at-130-fps-on-an-rtx-3080-at-1440p-without-hardware-ray-tracing/)
- [TechPowerUp - pipeline GPU-only para arvores procedurais](https://www.techpowerup.com/338260/researchers-unveils-real-time-gpu-only-pipeline-for-fully-procedural-trees)
