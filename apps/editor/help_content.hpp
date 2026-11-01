// ToolEngine Editor - conteudo da janela Help (topicos + exemplos). Texto em INGLES e PORTUGUES (escolhido pelo idioma da UI).
// Marcacao simples: "# titulo", "- item", bloco ``` ... ``` = codigo (com botao Copiar), o resto = paragrafo.
// Para adicionar um topico: acrescente uma entrada em helpTopics(). O "id" e o que o icone "?" de cada janela abre.
#pragma once
#include <string>
#include <vector>

namespace editor {

struct HelpTopic { const char* id; const char* group; const char* titleEn; const char* titlePt; const char* bodyEn; const char* bodyPt; };

inline const std::vector<HelpTopic>& helpTopics() {
  static const std::vector<HelpTopic> t = {
  // ------------------------------------------------------------------------------------------------ BASICS
  {"start", "Basics", "Getting started", "Primeiros passos",
R"H(# Welcome
ToolEngine is a light 3D engine with an editor. Everything you see is a window you can drag, dock or close (Window menu).
# The 5-minute tour
- Hierarchy: the list of objects in the scene. "+ Create" adds cubes, cameras, lights, audio. Double-click an object to focus it.
- Inspector: the components of the selected object (position, mesh, physics, tools, script, tags).
- Scene: the editor view. Left click selects, W/E/R = Move/Rotate/Scale gizmo, right-drag looks around, middle-drag pans, wheel zooms.
- Game: what the Principal camera sees. Press Play to run physics, tools and sound.
- Assets: your project files. Drop .glb/.gltf/.fbx models and .wav/.mp3 sounds into the window to import them.
# Your first playable cube
1. Create > Cube. In the Inspector press "Add Physics" (Dynamic). Move it up a little.
2. Create > Plane, scale it to 14 x 14, press "Add Physics" and set Body = Static (it is the ground).
3. On the cube press "Add Tool > Jump". Press Play, then Space (or the A button of a gamepad).
Everything is saved with the scene (Ctrl+S). Ctrl+Z / Ctrl+Y undo and redo.)H",
R"H(# Bem-vindo
A ToolEngine e uma engine 3D leve com editor. Tudo que voce ve e uma janela que pode arrastar, encaixar ou fechar (menu Window).
# Tour de 5 minutos
- Hierarchy: lista de objetos da cena. "+ Create" adiciona cubos, cameras, luzes, audio. Clique duas vezes em um objeto para focar nele.
- Inspector: os componentes do objeto selecionado (posicao, malha, fisica, tools, script, tags).
- Scene: a visao do editor. Clique esquerdo seleciona, W/E/R = gizmo Mover/Girar/Escalar, arrastar com o direito gira a visao, botao do meio move, roda aproxima.
- Game: o que a camera Principal ve. Aperte Play para rodar fisica, tools e som.
- Assets: os arquivos do projeto. Solte modelos .glb/.gltf/.fbx e sons .wav/.mp3 na janela para importar.
# Seu primeiro cubo jogavel
1. Create > Cube. No Inspector aperte "Add Physics" (Dynamic). Suba-o um pouco.
2. Create > Plane, escale para 14 x 14, aperte "Add Physics" e troque Body = Static (e o chao).
3. No cubo aperte "Add Tool > Jump". Aperte Play e depois Espaco (ou o botao A do controle).
Tudo e salvo junto com a cena (Ctrl+S). Ctrl+Z / Ctrl+Y desfazem e refazem.)H"},

  {"scene", "Basics", "Scene view, gizmos and free navigation", "Janela Scene, gizmos e navegacao livre",
R"H(# Mouse
- Left click: select (a ray is cast from the cursor to the objects). Drag a gizmo handle to change the object.
- Right-drag: look around. Middle-drag: pan. Wheel: zoom.
- Double-click an object in the Hierarchy: the Scene view focuses on it.
# Gizmos
- W = Move (drag an arrow: one axis). E = Rotate (drag a ring). R = Scale (drag an axis tip, or the center square for uniform).
- Hold Ctrl while dragging to snap: 0.25 m, 15 degrees, 0.1 scale.
# Free navigation (Alt)
Press Alt inside the Scene view to fly like a first-person player: mouse looks, W A S D move, Q/E down/up, Shift is faster, wheel changes speed. Esc or Alt again leaves.
# Undo
Ctrl+Z undoes the last change (gizmo, Inspector, create, delete). Ctrl+Y or Ctrl+Shift+Z redoes.)H",
R"H(# Mouse
- Clique esquerdo: seleciona (um raio sai do cursor ate os objetos). Arraste uma alca do gizmo para alterar o objeto.
- Arrastar com o direito: olhar em volta. Botao do meio: mover a visao. Roda: zoom.
- Clique duplo em um objeto na Hierarchy: a Scene foca nele.
# Gizmos
- W = Mover (arraste uma seta: um eixo). E = Girar (arraste um anel). R = Escalar (arraste a ponta de um eixo, ou o quadrado central para escala uniforme).
- Segure Ctrl ao arrastar para encaixar: 0,25 m, 15 graus, 0,1 de escala.
# Navegacao livre (Alt)
Aperte Alt dentro da Scene para voar como um jogador em primeira pessoa: o mouse olha, W A S D andam, Q/E descem/sobem, Shift acelera, a roda muda a velocidade. Esc ou Alt de novo sai.
# Desfazer
Ctrl+Z desfaz a ultima mudanca (gizmo, Inspector, criar, apagar). Ctrl+Y ou Ctrl+Shift+Z refaz.)H"},

