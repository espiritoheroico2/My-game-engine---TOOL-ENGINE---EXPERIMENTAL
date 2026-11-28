# 36 - Migração do sistema antigo de partículas (F4 e F5)

Status: concluído. O componente `ParticleComp` (sistema antigo, `fx.hpp`) foi removido. Existe um único sistema de partículas: `ParticleFxComp`, sobre a biblioteca `engine/particles`.

## F4 - Objeto "Particle" na Hierarchy

O menu **+ Create** da Hierarchy ganhou duas opções: **Particle** (preset de fumaça) e **Particle (fire)** (preset de fogo). Cada uma cria um objeto comum (Transform e nome "Particle") com `ParticleFxComp`, grava `Effects/*.json` se ainda não existirem (`fxlib::makeParticleObject`, em `fx_particles.hpp`) e seleciona o objeto. Ele aparece na lista, é selecionado e movido como qualquer objeto, e o preset (fumaça, fogo ou fluido) pode ser trocado no Inspector.

## F5 - Quadro de migração

| Recurso do `ParticleComp` antigo | Como ficou em `ParticleFxComp` / biblioteca |
|---|---|
| `rate`, `emitting`, `maxParticles` (1 a 5000) | `emitter.rate`, `playing`, `maxParticles`. O sistema agora respeita o máximo exato (o pool arredonda para chunks de 4096, mas há um limite de vivas). |
| `lifetime` (±20%) | `init.life` = [0,8 x, 1,2 x] |
| `speed` (0,75x a 1,25x), `spread` (cone) | `init.speed`, `init.coneDeg`, direção +Y |
| `size` e `sizeEnd` | `init.size` + módulo `sizeOverLife` |
| 4 cores ao longo da vida (início, 1/3, 2/3, fim) | **novo**: `colorOverLife` com 4 paradas (`mid1`, `mid2` no JSON; `stops`, `c2`, `c3` no módulo). Inspector: "Four color stops". |
| `alpha`, `fadeOpacity`, `fadeDelay` (s) | cor inicial com alfa + `alphaOverLife`. **novo**: parâmetro `delay` (fração da vida) |
| `form` (7 formas) | **novo**: `Effect.form` (JSON `form`). Desenhada na vista por software; a GPU usa o sprite padrão (formas na GPU ficam para M5, partículas com imagem) |
| `area` caixa / esfera + `areaSize` | `emitter.shape` = `box` (meias-medidas = tamanho/2) ou `sphere` (raio = tamanho[0]/2) |
| `gravityScale` (gravidade da cena x escala) | módulo `gravity` com vetor próprio (o editor mostra o vetor) |
| `bounce` + `floorY` (rebote no chão) | **novo**: módulo `floor` (`y`, `bounce`, `friction`); Inspector "FX path and floor" |
| `route` (curva Catmull-Rom, até 8 pontos locais) | **novo**: módulo `route` (`points`, `follow`); os pontos giram com o objeto; a Scene desenha a curva do objeto selecionado |
| `tornado` (redemoinho: velocidade, raio, altura) | **novo**: módulo `swirl` (`turns`, `radius`, `height`, `axis`, `follow`); o eixo gira com o objeto |
| `loop`, `worldSpace` | `emitter.loop`. `worldSpace` e `loop` não tinham efeito na simulação antiga e não foram portados |

Não se perdeu nenhum recurso. Diferenças conhecidas (registradas, sem perda de função):
- A caixa e a esfera de nascimento usam os eixos do mundo (a antiga seguia a rotação do objeto). A direção de emissão, a rota e o eixo do redemoinho seguem a rotação do objeto.
- A opacidade cai com a curva suave (smoothstep) em vez de linear.
- Na conversão de cenas a gravidade usada é a padrão do mundo (0, -9,8, 0) vezes `gravityScale`; a gravidade da cena não é conhecida na hora de carregar o objeto.
- Rota e tornado ignoram velocidade, gravidade e turbulência (como no antigo): a conversão zera a velocidade inicial e não cria gravidade nem chão.

## Conversão de cenas antigas

Ao abrir uma cena, `loadEntity` (`apps/editor/components.hpp`) vê a chave `"particles"` (formato do `ParticleComp`) e converte: lê os campos antigos em `tool::particles::LegacyEmitter`, converte com `effectFromLegacy` (`engine/particles/.../legacy.hpp`) e adiciona `ParticleFxComp`. O objeto fica sem arquivo de efeito (efeito feito à mão). O Console mostra uma linha por objeto convertido, com a taxa e o máximo. Ao salvar, só existe `particlesFx` (a chave `particles` deixa de ser gravada). Se o objeto tiver as duas chaves, `particlesFx` vence.

Scripts: `addComponent(obj, "ParticleFxComp")` é o nome novo; `"ParticleComp"` ainda é aceito e vira `ParticleFxComp`. A ação de narrativa `particles_on/off` liga e desliga `playing`.

## O que foi removido

`ParticleComp`; `Editor::updateParticles`, `Editor::ParticleSim`, `Editor::Particle` e o orçamento `particleBudget`; `fxdetail` (cor, alfa, rota e área antigas); `applySmokePreset`; o recurso "Particle System" do menu de recursos; a seção "Particles" do Inspector (e suas subseções); os ganchos `smokePreset` e `removeParticles`; as chaves de tradução órfãs em `assets/lang/pt-BR.json` e `es.json`; o objeto de exemplo da cena nova agora usa `ParticleFxComp` com o preset de fogo.

## Testes

- `tests/test_particles.cpp`: chão com rebote, trajetória, trajetória que acompanha o objeto, redemoinho, cor em 4 paradas, atraso de opacidade, JSON dos módulos novos e valores inválidos, limite de vivas e a conversão `effectFromLegacy`.
- `--selftest`: Particle na Hierarchy cria `ParticleFxComp`; cena antiga converte mantendo taxa e máximo, cores, área, rota, redemoinho e rebote; depois de carregar não sobra componente antigo; a cena convertida salva e reabre igual.
