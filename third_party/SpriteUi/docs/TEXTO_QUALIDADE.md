# Qualidade do texto: diagnostico

Medido com a fonte do projeto (`Fonts/Xmind/static/Xmind-Regular.ttf`), texto de teste
"Configuracoes Ajuda Hierarquia Gg". Referencia: rasterizacao exata do stb_truetype no tamanho de tela
(escala em em pixels, origem inteira). Erro = media de |amostra - referencia| em cobertura (0 a 1).

## Estado atual (fase 1 do roteiro)

O que o core entrega hoje para texto de bitmap, e o que continua fora:

- **Padrao de render:** `GenerateTextRenderItems` usa `FlagBitmapText`. O atlas `BuildTruetypeAtlas` e de cobertura
  e nunca vira MSDF. O metadado do atlas (`FontRenderMetadata`) deriva a flag (`TextShaderFlagFor`).
- **Resolucao do bitmap:** `ChooseBitmapRasterPixelSize` escolhe o em de rasterizacao como
  `ceil(tamanho logico x DPI x O)`, com O = 1,25 configuravel e buckets 16, 24, 32, 48, 64, 96, 128, 192 e 256.
  Exemplo de jogo: um rotulo de 16 pt em 2x de escala pede 40 px e usa o bucket 48. Acima de 256 px o bucket
  satura; texto que precisa de mais resolucao depende de MSDF (fase 3).
- **Cache:** `FontAtlasCache` guarda atlas por `FontAtlasKey` (conteudo da fonte, estilo, formato, bucket e
  charset), com limites de entradas e bytes. Nunca remove entrada em uso. A textura GPU e do host, que libera
  atras da fence.
- **Contrato logico para fisico:** layout, caret e hit-test nao dependem do DPI. Os glifos caem no grid de
  `devicePixelScale`, com erro de no maximo meio pixel fisico, e a conversao para pixels de tela acontece uma
  vez, no backend.
- **Limites de entrada:** atlas com no maximo `maxGlyphCount` codepoints (padrao 4096, teto 65536), em ate
  512 px e padding ate 64. Fonte com cabecalho ou tabela fora dos limites e recusada; `FontLoadPolicy` limita o
  tamanho do arquivo (32 MiB) e pode recusar fontes nao confiaveis.

Nao esta implementado: HarfBuzz (kerning, ligaduras, bidi), FreeType (parser completo), MSDF real, atlas
multipagina e a integracao com o cache de textura do host. Ver `docs/IMPLEMENTATION_STATUS.md`, secao de escopo
do texto.

Diagnostico historico (abaixo) mantido como registro da medicao que motivou a fase 1.

## Causas

1. **Origem fracionaria do glifo (causa principal).** Pen e linha de base caem em posicoes fracionarias
   (avancos somados sem arredondar; linha de base = topo da linha + ascent). `GenerateTextRenderItems` nao
   arredonda. Com a origem deslocada em 0,5 px, o erro medio sobe para 0,08 a 0,14, contra 0,00 com origem
   inteira (atlas no tamanho exato, 12, 16 e 24 px).

2. **Um unico atlas a 32 px para todos os tamanhos.** O host (`apps/editor/spriteui_host.hpp`) rasteriza a
   32 px e amostra em 12 a 36 px. Erro medio por tamanho de tela, atlas 32 px: 12 px 0,066; 16 px 0,002;
   18 px 0,023; 24 px 0,029; 36 px 0,055. Reducao de 2,7x (12 px) e o pior caso de minificacao.
   Escolha do tamanho do atlas (media sobre 12, 16, 18, 24, 36 px): 16 px 0,079; 20 px 0,063; 24 px 0,032;
   32 px 0,035; 40 px 0,047. Pior caso: 24 px 0,093 (36 px); 32 px 0,066 (12 px).

3. **Oversampling nao ajuda.** O stb_truetype calcula cobertura de area exata. Com atlas no proprio tamanho de
   tela o erro e 0,0000, entao sobreamostrar so acrescentaria filtragem. Nao foi implementado.

4. **Gama do blend.** O SDL mistura alfa em espaco sRGB, sem linearizar. Texto claro (sRGB 0,90) sobre fundo
   escuro (0,08) com cobertura 0,5 sai com luminancia 0,49, contra 0,66 do blend linear: o traco fica fino.
   Um expoente de cobertura de 0,5 aproxima o blend linear (erro medio 0,005). Vale para texto claro sobre
   fundo escuro, que e o tema do editor. Texto escuro sobre fundo claro piora com esse expoente.

5. **Amostragem.** O host usa `SDL_SCALEMODE_LINEAR`, correto. O harness de captura
   (`tests/Visual/ElementCaptures.cpp`, `AtlasSampler`) usa vizinho mais proximo e mistura em linear, entao as
   capturas antigas (`progress/janela-*.png`) nao representam o que o SDL mostra.

6. **Atlas.** 1024 x 1024, 352 glifos (U+0020 a U+017F), linha usada ate o pixel 184 (18% da altura). Sobra
   espaco, nao e gargalo. Borda de 1 px ja existe; a amostra no limite do retangulo do glifo nao vaza.

## Correcao

- Atlas do host: 24 px (era 32) e `coverageGamma` 0,5 (texto claro sobre fundo escuro, blend sRGB).
- Layout: `devicePixelScale` (padrao 1). Quads absolutos alinhados ao pixel de dispositivo; a largura da string
  continua a soma dos avancos.
- Limite conhecido: um unico tamanho de atlas. O ideal e um atlas por tamanho de tela em uso (proximo passo).

## Medicao

- Ferramentas de medicao e captura ficaram fora do repositorio (scratchpad). A captura usa o caminho do host:
  `SpriteUiSdlRenderer` com o renderer de software do SDL, o mesmo blend sRGB do editor.
- Capturas: `progress/texto-qualidade-01` a `04` (antes e depois, 1x e 1,5x) e `05` (comparacao 1x ampliada).
- Tinta total (soma do brilho acima do fundo): 1x sobe 21% (1997 para 2409); 1,5x sobe 18% (4497 para 5307).