  {"objects", "Basics", "Objects, tags and scripts", "Objetos, tags e scripts",
R"H(# Objects
Every object has a Name and a Transform. Add what you need: Mesh Renderer (draws a shape or model), Light, Camera, Audio Source, Physics, Tools, Script.
# Tags
Tags group objects (for example "enemy"). Rename a tag in the Inspector and it changes on every object. Built-in tags (Principal, player, enemy) cannot be renamed, but you can link your own tag to one in Settings > Tags (for example "jogador" -> "player").
# Scripts (ToolObject)
Every new object already has a script file: Assets/Scripts/<Name>.tool.js with start(), update(dt), OnDelete(), DeleteAndReuse(), onCollisionEnter(other) and onTriggerEnter(other).
- "Edit script" edits it inside the Inspector. "Edit external" opens it in your code editor (Settings > Project sets which one).
- The script runtime is not running yet: use Tools (Jump, Move, Follow, Spin, OnCollision) today; the same calls will exist in scripts (see the Script API topic).)H",
R"H(# Objetos
Todo objeto tem Nome e Transform. Adicione o que precisar: Mesh Renderer (desenha uma forma ou modelo), Light, Camera, Audio Source, Physics, Tools, Script.
# Tags
Tags agrupam objetos (por exemplo "enemy"). Renomear uma tag no Inspector muda em todos os objetos. As tags pre-definidas (Principal, player, enemy) nao podem ser renomeadas, mas voce pode ligar uma tag sua a uma delas em Settings > Tags (por exemplo "jogador" -> "player").
# Scripts (ToolObject)
Todo objeto novo ja tem um arquivo: Assets/Scripts/<Nome>.tool.js com start(), update(dt), OnDelete(), DeleteAndReuse(), onCollisionEnter(other) e onTriggerEnter(other).
- "Edit script" edita dentro do Inspector. "Edit external" abre no seu editor de codigo (Settings > Project escolhe qual).
- Os scripts rodam durante o Play (runtime QuickJS). Sem codigo, use os Tools (Jump, Move, Follow, Spin, OnCollision). Veja o topico Script API.)H"},

  // ------------------------------------------------------------------------------------------------ PHYSICS
  {"physics", "Physics", "Physics (Newtoon)", "Fisica (Newtoon)",
R"H(# Add physics to an object
Inspector > Add Physics. Choose the Body:
- Static: never moves (ground, walls).
- Dynamic: falls, bounces and is pushed by others. Mass, friction and bounciness change how it behaves.
- Kinematic: moves only when YOU move it (platforms, doors); it pushes dynamic objects.
The collider size comes from the object's Scale. Collider = Auto uses a sphere for spheres and a box for everything else.
# World gravity (any axis)
Settings > Physics > Gravity X / Y / Z. Earth is (0, -9.81, 0). Try (0, 0, -9.81) to make the world "fall" toward -Z, or (0,0,0) for zero-g.
# Vector physics (per object)
Turn on "Vector physics" in an object's Physics component and give it its own gravity vector. It REPLACES the world gravity for that object only: a ball that falls sideways, a leaf that floats up, an enemy that walks on the ceiling.
# Triggers and events
Trigger = the object detects overlaps but does not push anything (pickup zones, damage areas). Use the OnCollision tool or the onCollisionEnter / onTriggerEnter script functions.
# Tips
- Fast small objects: tick CCD so they cannot pass through walls.
- Stacks and piles: raise Solver iterations in Settings > Physics.
- Objects at rest go to sleep automatically (zero cost) and wake when touched.)H",
R"H(# Adicionar fisica a um objeto
Inspector > Add Physics. Escolha o Body:
- Static: nunca se move (chao, paredes).
- Dynamic: cai, quica e e empurrado pelos outros. Massa, atrito e quique mudam o comportamento.
- Kinematic: so se move quando VOCE o move (plataformas, portas); empurra os dinamicos.
O tamanho do collider vem da Escala do objeto. Collider = Auto usa esfera para esferas e caixa para o resto.
# Gravidade do mundo (qualquer eixo)
Settings > Physics > Gravity X / Y / Z. A Terra e (0, -9,81, 0). Teste (0, 0, -9,81) para o mundo "cair" para -Z, ou (0,0,0) para gravidade zero.
# Vector physics (por objeto)
Ligue "Vector physics" no componente Physics do objeto e de a ele um vetor de gravidade proprio. Ele SUBSTITUI a gravidade do mundo so para aquele objeto: uma bola que cai de lado, uma folha que sobe, um inimigo que anda no teto.
# Triggers e eventos
Trigger = o objeto detecta sobreposicao mas nao empurra nada (areas de coleta, areas de dano). Use o tool OnCollision ou as funcoes onCollisionEnter / onTriggerEnter dos scripts.
# Dicas
- Objetos pequenos e rapidos: marque CCD para nao atravessarem paredes.
- Pilhas de objetos: aumente Solver iterations em Settings > Physics.
- Objetos parados dormem sozinhos (custo zero) e acordam ao serem tocados.)H"},

  {"tools", "Physics", "Tools (ready-made behaviors)", "Tools (comportamentos prontos)",
R"H(A Tool is a behavior you attach to an object, like adding an addon to its main code. Inspector > Add Tool.
- Jump: when the action (for example "jump") is pressed, the object jumps. Height = how high (m). "Allow air jump" enables double jumps. Needs a Dynamic Physics body.
- Move: reads two axes (moveX, moveY) from the Input Map and moves the object. Speed in m/s. "Face direction" turns the object. "Camera relative" moves relative to the Principal camera.
- Follow Target: moves this object (usually a camera) toward another object by name, keeping an offset. Smoothing 0 = glued, 8 = soft. "Look at target" aims at it.
- Spin: rotates continuously (degrees per second on X/Y/Z).
- On Collision: when this object touches another (optionally only objects with a tag): log a message, destroy the other, destroy itself, or bounce.
- Script library: attaches an extra .js file (a library or addon) to the object's script. Created in Assets/Scripts/Libs; it runs once the script runtime exists.
Tools can be combined: Move + Jump + Follow Target on the camera is a complete third-person controller. Each Tool has a "Player" field: which Input Map controller it listens to.)H",
R"H(Um Tool e um comportamento que voce acopla ao objeto, como adicionar um addon ao codigo principal dele. Inspector > Add Tool.
- Jump: quando a acao (por exemplo "jump") e apertada, o objeto pula. Height = altura (m). "Allow air jump" permite pulo duplo. Precisa de um corpo Dynamic.
- Move: le dois eixos (moveX, moveY) do Input Map e move o objeto. Speed em m/s. "Face direction" vira o objeto. "Camera relative" move em relacao a camera Principal.
- Follow Target: move este objeto (normalmente uma camera) ate outro objeto pelo nome, mantendo um deslocamento. Smoothing 0 = colado, 8 = suave. "Look at target" mira nele.
- Spin: gira continuamente (graus por segundo em X/Y/Z).
- On Collision: quando este objeto toca outro (opcionalmente so objetos com uma tag): escreve no Console, destroi o outro, destroi a si mesmo, ou quica.
- Script library: acopla um arquivo .js extra (biblioteca ou addon) ao script do objeto. Criado em Assets/Scripts/Libs; roda quando o runtime de scripts existir.
Tools se combinam: Move + Jump no personagem e Follow Target na camera formam um controle em terceira pessoa completo. Cada Tool tem um campo "Player": qual controle do Input Map ele escuta.)H"},

  {"inputmap", "Physics", "Input Map (keyboard and gamepad)", "Mapa de botoes (teclado e controle)",
R"H(Window > Input Map. An Input Map has one controller map per player. Each map has actions:
- Button action: "jump" = key Space OR gamepad A. Both work at the same time.
- Axis action: "moveX" = key A (-) and D (+) OR the left stick X.
Pick keys and buttons from the lists, or press "Listen" and touch the key / button you want.
# Controllers
- Add as many maps as you want (Player 1, Player 2, ...). Each map picks its gamepad (1 to 4) or "keyboard only".
- Xbox controllers are read through XInput on Windows (SDL3); PlayStation and Switch pads work too. Buttons are by position: South = A on Xbox = Cross on PlayStation.
# Using actions
In Tools choose the action name and the Player. In scripts: Input.pressed("jump"), Input.down("fire"), Input.axis("moveX").
The map is saved in Project/input_map.json (plain text you can edit).)H",
R"H(Window > Input Map. Um Input Map tem um mapa de controle por jogador. Cada mapa tem acoes:
- Acao de botao: "jump" = tecla Espaco OU botao A do controle. Os dois funcionam juntos.
- Acao de eixo: "moveX" = tecla A (-) e D (+) OU o analogico esquerdo X.
Escolha teclas e botoes nas listas, ou aperte "Listen" e toque na tecla / botao desejado.
# Controles
- Adicione quantos mapas quiser (Player 1, Player 2, ...). Cada mapa escolhe seu controle (1 a 4) ou "so teclado".
- Controles Xbox sao lidos pelo XInput no Windows (via SDL3); controles de PlayStation e Switch tambem funcionam. Os botoes sao por posicao: South = A no Xbox = Cruz no PlayStation.
# Usando as acoes
Nos Tools escolha o nome da acao e o Player. Nos scripts: Input.pressed("jump"), Input.down("fire"), Input.axis("moveX").
O mapa fica em Project/input_map.json (texto que voce pode editar).)H"},

  // ------------------------------------------------------------------------------------------------ EXAMPLES
  {"ex_jump", "Examples", "Example: make a cube jump", "Exemplo: fazer um cubo pular",
R"H(# With Tools (works today)
1. Create > Cube, then Add Physics (Dynamic). Put a static ground under it.
2. Add Tool > Jump. Action = jump, Height = 2, Player = 1.
3. Input Map: "jump" is Space and gamepad A by default. Change them to anything from the lists.
4. Press Play and press the key or button.
# The same thing in a script (when the script runtime is on)
```
class Cube extends ToolObject {
  start() {
    this.jumpHeight = 2;          // meters
  }
  update(dt) {
    // pressed = true only on the frame the key / button goes down
    if (Input.pressed("jump") && this.physics.grounded) {
      this.physics.jump(this.jumpHeight);
    }
  }
}
```
# Double jump
```
  start() { this.jumps = 0; }
  update(dt) {
    if (this.physics.grounded) this.jumps = 0;
    if (Input.pressed("jump") && this.jumps < 2) { this.physics.jump(2); this.jumps++; }
  }
```
# Jump with only a gamepad button
In the Input Map clear the Keyboard field of "jump" and keep "A / Cross (South)".)H",
R"H(# Com Tools (funciona hoje)
1. Create > Cube e depois Add Physics (Dynamic). Coloque um chao estatico embaixo.
2. Add Tool > Jump. Action = jump, Height = 2, Player = 1.
3. Input Map: "jump" e Espaco e o botao A do controle por padrao. Troque por qualquer coisa das listas.
4. Aperte Play e a tecla ou botao.
# A mesma coisa em script (quando o runtime de scripts estiver ligado)
```
class Cube extends ToolObject {
  start() {
    this.jumpHeight = 2;          // metros
  }
  update(dt) {
    // pressed = true so no quadro em que a tecla / botao desce
    if (Input.pressed("jump") && this.physics.grounded) {
      this.physics.jump(this.jumpHeight);
    }
  }
}
```
# Pulo duplo
```
  start() { this.jumps = 0; }
  update(dt) {
    if (this.physics.grounded) this.jumps = 0;
    if (Input.pressed("jump") && this.jumps < 2) { this.physics.jump(2); this.jumps++; }
  }
```
# Pulo so com botao de controle
No Input Map limpe o campo Keyboard de "jump" e mantenha "A / Cross (South)".)H"},

  {"ex_move", "Examples", "Example: walk with WASD or the stick", "Exemplo: andar com WASD ou analogico",
R"H(# With Tools
Add Physics (Dynamic, "Freeze rotation" on) and Add Tool > Move: X axis = moveX, Y axis = moveY, Speed = 5. W A S D and the left stick move the object.
Turn on "Camera relative" so "forward" is where the Principal camera looks, and "Face direction" so the object turns.
# In a script
```
class Player extends ToolObject {
  start() { this.speed = 5; }
  update(dt) {
    const x = Input.axis("moveX");   // -1 .. 1  (A/D or stick)
    const y = Input.axis("moveY");   // -1 .. 1  (S/W or stick)
    this.physics.setVelocityXZ(x * this.speed, -y * this.speed);
  }
}
```)H",
R"H(# Com Tools
Add Physics (Dynamic, "Freeze rotation" ligado) e Add Tool > Move: eixo X = moveX, eixo Y = moveY, Speed = 5. W A S D e o analogico esquerdo movem o objeto.
Ligue "Camera relative" para "frente" ser para onde a camera Principal olha, e "Face direction" para o objeto virar.
# Em script
```
class Player extends ToolObject {
  start() { this.speed = 5; }
  update(dt) {
    const x = Input.axis("moveX");   // -1 .. 1  (A/D ou analogico)
    const y = Input.axis("moveY");   // -1 .. 1  (S/W ou analogico)
    this.physics.setVelocityXZ(x * this.speed, -y * this.speed);
  }
}
```)H"},

  {"ex_camera", "Examples", "Example: camera follows an object", "Exemplo: camera seguindo um objeto",
R"H(# With Tools
1. Name your player object "Player".
2. Select the Camera (tagged Principal). Add Tool > Follow Target.
3. Target = Player, Offset = (0, 4, 8), Smoothing = 6, tick "Look at target".
Press Play: the camera trails the player. Smoothing 0 glues it; higher = snappier, lower = lazier.
# Top-down camera
Offset = (0, 12, 0.1) and Look at target.
# In a script (on the camera)
```
class FollowCam extends ToolObject {
  start() {
    this.target = Scene.find("Player");
    this.offset = vec3(0, 4, 8);
  }
  update(dt) {
    const want = this.target.position.add(this.offset);
    this.position = this.position.lerp(want, 1 - Math.exp(-6 * dt));
    this.lookAt(this.target.position);
  }
}
```)H",
R"H(# Com Tools
1. De o nome "Player" ao seu objeto jogador.
2. Selecione a Camera (com a tag Principal). Add Tool > Follow Target.
3. Target = Player, Offset = (0, 4, 8), Smoothing = 6, marque "Look at target".
Aperte Play: a camera acompanha o jogador. Smoothing 0 cola; maior = mais rapido, menor = mais preguicoso.
# Camera de cima
Offset = (0, 12, 0,1) e Look at target.
# Em script (na camera)
```
class FollowCam extends ToolObject {
  start() {
    this.target = Scene.find("Player");
    this.offset = vec3(0, 4, 8);
  }
  update(dt) {
    const want = this.target.position.add(this.offset);
    this.position = this.position.lerp(want, 1 - Math.exp(-6 * dt));
    this.lookAt(this.target.position);
  }
}
```)H"},

  {"ex_collision", "Examples", "Example: collisions, pickups and damage zones", "Exemplo: colisoes, coletaveis e areas de dano",
R"H(# Pickup (coin) with Tools
1. Create a Sphere named "Coin", Add Physics: Body = Static, tick Trigger.
2. Give the Player object the tag "player" (or link your tag in Settings > Tags).
3. On the Coin: Add Tool > On Collision. Only tag = player, Action = Destroy this object.
The coin disappears when the player touches it. Pressing Stop brings the scene back.
# Bounce pad
On a Static box: On Collision, Only tag = player, Action = Bounce with speed 12 (it launches the object that has the tool; put the tool on the player to launch it).
# In scripts
```
class Coin extends ToolObject {
  onTriggerEnter(other) {
    if (other.hasTag("player")) {
      Audio.play("Assets/coin.wav");
      Game.score = (Game.score || 0) + 1;
      this.destroy();
    }
  }
}

class Spikes extends ToolObject {
  onCollisionEnter(other) {
    if (other.hasTag("player")) other.send("damage", 10);
  }
}
```
# Ray cast (is something in front?)
```
const hit = Physics.raycast(this.position, this.forward, 10);
if (hit) console.log("saw", hit.object.name, "at", hit.distance, "m");
```)H",
R"H(# Coletavel (moeda) com Tools
1. Crie uma Sphere chamada "Coin", Add Physics: Body = Static, marque Trigger.
2. De ao objeto Player a tag "player" (ou ligue a sua tag em Settings > Tags).
3. Na Coin: Add Tool > On Collision. Only tag = player, Action = Destroy this object.
A moeda some quando o jogador encosta. Ao apertar Stop a cena volta.
# Trampolim
Numa caixa Static: On Collision, Only tag = player, Action = Bounce com velocidade 12 (lanca o objeto que tem o tool; ponha o tool no jogador para lanca-lo).
# Em scripts
```
class Coin extends ToolObject {
  onTriggerEnter(other) {
    if (other.hasTag("player")) {
      Audio.play("Assets/coin.wav");
      Game.score = (Game.score || 0) + 1;
      this.destroy();
    }
  }
}

class Spikes extends ToolObject {
  onCollisionEnter(other) {
    if (other.hasTag("player")) other.send("damage", 10);
  }
}
```
# Raycast (tem algo na frente?)
```
const hit = Physics.raycast(this.position, this.forward, 10);
if (hit) console.log("viu", hit.object.name, "a", hit.distance, "m");
```)H"},

  {"ex_gravity", "Examples", "Example: wall walking, zero-g and moon gravity", "Exemplo: andar na parede, gravidade zero e gravidade da lua",
R"H(# Moon
Settings > Physics > preset Moon: gravity (0, -1.62, 0). Jumps become much higher with the same Jump tool.
# Zero-g room
Preset Zero-G. Use Add Force in scripts or the Spin tool for floating objects.
# A ball that rolls to the wall
Select the ball > Physics > tick "Vector physics" > press the "Right" button: gravity (9.81, 0, 0). The rest of the world still falls down.
# Gravity switch
```
class GravityButton extends ToolObject {
  onTriggerEnter(other) {
    other.physics.gravity = vec3(0, 9.81, 0);   // falls UP, only this object
  }
}
```)H",
R"H(# Lua
Settings > Physics > preset Moon: gravidade (0, -1,62, 0). Os pulos ficam bem mais altos com o mesmo tool Jump.
# Sala de gravidade zero
Preset Zero-G. Use AddForce em scripts ou o tool Spin para objetos flutuando.
# Uma bola que rola para a parede
Selecione a bola > Physics > marque "Vector physics" > aperte "Right": gravidade (9,81, 0, 0). O resto do mundo continua caindo.
# Botao de gravidade
```
class GravityButton extends ToolObject {
  onTriggerEnter(other) {
    other.physics.gravity = vec3(0, 9.81, 0);   // cai para CIMA, so este objeto
  }
}
```)H"},

  {"api", "Examples", "Script API", "Script API",
R"H(Scripts run inside a sandbox: they can touch the scene and the Input Map, never your files or the network. Every script class extends ToolObject.
# Lifecycle
start(), update(dt), onCollisionEnter(other), onTriggerEnter(other), OnDelete(), DeleteAndReuse()
# Object
- this.position / this.rotation / this.scale (vec3), this.forward, this.name, this.hasTag("x"), this.destroy()
- this.physics: grounded, velocity, gravity (vector physics), jump(height), addForce(v), addImpulse(v), setVelocityXZ(x, z)
- Scene.find("Name"), Scene.findByTag("enemy"), Scene.spawn("Assets/enemy.prefab")
# World
- Input.down(a), Input.pressed(a), Input.released(a), Input.axis(a)
- Physics.raycast(origin, dir, maxDist), Physics.gravity
- Audio.play(path), console.log(...)
The Tools in the Inspector are the native version of these calls and keep working when scripts arrive.)H",
R"H(Scripts rodam em uma sandbox: mexem na cena e no Input Map, nunca nos seus arquivos ou na rede. Toda classe de script estende ToolObject.
# Ciclo de vida
start(), update(dt), onCollisionEnter(other), onTriggerEnter(other), OnDelete(), DeleteAndReuse()
# Objeto
- this.position / this.rotation / this.scale (vec3), this.forward, this.name, this.hasTag("x"), this.destroy()
- this.physics: grounded, velocity, gravity (vector physics), jump(altura), addForce(v), addImpulse(v), setVelocityXZ(x, z)
- Scene.find("Nome"), Scene.findByTag("enemy"), Scene.spawn("Assets/enemy.prefab")
# Mundo
- Input.down(a), Input.pressed(a), Input.released(a), Input.axis(a)
- Physics.raycast(origem, dir, distMax), Physics.gravity
- Audio.play(caminho), console.log(...)
Os Tools do Inspector sao a versao nativa dessas chamadas e continuam funcionando quando os scripts chegarem.)H"},

  // ------------------------------------------------------------------------------------------------ RENDER
  {"lighting", "Rendering", "Lighting, shadows and post-processing", "Iluminacao, sombras e pos-processamento",
R"H(# Lights
- Global Light = the sun (color, direction, day/night cycle). Only the first one is used.
- Light source (Point/Spot) lights nearby objects. Tick "Casts shadows" on a point light to give it real shadows (costs 6 extra depth passes only while enabled).
# Shadows
Each object decides: "Cast shadow" (throws a shadow) and "Receive shadow" (gets shadows). A ground usually only receives; a tree trunk usually only casts.
Settings > Lighting: resolution, distance, bias, soft edges (PCF), strength. Fewer casters = faster: zero cost if nobody casts.
# Post-processing
Global (Settings > Lighting) and per camera (Inspector > Camera): exposure, contrast, saturation, tint, vignette, sharpen. Camera values add to or replace the global ones.)H",
R"H(# Luzes
- Global Light = o sol (cor, direcao, ciclo dia/noite). So a primeira e usada.
- Light source (Point/Spot) ilumina objetos proximos. Marque "Casts shadows" em uma luz pontual para dar sombras reais a ela (custa 6 passes de profundidade extras so enquanto ligado).
# Sombras
Cada objeto decide: "Cast shadow" (projeta sombra) e "Receive shadow" (recebe sombra). O chao normalmente so recebe; o tronco de uma arvore normalmente so projeta.
Settings > Lighting: resolucao, distancia, bias, bordas suaves (PCF), forca. Menos projetores = mais rapido: custo zero se ninguem projeta.
# Pos-processamento
Global (Settings > Lighting) e por camera (Inspector > Camera): exposicao, contraste, saturacao, tinta, vinheta, nitidez. Os valores da camera somam ou substituem os globais.)H"},

  {"materials", "Rendering", "Materials and shaders", "Materiais e shaders",
R"H(Inspector > Material. A Material file (.tmat) holds color, roughness, metallic, emissive and an optional shader file. Assign it to a Mesh Renderer.
- Create: Assets > right-click > New material (or Inspector > Material > New).
- Shader file: a .hlsl / .glsl pixel shader applied on top of the object's color/texture. It runs on the GPU backends (Vulkan / OpenGL / D3D12); the CPU renderer ignores it and shows the plain color.
- Keep shaders small: they run for every pixel of the object.)H",
R"H(Inspector > Material. Um arquivo de Material (.tmat) guarda cor, rugosidade, metalico, emissivo e um arquivo de shader opcional. Atribua a um Mesh Renderer.
- Criar: Assets > botao direito > New material (ou Inspector > Material > New).
- Arquivo de shader: um pixel shader .hlsl / .glsl aplicado por cima da cor/textura do objeto. Roda nos backends de GPU (Vulkan / OpenGL / D3D12); o renderizador de CPU ignora e mostra a cor simples.
- Mantenha shaders pequenos: rodam para cada pixel do objeto.)H"},

  // ------------------------------------------------------------------------------------------------ WINDOWS
  {"narrative", "Windows", "Narrative Controller", "Narrative Controller",
R"H(A visual script made of nodes: triggers, conditions, variables and actions. Drag from a node's output pin to another node's input pin to connect them. Save writes a .json file; the graph runs on the same events as scripts.)H",
R"H(Um script visual feito de nos: gatilhos, condicoes, variaveis e acoes. Arraste do pino de saida de um no ate o pino de entrada de outro para ligar. Save grava um arquivo .json; o grafo roda nos mesmos eventos dos scripts.)H"},

  {"profiler", "Windows", "Profiler and graphics settings", "Profiler e configuracoes graficas",
R"H(Profiler: FPS, frame time, 1% low, per-zone times (events, update, panels, scene, game, post-fx, UI, present), memory and draw counters, and the physics step (bodies, awake, pairs, contacts).
Settings > Graphics: API (CPU / Vulkan / OpenGL / D3D12), render scale / fixed resolution, FPS limit, GPU info, shadow quality. If a GPU backend reports errors for several frames the editor falls back to CPU by itself.)H",
R"H(Profiler: FPS, tempo de quadro, 1% low, tempos por zona (eventos, update, paineis, cena, jogo, pos-fx, UI, present), memoria e contadores de desenho, e o passo de fisica (corpos, acordados, pares, contatos).
Settings > Graphics: API (CPU / Vulkan / OpenGL / D3D12), escala de render / resolucao fixa, limite de FPS, info da GPU, qualidade de sombras. Se um backend de GPU der erro por varios quadros o editor volta para CPU sozinho.)H"},

  {"shortcuts", "Windows", "Keyboard shortcuts", "Atalhos de teclado",
R"H(- Ctrl+S save scene   - Ctrl+Z undo   - Ctrl+Y / Ctrl+Shift+Z redo   - Delete remove selected (Hierarchy)
- W / E / R move / rotate / scale gizmo   - Alt free navigation (Esc leaves)   - Ctrl while dragging = snap
- Double-click in Hierarchy = focus   - F1 = this Help window)H",
R"H(- Ctrl+S salvar cena   - Ctrl+Z desfazer   - Ctrl+Y / Ctrl+Shift+Z refazer   - Delete remove o selecionado (Hierarchy)
- W / E / R gizmo mover / girar / escalar   - Alt navegacao livre (Esc sai)   - Ctrl ao arrastar = encaixe
- Clique duplo na Hierarchy = focar   - F1 = esta janela de Help)H"},
  // ------------------------------------------------------------------------------------------------ SCRIPTS (novo)
  {"script_examples", "Scripts", "ToolObject script: examples", "Script ToolObject: exemplos",
R"H(# Status
Scripts are OPTIONAL: an object has no script until you press "Add Script" in the Inspector.
The examples below run on Play (QuickJS runtime). The code is saved with the object. Each script has its own context and can only ask the engine for changes through: send, setActive, instantiate, destroy, addComponent, removeComponent, setPosition("Name", x, y, z). To move another object, use setPosition; writing another object's transform directly is not supported. An error stops only that object's script and appears in the Console and in the Inspector (Script status). Reload scripts restarts them during Play.

# 1. Control an object (move and spin)
```
export function update(obj, dt) {
  obj.transform.pos[1] += Math.sin(time.now) * dt;   // sobe e desce
  obj.transform.rot[1] += 45 * dt;                   // gira 45 graus/s no eixo Y
}
```

# 2. Send an event to another object
Send by object name, by tag, or to itself:
```
export function onCollisionEnter(obj, other) {
  send("Door", "open");        // pelo nome do objeto
  send("tag:enemy", "alert");  // pela tag (todos os objetos com essa tag)
  send(obj, "hit");            // para si mesmo
}
```

# 3. Receive an event
```
export function OnEventTrigger(obj, name) {
  if (name === "cena3") {
    obj.transform.pos = [4, 4, 5];
    debug.console("posicao corrigida");
  }
}
```
The same event names can start a Narrative graph (trigger block "trigger" with that event).

# 4. Instantiate and destroy
```
export function start(obj) {
  const b = instantiate("Bullet");   // cria uma copia do objeto chamado Bullet
  destroy(b, 2.0);                   // apaga depois de 2 segundos
}
```

# 5. Add or remove components
```
export function start(obj) {
  addComponent(obj, "Light");
  removeComponent(obj, "Particles");
}
```

# 6. Disable and enable objects
```
setActive("Enemy 3", false);   // desativa (some e nao atualiza)
setActive(obj, true);          // ativa de novo
```

# In the Narrative Controller (works today)
- Block **send** (action `send`, argument `target|event`): sends an event to an object by name, `tag:<tag>` or `*` (all).
- Blocks **spawn** / **destroy** / **ui_show** / **ui_hide** / **particles_on** / **particles_off** take an object name as argument.
)H",
R"H(# Situacao
Scripts sao OPCIONAIS: um objeto so tem script depois que voce clica em "Add Script" no Inspector.
Os exemplos abaixo rodam durante o Play (runtime QuickJS). O codigo fica salvo com o objeto. Cada script tem o seu proprio contexto e so pode pedir mudancas ao motor por: send, setActive, instantiate, destroy, addComponent, removeComponent, setPosition("Nome", x, y, z). Para mover outro objeto, use setPosition; escrever direto no transform de outro objeto ainda nao e suportado. Um erro para so o script daquele objeto e aparece no Console e no Inspector (Script status). Reload scripts reinicia os scripts durante o Play.

