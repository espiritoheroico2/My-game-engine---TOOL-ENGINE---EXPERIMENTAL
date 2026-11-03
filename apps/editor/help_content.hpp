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
Scripts are optional. "Add Script" in the Inspector creates Assets/Scripts/<Name>.tool.js with start(obj), update(obj, dt) and OnEventTrigger(obj, name). See "Scripts: examples (QuickJS)".
- "Edit script" edits it inside the Inspector. "Edit external" opens it in your code editor (Settings > Project sets which one).
- The script runtime is not running yet: use Tools (Jump, Move, Follow, Spin, OnCollision) today; the same calls will exist in scripts (see the Script API topic).)H",
R"H(# Objetos
Todo objeto tem Nome e Transform. Adicione o que precisar: Mesh Renderer (desenha uma forma ou modelo), Light, Camera, Audio Source, Physics, Tools, Script.
# Tags
Tags agrupam objetos (por exemplo "enemy"). Renomear uma tag no Inspector muda em todos os objetos. As tags pre-definidas (Principal, player, enemy) nao podem ser renomeadas, mas voce pode ligar uma tag sua a uma delas em Settings > Tags (por exemplo "jogador" -> "player").
# Scripts (ToolObject)
Scripts sao opcionais. "Add Script" no Inspector cria Assets/Scripts/<Nome>.tool.js com start(obj), update(obj, dt) e OnEventTrigger(obj, name). Veja "Scripts: exemplos (QuickJS)".
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
Trigger = the object detects overlaps but does not push anything (pickup zones, damage areas). Use the OnCollision tool (Inspector > Add Tool).
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
Trigger = o objeto detecta sobreposicao mas nao empurra nada (areas de coleta, areas de dano). Para isso use o tool OnCollision (Inspector > Add Tool).
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
In Tools choose the action name and the Player. In scripts, read the keys directly: input.pressed("Space"), input.down("W"), input.mouseDelta() (see "Scripts: examples").
The map is saved in Project/input_map.json (plain text you can edit).)H",
R"H(Window > Input Map. Um Input Map tem um mapa de controle por jogador. Cada mapa tem acoes:
- Acao de botao: "jump" = tecla Espaco OU botao A do controle. Os dois funcionam juntos.
- Acao de eixo: "moveX" = tecla A (-) e D (+) OU o analogico esquerdo X.
Escolha teclas e botoes nas listas, ou aperte "Listen" e toque na tecla / botao desejado.
# Controles
- Adicione quantos mapas quiser (Player 1, Player 2, ...). Cada mapa escolhe seu controle (1 a 4) ou "so teclado".
- Controles Xbox sao lidos pelo XInput no Windows (via SDL3); controles de PlayStation e Switch tambem funcionam. Os botoes sao por posicao: South = A no Xbox = Cruz no PlayStation.
# Usando as acoes
Nos Tools escolha o nome da acao e o Player. Nos scripts, leia as teclas direto: input.pressed("Space"), input.down("W"), input.mouseDelta() (veja "Scripts: exemplos").
O mapa fica em Project/input_map.json (texto que voce pode editar).)H"},

  // ------------------------------------------------------------------------------------------------ EXAMPLES
  {"ex_jump", "Examples", "Example: make a cube jump", "Exemplo: fazer um cubo pular",
R"H(# With Tools (works today)
1. Create > Cube, then Add Physics (Dynamic). Put a static ground under it.
2. Add Tool > Jump. Action = jump, Height = 2, Player = 1.
3. Input Map: "jump" is Space and gamepad A by default. Change them to anything from the lists.
4. Press Play and press the key or button.
# The same thing in a script (when the script runtime is on)
Scripted version: not available with this runtime yet. Use the native Tool above, or see the working script examples in "Scripts: examples (QuickJS)".
# Double jump
Scripted version: not available with this runtime yet. Use the native Tool above, or see the working script examples in "Scripts: examples (QuickJS)".
# Jump with only a gamepad button
In the Input Map clear the Keyboard field of "jump" and keep "A / Cross (South)".)H",
R"H(# Com Tools (funciona hoje)
1. Create > Cube e depois Add Physics (Dynamic). Coloque um chao estatico embaixo.
2. Add Tool > Jump. Action = jump, Height = 2, Player = 1.
3. Input Map: "jump" e Espaco e o botao A do controle por padrao. Troque por qualquer coisa das listas.
4. Aperte Play e a tecla ou botao.
# A mesma coisa em script (quando o runtime de scripts estiver ligado)
Versao em script: ainda nao disponivel neste runtime. Use o Tool nativo acima, ou veja os exemplos de script que funcionam em "Scripts: exemplos (QuickJS)".
# Pulo duplo
Versao em script: ainda nao disponivel neste runtime. Use o Tool nativo acima, ou veja os exemplos de script que funcionam em "Scripts: exemplos (QuickJS)".
# Pulo so com botao de controle
No Input Map limpe o campo Keyboard de "jump" e mantenha "A / Cross (South)".)H"},

  {"ex_move", "Examples", "Example: walk with WASD or the stick", "Exemplo: andar com WASD ou analogico",
R"H(# With Tools
Add Physics (Dynamic, "Freeze rotation" on) and Add Tool > Move: X axis = moveX, Y axis = moveY, Speed = 5. W A S D and the left stick move the object.
Turn on "Camera relative" so "forward" is where the Principal camera looks, and "Face direction" so the object turns.
# In a script
Scripted version: not available with this runtime yet. Use the native Tool above, or see the working script examples in "Scripts: examples (QuickJS)".
)H",
R"H(# Com Tools
Add Physics (Dynamic, "Freeze rotation" ligado) e Add Tool > Move: eixo X = moveX, eixo Y = moveY, Speed = 5. W A S D e o analogico esquerdo movem o objeto.
Ligue "Camera relative" para "frente" ser para onde a camera Principal olha, e "Face direction" para o objeto virar.
# Em script
Versao em script: ainda nao disponivel neste runtime. Use o Tool nativo acima, ou veja os exemplos de script que funcionam em "Scripts: exemplos (QuickJS)".
)H"},

  {"ex_camera", "Examples", "Example: camera follows an object", "Exemplo: camera seguindo um objeto",
R"H(# With Tools
1. Name your player object "Player".
2. Select the Camera (tagged Principal). Add Tool > Follow Target.
3. Target = Player, Offset = (0, 4, 8), Smoothing = 6, tick "Look at target".
Press Play: the camera trails the player. Smoothing 0 glues it; higher = snappier, lower = lazier.
# Top-down camera
Offset = (0, 12, 0.1) and Look at target.
# In a script (on the camera)
Scripted version: not available with this runtime yet. Use the native Tool above, or see the working script examples in "Scripts: examples (QuickJS)".
)H",
R"H(# Com Tools
1. De o nome "Player" ao seu objeto jogador.
2. Selecione a Camera (com a tag Principal). Add Tool > Follow Target.
3. Target = Player, Offset = (0, 4, 8), Smoothing = 6, marque "Look at target".
Aperte Play: a camera acompanha o jogador. Smoothing 0 cola; maior = mais rapido, menor = mais preguicoso.
# Camera de cima
Offset = (0, 12, 0,1) e Look at target.
# Em script (na camera)
Versao em script: ainda nao disponivel neste runtime. Use o Tool nativo acima, ou veja os exemplos de script que funcionam em "Scripts: exemplos (QuickJS)".
)H"},

  {"ex_collision", "Examples", "Example: collisions, pickups and damage zones", "Exemplo: colisoes, coletaveis e areas de dano",
R"H(# Pickup (coin) with Tools
1. Create a Sphere named "Coin", Add Physics: Body = Static, tick Trigger.
2. Give the Player object the tag "player" (or link your tag in Settings > Tags).
3. On the Coin: Add Tool > On Collision. Only tag = player, Action = Destroy this object.
The coin disappears when the player touches it. Pressing Stop brings the scene back.
# Bounce pad
On a Static box: On Collision, Only tag = player, Action = Bounce with speed 12 (it launches the object that has the tool; put the tool on the player to launch it).
# In scripts
Scripted version: not available with this runtime yet. Use the native Tool above, or see the working script examples in "Scripts: examples (QuickJS)".
# Ray cast (is something in front?)
Scripted version: not available with this runtime yet. Use the native Tool above, or see the working script examples in "Scripts: examples (QuickJS)".
)H",
R"H(# Coletavel (moeda) com Tools
1. Crie uma Sphere chamada "Coin", Add Physics: Body = Static, marque Trigger.
2. De ao objeto Player a tag "player" (ou ligue a sua tag em Settings > Tags).
3. Na Coin: Add Tool > On Collision. Only tag = player, Action = Destroy this object.
A moeda some quando o jogador encosta. Ao apertar Stop a cena volta.
# Trampolim
Numa caixa Static: On Collision, Only tag = player, Action = Bounce com velocidade 12 (lanca o objeto que tem o tool; ponha o tool no jogador para lanca-lo).
# Em scripts
Versao em script: ainda nao disponivel neste runtime. Use o Tool nativo acima, ou veja os exemplos de script que funcionam em "Scripts: exemplos (QuickJS)".
# Raycast (tem algo na frente?)
Versao em script: ainda nao disponivel neste runtime. Use o Tool nativo acima, ou veja os exemplos de script que funcionam em "Scripts: exemplos (QuickJS)".
)H"},

  {"ex_gravity", "Examples", "Example: wall walking, zero-g and moon gravity", "Exemplo: andar na parede, gravidade zero e gravidade da lua",
R"H(# Moon
Settings > Physics > preset Moon: gravity (0, -1.62, 0). Jumps become much higher with the same Jump tool.
# Zero-g room
Preset Zero-G. Use Add Force in scripts or the Spin tool for floating objects.
# A ball that rolls to the wall
Select the ball > Physics > tick "Vector physics" > press the "Right" button: gravity (9.81, 0, 0). The rest of the world still falls down.
# Gravity switch
Scripted version: not available with this runtime yet. Use the native Tool above, or see the working script examples in "Scripts: examples (QuickJS)".
)H",
R"H(# Lua
Settings > Physics > preset Moon: gravidade (0, -1,62, 0). Os pulos ficam bem mais altos com o mesmo tool Jump.
# Sala de gravidade zero
Preset Zero-G. Use AddForce em scripts ou o tool Spin para objetos flutuando.
# Uma bola que rola para a parede
Selecione a bola > Physics > marque "Vector physics" > aperte "Right": gravidade (9,81, 0, 0). O resto do mundo continua caindo.
# Botao de gravidade
Versao em script: ainda nao disponivel neste runtime. Use o Tool nativo acima, ou veja os exemplos de script que funcionam em "Scripts: exemplos (QuickJS)".
)H"},

  {"api", "Examples", "Script API (QuickJS)", "API de script (QuickJS)",
R"H(Scripts are plain JavaScript functions (QuickJS). There are no classes and no `this` in scripts. Each object with a script has its own context, and its variables are private to it.
# Entry points
- start(obj): once, when the object is active during Play
- update(obj, dt): every frame
- OnEventTrigger(obj, name): every event sent to the object (send, or the Narrative)
# The object (obj)
- obj.name: the object's name (copies have names like "Bala#1")
- obj.transform.pos / rot / scale: arrays [x, y, z]. Rotation is in DEGREES. Change them in place (obj.transform.pos[1] += 1), or assign a new array.
- Forward is the object's -Z axis turned by rot[1]: forward = (-sin(rot[1]), 0, -cos(rot[1])) in radians. The cameras look the same way.
# Time and log
- time.now (game seconds), time.dt, debug.console(message) (writes to the Console)
- Math, JSON and the usual JavaScript functions work.
# Changes (queued: applied right after the script returns)
- send(target, event): target = object name, "tag:<tag>" or "*"
- setActive(target, on): on/off
- instantiate(template, x, y, z): copy of the object named template at (x, y, z). Returns the copy's name ("Bala#1").
- destroy(target, seconds): removes now, or after the given seconds
- setPosition(target, x, y, z) / setRotation(target, x, y, z) / setColor(target, r, g, b) (colors 0 to 1, MeshRenderer)
- addComponent(target, type) / removeComponent(target, type): types MeshRenderer, AudioSource, ParticleComp
# Reading
- position(name): [x, y, z] of an object, or undefined if it does not exist
- input.down("W"): held now. input.pressed("Space"): pressed this frame. input.mouseDelta(): [dx, dy] mouse movement this frame.
- Keys: W A S D Q E R F 1 2 Space Shift Ctrl Alt Up Down Left Right Enter Escape, and Mouse0 (left) / Mouse1 (right)
# Limits
Memory 16 MB for all scripts, stack 256 KB, 5 ms per call. An error stops only that object's script; the message shows in the Console and in the Inspector.
# Errors and try/catch
- An error stops only this object's script. The Console shows the object name, the function and the line (for example "[Cube] update: TypeError ... (linha 5)"). The Inspector shows the same message.
- Catch an error in your code: try { setPosition("Camera", "abc", 0, 0); } catch (e) { debug.warn("camera not moved: " + e.message); }
- debug.console(message) writes an info line; debug.warn(message) a warning and debug.error(message) an error. The script keeps running.
- Names are checked: a command to an object that is not in the scene shows a warning (TOOL-S005) and does nothing.
- While you type (Inspector > ToolObject Script) the editor checks the script and lists syntax errors and common mistakes with their line. Console > Check scripts checks every object. Codes: docs/SCRIPT_ERRORS.md.
# Not available yet
this.position, Input.axis, Scene.find, Physics.raycast, onCollisionEnter, import of script libraries, custom shader parameters. Use the Tools (Inspector > Add Tool) for physics, collisions and the native versions of these actions.)H",
R"H(Scripts sao funcoes JavaScript simples (QuickJS). Nao ha classes nem `this` nos scripts. Cada objeto com script tem o seu proprio contexto, e as variaveis dele sao privadas.
# Funcoes de entrada
- start(obj): uma vez, quando o objeto fica ativo durante o Play
- update(obj, dt): todo quadro
- OnEventTrigger(obj, name): cada evento enviado ao objeto (send, ou o Narrative)
# O objeto (obj)
- obj.name: o nome do objeto (as copias tem nomes como "Bala#1")
- obj.transform.pos / rot / scale: listas [x, y, z]. A rotacao e em GRAUS. Mude no lugar (obj.transform.pos[1] += 1) ou atribua uma lista nova.
- A frente e o eixo -Z do objeto girado por rot[1]: frente = (-sen(rot[1]), 0, -cos(rot[1])) em radianos. As cameras olham do mesmo jeito.
# Tempo e log
- time.now (segundos de jogo), time.dt, debug.console(mensagem) (escreve no Console)
- Math, JSON e as funcoes comuns do JavaScript funcionam.
# Pedidos (entram na fila: aplicados logo depois que o script devolve o controle)
- send(alvo, evento): alvo = nome do objeto, "tag:<tag>" ou "*"
- setActive(alvo, ligado): true/false
- instantiate(modelo, x, y, z): copia do objeto chamado modelo na posicao (x, y, z). Devolve o nome da copia ("Bala#1").
- destroy(alvo, segundos): apaga agora, ou depois dos segundos informados
- setPosition(alvo, x, y, z) / setRotation(alvo, x, y, z) / setColor(alvo, r, g, b) (cores de 0 a 1, MeshRenderer)
- addComponent(alvo, tipo) / removeComponent(alvo, tipo): tipos MeshRenderer, AudioSource, ParticleComp
# Leitura
- position(nome): [x, y, z] de um objeto, ou undefined se nao existir
- input.down("W"): mantida agora. input.pressed("Space"): apertada neste quadro. input.mouseDelta(): [dx, dy] movimento do mouse neste quadro.
- Teclas: W A S D Q E R F 1 2 Space Shift Ctrl Alt Up Down Left Right Enter Escape, e Mouse0 (esquerdo) / Mouse1 (direito)
# Limites
Memoria de 16 MB para todos os scripts, pilha de 256 KB, 5 ms por chamada. Um erro para so o script daquele objeto; a mensagem aparece no Console e no Inspector.
# Erros e try/catch
- Um erro para so o script deste objeto. O Console mostra o nome do objeto, a funcao e a linha (por exemplo "[Cubo] update: TypeError ... (linha 5)"). O Inspector mostra a mesma mensagem.
- Trate um erro no seu codigo: try { setPosition("Camera", "abc", 0, 0); } catch (e) { debug.warn("camera nao movida: " + e.message); }
- debug.console(mensagem) escreve uma linha de info; debug.warn(mensagem) um aviso e debug.error(mensagem) um erro. O script continua rodando.
- Nomes sao conferidos: um comando para um objeto que nao esta na cena mostra um aviso (TOOL-S005) e nao faz nada.
- Enquanto voce digita (Inspector > ToolObject Script) o editor verifica o script e lista erros de sintaxe e erros comuns com a linha. Console > Check scripts verifica todos os objetos. Codigos: docs/SCRIPT_ERRORS.md.
# Ainda nao disponivel
this.position, Input.axis, Scene.find, Physics.raycast, onCollisionEnter, import de bibliotecas de script, parametros de shader. Para fisica, colisoes e as versoes nativas dessas acoes, use os Tools (Inspector > Add Tool).)H"},
  // ------------------------------------------------------------------------------------------------ RENDER
  {"lighting", "Rendering", "Lighting, shadows and post-processing", "Iluminacao, sombras e pos-processamento",
R"H(# Lights
- Global Light = the sun (color, direction, day/night cycle). Only the first one is used.
- Light source (Point/Spot) lights nearby objects. They do NOT cast shadows yet: only the Global Light (sun) does. Point and spot shadows are planned (see docs/LIGHTING_REVIEW.md).
# Shadows
Each object decides: "Cast shadow" (throws a shadow) and "Receive shadow" (gets shadows). A ground usually only receives; a tree trunk usually only casts.
Settings > Lighting: resolution, distance, bias, soft edges (PCF), strength. Fewer casters = faster: zero cost if nobody casts.
# Post-processing
Global (Settings > Lighting) and per camera (Inspector > Camera): exposure, contrast, saturation, tint, vignette, sharpen. Camera values add to or replace the global ones.)H",
R"H(# Luzes
- Global Light = o sol (cor, direcao, ciclo dia/noite). So a primeira e usada.
- Light source (Point/Spot) ilumina objetos proximos. Ainda NAO projetam sombras: so a luz global (sol) projeta. Sombras de luzes pontuais e de spot estao planejadas (veja docs/LIGHTING_REVIEW.md).
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
  {"script_examples", "Scripts", "Scripts: examples (QuickJS)", "Scripts: exemplos (QuickJS)",
R"H(# Status
Scripts are OPTIONAL: an object has no script until you press "Add Script" in the Inspector. The code is saved with the object and runs on Play. Every example below was run by the editor's selftest, so the code is valid.
Copy an example, paste it in the script of an object (Inspector > Script > Edit), and press Play. The objects named in the examples must exist in the scene with exactly those names.

# 1. Move and spin
```
export function update(obj, dt) {
  obj.transform.pos[1] = 1 + Math.sin(time.now) * 0.5;   // sobe e desce
  obj.transform.rot[1] += 45 * dt;                       // gira 45 graus por segundo (eixo Y)
}
```

# 2. Walk with WASD (no physics)
```
export function update(obj, dt) {
  const r = obj.transform.rot[1] * Math.PI / 180;
  const fx = -Math.sin(r), fz = -Math.cos(r);   // frente do objeto
  const dx = Math.cos(r), dz = -Math.sin(r);    // direita do objeto
  let mx = 0, mz = 0;
  if (input.down("W")) { mx += fx; mz += fz; }
  if (input.down("S")) { mx -= fx; mz -= fz; }
  if (input.down("D")) { mx += dx; mz += dz; }
  if (input.down("A")) { mx -= dx; mz -= dz; }
  obj.transform.pos[0] += mx * 5 * dt;
  obj.transform.pos[2] += mz * 5 * dt;
}
```

# 3. First person
Put this on the player. The camera must be a separate object named "Camera" (not a child of the player) with the Principal tag.
```
let yaw = 0, pitch = 0;
export function update(obj, dt) {
  const m = input.mouseDelta();
  yaw -= m[0] * 0.2;                                          // mouse para os lados gira
  pitch = Math.max(-80, Math.min(80, pitch - m[1] * 0.2));    // mouse para cima olha para cima
  obj.transform.rot[1] = yaw;
  const r = yaw * Math.PI / 180;
  const fx = -Math.sin(r), fz = -Math.cos(r), dx = Math.cos(r), dz = -Math.sin(r);
  let mx = 0, mz = 0;
  if (input.down("W")) { mx += fx; mz += fz; }
  if (input.down("S")) { mx -= fx; mz -= fz; }
  if (input.down("D")) { mx += dx; mz += dz; }
  if (input.down("A")) { mx -= dx; mz -= dz; }
  obj.transform.pos[0] += mx * 5 * dt;
  obj.transform.pos[2] += mz * 5 * dt;
  const p = obj.transform.pos;
  setPosition("Camera", p[0], p[1] + 1.6, p[2]);   // olhos a 1,6 m do chao
  setRotation("Camera", pitch, yaw, 0);
}
```

# 4. Third person
Put this on the player. The camera "Camera" is a separate object and follows behind.
```
let orbita = 0;
export function update(obj, dt) {
  const m = input.mouseDelta();
  orbita -= m[0] * 0.3;                              // arrastar o mouse gira a camera em volta
  obj.transform.rot[1] = orbita;
  const r = orbita * Math.PI / 180;
  const fx = -Math.sin(r), fz = -Math.cos(r), dx = Math.cos(r), dz = -Math.sin(r);
  let mx = 0, mz = 0;
  if (input.down("W")) { mx += fx; mz += fz; }
  if (input.down("S")) { mx -= fx; mz -= fz; }
  if (input.down("D")) { mx += dx; mz += dz; }
  if (input.down("A")) { mx -= dx; mz -= dz; }
  obj.transform.pos[0] += mx * 5 * dt;
  obj.transform.pos[2] += mz * 5 * dt;
  const p = obj.transform.pos;
  const d = 4, alt = 1.8;                            // distancia e altura da camera
  setPosition("Camera", p[0] - fx * d, p[1] + alt, p[2] - fz * d);
  setRotation("Camera", -15, orbita, 0);             // olha um pouco para baixo, para o jogador
}
```

# 5. Projectile and shoot
Put this on the player. Create a cube named "Bala" in the scene (it stays still; only the copies fly). Press F to shoot.
```
export function update(obj, dt) {
  if (input.pressed("F")) {
    const p = obj.transform.pos, r = obj.transform.rot;
    const bala = instantiate("Bala", p[0], p[1] + 1, p[2]);   // devolve o nome da copia: "Bala#1", "Bala#2"...
    setRotation(bala, 0, r[1], 0);
  }
}
```
Put this on the object "Bala" too. It makes each copy fly, hit the object "Alvo" and disappear after 3 seconds.
```
let idade = 0;
export function update(obj, dt) {
  if (obj.name === "Bala") return;                 // o modelo fica parado: so as copias voam
  idade += dt;
  const r = obj.transform.rot[1] * Math.PI / 180, v = 25 * dt;
  obj.transform.pos[0] -= Math.sin(r) * v;
  obj.transform.pos[2] -= Math.cos(r) * v;
  const alvo = position("Alvo");
  if (alvo) {
    const dx = alvo[0] - obj.transform.pos[0], dy = alvo[1] - obj.transform.pos[1], dz = alvo[2] - obj.transform.pos[2];
    if (dx * dx + dy * dy + dz * dz < 0.25) { send("Alvo", "acertado"); destroy(obj.name); return; }
  }
  if (idade > 3) destroy(obj.name);
}
```
And on the object "Alvo", react to the hit:
```
export function OnEventTrigger(obj, name) {
  if (name === "acertado") {
    setColor(obj.name, 1, 0, 0);
    debug.console("acertou o alvo!");
  }
}
```

# 6. Change the mesh color
Cycles the object's color every half second (values from 0 to 1).
```
let ultima = -1;
export function update(obj, dt) {
  const i = Math.floor(time.now * 2) % 3;
  if (i === ultima) return;
  ultima = i;
  if (i === 0) setColor(obj.name, 1, 0.2, 0.2);
  else if (i === 1) setColor(obj.name, 0.2, 1, 0.2);
  else setColor(obj.name, 0.2, 0.4, 1);
}
```

# 7. Change a shader property
Not exposed to scripts yet. What a script can change on a material today is the base color, with setColor (example 6). Roughness, metallic and custom shader parameters depend on the material (.tmat) parameter system, which is not done yet.

# 8. Send an event
```
export function update(obj, dt) {
  if (input.pressed("E")) {
    send("Porta", "abrir");          // pelo nome do objeto
    send("tag:inimigo", "alerta");   // pela tag: todos os objetos com a tag "inimigo"
    send("*", "tudo");               // para todos os objetos
  }
}
```

# 9. Receive an event
```
export function OnEventTrigger(obj, name) {
  if (name === "abrir") {
    setColor(obj.name, 0.2, 1, 0.2);
    debug.console(obj.name + " recebeu abrir");
  }
}
```

# 10. Send a trigger to the Narrative
The Narrative graph receives the same events as the scripts. Add a "trigger" block with the name "cena3".
```
export function update(obj, dt) {
  if (input.pressed("Enter")) {
    send("*", "cena3");   // chega aos scripts e ao Narrative (bloco trigger "cena3")
  }
}
```

# 11. Move toward another object
```
export function update(obj, dt) {
  const alvo = position("Alvo");           // [x, y, z], ou undefined se nao existe
  if (!alvo) return;
  const p = obj.transform.pos;
  const dx = alvo[0] - p[0], dz = alvo[2] - p[2], d = Math.hypot(dx, dz);
  if (d > 0.1) { p[0] += dx / d * 2 * dt; p[2] += dz / d * 2 * dt; }
}
```

# 12. Start, instantiate, destroy and components
```
export function start(obj) {
  const b = instantiate("Bala", 0, 2, 0);   // copia do objeto "Bala" (devolve o nome)
  destroy(b, 2.0);                          // apaga a copia depois de 2 s
  addComponent(obj, "AudioSource");         // tipos: MeshRenderer, AudioSource, ParticleComp
}
```

# 13. Turn objects on and off
```
export function update(obj, dt) {
  if (input.pressed("1")) setActive("Inimigo 3", false);
  if (input.pressed("2")) setActive("Inimigo 3", true);
}
```

# Common mistakes
- `this.position`, `Input.axis`, `Scene.find`, `class ... extends` do not exist: write functions.
- setPosition / setRotation change the LOCAL transform: for a child object they are relative to the parent.
- Names are exact: "Alvo" is not "alvo".
- Changes are applied after the script returns: instantiate's copy exists right after the call, but its script starts on the next frame.)H",
R"H(# Situacao
Scripts sao OPCIONAIS: um objeto so tem script depois que voce clica em "Add Script" no Inspector. O codigo fica salvo com o objeto e roda durante o Play. Todos os exemplos abaixo foram executados pelo teste do editor (selftest), entao o codigo e valido.
Copie um exemplo, cole no script de um objeto (Inspector > Script > Editar) e aperte Play. Os objetos citados nos exemplos precisam existir na cena com exatamente esses nomes.

# 1. Mover e girar
```
export function update(obj, dt) {
  obj.transform.pos[1] = 1 + Math.sin(time.now) * 0.5;   // sobe e desce
  obj.transform.rot[1] += 45 * dt;                       // gira 45 graus por segundo (eixo Y)
}
```

# 2. Andar com WASD (sem fisica)
```
export function update(obj, dt) {
  const r = obj.transform.rot[1] * Math.PI / 180;
  const fx = -Math.sin(r), fz = -Math.cos(r);   // frente do objeto
  const dx = Math.cos(r), dz = -Math.sin(r);    // direita do objeto
  let mx = 0, mz = 0;
  if (input.down("W")) { mx += fx; mz += fz; }
  if (input.down("S")) { mx -= fx; mz -= fz; }
  if (input.down("D")) { mx += dx; mz += dz; }
  if (input.down("A")) { mx -= dx; mz -= dz; }
  obj.transform.pos[0] += mx * 5 * dt;
  obj.transform.pos[2] += mz * 5 * dt;
}
```

# 3. Primeira pessoa
Coloque no jogador. A camera deve ser um objeto separado chamado "Camera" (nao filho do jogador), com a tag Principal.
```
let yaw = 0, pitch = 0;
export function update(obj, dt) {
  const m = input.mouseDelta();
  yaw -= m[0] * 0.2;                                          // mouse para os lados gira
  pitch = Math.max(-80, Math.min(80, pitch - m[1] * 0.2));    // mouse para cima olha para cima
  obj.transform.rot[1] = yaw;
  const r = yaw * Math.PI / 180;
  const fx = -Math.sin(r), fz = -Math.cos(r), dx = Math.cos(r), dz = -Math.sin(r);
  let mx = 0, mz = 0;
  if (input.down("W")) { mx += fx; mz += fz; }
  if (input.down("S")) { mx -= fx; mz -= fz; }
  if (input.down("D")) { mx += dx; mz += dz; }
  if (input.down("A")) { mx -= dx; mz -= dz; }
  obj.transform.pos[0] += mx * 5 * dt;
  obj.transform.pos[2] += mz * 5 * dt;
  const p = obj.transform.pos;
  setPosition("Camera", p[0], p[1] + 1.6, p[2]);   // olhos a 1,6 m do chao
  setRotation("Camera", pitch, yaw, 0);
}
```

# 4. Terceira pessoa
Coloque no jogador. A camera "Camera" e um objeto separado que segue atras dele.
```
let orbita = 0;
export function update(obj, dt) {
  const m = input.mouseDelta();
  orbita -= m[0] * 0.3;                              // arrastar o mouse gira a camera em volta
  obj.transform.rot[1] = orbita;
  const r = orbita * Math.PI / 180;
  const fx = -Math.sin(r), fz = -Math.cos(r), dx = Math.cos(r), dz = -Math.sin(r);
  let mx = 0, mz = 0;
  if (input.down("W")) { mx += fx; mz += fz; }
  if (input.down("S")) { mx -= fx; mz -= fz; }
  if (input.down("D")) { mx += dx; mz += dz; }
  if (input.down("A")) { mx -= dx; mz -= dz; }
  obj.transform.pos[0] += mx * 5 * dt;
  obj.transform.pos[2] += mz * 5 * dt;
  const p = obj.transform.pos;
  const d = 4, alt = 1.8;                            // distancia e altura da camera
  setPosition("Camera", p[0] - fx * d, p[1] + alt, p[2] - fz * d);
  setRotation("Camera", -15, orbita, 0);             // olha um pouco para baixo, para o jogador
}
```

# 5. Projetil e tiro
Coloque no jogador. Crie um cubo chamado "Bala" na cena (ele fica parado; so as copias voam). Aperte F para atirar.
```
export function update(obj, dt) {
  if (input.pressed("F")) {
    const p = obj.transform.pos, r = obj.transform.rot;
    const bala = instantiate("Bala", p[0], p[1] + 1, p[2]);   // devolve o nome da copia: "Bala#1", "Bala#2"...
    setRotation(bala, 0, r[1], 0);
  }
}
```
Coloque tambem no objeto "Bala". Ele faz cada copia voar, acertar o objeto "Alvo" e sumir depois de 3 segundos.
```
let idade = 0;
export function update(obj, dt) {
  if (obj.name === "Bala") return;                 // o modelo fica parado: so as copias voam
  idade += dt;
  const r = obj.transform.rot[1] * Math.PI / 180, v = 25 * dt;
  obj.transform.pos[0] -= Math.sin(r) * v;
  obj.transform.pos[2] -= Math.cos(r) * v;
  const alvo = position("Alvo");
  if (alvo) {
    const dx = alvo[0] - obj.transform.pos[0], dy = alvo[1] - obj.transform.pos[1], dz = alvo[2] - obj.transform.pos[2];
    if (dx * dx + dy * dy + dz * dz < 0.25) { send("Alvo", "acertado"); destroy(obj.name); return; }
  }
  if (idade > 3) destroy(obj.name);
}
```
E no objeto "Alvo", reaja ao acerto:
```
export function OnEventTrigger(obj, name) {
  if (name === "acertado") {
    setColor(obj.name, 1, 0, 0);
    debug.console("acertou o alvo!");
  }
}
```

# 6. Mudar a cor do mesh
Troca a cor do proprio objeto a cada meio segundo (valores de 0 a 1).
```
let ultima = -1;
export function update(obj, dt) {
  const i = Math.floor(time.now * 2) % 3;
  if (i === ultima) return;
  ultima = i;
  if (i === 0) setColor(obj.name, 1, 0.2, 0.2);
  else if (i === 1) setColor(obj.name, 0.2, 1, 0.2);
  else setColor(obj.name, 0.2, 0.4, 1);
}
```

# 7. Mudar uma propriedade do shader
Ainda nao exposto aos scripts. O que um script muda hoje no material e a cor base, com setColor (exemplo 6). Rugosidade, metalico e parametros proprios do shader dependem do sistema de parametros do material (.tmat), que ainda nao foi feito.

# 8. Enviar um evento
```
export function update(obj, dt) {
  if (input.pressed("E")) {
    send("Porta", "abrir");          // pelo nome do objeto
    send("tag:inimigo", "alerta");   // pela tag: todos os objetos com a tag "inimigo"
    send("*", "tudo");               // para todos os objetos
  }
}
```

# 9. Receber um evento
```
export function OnEventTrigger(obj, name) {
  if (name === "abrir") {
    setColor(obj.name, 0.2, 1, 0.2);
    debug.console(obj.name + " recebeu abrir");
  }
}
```

# 10. Enviar um trigger para o Narrative
O grafo do Narrative recebe os mesmos eventos dos scripts. Adicione um bloco "trigger" com o nome "cena3".
```
export function update(obj, dt) {
  if (input.pressed("Enter")) {
    send("*", "cena3");   // chega aos scripts e ao Narrative (bloco trigger "cena3")
  }
}
```

# 11. Andar ate outro objeto
```
export function update(obj, dt) {
  const alvo = position("Alvo");           // [x, y, z], ou undefined se nao existe
  if (!alvo) return;
  const p = obj.transform.pos;
  const dx = alvo[0] - p[0], dz = alvo[2] - p[2], d = Math.hypot(dx, dz);
  if (d > 0.1) { p[0] += dx / d * 2 * dt; p[2] += dz / d * 2 * dt; }
}
```

# 12. Start, instanciar, destruir e componentes
```
export function start(obj) {
  const b = instantiate("Bala", 0, 2, 0);   // copia do objeto "Bala" (devolve o nome)
  destroy(b, 2.0);                          // apaga a copia depois de 2 s
  addComponent(obj, "AudioSource");         // tipos: MeshRenderer, AudioSource, ParticleComp
}
```

# 13. Ligar e desligar objetos
```
export function update(obj, dt) {
  if (input.pressed("1")) setActive("Inimigo 3", false);
  if (input.pressed("2")) setActive("Inimigo 3", true);
}
```

# Erros comuns
- `this.position`, `Input.axis`, `Scene.find`, `class ... extends` nao existem: escreva funcoes.
- setPosition / setRotation mudam a posicao LOCAL: em um objeto filho, sao relativas ao pai.
- Os nomes sao exatos: "Alvo" nao e "alvo".
- Os pedidos sao aplicados depois que o script devolve o controle: a copia do instantiate existe logo apos a chamada, mas o script dela so comeca no proximo quadro.)H"},
  {"tutorial_fps", "Scripts", "Tutorial: make an FPS game", "Tutorial: criar um jogo de FPS",
R"H(# What you will build
A small first-person shooting range: walk with WASD, look with the mouse, shoot with the left button, and hit a target that turns red.
Objects: Player, Camera, Bala (bullet model), Alvo (target) and a floor.
# 1. Create the objects
1. Add > Cube, rename it "Player". Scale 0.6 x 1.8 x 0.6, position y = 0.9 (feet on the floor).
2. Add > Camera, rename it "Camera" and tick Principal. It is NOT a child of Player.
3. Add > Cube, rename it "Bala". Scale 0.1 x 0.1 x 0.3, position y = -50 (under the floor: the copies appear where you shoot).
4. Add > Cube, rename it "Alvo". Position (0, 1, -10).
5. Floor: a Cube with scale 40 x 0.1 x 40 at y = 0.
The names matter: the scripts use "Camera", "Bala" and "Alvo".
# 2. Player script (look, walk and shoot)
Add Script to Player and paste this:
```
let yaw = 0, pitch = 0, cooldown = 0;
export function update(obj, dt) {
  // 1. olhar com o mouse
  const m = input.mouseDelta();
  yaw -= m[0] * 0.2;
  pitch = Math.max(-80, Math.min(80, pitch - m[1] * 0.2));
  obj.transform.rot[1] = yaw;
  // 2. andar com WASD, na direcao em que voce olha
  const r = yaw * Math.PI / 180;
  const fx = -Math.sin(r), fz = -Math.cos(r), dx = Math.cos(r), dz = -Math.sin(r);
  let mx = 0, mz = 0;
  if (input.down("W")) { mx += fx; mz += fz; }
  if (input.down("S")) { mx -= fx; mz -= fz; }
  if (input.down("D")) { mx += dx; mz += dz; }
  if (input.down("A")) { mx -= dx; mz -= dz; }
  obj.transform.pos[0] += mx * 5 * dt;
  obj.transform.pos[2] += mz * 5 * dt;
  // 3. camera nos olhos
  const p = obj.transform.pos;
  setPosition("Camera", p[0], p[1] + 1.6, p[2]);
  setRotation("Camera", pitch, yaw, 0);
  // 4. atirar com o botao esquerdo (um tiro a cada 0,25 s)
  cooldown -= dt;
  if (input.down("Mouse0") && cooldown <= 0) {
    cooldown = 0.25;
    const b = instantiate("Bala", p[0] + fx * 0.8, p[1] + 1.5, p[2] + fz * 0.8);
    setRotation(b, pitch, yaw, 0);   // a bala sai na direcao do olhar (tambem para cima e para baixo)
  }
}
```
# 3. Bala script (the bullet)
Add Script to Bala and paste this:
```
// A copia da bala ("Bala#1", "Bala#2"...) some sozinha depois de 2 s. O modelo "Bala" fica parado embaixo do chao.
export function start(obj) {
  if (obj.name.indexOf("#") > 0) destroy(obj.name, 2);
}
export function update(obj, dt) {
  if (obj.name.indexOf("#") < 0) return;   // o modelo nao se move
  const rx = obj.transform.rot[0] * Math.PI / 180, ry = obj.transform.rot[1] * Math.PI / 180;
  const speed = 40 * dt;
  obj.transform.pos[0] += -Math.cos(rx) * Math.sin(ry) * speed;
  obj.transform.pos[1] += Math.sin(rx) * speed;
  obj.transform.pos[2] += -Math.cos(rx) * Math.cos(ry) * speed;
  // acertou o alvo? (distancia menor que 0,8 m)
  const t = position("Alvo"), q = obj.transform.pos;
  if (t && Math.hypot(t[0] - q[0], t[1] - q[1], t[2] - q[2]) < 0.8) {
    send("Alvo", "acertado");
    destroy(obj.name);
  }
}
```
# 4. Alvo script (the target)
Add Script to Alvo and paste this:
```
export function OnEventTrigger(obj, name) {
  if (name === "acertado") {
    setColor(obj.name, 1, 0.1, 0.1);
    debug.console("acertou o alvo!");
  }
}
```
# 5. Test it
Press Play. WASD walks, the mouse looks, the left button shoots. A hit turns the target red and the Console shows "acertou o alvo!".
If something does not work, open the Console and press Check scripts: each problem shows the object and the line.
Limits: the bullet has no physics, so it goes through walls. For real collisions use the Tools (Inspector > Add Tool).
# Next steps
- Ammo: keep a number in the Player script and shoot only while it is above zero.
- Enemies: create them with instantiate, as in examples 5 and 8, and send them events with send.
)H",
R"H(# O que voce vai construir
Um estande de tiro em primeira pessoa: anda com WASD, olha com o mouse, atira com o botao esquerdo e acerta um alvo que fica vermelho.
Objetos: Player, Camera, Bala (modelo da bala), Alvo e um chao.
# 1. Crie os objetos
1. Add > Cube, renomeie para "Player". Escala 0,6 x 1,8 x 0,6 e posicao y = 0,9 (pes no chao).
2. Add > Camera, renomeie para "Camera" e marque Principal. Ela NAO e filha do Player.
3. Add > Cube, renomeie para "Bala". Escala 0,1 x 0,1 x 0,3 e posicao y = -50 (embaixo do chao: as copias aparecem onde voce atira).
4. Add > Cube, renomeie para "Alvo". Posicao (0, 1, -10).
5. Chao: um Cube com escala 40 x 0,1 x 40 em y = 0.
Os nomes importam: os scripts usam "Camera", "Bala" e "Alvo".
# 2. Script do Player (olhar, andar e atirar)
Add Script no Player e cole:
```
let yaw = 0, pitch = 0, cooldown = 0;
export function update(obj, dt) {
  // 1. olhar com o mouse
  const m = input.mouseDelta();
  yaw -= m[0] * 0.2;
  pitch = Math.max(-80, Math.min(80, pitch - m[1] * 0.2));
  obj.transform.rot[1] = yaw;
  // 2. andar com WASD, na direcao em que voce olha
  const r = yaw * Math.PI / 180;
  const fx = -Math.sin(r), fz = -Math.cos(r), dx = Math.cos(r), dz = -Math.sin(r);
  let mx = 0, mz = 0;
  if (input.down("W")) { mx += fx; mz += fz; }
  if (input.down("S")) { mx -= fx; mz -= fz; }
  if (input.down("D")) { mx += dx; mz += dz; }
  if (input.down("A")) { mx -= dx; mz -= dz; }
  obj.transform.pos[0] += mx * 5 * dt;
  obj.transform.pos[2] += mz * 5 * dt;
  // 3. camera nos olhos
  const p = obj.transform.pos;
  setPosition("Camera", p[0], p[1] + 1.6, p[2]);
  setRotation("Camera", pitch, yaw, 0);
  // 4. atirar com o botao esquerdo (um tiro a cada 0,25 s)
  cooldown -= dt;
  if (input.down("Mouse0") && cooldown <= 0) {
    cooldown = 0.25;
    const b = instantiate("Bala", p[0] + fx * 0.8, p[1] + 1.5, p[2] + fz * 0.8);
    setRotation(b, pitch, yaw, 0);   // a bala sai na direcao do olhar (tambem para cima e para baixo)
  }
}
```
# 3. Script da Bala (o projetil)
Add Script na Bala e cole:
```
// A copia da bala ("Bala#1", "Bala#2"...) some sozinha depois de 2 s. O modelo "Bala" fica parado embaixo do chao.
export function start(obj) {
  if (obj.name.indexOf("#") > 0) destroy(obj.name, 2);
}
export function update(obj, dt) {
  if (obj.name.indexOf("#") < 0) return;   // o modelo nao se move
  const rx = obj.transform.rot[0] * Math.PI / 180, ry = obj.transform.rot[1] * Math.PI / 180;
  const speed = 40 * dt;
  obj.transform.pos[0] += -Math.cos(rx) * Math.sin(ry) * speed;
  obj.transform.pos[1] += Math.sin(rx) * speed;
  obj.transform.pos[2] += -Math.cos(rx) * Math.cos(ry) * speed;
  // acertou o alvo? (distancia menor que 0,8 m)
  const t = position("Alvo"), q = obj.transform.pos;
  if (t && Math.hypot(t[0] - q[0], t[1] - q[1], t[2] - q[2]) < 0.8) {
    send("Alvo", "acertado");
    destroy(obj.name);
  }
}
```
# 4. Script do Alvo
Add Script no Alvo e cole:
```
export function OnEventTrigger(obj, name) {
  if (name === "acertado") {
    setColor(obj.name, 1, 0.1, 0.1);
    debug.console("acertou o alvo!");
  }
}
```
# 5. Teste
Aperte Play. WASD anda, o mouse olha, o botao esquerdo atira. Ao acertar, o alvo fica vermelho e o Console mostra "acertou o alvo!".
Se algo nao funcionar, abra o Console e clique em Check scripts: cada problema mostra o objeto e a linha.
Limites: a bala nao tem fisica, entao atravessa paredes. Para colisao real use os Tools (Inspector > Add Tool).
# Proximos passos
- Municao: guarde um numero no script do Player e so atire enquanto ele for maior que zero.
- Inimigos: crie com instantiate e mande eventos com send, como nos exemplos 5 e 8.
)H"},
  {"tutorial_doors", "Scripts", "Tutorial: doors that open with E", "Tutorial: portas que abrem com E",
R"H(# What you will build
A door that slides up when you stand near it (less than 2.5 m) and press E. An event named "abrir" (for example from a Narrative node) opens it too.
# 1. Create the objects
1. Add > Cube, rename it "Porta". Scale 2 x 3 x 0.2, position (0, 1.5, -3). The door starts closed.
2. Add > Cube, rename it "Jogador". Scale 0.6 x 1.8 x 0.6, position (0, 0.9, 0).
3. Add a Camera (Principal) to see the scene while you play; it can be anywhere.
The name "Jogador" matters: the door looks for it.
# 2. Player movement
Add Script to Jogador and paste the walk script of example 2 (WASD):
```
export function update(obj, dt) {
  const r = obj.transform.rot[1] * Math.PI / 180;
  const fx = -Math.sin(r), fz = -Math.cos(r);
  const dx = Math.cos(r), dz = -Math.sin(r);
  let mx = 0, mz = 0;
  if (input.down("W")) { mx += fx; mz += fz; }
  if (input.down("S")) { mx -= fx; mz -= fz; }
  if (input.down("D")) { mx += dx; mz += dz; }
  if (input.down("A")) { mx -= dx; mz -= dz; }
  obj.transform.pos[0] += mx * 5 * dt;
  obj.transform.pos[2] += mz * 5 * dt;
}
```
# 3. Door script
Add Script to Porta and paste this:
```
let open = false, closedY = 0, ready = false;
export function start(obj) { closedY = obj.transform.pos[1]; ready = true; }
export function update(obj, dt) {
  if (!ready) return;
  if (!open && input.pressed("E")) {
    const p = position("Jogador"), q = obj.transform.pos;
    if (!p) return;                      // o jogador nao esta na cena
    const dist = Math.hypot(p[0] - q[0], p[2] - q[2]);
    if (dist < 2.5) { open = true; debug.console("porta aberta"); }
    else debug.console("chegue mais perto da porta (" + dist.toFixed(1) + " m)");
  }
  // sobe 3 m devagar ou volta para o chao
  const target = open ? closedY + 3 : closedY;
  obj.transform.pos[1] += (target - obj.transform.pos[1]) * Math.min(1, dt * 4);
}
// um evento "abrir" (por exemplo, de um no do Narrative) tambem abre a porta
export function OnEventTrigger(obj, name) {
  if (name === "abrir") open = true;
}
```
# 4. Test it
Press Play, walk toward the door and press E. The door slides up 3 m. Too far away, the Console says how far you are.
Limits: the door has no physics, so the player walks through it while it is closed. To block passage, use the Tools (Inspector > Add Tool) or a Narrative condition.
# Next steps
- Two doors: copy the object and rename it; the script uses the object's own name, so each door works alone.
- Locked door: create a variable in the Narrative and test it before opening, or check a key in your script.
)H",
R"H(# O que voce vai construir
Uma porta que sobe quando voce fica perto dela (menos de 2,5 m) e aperta E. Um evento chamado "abrir" (por exemplo, de um no do Narrative) tambem abre a porta.
# 1. Crie os objetos
1. Add > Cube, renomeie para "Porta". Escala 2 x 3 x 0,2, posicao (0, 1.5, -3). A porta comeca fechada.
2. Add > Cube, renomeie para "Jogador". Escala 0,6 x 1,8 x 0,6, posicao (0, 0.9, 0).
3. Adicione uma Camera (Principal) para ver a cena ao jogar; ela pode estar em qualquer lugar.
O nome "Jogador" importa: a porta procura por ele.
# 2. Movimento do jogador
Add Script no Jogador e cole o script de andar do exemplo 2 (WASD):
```
export function update(obj, dt) {
  const r = obj.transform.rot[1] * Math.PI / 180;
  const fx = -Math.sin(r), fz = -Math.cos(r);
  const dx = Math.cos(r), dz = -Math.sin(r);
  let mx = 0, mz = 0;
  if (input.down("W")) { mx += fx; mz += fz; }
  if (input.down("S")) { mx -= fx; mz -= fz; }
  if (input.down("D")) { mx += dx; mz += dz; }
  if (input.down("A")) { mx -= dx; mz -= dz; }
  obj.transform.pos[0] += mx * 5 * dt;
  obj.transform.pos[2] += mz * 5 * dt;
}
```
# 3. Script da porta
Add Script na Porta e cole:
```
let open = false, closedY = 0, ready = false;
export function start(obj) { closedY = obj.transform.pos[1]; ready = true; }
export function update(obj, dt) {
  if (!ready) return;
  if (!open && input.pressed("E")) {
    const p = position("Jogador"), q = obj.transform.pos;
    if (!p) return;                      // o jogador nao esta na cena
    const dist = Math.hypot(p[0] - q[0], p[2] - q[2]);
    if (dist < 2.5) { open = true; debug.console("porta aberta"); }
    else debug.console("chegue mais perto da porta (" + dist.toFixed(1) + " m)");
  }
  // sobe 3 m devagar ou volta para o chao
  const target = open ? closedY + 3 : closedY;
  obj.transform.pos[1] += (target - obj.transform.pos[1]) * Math.min(1, dt * 4);
}
// um evento "abrir" (por exemplo, de um no do Narrative) tambem abre a porta
export function OnEventTrigger(obj, name) {
  if (name === "abrir") open = true;
}
```
# 4. Teste
Aperte Play, ande ate a porta e aperte E. A porta sobe 3 m. Se estiver longe, o Console diz a distancia.
Limites: a porta nao tem fisica, entao o jogador atravessa a porta fechada. Para bloquear a passagem, use os Tools (Inspector > Add Tool) ou uma condicao no Narrative.
# Proximos passos
- Duas portas: copie o objeto e renomeie; o script usa o nome do proprio objeto, entao cada porta funciona sozinha.
- Porta trancada: crie uma variavel no Narrative e teste antes de abrir, ou verifique uma chave no seu script.
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
