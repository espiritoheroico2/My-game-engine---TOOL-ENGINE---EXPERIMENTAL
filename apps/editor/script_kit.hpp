// script_kit.hpp - Arquivos de apoio para o VS Code (autocomplete da API de script). O editor os escreve na raiz do
// projeto ao abrir: toolengine.d.ts e .vscode/toolengine.code-snippets sempre que mudam; jsconfig.json so se nao existir
// (para nao apagar o do usuario). Os snippets vem dos exemplos do Help (testados pelo selftest).
#pragma once

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace editor {

namespace scriptkit_detail {

inline constexpr const char* kDts = R"KIT(// toolengine.d.ts - tipos da API de script do ToolEngine (QuickJS). Gerado pelo editor; nao edite.
// Use com o VS Code: o autocomplete le este arquivo pelo jsconfig.json da raiz do projeto.

/** Teclas aceitas por input.down / input.pressed. */
type ToolKey = "W" | "A" | "S" | "D" | "Q" | "E" | "R" | "F" | "1" | "2" | "Space" | "Shift" | "Ctrl" | "Alt"
  | "Up" | "Down" | "Left" | "Right" | "Enter" | "Escape" | "Mouse0" | "Mouse1";

/** Tipos de componente que scripts podem adicionar ou remover. */
type ToolComponent = "MeshRenderer" | "AudioSource" | "ParticleComp";

/** O objeto que o script controla. Rotacao em graus. Mude os arrays no lugar ou atribua novos. */
interface ToolObject {
  readonly name: string;
  transform: {
    pos: [number, number, number];
    rot: [number, number, number];
    scale: [number, number, number];
  };
}

/** Assinaturas das funcoes de entrada. Voce so escreve as que precisar. */
interface ToolEntryPoints {
  start?(obj: ToolObject): void;
  update?(obj: ToolObject, dt: number): void;
  OnEventTrigger?(obj: ToolObject, name: string): void;
}

/** Tempo do jogo. */
declare const time: {
  /** Segundos de jogo desde o Play. */
  readonly now: number;
  /** Duracao do quadro atual, em segundos. */
  readonly dt: number;
};

/** Log: escreve no Console do editor. */
declare const debug: {
  /** Linha de informacao no Console. */
  console(message: any): void;
  /** Aviso (amarelo) no Console. O script continua. */
  warn(message: any): void;
  /** Erro (vermelho) no Console. O script continua. */
  error(message: any): void;
};

/** Entrada do teclado e do mouse neste quadro. */
declare const input: {
  /** true enquanto a tecla estiver mantida. */
  down(key: ToolKey): boolean;
  /** true somente no quadro em que a tecla foi apertada. */
  pressed(key: ToolKey): boolean;
  /** Movimento do mouse neste quadro: [dx, dy]. */
  mouseDelta(): [number, number];
};

/** Envia um evento. target: nome do objeto, "tag:<tag>" ou "*". Aplicado logo apos o script. */
declare function send(target: string, event: string): void;

/** Liga ou desliga um objeto pelo nome. */
declare function setActive(target: string, on: boolean): void;

/** Cria uma copia do objeto chamado template. Devolve o nome da copia (ex.: "Bala#1"). */
declare function instantiate(template: string, x?: number, y?: number, z?: number): string;

/** Remove o objeto agora, ou depois de `seconds`. */
declare function destroy(target: string, seconds?: number): void;

/** Posicao, rotacao (graus) e cor (0 a 1, MeshRenderer) de um objeto pelo nome. */
declare function setPosition(target: string, x: number, y: number, z: number): void;
declare function setRotation(target: string, x: number, y: number, z: number): void;
declare function setColor(target: string, r: number, g: number, b: number): void;

/** Adiciona ou remove um componente. Nao e possivel trocar corpo de fisica durante o Play. */
declare function addComponent(target: string, type: ToolComponent): void;
declare function removeComponent(target: string, type: ToolComponent): void;

/** [x, y, z] de um objeto, ou undefined se nao existir. */
declare function position(name: string): [number, number, number] | undefined;
)KIT";

inline constexpr const char* kJsconfig = R"KIT({
  "compilerOptions": {
    "target": "ES2020",
    "module": "ESNext",
    "allowJs": true,
    "checkJs": false,
    "lib": ["ES2020"]
  },
  "include": ["Assets/**/*.js", "toolengine.d.ts"],
  "exclude": ["Library", "Build"]
}
)KIT";