# 1. Controlar um objeto (mover e girar)
```
export function update(obj, dt) {
  obj.transform.pos[1] += Math.sin(time.now) * dt;   // sobe e desce
  obj.transform.rot[1] += 45 * dt;                   // gira 45 graus/s no eixo Y
}
```

# 2. Enviar evento para outro objeto
Envie pelo nome do objeto, pela tag, ou para si mesmo:
```
export function onCollisionEnter(obj, other) {
  send("Door", "open");        // pelo nome do objeto
  send("tag:enemy", "alert");  // pela tag (todos os objetos com essa tag)
  send(obj, "hit");            // para si mesmo
}
```

# 3. Receber um evento
```
export function OnEventTrigger(obj, name) {
  if (name === "cena3") {
    obj.transform.pos = [4, 4, 5];
    debug.console("posicao corrigida");
  }
}
```
Os mesmos nomes de evento podem iniciar um grafo do Narrative (bloco "trigger" com esse evento).

# 4. Criar e destruir objetos
```
export function start(obj) {
  const b = instantiate("Bullet");   // cria uma copia do objeto chamado Bullet
  destroy(b, 2.0);                   // apaga depois de 2 segundos
}
```

# 5. Adicionar e remover componentes
```
export function start(obj) {
  addComponent(obj, "Light");
  removeComponent(obj, "Particles");
}
```

