# Impostores (LOD por imagem)

Status: estudo e formalização da ideia. Não implementado. Tarefa D3 do plano 0.9 (`docs/planos/35_PLANO_V09.md`), dependente de F8 (este documento) e D2 (LODs por objeto e AutoLOD).

## 1. Ideia

Um objeto distante deixa de ser desenhado como malha e passa a ser desenhado como um plano (billboard) com a imagem dele. A troca acontece a partir de uma distância X.

- **Impostor de LOD**: a partir da distância de troca, o objeto é substituído por um plano com a imagem pré-renderizada do objeto.
- **Atlas por ângulo**: uma imagem só não serve quando a câmera vê o objeto de outro lado. Por isso o impostor tem várias vistas (atlas) do objeto, uma por ângulo (frente, lado, costas, etc.). Na troca, escolhe-se a vista mais próxima do ângulo atual da câmera.
- **Impostor que sempre encara a câmera**: o plano gira em torno do eixo vertical para sempre olhar para a câmera. É o caso ideal para árvores e vegetação, em que a variação de ângulo pouco aparece.
- **Gerenciador de impostores**: um sistema único, que decide quais objetos viram impostor, gera e guarda as imagens, e faz a troca. Em vez de cada objeto ter seu próprio controle, o gerenciador agrupa o trabalho, o que reduz o consumo de CPU e de GPU.

## 2. Modos de geração

Há duas opções.

**A. Mapa por objeto.** Cada objeto tem o seu atlas. É simples e fiel, mas gasta memória e tempo de geração por objeto.

**B. Grupo de impostores com objeto de referência.** Vários objetos iguais (mesma malha e material) reusam a mesma imagem. Um objeto de referência, chamado "Allie", é o objeto verdadeiro copiado para gerar as imagens. Ele fica fora da cena de jogo e só serve para gerar o atlas. Os objetos do grupo apenas referenciam o atlas, com a própria posição, escala e cor.

O modo B é o caminho esperado para florestas e multidões. O modo A serve para objetos únicos de destaque.

## 3. Formato de dados

Descrição do impostor, em JSON no projeto (campos propostos):

| Campo | Tipo | Significado |
|---|---|---|
| `switchDistance` | float (m) | Distância X a partir da qual o objeto vira impostor. Com histerese para não piscar na borda. |
| `angles` | int | Quantidade de vistas no atlas (ex.: 8 ou 16, distribuídas em 360 graus). |
| `atlasResolution` | int | Resolução de cada vista, em pixels (ex.: 256 ou 512). O atlas final é `angles` vistas em grade. |
| `mode` | `billboard` ou `atlas` | `billboard`: um plano que encara a câmera, sem vistas. `atlas`: escolhe a vista pelo ângulo. |
| `group` | nome ou id | Grupo de impostores que compartilha o mesmo atlas. Vazio = mapa próprio do objeto. |
| `reference` | id do objeto | Objeto de referência ("Allie") usado para gerar o atlas do grupo. |
| `upAxis` | eixo | Eixo vertical do billboard (normalmente Y). |

Regras:
- Objetos do mesmo `group` devem ter a mesma malha, material e escala-base; caso contrário não podem compartilhar o atlas.
- Se o objeto de referência mudar, o atlas do grupo é marcado como inválido e regenerado.

## 4. Integração

**Com LOD (D2).** O impostor é o último nível de LOD. A cadeia fica: LOD 0 (malha completa), LOD 1 e 2 (malhas simplificadas), impostor. A troca usa a mesma distância já calculada pelo LOD, para não duplicar o cálculo. A troca de LOD é feita pelo gerenciador, não pelo objeto.

**Com o culling.** O impostor é um quad com uma caixa de volume pequena. O culling de frustum e de oclusão usa a caixa do impostor, e não a da malha completa. Objetos fora do frustum não geram trabalho nem para o impostor. Quando o impostor estiver atrás de uma superfície, a oclusão em GPU (D1) deve tratá-lo como os demais objetos.

**Com o render.** Impostores de um mesmo atlas são desenhados em lote (instancing), com um único material. Sombra: ver seção 5.

## 5. Limites

- **Sombra.** Um plano não projeta sombra com a forma do objeto. Opções: não projetar sombra (aceitável para vegetação distante); usar uma sombra em silhueta ou um blob; ou manter a malha simplificada (LOD 1 ou 2) como projetora de sombra. A escolha precisa ser medida antes de adotar.
- **Parallax.** Com poucas vistas, o objeto parece "girar" em saltos quando a câmera anda. Mais vistas reduzem o salto, mas aumentam memória.
- **Transparência.** Bordas de folhagem e objetos com opacidade (Opacity do Inspector) precisam de canal alfa e de ordenação. O impostor transparente segue as mesmas limitações do passe transparente atual (uma camada por pixel).
- **Distância mínima.** Não trocar para impostor perto da câmera. Abaixo da distância mínima, o objeto deve usar a malha, senão a imagem fica borrada.
- **Iluminação.** O atlas é gerado com uma iluminação fixa. Objetos que mudam de luz ou de cor em tempo de execução não ficam corretos sem regeneração.
- **Memória.** Um atlas de 16 vistas em 512 px ocupa 16 x 512 x 512 pixels por material. Medir o tamanho real antes de definir o padrão.

## 6. Riscos

- Troca visível (salto de imagem) na borda da distância, se não houver histerese e transição.
- Atlas desatualizado se o material ou a malha mudar sem invalidação.
- Custo de geração ao carregar a cena. Precisa de cache em disco (D7) para não gerar a cada abertura.
- Sombra errada em cenas com sol forte; depende da escolha da seção 5.
- Complexidade do gerenciador: um sistema único não pode virar o gargalo de CPU que ele deveria remover.

## 7. Critérios de aceite (propostos; limiares a confirmar com medição)

A medição segue a regra do plano 0.9: 3 execuções e mediana, com build, selftest e ctest.

1. **Desempenho.** Cena com muitos objetos iguais (ex.: 10 mil árvores), a partir da distância de troca, deve reduzir o tempo de quadro em relação à malha completa. O ganho e a cena exata são definidos antes da medição.
2. **Qualidade.** Diferença de imagem entre malha e impostor, medida por pixel em ângulos fixos, abaixo de um limiar definido antes da implementação.
3. **Troca.** Nenhum salto visível em câmera lenta; a histerese evita alternância a cada quadro na borda.
4. **Memória.** Tamanho do atlas e memória por grupo registrados, por modo e resolução.
5. **Custo de geração.** Tempo para gerar o atlas de um grupo, registrado.
6. **Compatibilidade.** O caminho sem impostor continua igual (selftest e ctest passam; imagem idêntica quando nenhum objeto usa impostor).
7. **Invalidação.** Alterar a referência ou o material regenera o atlas do grupo.

## 8. Ordem de implementação

1. Formato de dados e campos da seção 3, com carga e salvamento no projeto e teste de ida e volta.
2. Impostor de modo `billboard` (sem atlas) para um objeto, com a troca pela distância e histerese.
3. Gerenciador de impostores: lista de objetos, troca e desenho em lote.
4. Geração de atlas offline por ângulo, no modo A (mapa por objeto), com a câmera de geração fora da cena de jogo.
5. Modo B: grupo com objeto de referência ("Allie") e invalidação.
6. Integração com o LOD (D2) e com o culling (frustum e oclusão).
7. Tratamento de sombra, escolhido pela medição da seção 5.
8. Cache em disco do atlas (D7), se a geração for cara.

Dependências: F8 (este documento) e D2 (LOD). Não depende de D1, mas ganha com ela.