inline constexpr const char* kSnippets = R"KIT({
  "ToolEngine example 1: Move and spin": {
    "prefix": "tool1",
    "scope": "javascript",
    "description": "Move and spin",
    "body": [
      "export function update(obj, dt) {",
      "  obj.transform.pos[1] = 1 + Math.sin(time.now) * 0.5;   // sobe e desce",
      "  obj.transform.rot[1] += 45 * dt;                       // gira 45 graus por segundo (eixo Y)",
      "}"
    ]
  },
  "ToolEngine example 2: Walk with WASD (no physics)": {
    "prefix": "tool2",
    "scope": "javascript",
    "description": "Walk with WASD (no physics)",
    "body": [
      "export function update(obj, dt) {",
      "  const r = obj.transform.rot[1] * Math.PI / 180;",
      "  const fx = -Math.sin(r), fz = -Math.cos(r);   // frente do objeto",
      "  const dx = Math.cos(r), dz = -Math.sin(r);    // direita do objeto",
      "  let mx = 0, mz = 0;",
      "  if (input.down(\"W\")) { mx += fx; mz += fz; }",
      "  if (input.down(\"S\")) { mx -= fx; mz -= fz; }",
      "  if (input.down(\"D\")) { mx += dx; mz += dz; }",
      "  if (input.down(\"A\")) { mx -= dx; mz -= dz; }",
      "  obj.transform.pos[0] += mx * 5 * dt;",
      "  obj.transform.pos[2] += mz * 5 * dt;",
      "}"
    ]
  },
  "ToolEngine example 3: First person": {
    "prefix": "tool3",
    "scope": "javascript",
    "description": "First person",
    "body": [
      "let yaw = 0, pitch = 0;",
      "export function update(obj, dt) {",
      "  const m = input.mouseDelta();",
      "  yaw -= m[0] * 0.2;                                          // mouse para os lados gira",
      "  pitch = Math.max(-80, Math.min(80, pitch - m[1] * 0.2));    // mouse para cima olha para cima",
      "  obj.transform.rot[1] = yaw;",
      "  const r = yaw * Math.PI / 180;",
      "  const fx = -Math.sin(r), fz = -Math.cos(r), dx = Math.cos(r), dz = -Math.sin(r);",
      "  let mx = 0, mz = 0;",
      "  if (input.down(\"W\")) { mx += fx; mz += fz; }",
      "  if (input.down(\"S\")) { mx -= fx; mz -= fz; }",
      "  if (input.down(\"D\")) { mx += dx; mz += dz; }",
      "  if (input.down(\"A\")) { mx -= dx; mz -= dz; }",
      "  obj.transform.pos[0] += mx * 5 * dt;",
      "  obj.transform.pos[2] += mz * 5 * dt;",
      "  const p = obj.transform.pos;",
      "  setPosition(\"Camera\", p[0], p[1] + 1.6, p[2]);   // olhos a 1,6 m do chao",
      "  setRotation(\"Camera\", pitch, yaw, 0);",
      "}"
    ]
  },
  "ToolEngine example 4: Third person": {
    "prefix": "tool4",
    "scope": "javascript",
    "description": "Third person",
    "body": [
      "let orbita = 0;",
      "export function update(obj, dt) {",
      "  const m = input.mouseDelta();",
      "  orbita -= m[0] * 0.3;                              // arrastar o mouse gira a camera em volta",
      "  obj.transform.rot[1] = orbita;",
      "  const r = orbita * Math.PI / 180;",
      "  const fx = -Math.sin(r), fz = -Math.cos(r), dx = Math.cos(r), dz = -Math.sin(r);",
      "  let mx = 0, mz = 0;",
      "  if (input.down(\"W\")) { mx += fx; mz += fz; }",
      "  if (input.down(\"S\")) { mx -= fx; mz -= fz; }",
      "  if (input.down(\"D\")) { mx += dx; mz += dz; }",
      "  if (input.down(\"A\")) { mx -= dx; mz -= dz; }",
      "  obj.transform.pos[0] += mx * 5 * dt;",
      "  obj.transform.pos[2] += mz * 5 * dt;",
      "  const p = obj.transform.pos;",
      "  const d = 4, alt = 1.8;                            // distancia e altura da camera",
      "  setPosition(\"Camera\", p[0] - fx * d, p[1] + alt, p[2] - fz * d);",
      "  setRotation(\"Camera\", -15, orbita, 0);             // olha um pouco para baixo, para o jogador",
      "}"
    ]
  },
  "ToolEngine example 5: Projectile and shoot": {
    "prefix": "tool5",
    "scope": "javascript",
    "description": "Projectile and shoot",
    "body": [
      "export function update(obj, dt) {",
      "  if (input.pressed(\"F\")) {",
      "    const p = obj.transform.pos, r = obj.transform.rot;",
      "    const bala = instantiate(\"Bala\", p[0], p[1] + 1, p[2]);   // devolve o nome da copia: \"Bala#1\", \"Bala#2\"...",
      "    setRotation(bala, 0, r[1], 0);",
      "  }",
      "}"
    ]
  },
  "ToolEngine example 6: Change the mesh color": {
    "prefix": "tool6",
    "scope": "javascript",
    "description": "Change the mesh color",
    "body": [
      "let ultima = -1;",
      "export function update(obj, dt) {",
      "  const i = Math.floor(time.now * 2) % 3;",
      "  if (i === ultima) return;",
      "  ultima = i;",
      "  if (i === 0) setColor(obj.name, 1, 0.2, 0.2);",
      "  else if (i === 1) setColor(obj.name, 0.2, 1, 0.2);",
      "  else setColor(obj.name, 0.2, 0.4, 1);",
      "}"
    ]
  },
  "ToolEngine example 8: Send an event": {
    "prefix": "tool8",
    "scope": "javascript",
    "description": "Send an event",
    "body": [
      "export function update(obj, dt) {",
      "  if (input.pressed(\"E\")) {",
      "    send(\"Porta\", \"abrir\");          // pelo nome do objeto",
      "    send(\"tag:inimigo\", \"alerta\");   // pela tag: todos os objetos com a tag \"inimigo\"",
      "    send(\"*\", \"tudo\");               // para todos os objetos",
      "  }",
      "}"
    ]
  },
  "ToolEngine example 9: Receive an event": {
    "prefix": "tool9",
    "scope": "javascript",
    "description": "Receive an event",
    "body": [
      "export function OnEventTrigger(obj, name) {",
      "  if (name === \"abrir\") {",
      "    setColor(obj.name, 0.2, 1, 0.2);",
      "    debug.console(obj.name + \" recebeu abrir\");",
      "  }",
      "}"
    ]
  },
  "ToolEngine example 10: Send a trigger to the Narrative": {
    "prefix": "tool10",
    "scope": "javascript",
    "description": "Send a trigger to the Narrative",
    "body": [
      "export function update(obj, dt) {",
      "  if (input.pressed(\"Enter\")) {",
      "    send(\"*\", \"cena3\");   // chega aos scripts e ao Narrative (bloco trigger \"cena3\")",
      "  }",
      "}"
    ]
  },
  "ToolEngine example 11: Move toward another object": {
    "prefix": "tool11",
    "scope": "javascript",
    "description": "Move toward another object",
    "body": [
      "export function update(obj, dt) {",
      "  const alvo = position(\"Alvo\");           // [x, y, z], ou undefined se nao existe",
      "  if (!alvo) return;",
      "  const p = obj.transform.pos;",
      "  const dx = alvo[0] - p[0], dz = alvo[2] - p[2], d = Math.hypot(dx, dz);",
      "  if (d > 0.1) { p[0] += dx / d * 2 * dt; p[2] += dz / d * 2 * dt; }",
      "}"
    ]
  },
  "ToolEngine example 12: Start, instantiate, destroy and components": {
    "prefix": "tool12",
    "scope": "javascript",
    "description": "Start, instantiate, destroy and components",
    "body": [
      "export function start(obj) {",
      "  const b = instantiate(\"Bala\", 0, 2, 0);   // copia do objeto \"Bala\" (devolve o nome)",
      "  destroy(b, 2.0);                          // apaga a copia depois de 2 s",
      "  addComponent(obj, \"AudioSource\");         // tipos: MeshRenderer, AudioSource, ParticleComp",
      "}"
    ]
  },
  "ToolEngine example 13: Turn objects on and off": {
    "prefix": "tool13",
    "scope": "javascript",
    "description": "Turn objects on and off",
    "body": [
      "export function update(obj, dt) {",
      "  if (input.pressed(\"1\")) setActive(\"Inimigo 3\", false);",
      "  if (input.pressed(\"2\")) setActive(\"Inimigo 3\", true);",
      "}"
    ]
  },
  "ToolEngine: script skeleton": {
    "prefix": "tsc",
    "scope": "javascript",
    "description": "Script skeleton: start, update, OnEventTrigger",
    "body": [
      "export function start(obj) {",
      "\t$1",
      "}",
      "",
      "export function update(obj, dt) {",
      "\t$0",
      "}",
      "",
      "export function OnEventTrigger(obj, name) {",
      "}"
    ]
  },
  "ToolEngine: try/catch": {
    "prefix": "ttry",
    "scope": "javascript",
    "description": "Catch an error in your code and show it in the Console",
    "body": [
      "try {",
      "\t$1",
      "} catch (e) {",
      "\tdebug.warn(\"$2: \" + e.message);",
      "}"
    ]
  }
})KIT";