# 6. Desativar e ativar objetos
```
setActive("Enemy 3", false);   // desativa (some e nao atualiza)
setActive(obj, true);          // ativa de novo
```

# No Controlador Narrativo (funciona hoje)
- Bloco **send** (acao `send`, argumento `alvo|evento`): envia um evento para um objeto pelo nome, `tag:<tag>` ou `*` (todos).
- Blocos **spawn** / **destroy** / **ui_show** / **ui_hide** / **particles_on** / **particles_off** recebem o nome do objeto como argumento.
- Acao **setActive** (argumento `nome|1` ou `nome|0`): ativa ou desativa um objeto.
)H"},
  {"jslib", "Scripts", "JS script library (include)", "Biblioteca JS (include)",
R"H(# What it is
The **Script library** tool (Inspector > Tools > Add Tool > Script library) attaches an extra `.js` file to an object.
The functions exported by that file become available to the object's code, as if they were written in it.

# When to use
- Formulas shared by many objects (damage, score, experience curve).
- A state machine or helper used by enemies and by the player.
- Reusing one file in several objects without copying the code.

# How it works
1. Add Tool > Script library: the engine creates `Assets/Scripts/Libs/LibN.js` with an example function.
2. Edit it with "Edit external" or "Edit script".
3. Export the functions you want to share:
```
// Assets/Scripts/Libs/Combat.js
export function damage(base, armor) {
  return Math.max(1, base - armor);
}
```
4. In the object's script: `import { damage } from "Libs/Combat.js";` (the runtime loads the library before the object's code).

