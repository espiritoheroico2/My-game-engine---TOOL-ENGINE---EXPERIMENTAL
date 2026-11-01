#!/usr/bin/env python3
"""Gera assets/lang/{en,pt-BR,es}.json.
IDIOMA-BASE = INGLES: a chave de cada texto no codigo e o proprio texto em ingles; o pacote "en" e so um
marcador (nada a traduzir). Frase sem traducao num pacote aparece em ingles. Colunas abaixo: ingles <TAB> pt-BR <TAB> es.
Para criar outro idioma sem recompilar: Settings > Language > Export template, traduza e salve em assets/lang/."""
import json
from pathlib import Path
root = Path(__file__).resolve().parent.parent
ROWS = """Lighting	Iluminação	Iluminación
Tags	Tags	Tags
Project	Projeto	Proyecto
Add script	Adicionar script	Añadir script
Edit script	Editar script	Editar script
Save script	Salvar script	Guardar script
Close	Fechar	Cerrar
Script enabled	Script ativo	Script activo
(no file)	(sem arquivo)	(sin archivo)
(unsaved)	(não salvo)	(sin guardar)
Script saved.	Script salvo.	Script guardado.
Could not read the script file.	Não foi possível ler o arquivo do script.	No se pudo leer el archivo del script.
Could not save the script.	Não foi possível salvar o script.	No se pudo guardar el script.
Scripts are saved with the project. Running them needs the script runtime (planned).	Os scripts são salvos com o projeto. Executá-los exige o runtime de scripts (planejado).	Los scripts se guardan con el proyecto. Ejecutarlos requiere el runtime de scripts (planificado).
Add tag	Adicionar tag	Añadir tag
new tag...	nova tag...	nueva tag...
existing...	existentes...	existentes...
cancel	cancelar	cancelar
Too many tags (limit 128).	Tags demais (limite 128).	Demasiadas tags (límite 128).
Could not rename: the name is empty, too long, already used, or a built-in tag.	Não foi possível renomear: nome vazio, longo demais, já usado ou tag pré-definida.	No se pudo renombrar: nombre vacío, demasiado largo, ya usado o tag predefinida.
Double-click to rename (changes every object that has this tag).	Duplo clique para renomear (muda todos os objetos com esta tag).	Doble clic para renombrar (cambia todos los objetos con esta tag).
Built-in tag (cannot be renamed). Link your own tag to it in Settings > Tags.	Tag pré-definida (não pode ser renomeada). Ligue a sua tag a ela em Settings > Tags.	Tag predefinida (no se puede renombrar). Enlaza tu tag en Settings > Tags.
Built-in tags (used by the engine)	Tags pré-definidas (usadas pela engine)	Tags predefinidas (usadas por el motor)
Built-in tags cannot be renamed. To use your own name, link it below.	Tags pré-definidas não podem ser renomeadas. Para usar seu nome, ligue abaixo.	Las tags predefinidas no se pueden renombrar. Para usar tu nombre, enlázala abajo.
Links (your tag -> built-in tag)	Ligações (sua tag -> tag pré-definida)	Enlaces (tu tag -> tag predefinida)
Link	Ligar	Enlazar
Remove link	Remover ligação	Quitar enlace
your tag (e.g. jogador)	sua tag (ex.: jogador)	tu tag (ej.: jugador)
Could not link: the name is empty, too long, or is itself a built-in tag.	Não foi possível ligar: nome vazio, longo demais ou é uma tag pré-definida.	No se pudo enlazar: nombre vacío, demasiado largo o es una tag predefinida.
All tags in this project	Todas as tags do projeto	Todas las tags del proyecto
Renaming a tag changes it in every object and in the other scene files.	Renomear uma tag muda todos os objetos e os arquivos das outras cenas.	Renombrar una tag cambia todos los objetos y los archivos de otras escenas.
Cast shadow	Projetar sombra	Proyectar sombra
Receive shadow	Receber sombra	Recibir sombra
Sun casts shadows	Sol projeta sombras	El sol proyecta sombras
Enable shadows	Ativar sombras	Activar sombras
Real-time shadows	Sombras em tempo real	Sombras en tiempo real
Shadow quality	Qualidade da sombra	Calidad de sombra
Low (512)	Baixa (512)	Baja (512)
Medium (1024)	Média (1024)	Media (1024)
High (2048)	Alta (2048)	Alta (2048)
Ultra (4096)	Ultra (4096)	Ultra (4096)
Max distance (m)	Distância máxima (m)	Distancia máxima (m)
Edge softness	Suavidade da borda	Suavidad del borde
Hard edges	Bordas duras	Bordes duros
Soft 3x3	Suave 3x3	Suave 3x3
Softer 5x5	Mais suave 5x5	Más suave 5x5
Shadow strength	Força da sombra	Fuerza de la sombra
Bias	Bias (ajuste anti-acne)	Bias (ajuste anti-acné)
Normal bias (texels)	Bias normal (texels)	Bias normal (texels)
Sun (Global Light)	Sol (Global Light)	Sol (Global Light)
No Global Light in this scene (Hierarchy > + Create > Global Light).	Não há Global Light nesta cena (Hierarquia > + Criar > Global Light).	No hay Global Light en esta escena (Jerarquía > + Crear > Global Light).
Each object chooses Cast shadow / Receive shadow in the Inspector. Only the sun casts shadows. Lower quality = more FPS.	Cada objeto escolhe Projetar/Receber sombra no Inspetor. Só o sol projeta sombras. Menos qualidade = mais FPS.	Cada objeto elige Proyectar/Recibir sombra en el Inspector. Solo el sol proyecta sombras. Menos calidad = más FPS.
These settings belong to the scene '%s' and are saved with it.	Estas configurações pertencem à cena '%s' e são salvas com ela.	Estos ajustes pertenecen a la escena '%s' y se guardan con ella.
Global post-processing	Pós-processamento global	Posprocesado global
Post-processing (this camera)	Pós-processamento (esta câmera)	Posprocesado (esta cámara)
Enable post-processing	Ativar pós-processamento	Activar posprocesado
Add to the scene's global effect	Somar ao efeito global da cena	Sumar al efecto global de la escena
Replace the global effect	Substituir o efeito global	Reemplazar el efecto global
Mode	Modo	Modo
Exposure (EV)	Exposição (EV)	Exposición (EV)
Contrast	Contraste	Contraste
Saturation	Saturação	Saturación
Tint color	Cor do tom	Color del tono
Tint strength	Força do tom	Fuerza del tono
Vignette	Vinheta	Viñeta
Vignette size	Tamanho da vinheta	Tamaño de la viñeta
Sharpen	Nitidez	Nitidez
Reset effect	Redefinir efeito	Restablecer efecto
Post-fx	Pós-fx	Pos-fx
Applies to every camera. A camera can add to it or replace it (Inspector > Camera).	Vale para todas as câmeras. Uma câmera pode somar ou substituir (Inspetor > Câmera).	Vale para todas las cámaras. Una cámara puede sumar o reemplazar (Inspector > Cámara).
Move (W)	Mover (W)	Mover (W)
Rotate (E)	Girar (E)	Rotar (E)
Scale (R)	Escalar (R)	Escalar (R)
FREE NAVIGATION: WASD move, Q/E down/up, Shift fast, wheel speed, Alt/Esc exit	NAVEGAÇÃO LIVRE: WASD anda, Q/E desce/sobe, Shift rápido, roda muda a velocidade, Alt/Esc sai	NAVEGACIÓN LIBRE: WASD mueve, Q/E baja/sube, Shift rápido, rueda cambia velocidad, Alt/Esc sale
right: orbit | middle: pan | wheel: zoom | Alt: free navigation | double-click in Hierarchy: focus	direito: orbitar | meio: mover | roda: zoom | Alt: navegação livre | duplo clique na Hierarquia: focar	derecho: orbitar | medio: mover | rueda: zoom | Alt: navegación libre | doble clic en Jerarquía: enfocar
Focus	Focar	Enfocar
Undo	Desfazer	Deshacer
Redo	Refazer	Rehacer
Help	Ajuda	Ayuda
Help for this window (F1)	Ajuda desta janela (F1)	Ayuda de esta ventana (F1)
search...	buscar...	buscar...
Copy code	Copiar código	Copiar código
Basics	Básico	Básico
Physics	Física	Física
Examples	Exemplos	Ejemplos
Windows	Janelas	Ventanas
Input Map	Mapa de botões	Mapa de botones
Physics (world gravity)...	Física (gravidade do mundo)...	Física (gravedad del mundo)...
External code editor	Editor de código externo	Editor de código externo
Editor command	Comando do editor	Comando del editor
empty = system default (Notepad on Windows)	vazio = padrão do sistema (Bloco de Notas no Windows)	vacío = predeterminado del sistema (Bloc de notas en Windows)
Examples: code   |   notepad++.exe   |   C:\\Program Files\\Notepad++\\notepad++.exe. Run directly, never through a shell.	Exemplos: code   |   notepad++.exe   |   C:\\Program Files\\Notepad++\\notepad++.exe. Executado direto, nunca por um shell.	Ejemplos: code   |   notepad++.exe   |   C:\\Program Files\\Notepad++\\notepad++.exe. Se ejecuta directo, nunca por un shell.
Edit external	Editar externo	Editar externo
Opens the file in your code editor (Settings > Project).	Abre o arquivo no seu editor de código (Settings > Project).	Abre el archivo en tu editor de código (Settings > Project).
(none)	(nenhum)	(ninguno)
Add Physics	Adicionar física	Añadir física
Remove Physics	Remover física	Quitar física
Static	Estático	Estático
Dynamic	Dinâmico	Dinámico
Kinematic	Cinemático	Cinemático
Body	Corpo	Cuerpo
Collider	Collider	Collider
Auto (from mesh)	Automático (pela malha)	Automático (por la malla)
Box	Caixa	Caja
Mass (kg)	Massa (kg)	Masa (kg)
Gravity scale	Escala da gravidade	Escala de gravedad
Linear drag	Arrasto linear	Arrastre lineal
Angular drag	Arrasto angular	Arrastre angular
Use gravity	Usar gravidade	Usar gravedad
Freeze rotation	Travar rotação	Bloquear rotación
Continuous collision detection: fast small objects cannot pass through walls.	Detecção contínua de colisão: objetos pequenos e rápidos não atravessam paredes.	Detección continua de colisión: los objetos pequeños y rápidos no atraviesan paredes.
Friction	Atrito	Fricción
Bounciness	Quique	Rebote
Trigger (detects, does not push)	Gatilho (detecta, não empurra)	Disparador (detecta, no empuja)
Vector physics (own gravity)	Vector physics (gravidade própria)	Vector physics (gravedad propia)
Replaces the world gravity for this object only.	Substitui a gravidade do mundo só para este objeto.	Reemplaza la gravedad del mundo solo para este objeto.
Gravity vector (m/s2)	Vetor de gravidade (m/s2)	Vector de gravedad (m/s2)
Down	Baixo	Abajo
Up	Cima	Arriba
Left	Esquerda	Izquierda
Right	Direita	Derecha
Forward	Frente	Adelante
Back	Trás	Atrás
Zero	Zero	Cero
World gravity: %.2f, %.2f, %.2f	Gravidade do mundo: %.2f, %.2f, %.2f	Gravedad del mundo: %.2f, %.2f, %.2f
Add Tool...	Adicionar Tool...	Añadir Tool...
Jump	Pular	Saltar
Move	Mover	Mover
Follow Target	Seguir alvo	Seguir objetivo
Spin	Girar	Girar
On Collision	Ao colidir	Al colisionar
Script library	Biblioteca de script	Biblioteca de script
Jumps when the action is pressed (needs a Dynamic Physics body).	Pula quando a ação é apertada (precisa de corpo Dynamic).	Salta cuando se pulsa la acción (necesita cuerpo Dynamic).
Moves with two axes from the Input Map.	Move com dois eixos do Input Map.	Se mueve con dos ejes del Input Map.
Follows another object by name, keeping an offset (cameras).	Segue outro objeto pelo nome, mantendo um deslocamento (câmeras).	Sigue a otro objeto por nombre, manteniendo un desplazamiento (cámaras).
Rotates continuously.	Gira continuamente.	Gira continuamente.
Reacts when this object touches another.	Reage quando este objeto toca outro.	Reacciona cuando este objeto toca otro.
Attaches an extra .js library/addon to this object's code.	Acopla uma biblioteca/addon .js extra ao código deste objeto.	Acopla una biblioteca/addon .js extra al código de este objeto.
Player (controller map)	Jogador (mapa de controle)	Jugador (mapa de control)
Action	Ação	Acción
Height (m)	Altura (m)	Altura (m)
Allow air jump	Permitir pulo no ar	Permitir salto en el aire
X axis	Eixo X	Eje X
Y axis (forward)	Eixo Y (frente)	Eje Y (adelante)
Speed (m/s)	Velocidade (m/s)	Velocidad (m/s)
Face direction	Virar para a direção	Girar hacia la dirección
Camera relative	Relativo à câmera	Relativo a la cámara
Target object name	Nome do objeto alvo	Nombre del objeto objetivo
Offset	Deslocamento	Desplazamiento
Smoothing	Suavização	Suavizado
Look at target	Olhar para o alvo	Mirar al objetivo
Degrees / second (X Y Z)	Graus / segundo (X Y Z)	Grados / segundo (X Y Z)
Only objects with tag	Só objetos com a tag	Solo objetos con la etiqueta
Log to Console	Escrever no Console	Escribir en la Consola
Destroy the other object	Destruir o outro objeto	Destruir el otro objeto
Destroy this object	Destruir este objeto	Destruir este objeto
Bounce this object	Quicar este objeto	Rebotar este objeto
Bounce speed (m/s)	Velocidade do quique (m/s)	Velocidad del rebote (m/s)
This tool needs a Physics component on the object.	Este tool precisa de um componente Physics no objeto.	Este tool necesita un componente Physics en el objeto.
Library file	Arquivo da biblioteca	Archivo de la biblioteca
Runs once the script runtime exists.	Roda quando o runtime de scripts existir.	Se ejecuta cuando exista el runtime de scripts.
World physics of this scene (saved with the scene). Objects with 'Vector physics' ignore the world gravity.	Física do mundo desta cena (salva com a cena). Objetos com 'Vector physics' ignoram a gravidade do mundo.	Física del mundo de esta escena (se guarda con la escena). Los objetos con 'Vector physics' ignoran la gravedad del mundo.
Physics enabled	Física ativada	Física activada
World gravity (m/s2) - any axis	Gravidade do mundo (m/s2) - qualquer eixo	Gravedad del mundo (m/s2) - cualquier eje
Gravity X / Y / Z	Gravidade X / Y / Z	Gravedad X / Y / Z
Earth	Terra	Tierra
Moon	Lua	Luna
Mars	Marte	Marte
Zero-G	Gravidade zero	Gravedad cero
Sideways (+X)	De lado (+X)	De lado (+X)
Toward -Z	Para -Z	Hacia -Z
Upside down	De cabeça para baixo	Al revés
Simulation	Simulação	Simulación
Solver iterations	Iterações do solver	Iteraciones del solver
Steps per second	Passos por segundo	Pasos por segundo
More iterations stack better; more steps per second is more precise. Both cost CPU.	Mais iterações empilham melhor; mais passos por segundo é mais preciso. Ambos custam CPU.	Más iteraciones apilan mejor; más pasos por segundo es más preciso. Ambos cuestan CPU.
Draw colliders in the Scene view	Desenhar colliders na janela Scene	Dibujar colliders en la ventana Scene
Live	Ao vivo	En vivo
Gamepads detected: %d	Controles detectados: %d	Mandos detectados: %d
+ Add controller map	+ Adicionar mapa de controle	+ Añadir mapa de control
Reset to defaults	Restaurar padrão	Restaurar predeterminado
Saved in Project/input_map.json	Salvo em Project/input_map.json	Guardado en Project/input_map.json
Keyboard only	Só teclado	Solo teclado
Controller	Controle	Mando
Delete map	Apagar mapa	Borrar mapa
Keyboard	Teclado	Teclado
Gamepad	Controle	Mando
Now	Agora	Ahora
Button	Botão	Botón
Axis	Eixo	Eje
Listen	Ouvir	Escuchar
Press a key...	Aperte uma tecla...	Pulsa una tecla...
Press...	Aperte...	Pulsa...
Move stick...	Mova o analógico...	Mueve el stick...
Invert	Inverter	Invertir
+ Add button	+ Botão	+ Botón
+ Add axis	+ Eixo	+ Eje
Physics (Newtoon)	Física (Newtoon)	Física (Newtoon)
Tools	Tools	Tools
Create child	Criar filho	Crear hijo
Detach from parent	Soltar do pai	Soltar del padre
+ Add Resource	+ Adicionar recurso	+ Añadir recurso
Physics	Física	Física
Physics body (Newtoon)	Corpo físico (Newtoon)	Cuerpo físico (Newtoon)
Physics already added	Física já adicionada	Física ya añadida
Tools (behaviours)	Tools (comportamentos)	Tools (comportamientos)
Rendering and world	Renderização e mundo	Renderizado y mundo
Mesh (cube)	Malha (cubo)	Malla (cubo)
Particle System	Sistema de partículas	Sistema de partículas
Game UI (canvas)	UI do jogo (canvas)	UI del juego (canvas)
Pause	Pausar	Pausar
Resume	Continuar	Continuar
Step	Passo	Paso
Game UI (camera layer)	UI do jogo (camada da câmera)	UI del juego (capa de la cámara)
Visible	Visível	Visible
Camera	Câmera	Cámara
(all Principal cameras)	(todas as câmeras Principais)	(todas las cámaras Principales)
Type	Tipo	Tipo
Text	Texto	Texto
Panel	Painel	Panel
Button	Botão	Botón
Bar	Barra	Barra
Text (use {variable})	Texto (use {variável})	Texto (use {variable})
Event name (on click)	Nome do evento (ao clicar)	Nombre del evento (al hacer clic)
Variable	Variável	Variable
Full at	Cheio em	Lleno en
Position (0..1)	Posição (0..1)	Posición (0..1)
Size (0..1)	Tamanho (0..1)	Tamaño (0..1)
Color	Cor	Color
Font scale	Escala da fonte	Escala de fuente
+ Text	+ Texto	+ Texto
+ Button	+ Botão	+ Botón
+ Bar	+ Barra	+ Barra
+ Panel	+ Painel	+ Panel
Appears over the Game view of the chosen camera. Button clicks (while playing) send their event name.	Aparece sobre a visão Game da câmera escolhida. Cliques em botões (durante o Play) enviam o nome do evento.	Aparece sobre la vista Game de la cámara elegida. Los clics en botones (durante el Play) envían el nombre del evento.
Remove UI	Remover UI	Quitar UI
Particles	Partículas	Partículas
Emitting	Emitindo	Emitiendo
Max particles	Máx. partículas	Máx. partículas
Rate (per second)	Taxa (por segundo)	Tasa (por segundo)
Lifetime (s)	Vida (s)	Vida (s)
Speed	Velocidade	Velocidad
Spread (deg)	Abertura (graus)	Apertura (grados)
Size start	Tamanho inicial	Tamaño inicial
Size end	Tamanho final	Tamaño final
Color start	Cor inicial	Color inicial
Color end	Cor final	Color final
Opacity	Opacidade	Opacidad
Gravity scale (world physics)	Escala de gravidade (física do mundo)	Escala de gravedad (física del mundo)
Bounce on floor	Quique no chão	Rebote en el suelo
Floor Y	Y do chão	Y del suelo
Emits along the object's +Y axis. Drawn as soft dots over the view (no depth test).	Emite ao longo do eixo +Y do objeto. Desenhadas como pontos suaves sobre a imagem (sem teste de profundidade).	Emite a lo largo del eje +Y del objeto. Se dibujan como puntos suaves sobre la imagen (sin prueba de profundidad).
Remove Particles	Remover partículas	Quitar partículas
Runs once when the game starts (Play).	Roda uma vez quando o jogo começa (Play).	Se ejecuta una vez al iniciar el juego (Play).
Runs every frame while playing.	Roda a cada quadro durante o Play.	Se ejecuta en cada fotograma durante el Play.
object (name)	objeto (nome)	objeto (nombre)
Runs when an object with this name is created while playing. Empty = any object.	Roda quando um objeto com este nome é criado durante o Play. Vazio = qualquer objeto.	Se ejecuta cuando se crea un objeto con este nombre durante el Play. Vacío = cualquier objeto.
slot	slot	slot
Saves/loads the narrative variables and flows to Saves/<slot>.save.json	Salva/carrega as variáveis e fluxos da narrativa em Saves/<slot>.save.json	Guarda/carga las variables y flujos de la narrativa en Saves/<slot>.save.json
shadow map reused	shadow map reaproveitado	shadow map reutilizado
Coming in a future update (locked)	Em breve, numa atualização futura (bloqueado)	Próximamente, en una actualización futura (bloqueado)
Textures / Normal map	Texturas / Normal map	Texturas / Normal map
Material (.tmat) and custom shader	Material (.tmat) e shader próprio	Material (.tmat) y shader propio
Point light shadows	Sombras de point light	Sombras de point light
Compress as NTC	Comprimir como NTC	Comprimir como NTC
Ray tracing (DXR)	Ray tracing (DXR)	Ray tracing (DXR)
Planned: see docs/TEXTURES.md, docs/RENDER_PERF.md, docs/DX12_DXR.md	Planejado: veja docs/TEXTURES.md, docs/RENDER_PERF.md, docs/DX12_DXR.md	Planificado: vea docs/TEXTURES.md, docs/RENDER_PERF.md, docs/DX12_DXR.md
Add Script	Adicionar Script	Añadir Script
- Remove Resource	- Remover recurso	- Quitar recurso
Add a component to this object: physics, tools, mesh, light, camera, audio, particles, UI, script	Adiciona um componente a este objeto: física, tools, malha, luz, câmera, áudio, partículas, UI, script	Añade un componente a este objeto: física, tools, malla, luz, cámara, audio, partículas, UI, script
Remove a component from this object (the script file stays in Assets/Scripts)	Remove um componente deste objeto (o arquivo do script continua em Assets/Scripts)	Quita un componente de este objeto (el archivo del script se mantiene en Assets/Scripts)
Creates a script file for this object (optional).	Cria um arquivo de script para este objeto (opcional).	Crea un archivo de script para este objeto (opcional).
Create an object, camera, light or sound	Cria um objeto, câmera, luz ou som	Crea un objeto, cámara, luz o sonido
Copy the selected object	Copia o objeto selecionado	Copia el objeto seleccionado
Delete the selected object and its children (Delete key)	Apaga o objeto selecionado e os filhos (tecla Delete)	Borra el objeto seleccionado y sus hijos (tecla Supr)
Tools (all)	Tools (todas)	Tools (todas)
Mesh	Malha	Malla
Point/Spot light	Luz pontual/spot	Luz puntual/spot
Objects have no script until you add one.	Objetos não têm script até você adicionar um.	Los objetos no tienen script hasta que añadas uno.
Help: JS library	Ajuda: biblioteca JS	Ayuda: biblioteca JS
Help: script examples	Ajuda: exemplos de script	Ayuda: ejemplos de script
Active	Ativo	Activo
Inactive objects are not drawn, simulated or lit, and their Tools do not run.	Objetos inativos não são desenhados, simulados nem iluminados, e suas Tools não rodam.	Los objetos inactivos no se dibujan, simulan ni iluminan, y sus Tools no se ejecutan.
Frustum culling (skip objects outside the camera view)	Frustum culling (pula objetos fora da visão da câmera)	Frustum culling (omite objetos fuera de la vista de la cámara)
Objects outside the camera view are not drawn. Objects that cast shadows are always drawn.	Objetos fora da visão da câmera não são desenhados. Objetos que projetam sombra são sempre desenhados.	Los objetos fuera de la vista de la cámara no se dibujan. Los que proyectan sombra siempre se dibujan.
Script status	Status do script	Estado del script
Reload scripts	Recarregar scripts	Recargar scripts
Restarts the scripts of this scene (state is reset).	Reinicia os scripts desta cena (o estado é zerado).	Reinicia los scripts de esta escena (el estado se reinicia).
Scripts run while the game is playing (Play).	Os scripts rodam enquanto o jogo está em Play.	Los scripts se ejecutan mientras el juego está en Play.
This build has no script runtime (QuickJS is off).	Esta build não tem runtime de scripts (QuickJS desligado).	Esta compilación no tiene runtime de scripts (QuickJS desactivado).
Pause graph	Pausar gráfico	Pausar gráfico
Time per zone (average ms)	Tempo por zona (média em ms)	Tiempo por zona (media en ms)
Events + input	Eventos + entrada	Eventos + entrada
Update (audio, pool)	Atualização (áudio, pool)	Actualización (audio, pool)
Panels (hierarchy, inspector...)	Painéis (hierarquia, inspetor...)	Paneles (jerarquía, inspector...)
Scene view render	Render da janela Scene	Render de la ventana Scene
Game view render	Render da janela Game	Render de la ventana Game
Post-processing	Pós-processamento	Posprocesado
UI draw (ImGui)	Desenho da UI (ImGui)	Dibujo de la UI (ImGui)
Present + frame limiter	Apresentar + limitador de FPS	Presentar + limitador de FPS
Rendering	Renderização	Renderizado
Scene and memory	Cena e memória	Escena y memoria
process memory: not available on this platform	memória do processo: indisponível nesta plataforma	memoria del proceso: no disponible en esta plataforma
Tip: lower render scale and shadow quality first when you need more FPS.	Dica: reduza primeiro a escala de render e a qualidade da sombra para ganhar FPS.	Consejo: reduce primero la escala de render y la calidad de sombra para ganar FPS.
GPU details	Detalhes da GPU	Detalles de la GPU
Export preset	Preset de exportação	Preset de exportación
Export is not active yet. The preset is only stored for the future.	A exportação ainda não está ativa. O preset só fica guardado para o futuro.	La exportación aún no está activa. El preset solo se guarda para el futuro.
Target platform	Plataforma de destino	Plataforma de destino
Game name	Nome do jogo	Nombre del juego
Compress assets	Comprimir assets	Comprimir assets
Leave editor-only data out	Deixar de fora dados só do editor	Excluir datos solo del editor
Export...	Exportar...	Exportar...
Lighting and shadows...	Iluminação e sombras...	Iluminación y sombras...
Tags...	Tags...	Tags...
Project and export preset...	Projeto e preset de exportação...	Proyecto y preset de exportación...
Principal cameras: %zu%s	Câmeras Principal: %zu%s	Cámaras Principal: %zu%s
%zu Principal cameras: split screen is on.	%zu câmeras Principal: tela dividida ativa.	%zu cámaras Principal: pantalla dividida activa.
scene: %s	cena: %s	escena: %s
search	buscar	buscar
Hierarchy	Hierarquia	Jerarquía
+ Enemy	+ Inimigo	+ Enemigo
Clear pool	Limpar pool	Vaciar pool
(unnamed)	(sem nome)	(sin nombre)
Inspector	Inspetor	Inspector
no object selected	nenhum objeto selecionado	ningún objeto seleccionado
Entity %u (generation %u)	Entidade %u (geração %u)	Entidad %u (generación %u)
Name	Nome	Nombre
Position	Posição	Posición
Rotation	Rotação	Rotación
Scale	Escala	Escala
Cube	Cubo	Cubo
Sphere	Esfera	Esfera
Plane	Plano	Plano
Shape	Forma	Forma
Color	Cor	Color
Roughness	Rugosidade	Rugosidad
Metallic	Metálico	Metálico
Light source	Luz	Luz
Directional	Direcional	Direccional
Point	Pontual	Puntual
Type	Tipo	Tipo
Light color	Cor da luz	Color de la luz
Intensity	Intensidade	Intensidad
Range	Alcance	Alcance
Direction	Direção	Dirección
Console	Console	Consola
Clear	Limpar	Limpiar
errors: %zu  warnings: %zu	erros: %zu  avisos: %zu	errores: %zu  avisos: %zu
Profiler	Profiler	Profiler
frame: %.2f ms  (%.0f FPS)	quadro: %.2f ms  (%.0f FPS)	fotograma: %.2f ms  (%.0f FPS)
DeleteAndReuse pool: dormant=%zu reused=%zu freed=%zu	pool DeleteAndReuse: dormentes=%zu reusados=%zu liberados=%zu	pool DeleteAndReuse: inactivos=%zu reutilizados=%zu liberados=%zu
live entities: %zu	entidades vivas: %zu	entidades vivas: %zu
Zones: use TOOL_ZONE("name") in code (debug level 2).	Zonas: use TOOL_ZONE("nome") no código (nível de debug 2).	Zonas: usa TOOL_ZONE("nombre") en el código (nivel de depuración 2).
Viewport	Viewport	Vista
Stop	Parar	Detener
right button: orbit | wheel: zoom | click: select | %llu tris, %llu px	botão direito: girar | roda: zoom | clique: selecionar | %llu tri, %llu px	botón derecho: girar | rueda: zoom | clic: seleccionar | %llu tris, %llu px
Active API: %s	API ativa: %s	API activa: %s
Graphics API	API gráfica	API gráfica
GPU backends are not available in this build (use the 'beta' preset).	Backends de GPU indisponíveis nesta compilação (use o preset 'beta').	Backends de GPU no disponibles en esta compilación (usa el preset 'beta').
Quality	Qualidade	Calidad
Render scale	Escala de renderização	Escala de renderizado
GPU: %u draws, %u triangles, %u uploads/frame	GPU: %u draws, %u triângulos, %u uploads/quadro	GPU: %u draws, %u triángulos, %u cargas/fotograma
Theme	Tema	Tema
Custom	Personalizado	Personalizado
UI theme	Tema da UI	Tema de la UI
User themes	Temas do usuário	Temas del usuario
Base colors	Cores-base	Colores base
Changing a base color recalculates the whole palette; fine tweaks live in 'Advanced colors'.	Mudar uma cor-base recalcula a paleta inteira; ajustes finos ficam em 'Cores avançadas'.	Cambiar un color base recalcula toda la paleta; los ajustes finos están en 'Colores avanzados'.
Shape and size	Forma e tamanho	Forma y tamaño
Rounding	Arredondamento	Redondeo
UI scale	Escala da UI	Escala de la UI
Advanced colors (all)	Cores avançadas (todas)	Colores avanzados (todos)
filter	filtrar	filtrar
Save theme	Salvar tema	Guardar tema
theme name	nome do tema	nombre del tema
saved to %s	salvo em %s	guardado en %s
failed to save	falha ao salvar	error al guardar
Reset to default theme	Restaurar tema padrão	Restaurar tema predeterminado
Reset window layout	Restaurar layout das janelas	Restaurar disposición de ventanas
Language packs	Pacotes de idioma	Paquetes de idioma
%zu of %zu phrases seen so far are translated in this language.	%zu de %zu frases já vistas estão traduzidas neste idioma.	%zu de %zu frases vistas están traducidas en este idioma.
Create or edit a pack	Criar ou editar um pacote	Crear o editar un paquete
Packs are JSON files in the '%s' folder. Untranslated phrases appear in English.	Pacotes são arquivos JSON na pasta '%s'. Frases sem tradução aparecem em português.	Los paquetes son archivos JSON en la carpeta '%s'. Las frases sin traducir aparecen en portugués.
Reload packs	Recarregar pacotes	Recargar paquetes
Export template	Exportar modelo	Exportar plantilla
template written to %s/modelo.json	modelo gravado em %s/modelo.json	plantilla guardada en %s/modelo.json
Settings	Configurações	Configuración
Assets	Assets	Recursos
'assets' folder not found in the current directory	pasta 'assets' não encontrada no diretório atual	carpeta 'assets' no encontrada en el directorio actual
File	Arquivo	Archivo
New narrative graph	Novo grafo narrativo	Nuevo grafo narrativo
Save graph	Salvar grafo	Guardar grafo
Settings...	Configurações...	Configuración...
Quit	Sair	Salir
Window	Janela	Ventana
Narrative Controller	Controlador Narrativo	Controlador Narrativo
Reset layout	Restaurar layout	Restaurar disposición
Options	Opções	Opciones
Appearance and colors...	Aparência e cores...	Apariencia y colores...
Language...	Idioma...	Idioma...
Graphics API...	API gráfica...	API gráfica...
API: %s	API: %s	API: %s
entities: %zu	entidades: %zu	entidades: %zu
Background	Fundo	Fondo
Surface	Superfície	Superficie
Text	Texto	Texto
Accent	Destaque	Acento
Border	Borda	Borde
New	Novo	Nuevo
Save	Salvar	Guardar
Compile	Compilar	Compilar
file	arquivo	archivo
Open	Abrir	Abrir
Variables	Variáveis	Variables
new	nova	nueva
input	entrada	entrada
then	então	entonces
else	senão	si no
output	saída	salida
Properties	Propriedades	Propiedades
select a node	selecione um nó	selecciona un nodo
inputs (ids)	entradas (ids)	entradas (ids)
Quick test	Teste rápido	Prueba rápida
event	evento	evento
Fire	Disparar	Disparar
id	id	id
variable	variável	variable
condition (id)	condição (id)	condición (id)
name	nome	nombre
argument	argumento	argumento
input (id)	entrada (id)	entrada (id)
group	grupo	grupo
value	valor	valor
hysteresis	histerese	histéresis
seconds	segundos	segundos
Appearance	Aparência	Apariencia
Language	Idioma	Idioma
Graphics	Gráficos	Gráficos
Dark	Escuro	Oscuro
Light	Claro	Claro
Midnight	Meia-noite	Medianoche
Forest	Floresta	Bosque
Ember	Brasa	Brasa
High contrast	Alto contraste	Alto contraste
+ Create	+ Criar	+ Crear
3D sound (needs a Sound Ear)	Som 3D (precisa de um Ouvido)	Sonido 3D (necesita un Oído)
Add Audio Source to scene	Adicionar Fonte de Áudio à cena	Añadir Fuente de Audio a la escena
Add to scene	Adicionar à cena	Añadir a la escena
Ambient light	Luz ambiente	Luz ambiental
Assets assigned to this scene	Assets alocados nesta cena	Assets asignados a esta escena
Assign to current scene	Alocar na cena atual	Asignar a la escena actual
Audio	Áudio	Audio
Audio Source	Fonte de Áudio	Fuente de Audio
Camera	Câmera	Cámara
Cancel	Cancelar	Cancelar
Create	Criar	Crear
Delete	Apagar	Eliminar
Direction (azimuth)	Direção (azimute)	Dirección (acimut)
Drop files from your computer onto the editor window to import them.	Arraste arquivos do computador para a janela do editor para importá-los.	Arrastra archivos de tu ordenador a la ventana del editor para importarlos.
Duplicate	Duplicar	Duplicar
Dynamic (day/night cycle while playing)	Dinâmica (ciclo dia/noite ao jogar)	Dinámica (ciclo día/noche al jugar)
Edit	Editar	Editar
Edit file	Editar arquivo	Editar archivo
Empty object	Objeto vazio	Objeto vacío
Far	Longe	Lejos
Field of view	Campo de visão	Campo de visión
Game	Jogo	Juego
Game view (grid, split)...	Janela do jogo (grid, split)...	Ventana de juego (cuadrícula, split)...
Gizmos	Gizmos	Gizmos
Global Light	Luz Global	Luz Global
Global Light (sun)	Luz Global (sol)	Luz Global (sol)
Grid	Grid	Cuadrícula
Grid and gizmos	Grid e gizmos	Cuadrícula y gizmos
Height (elevation)	Altura (elevação)	Altura (elevación)
Horizontal split (top / bottom)	Divisão horizontal (cima / baixo)	División horizontal (arriba / abajo)
Icons	Ícones	Iconos
Import a .glb/.gltf/.fbx in Assets and pick it here.	Importe um .glb/.gltf/.fbx em Assets e escolha aqui.	Importa un .glb/.gltf/.fbx en Assets y elígelo aquí.
Import asset...	Importar asset...	Importar asset...
Import...	Importar...	Importar...
Invalid name (no / \\ : * ? " < > |).	Nome inválido (sem / \\ : * ? " < > |).	Nombre inválido (sin / \\ : * ? " < > |).
List	Lista	Lista
Loop	Repetir	Bucle
Main Camera (Principal)	Câmera Principal	Cámara Principal
Master volume	Volume geral	Volumen general
Max distance	Distância máxima	Distancia máxima
Min distance	Distância mínima	Distancia mínima
Model	Modelo	Modelo
Model file	Arquivo do modelo	Archivo del modelo
More than two Principal cameras: only the first two are shown.	Mais de duas câmeras Principais: só as duas primeiras aparecem.	Más de dos cámaras Principales: solo se muestran las dos primeras.
Near	Perto	Cerca
New folder	Nova pasta	Nueva carpeta
New name:	Novo nome:	Nuevo nombre:
New scene...	Nova cena...	Nueva escena...
New text file	Novo arquivo de texto	Nuevo archivo de texto
No camera has the 'Principal' tag. Create a camera (Hierarchy > + Create > Main Camera) or tick 'Principal' on a camera in the Inspector.	Nenhuma câmera tem a tag 'Principal'. Crie uma câmera (Hierarquia > + Criar > Câmera Principal) ou marque 'Principal' numa câmera no Inspetor.	Ninguna cámara tiene la etiqueta 'Principal'. Crea una cámara (Jerarquía > + Crear > Cámara Principal) o marca 'Principal' en una cámara en el Inspector.
OK	OK	OK
Only the first Global Light in the scene is used.	Só a primeira Luz Global da cena é usada.	Solo se usa la primera Luz Global de la escena.
Open scene	Abrir cena	Abrir escena
Play	Tocar	Reproducir
Play on start	Tocar ao iniciar	Reproducir al iniciar
Play preview	Ouvir prévia	Escuchar vista previa
Point Light	Luz pontual	Luz puntual
Pool demo (DeleteAndReuse)	Demo do pool (DeleteAndReuse)	Demo del pool (DeleteAndReuse)
Preview	Prévia	Vista previa
Principal (active game camera)	Principal (câmera ativa do jogo)	Principal (cámara activa del juego)
Reload list	Recarregar lista	Recargar lista
Rename	Renomear	Renombrar
Save scene	Salvar cena	Guardar escena
Scene	Cena	Escena
Scenes	Cenas	Escenas
Scenes in project	Cenas do projeto	Escenas del proyecto
Select a model or sound in Assets to assign it here.	Selecione um modelo ou som em Assets para usá-lo aqui.	Selecciona un modelo o sonido en Assets para usarlo aquí.
Shared by every Principal camera: changing it here changes it for all.	Compartilhado por todas as câmeras Principais: mudar aqui muda em todas.	Compartido por todas las cámaras Principales: cambiarlo aquí lo cambia en todas.
Show gizmos (cameras, lights, sounds)	Mostrar gizmos (câmeras, luzes, sons)	Mostrar gizmos (cámaras, luces, sonidos)
Show grid in the Game window	Mostrar grid na janela do jogo	Mostrar cuadrícula en la ventana de juego
Show grid in the Scene window	Mostrar grid na janela de cena	Mostrar cuadrícula en la ventana de escena
Sound ear (audio listener)	Ouvido de som (listener de áudio)	Oído de sonido (oyente de audio)
Sound file	Arquivo de som	Archivo de sonido
Speed (deg/s)	Velocidade (graus/s)	Velocidad (grados/s)
Split screen	Tela dividida	Pantalla dividida
Switching scenes saves the current one first.	Trocar de cena salva a atual antes.	Cambiar de escena guarda la actual antes.
The grid is an editor aid: it only appears in the Game window if you turn it on here.	O grid é uma ajuda do editor: só aparece na janela do jogo se você ativar aqui.	La cuadrícula es una ayuda del editor: solo aparece en la ventana de juego si la activas aquí.
This folder is empty.	Esta pasta está vazia.	Esta carpeta está vacía.
Used when two cameras have the 'Principal' tag. Saved with the scene.	Usado quando duas câmeras têm a tag 'Principal'. Salvo com a cena.	Se usa cuando dos cámaras tienen la etiqueta 'Principal'. Se guarda con la escena.
Vertical split (side by side)	Divisão vertical (lado a lado)	División vertical (lado a lado)
Match window size (x render scale)	Acompanhar a janela (x escala)	Seguir la ventana (x escala)
Game resolution	Resolução do jogo	Resolución del juego
Custom	Personalizada	Personalizada
Width	Largura	Ancho
Height	Altura	Alto
Reset	Restaurar	Restablecer
Below 1.0 renders fewer pixels (faster); above 1.0 renders more pixels (sharper, slower).	Abaixo de 1,0 desenha menos pixels (mais rápido); acima de 1,0 desenha mais (mais nítido, mais lento).	Por debajo de 1,0 dibuja menos píxeles (más rápido); por encima dibuja más (más nítido, más lento).
Sharp pixels when enlarging (no smoothing)	Pixels nítidos ao ampliar (sem suavização)	Píxeles nítidos al ampliar (sin suavizado)
Frame rate limit	Limite de FPS	Límite de FPS
Unlimited	Sem limite	Sin límite
Game renders at %d x %d.	O jogo é desenhado em %d x %d.	El juego se dibuja a %d x %d.
Volume	Volume	Volumen"""
tab = [r.split("\t") for r in ROWS.split("\n")]
assert all(len(r) == 3 for r in tab), [r for r in tab if len(r) != 3][:2]
out = root / "assets/lang"; out.mkdir(parents=True, exist_ok=True)
packs = [("en", "English", None), ("pt-BR", "Portugu\u00eas (Brasil)", 1), ("es", "Espa\u00f1ol", 2)]
for code, name, col in packs:
    strings = {} if col is None else {r[0]: r[col] for r in tab if r[col] != r[0]}
    (out / f"{code}.json").write_text(json.dumps({"code": code, "name": name, "strings": strings}, ensure_ascii=False, indent=1), encoding="utf-8")
print("ok", len(tab), "frases")