inline bool readAll(const std::filesystem::path& p, std::string& out) {
  std::ifstream f(p, std::ios::binary);
  if (!f) return false;
  std::ostringstream ss; ss << f.rdbuf(); out = ss.str();
  return true;
}

// Escreve so se o conteudo mudou (evita gravar em disco a cada abertura).
inline void writeIfChanged(const std::filesystem::path& p, const std::string& text) {
  std::string cur;
  if (readAll(p, cur) && cur == text) return;
  std::error_code ec;
  std::filesystem::create_directories(p.parent_path(), ec);
  std::ofstream f(p, std::ios::binary | std::ios::trunc);
  if (f) f << text;
}

} // namespace scriptkit_detail

// Escreve os arquivos de apoio na raiz do projeto. Nao lanca excecao: falha de disco nao deve travar o editor.
inline void ensureScriptKit(const std::filesystem::path& root) {
  if (root.empty()) return;
  scriptkit_detail::writeIfChanged(root / "toolengine.d.ts", scriptkit_detail::kDts);
  scriptkit_detail::writeIfChanged(root / ".vscode" / "toolengine.code-snippets", scriptkit_detail::kSnippets);
  std::error_code ec;
  if (!std::filesystem::exists(root / "jsconfig.json", ec))
    scriptkit_detail::writeIfChanged(root / "jsconfig.json", scriptkit_detail::kJsconfig);
}

} // namespace editor