# Notes
- A library runs in the same sandbox as the object's script: no file or network access outside the project.
- Keep libraries small and free of per-frame heavy loops. Heavy loops belong in a native Tool.
- Status: libraries are saved and listed today; they run once the script runtime (QuickJS) is enabled.
)H",
R"H(# O que e
A ferramenta **Script library** (Inspector > Tools > Add Tool > Script library) anexa um arquivo `.js` extra a um objeto.
As funcoes exportadas por esse arquivo ficam disponiveis no codigo do objeto, como se tivessem sido escritas nele.

# Quando usar
- Formulas compartilhadas por varios objetos (dano, pontuacao, curva de experiencia).
- Uma maquina de estados ou um auxiliar usado por inimigos e pelo jogador.
- Reaproveitar um arquivo em varios objetos sem copiar codigo.

# Como funciona
1. Add Tool > Script library: a engine cria `Assets/Scripts/Libs/LibN.js` com uma funcao de exemplo.
2. Edite com "Edit external" ou "Edit script".
3. Exporte as funcoes que quer compartilhar:
```
// Assets/Scripts/Libs/Combat.js
export function damage(base, armor) {
  return Math.max(1, base - armor);
}
```
4. No script do objeto: `import { damage } from "Libs/Combat.js";` (o runtime carrega a biblioteca antes do codigo do objeto).

# Observacoes
- A biblioteca roda no mesmo sandbox do script do objeto: sem acesso a arquivos ou rede fora do projeto.
- Mantenha bibliotecas pequenas e sem laços pesados por quadro. Laços pesados pertencem a uma Tool nativa.
- Situacao: as bibliotecas sao salvas e listadas hoje; rodam quando o runtime de scripts (QuickJS) estiver ativo.
)H"},
  };
  return t;
}

} // namespace editor
