// script_kit.hpp - Arquivos de apoio para o VS Code (autocomplete da API de script). O editor os escreve na raiz do
// projeto ao abrir: toolengine.d.ts sempre que muda; jsconfig.json so se nao existir (para nao apagar o do usuario).
// Os snippets (.vscode/toolengine.code-snippets) sao gerados dos exemplos do Help em script_complete.hpp.
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

// Escreve os arquivos de apoio na raiz do projeto (d.ts e jsconfig; os snippets vem de script_complete.hpp).
// Nao lanca excecao: falha de disco nao deve travar o editor.
inline void ensureScriptKit(const std::filesystem::path& root) {
  if (root.empty()) return;
  scriptkit_detail::writeIfChanged(root / "toolengine.d.ts", scriptkit_detail::kDts);
  std::error_code ec;
  if (!std::filesystem::exists(root / "jsconfig.json", ec))
    scriptkit_detail::writeIfChanged(root / "jsconfig.json", scriptkit_detail::kJsconfig);
}

} // namespace editor
