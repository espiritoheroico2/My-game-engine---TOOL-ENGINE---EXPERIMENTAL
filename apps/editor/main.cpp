// UI-CONTRATO: a interface do editor e toda SpriteUI (sem Dear ImGui). Catalogo e regras em ui_contract.hpp.
// Janelas: Hierarchy, Inspector, Scene, Game, Assets, Scenes, Console, Profiler, Narrative Controller, Settings,
// Coding, Input Map, Export game e Help. Quem faz o quadro: o laco SDL deste arquivo (eventos, dock_host, paineis).
// O estado esta em editor_state.hpp; Assets/Scenes em assets_panel.hpp; componentes em components.hpp.
// Uso: tool_editor [--frames N] [--api vulkan|opengl|d3d12|d3d11] [--bench-frames N] [--lang pt-BR] [--tab N] [--play] [--screenshot-tab ...]
//   (N > 0 roda sem interacao e sai; usado nos testes automaticos)
#include "assets_panel.hpp"
#include <cctype>
#include "editor_state.hpp"
#include "export_panel.hpp"
#include "input_map_panel.hpp"
#include "frame_input.hpp"
#include "extra_ui.hpp"
#include "fx.hpp"
#include "i18n.hpp"
#include "profiler_model.hpp"
#include "project_start.hpp"
#include "selftest.hpp"
#include "viewport_tools.hpp"
#include "script_window.hpp"
editor::script_window::Panel* gScriptPanel = nullptr;   // janela de script (SpriteUI); dono: main
#include "mainmenu_model.hpp"
#include "mainmenu_panel.hpp"
#include "spriteui_diagnostics.hpp"
#include "start_screen_spriteui.hpp"
#include "profiler_panel.hpp"
#include "statusbar_panel.hpp"
#include "help_panel.hpp"
#include "test_map_panel.hpp"
#include "console_model.hpp"
#include "console_panel.hpp"
#include "hierarchy_panel.hpp"
#include "inspector_panel.hpp"
#include "coding_panel.hpp"
#include "scenes_panel.hpp"
#include "narrative_panel.hpp"
#include "editor_info_panel.hpp"
#include "assets_sprite_panel.hpp"
#include "settings_changes.hpp"
#include "settings_panel.hpp"
#include "sui_palette.hpp"
#include "panel_router.hpp"
#include "scene_view_panel.hpp"
#include "game_view_panel.hpp"
#include "dock_host.hpp"
#include "view_overlays.hpp"
#include "settings_model.hpp"
#include "ui_style.hpp"


#include <SDL3/SDL.h>

#include <algorithm>
#include <functional>
#include <unordered_map>
#include <chrono>
#include <cmath>
#include <memory>
#include <exception>
#include <new>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace {

using editor::Editor;
using editor::T;
using editor::F;
using editor::W;
namespace ed_ = editor;

// Lista (caminhos relativos ao projeto) os assets de um tipo, para os menus de escolha do Inspector.
// Cache por tipo: a varredura recursiva so roda ao mudar a pasta de Assets, ao haver operacao de arquivo
// (ed_::assetsRevision) ou a cada 2 s. Antes rodava a cada abertura de combo.
std::vector<std::string> assetsOfKind(const Editor& ed, ed_::AssetKind kind) {
  struct Cache { unsigned rev = ~0u; double t = -1e9; std::string root; std::vector<std::string> list; };
  static Cache caches[8];
  const size_t slot = static_cast<size_t>(kind) < 8 ? static_cast<size_t>(kind) : 7;
  Cache& c = caches[slot];
  const std::string root = ed.project.assetsDir().string();
  const double now = static_cast<double>(SDL_GetTicks()) / 1000.0;
  if (c.rev == ed_::assetsRevision() && c.root == root && now - c.t < 2.0) return c.list;
  c.rev = ed_::assetsRevision(); c.t = now; c.root = root; c.list.clear();
  std::error_code ec;
  if (!std::filesystem::exists(ed.project.assetsDir(), ec)) return c.list;
  for (auto& e : std::filesystem::recursive_directory_iterator(ed.project.assetsDir(), std::filesystem::directory_options::skip_permission_denied, ec)) {
    if (c.list.size() >= 500) break;
    if (e.is_regular_file(ec) && ed_::kindOf(e.path(), false) == kind) c.list.push_back(ed.project.relative(e.path()));
  }
  std::sort(c.list.begin(), c.list.end());
  return c.list;
}




// Script do objeto: caminho, abrir/editar o texto, salvar.
using editor::ScriptIssue;
using editor::checkScriptSource;
using editor::issueSummary;



// Janela Hierarchy em SpriteUI (hierarchy_panel.hpp). O ImGui so cria a moldura da janela (titulo, X e posicao);
// a lista, a selecao e o arraste sao do painel. Os pedidos do painel sao aplicados aqui, na cena.
bool gHierarchySpriteOn = false;   // o painel SpriteUI existe
bool gHierarchyFrame = false;      // a janela ImGui esta aberta neste quadro
editor::sui::Box gHierarchyRect;   // area do conteudo, em pixels da janela

// Id do objeto para o modelo: geracao e indice juntos (um objeto apagado e recriado tem outro id).
editor::hierarchy_model::Id hierarchyKey(tool::Entity e) {
  return (static_cast<editor::hierarchy_model::Id>(e.generation) << 32) | e.index;
}
tool::Entity hierarchyEntity(editor::hierarchy_model::Id id) {
  tool::Entity e{};
  e.index = static_cast<decltype(e.index)>(id & 0xFFFFFFFFu);
  e.generation = static_cast<decltype(e.generation)>(id >> 32);
  return e;
}

editor::hierarchy_model::Kind hierarchyKindOf(tool::World& w, tool::Entity e) {
  using K = editor::hierarchy_model::Kind;
  if (w.has<ed_::CameraComp>(e)) return K::Camera;
  if (w.has<ed_::GlobalLight>(e) || w.has<ed_::LightComp>(e)) return K::Light;
  if (w.has<ed_::AudioSource>(e)) return K::Audio;
  if (w.has<ed_::ParticleComp>(e)) return K::Particles;
  if (w.has<ed_::UiCanvas>(e)) return K::Ui;
  return K::Object;
}

// Lista de objetos do quadro (pai, nome, tipo, ocultar e travar) e a selecao do Editor.
editor::hierarchy_panel::Source hierarchySource(Editor& ed) {
  tool::World& w = ed.scene.world();
  editor::hierarchy_panel::Source src;
  w.eachEntity([&](tool::Entity e) {
    editor::hierarchy_model::Item item;
    item.id = hierarchyKey(e);
    const tool::Entity par = ed.parentOf(e);
    item.parent = w.alive(par) ? hierarchyKey(par) : editor::hierarchy_model::NoId;
    const auto* nm = w.get<ed_::Name>(e);
    item.label = nm ? std::string(nm->text) : std::string(T("(unnamed)"));
    item.kind = hierarchyKindOf(w, e);
    item.hidden = w.has<ed_::Inactive>(e);
    item.locked = w.has<ed_::Locked>(e);
    src.items.push_back(std::move(item));
  });
  src.selected = w.alive(ed.selected) ? hierarchyKey(ed.selected) : editor::hierarchy_model::NoId;
  return src;
}

// Menu "+ Create": mesmos objetos do menu do ImGui, na mesma ordem de editor::hierarchy_panel::kCreateLabels.
void createFromHierarchyMenu(Editor& ed, int kind) {
  switch (kind) {
    case 0: ed.selected = ed.make("Empty"); break;
    case 1: ed.selected = ed.makeMesh("Cube", 0, 0, 0.5f, 0); break;
    case 2: ed.selected = ed.makeMesh("Sphere", 1, 0, 0.5f, 0); break;
    case 3: ed.selected = ed.makeMesh("Plane", 2, 0, 0, 0); break;
    case 4: ed.selected = ed.makeCamera(false); break;
    case 5: ed.selected = ed.makeCamera(true); break;
    case 6: ed.selected = ed.makeGlobalLight(); break;
    case 7: ed.selected = ed.makePointLight(); break;
    case 8: ed.selected = ed.makeAudio(""); break;
    default: break;
  }
}

void applyHierarchyRequests(Editor& ed, const editor::hierarchy_panel::Requests& reqs) {
  using A = editor::hierarchy_panel::Act;
  tool::World& w = ed.scene.world();
  for (const editor::hierarchy_panel::Action& a : reqs) {
    const tool::Entity e = hierarchyEntity(a.id);
    const bool alive = w.alive(e);
    const bool locked = alive && w.has<ed_::Locked>(e);
    switch (a.act) {
      case A::Select: ed.selected = alive ? e : tool::Entity{}; break;
      case A::Activate: if (alive) ed.focusOn(e); break;
      case A::Reparent: {
        const tool::Entity parent = hierarchyEntity(a.parent);
        if (alive && w.alive(parent) && !locked) ed.setParent(e, parent, true);
        break;
      }
      case A::Detach: if (alive && !locked) ed.setParent(e, tool::Entity{}, true); break;
      case A::ToggleHidden:
        if (!alive) break;
        if (w.has<ed_::Inactive>(e)) w.remove<ed_::Inactive>(e);
        else w.add<ed_::Inactive>(e, ed_::Inactive{});
        break;
      case A::ToggleLocked:
        if (!alive) break;
        if (w.has<ed_::Locked>(e)) w.remove<ed_::Locked>(e);
        else w.add<ed_::Locked>(e, ed_::Locked{});
        break;
      case A::Rename:
        if (alive && !a.text.empty()) if (auto* nm = w.get<ed_::Name>(e)) ed_::ser::copyStr(nm->text, sizeof nm->text, a.text.c_str());
        break;
      case A::Duplicate: if (alive) ed.selected = ed.duplicate(e); break;
      case A::Delete:
        if (!alive) break;
        if (locked) ed.statusLine = T("Object is locked: unlock it (padlock) before deleting.");
        else ed.destroy(e);
        break;
      case A::Create: createFromHierarchyMenu(ed, a.kind); break;
    }
  }
}


// --- Inspetor --------------------------------------------------------------------------------------------------
// Componentes do objeto (nome, Transform, Mesh Renderer, Light, Global Light, Camera, Audio Source) em SpriteUI
// (inspector_panel.hpp). Logo abaixo, no mesmo ImGui, seguem as secoes que ainda sao ImGui (fisica, voxel, UI,
// particulas, recursos, script, tags) e o rodape. O bloco SpriteUI reserva a sua altura com um Dummy do ImGui.
// --- janela Scenes (SpriteUI) -------------------------------------------------------------------------------
// Copia o estado do Editor para o modelo da janela antes de cada quadro (lista, cena atual, assets, candidato).
void syncScenesPanel(Editor& ed, editor::scenesui::Panel& panel) {
  namespace SM = editor::scenes_model;
  SM::Model& m = panel.model();
  if (ed.sceneList.empty()) ed.refreshScenes();
  m.scenes = ed.sceneList;
  m.current = ed.sceneInfo.name;
  m.assets = ed.sceneInfo.assets;
  if (std::find(m.scenes.begin(), m.scenes.end(), m.selected) == m.scenes.end()) m.selected = m.current;
  m.assetMissing.clear();
  for (const std::string& a : m.assets) {
    std::error_code ec;
    m.assetMissing.push_back(!std::filesystem::exists(ed.project.resolve(a), ec));
  }
  m.assignCandidate.clear();
  const std::string& sel = ed_::ui().selected;   // asset escolhido na janela Assets
  if (!sel.empty()) {
    const ed_::AssetKind k = ed_::kindOf(sel, false);
    if (k == ed_::AssetKind::Model || k == ed_::AssetKind::Audio) m.assignCandidate = sel;
  }
}

// Confirmar criar ou renomear, com as regras da janela Scenes.
void confirmScene(Editor& ed, editor::scenes_model::Model& m) {
  namespace SM = editor::scenes_model;
  if (m.mode == SM::NameMode::New) {
    if (ed.newScene(m.name)) SM::FinishName(m);
    else m.status = ed.statusLine;
    return;
  }
  if (m.mode == SM::NameMode::Rename) {
    std::error_code ec;
    const bool cur = m.selected == ed.sceneInfo.name;
    if (cur) ed.saveScene();
    std::filesystem::rename(ed.project.scenePath(m.selected), ed.project.scenePath(m.name), ec);
    if (ec) {
      m.status = ec.message();
      return;
    }
    if (cur) ed.sceneInfo.name = m.name;
    ed.refreshScenes();
    m.selected = m.name;
    SM::FinishName(m);
  }
}

// Aplica as acoes que a janela Scenes reportou neste quadro ao Editor.
// Acoes da janela Editor info: Save copia o rascunho para o projeto e grava project.json; Close fecha a janela.
void applyEditorInfoActions(Editor& ed, editor::infoui::Panel& panel, const std::vector<editor::infoui::View::Action>& acts) {
  using A = editor::infoui::View::Action;
  for (A a : acts) {
    if (a == A::Save) {
      panel.model().applyTo(ed.projSettings);
      const bool ok = ed.projSettings.save(ed.project);
      panel.model().setSaveResult(ok, ok ? std::string(T("Saved.")) : std::string(T("Could not save project.json.")));
    } else if (a == A::Close) {
      ed.showEditorInfo = false;
    }
  }
}

void applyScenesActions(Editor& ed, editor::scenesui::Panel& panel, const std::vector<editor::scenesui::View::Action>& acts) {
  using A = editor::scenesui::View::Action;
  namespace SM = editor::scenes_model;
  SM::Model& m = panel.model();
  for (A a : acts) {
    switch (a) {
      case A::Save: ed.saveScene(); break;
      case A::Reload: ed.refreshScenes(); break;
      case A::Open: ed.openScene(m.selected); break;
      case A::Delete: {
        std::error_code ec;
        std::filesystem::remove(ed.project.scenePath(m.selected), ec);
        ed.refreshScenes();
        SM::FinishName(m);
        break;
      }
      case A::Confirm: confirmScene(ed, m); break;
      case A::Assign: ed.assignAsset(m.assignCandidate); break;
      case A::RemoveAsset: {
        const std::size_t i = panel.view().removedIndex();
        if (i < ed.sceneInfo.assets.size()) ed.sceneInfo.assets.erase(ed.sceneInfo.assets.begin() + static_cast<std::ptrdiff_t>(i));
        break;
      }
      case A::Help: editor::openHelp("scenes"); break;
      case A::Close:
        ed.showScenes = false;
        SM::FinishName(m);
        break;
      default: break;
    }
  }
}

// --- janela Assets (SpriteUI) -------------------------------------------------------------------------------------
// Lista a pasta atual (so quando muda a pasta, a revisao dos assets ou a cada 2 s) e passa a selecao e a pasta para
// as outras partes do editor: a janela Scenes oferece Assign para o item selecionado; as importacoes usam a pasta.
void syncAssetsPanel(Editor& ed, editor::assetsui::Panel& panel) {
  namespace AM = editor::assets_model;
  AM::State& s = panel.model();
  static std::string listedFolder;
  static unsigned listedRev = ~0u;
  static double listedAt = -1e9;
  static std::filesystem::path kitRoot;
  ed.project.ensure();
  // Arquivos de apoio do VS Code: uma vez por raiz de projeto (mesmo passo do ImGui).
  if (kitRoot != ed.project.root) {
    kitRoot = ed.project.root;
    ed_::ensureScriptKit(kitRoot);
    ed_::writeVsCodeSnippets(kitRoot);
  }
  std::error_code ec;
  if (!std::filesystem::exists(ed.project.root / s.folder, ec)) s.folder = std::string(AM::kRoot);
  const double now = static_cast<double>(SDL_GetTicks()) / 1000.0;
  const bool stale = listedFolder != s.folder || listedRev != ed_::assetsRevision() || now - listedAt >= 2.0;
  if (stale) {
    listedFolder = s.folder;
    listedRev = ed_::assetsRevision();
    listedAt = now;
    s.items.clear();
    for (const auto& de : std::filesystem::directory_iterator(ed.project.root / s.folder,
                                                              std::filesystem::directory_options::skip_permission_denied, ec)) {
      AM::Item it;
      const std::filesystem::path path = de.path();
      std::error_code de_ec;
      it.dir = de.is_directory(de_ec);
      it.name = path.filename().string();
      it.rel = ed.project.relative(path);
      it.kind = ed_::kindOf(path, it.dir);
      if (!it.dir) {
        std::error_code se;
        it.size = std::filesystem::file_size(path, se);
        if (se) it.size = 0;
      }
      s.items.push_back(std::move(it));
    }
    AM::Sort(s.items);
  }
  ed_::ui().folder = s.folder;        // importacoes (processImports) usam a pasta desta janela
  ed_::ui().selected = s.selected;    // a janela Scenes oferece Assign para o item selecionado
}

// Item da pasta atual pelo caminho relativo (nullptr se nao existe).
const editor::assets_model::Item* assetItemOf(const editor::assets_model::State& s, const std::string& rel) {
  for (const auto& it : s.items) {
    if (it.rel == rel) return &it;
  }
  return nullptr;
}

// Abre um texto no lugar da lista (so ate o limite de bytes do modelo).
void openAssetText(Editor& ed, editor::assets_model::State& s, const editor::assets_model::Item& it) {
  namespace AM = editor::assets_model;
  if (!AM::TextEditable(it.size)) {
    s.status = "File too large to edit here (60 KB limit)";
    return;
  }
  std::string text;
  if (!ed_::readTextFile(ed.project.resolve(it.rel), text, 60000)) {
    s.status = "Could not read the file.";
    return;
  }
  AM::Finish(s);
  s.textOpen = true;
  s.textPath = it.rel;
  s.text = std::move(text);
  s.status.clear();
  s.scroll = 0;
}

// Duplo clique (e botao Open): o que o tipo do item oferece.
void openAssetItem(Editor& ed, editor::assets_model::State& s, const editor::assets_model::Item& it) {
  namespace AM = editor::assets_model;
  switch (AM::DoubleClickAction(it.kind)) {
    case AM::Open::EnterFolder:
      s.folder = it.rel;
      s.selected.clear();
      s.scroll = 0;
      break;
    case AM::Open::Instantiate: ed_::instantiateAsset(ed, it.rel); break;
    case AM::Open::OpenScene: ed.openScene(AM::SceneNameFrom(it.name)); break;
    case AM::Open::EditText: openAssetText(ed, s, it); break;
    default: break;
  }
}

// Criar pasta, criar arquivo ou renomear, com o nome que ja passou nas regras da janela.
void confirmAsset(Editor& ed, editor::assets_model::State& s) {
  namespace AM = editor::assets_model;
  const std::filesystem::path base = ed.project.root / s.folder;
  std::error_code ec;
  if (s.mode == AM::State::Mode::NewFolder) {
    std::filesystem::create_directories(base / s.name, ec);
    if (ec) {
      s.status = ec.message();
      return;
    }
    ed_::touchAssets();
    s.selected = ed.project.relative(base / s.name);
    AM::Finish(s);
  } else if (s.mode == AM::State::Mode::NewFile) {
    if (std::filesystem::exists(base / s.name, ec)) {
      s.status = "A file with that name already exists.";
      return;
    }
    if (!ed_::writeTextFile(base / s.name, "")) {
      s.status = "Failed to create the file.";
      return;
    }
    ed_::touchAssets();
    s.selected = ed.project.relative(base / s.name);
    AM::Finish(s);
  } else if (s.mode == AM::State::Mode::Rename) {
    const std::string old = s.selected;
    const std::string err = ed_::renameAsset(ed, old, s.name);
    if (!err.empty()) {
      s.status = err;
      return;
    }
    const std::string nr = ed.project.relative(ed.project.resolve(old).parent_path() / s.name);
    s.folder = AM::FollowRename(s.folder, old, nr);
    s.selected = nr;
    AM::Finish(s);
  }
}

// Apagar o item selecionado. Se a pasta atual estava dentro dele, a janela volta para Assets.
void deleteSelectedAsset(Editor& ed, editor::assets_model::State& s) {
  namespace AM = editor::assets_model;
  const std::string rel = s.selected;
  const std::string err = ed_::deleteAsset(ed, rel);
  if (!err.empty()) {
    s.status = err;
    AM::Finish(s);
    return;
  }
  s.selected = AM::AfterDelete(s.selected, rel);
  s.folder = AM::AfterDelete(s.folder, rel);
  if (s.folder.empty()) s.folder = std::string(AM::kRoot);
  AM::Finish(s);
}

// Aplica as acoes que a janela Assets reportou neste quadro ao Editor e ao disco.
void applyAssetsActions(Editor& ed, editor::assetsui::Panel& panel, const std::vector<editor::assetsui::View::Action>& acts) {
  using A = editor::assetsui::View::Action;
  namespace AM = editor::assets_model;
  AM::State& s = panel.model();
  auto& view = panel.view();
  for (A a : acts) {
    switch (a) {
      case A::Up:
        if (AM::CanGoUp(s.folder)) {
          s.folder = AM::Parent(s.folder);
          s.selected.clear();
          s.scroll = 0;
        }
        break;
      case A::Crumb:
        s.folder = view.pickedCrumb();
        s.selected.clear();
        s.scroll = 0;
        break;
      case A::Import:
        SDL_ShowOpenFileDialog([](void*, const char* const* files, int) { if (files) for (; *files; ++files) ed_::queueImport(*files); },
                               nullptr, nullptr, nullptr, 0, nullptr, true);
        break;
      case A::Open:
        if (const AM::Item* it = assetItemOf(s, s.selected)) openAssetItem(ed, s, *it);
        break;
      case A::Activate: {
        const std::size_t i = view.activatedItem();
        if (i < s.items.size()) openAssetItem(ed, s, s.items[i]);
        break;
      }
      case A::Instantiate:
        if (const AM::Item* it = assetItemOf(s, s.selected)) {
          if (it->kind == ed_::AssetKind::Model || it->kind == ed_::AssetKind::Audio) ed_::instantiateAsset(ed, it->rel);
        }
        break;
      case A::Assign:
        if (const AM::Item* it = assetItemOf(s, s.selected)) {
          if (it->kind == ed_::AssetKind::Model || it->kind == ed_::AssetKind::Audio) {
            ed.assignAsset(it->rel);
            s.status = "Assigned to scene: " + it->rel;
          }
        }
        break;
      case A::Confirm: confirmAsset(ed, s); break;
      case A::Delete: deleteSelectedAsset(ed, s); break;
      case A::SaveText: {
        const bool ok = ed_::writeTextFile(ed.project.resolve(s.textPath), s.text);
        s.status = ok ? "Saved." : "Failed to save.";
        ed_::touchAssets();
        ed.lib.clear();
        break;
      }
      case A::CloseText:
        s.textOpen = false;
        s.text.clear();
        s.textPath.clear();
        s.status.clear();
        break;
      case A::Help: editor::openHelp("assets"); break;
      case A::Close:
        ed.showAssets = false;
        s.textOpen = false;
        AM::Finish(s);
        break;
      default: break;
    }
  }
}

editor::scenesui::Panel* gScenesPanel = nullptr;   // o painel existe (dono: main); usado pelo atalho "Nova cena"

editor::inspector_panel::InspectorPanel* gInspectorPanel = nullptr;   // o painel existe (dono: main)
bool gInspectorSpriteOn = false;     // o painel SpriteUI existe
bool gInspectorFrame = false;        // a janela ImGui do Inspector esta aberta neste quadro
editor::sui::Box gInspectorOrigin;   // canto do bloco SpriteUI na tela (preenchido no quadro)
editor::sui::Box gInspectorVisible;  // parte do bloco que cabe na janela ImGui (a janela rola e corta)
bool gProfilerFrame = false;         // a janela ImGui do Profiler esta visivel neste quadro
editor::sui::Box gProfilerRect;       // area de conteudo da janela dockavel Profiler
bool gProfilerExportRequested = false;
bool gProfilerExportActive = false;
std::string gProfilerExportStatus;

// Dados do objeto selecionado para o painel. Os ponteiros valem neste quadro.
editor::inspector_panel::Context inspectorContext(Editor& ed) {
  editor::inspector_panel::Context c;
  tool::World& w = ed.scene.world();
  c.hasObject = w.alive(ed.selected);
  if (!c.hasObject) return c;
  const tool::Entity e = ed.selected;
  c.entityIndex = e.index;
  c.entityGeneration = e.generation;
  c.active = !w.has<ed_::Inactive>(e);
  c.locked = w.has<ed_::Locked>(e);
  c.name = w.get<ed_::Name>(e);
  c.transform = w.get<ed_::Transform>(e);
  c.mesh = w.get<ed_::MeshRenderer>(e);
  c.overrides = w.get<ed_::PartOverrides>(e);
  if (c.mesh && c.mesh->shape == 3 && c.mesh->node >= 0) {   // no de modelo: nomes das partes
    if (const auto* lm = ed.lib.model(c.mesh->model); lm && static_cast<size_t>(c.mesh->node) < lm->data.nodes.size()) {
      for (int pi : lm->data.nodes[static_cast<size_t>(c.mesh->node)].parts) {
        c.parts.emplace_back(pi, lm->data.parts[static_cast<size_t>(pi)].name);
      }
    }
  }
  c.light = w.get<ed_::LightComp>(e);
  c.global = w.get<ed_::GlobalLight>(e);
  c.camera = w.get<ed_::CameraComp>(e);
  c.principal = ed.isPrincipal(e);
  c.principalCount = ed.principalCameras().size();
  c.splitVertical = ed.sceneInfo.splitVertical;
  c.audio = w.get<ed_::AudioSource>(e);
  // Script: o componente, o caminho do arquivo e, durante o Play, o status vindo do ScriptHost.
  c.script = w.get<ed_::ScriptComp>(e);
  if (c.script) c.scriptPath = c.script->path;
  c.playing = ed.playing;
  if (ed.playing) c.scriptStatus = ed.scriptHost.statusOf(e.index, e.generation);
  c.scriptRuntime = ed_::ScriptHost::compiledIn();
  // Ferramentas prontas do objeto e se ele tem fisica (algumas ferramentas precisam).
  c.tools = w.get<ed_::ToolsComp>(e);
  c.hasPhysics = w.has<ed_::PhysicsComp>(e);
  // Voxel: os blocos do objeto e quantos estao preenchidos.
  c.voxel = w.get<ed_::VoxelChunk>(e);
  if (c.voxel) c.voxelSolid = editor::solidCount(*c.voxel);
  // Particulas do objeto (componente direto: o painel le e grava nos campos, como nas outras secoes).
  c.particles = w.get<ed_::ParticleComp>(e);
  // Recursos: o que o objeto ainda pode receber e o que ele tem (listas do Add / Remove Resource).
  using R = editor::inspector_model::Resource;
  const auto addR = [&c](R r) { c.addable.push_back(static_cast<int>(r)); };
  const auto remR = [&c](R r) { c.removable.push_back(static_cast<int>(r)); };
  if (!w.has<ed_::PhysicsComp>(e)) addR(R::Physics); else remR(R::Physics);
  for (int k = 0; k < static_cast<int>(ed_::ToolKind::Count); ++k) addR(static_cast<R>(static_cast<int>(R::ToolJump) + k));
  if (w.has<ed_::ToolsComp>(e)) remR(R::ToolsAll);
  if (!w.has<ed_::MeshRenderer>(e)) addR(R::Mesh); else remR(R::Mesh);
  if (!w.has<ed_::LightComp>(e)) addR(R::PointLight); else remR(R::PointLight);
  if (w.has<ed_::GlobalLight>(e)) remR(R::GlobalLight);
  if (!w.has<ed_::CameraComp>(e)) addR(R::Camera); else remR(R::Camera);
  if (!w.has<ed_::AudioSource>(e)) addR(R::Audio); else remR(R::Audio);
  if (!w.has<ed_::ParticleComp>(e)) addR(R::Particles); else remR(R::Particles);
  if (!w.has<ed_::ScriptComp>(e)) addR(R::Script); else remR(R::Script);
  if (!w.has<ed_::UiCanvas>(e)) addR(R::GameUi); else remR(R::GameUi);
  if (!w.has<ed_::VoxelChunk>(e)) addR(R::Voxel); else remR(R::Voxel);
  // UI de jogo do objeto e os nomes das cameras da cena (a escolha de onde a UI aparece).
  c.ui = w.get<ed_::UiCanvas>(e);
  w.each<ed_::CameraComp>([&](tool::Entity ce, ed_::CameraComp&) {
    if (auto* n = w.get<ed_::Name>(ce)) c.cameraNames.emplace_back(n->text);
  });
  // Fisica: o componente, a gravidade da cena e, no Play, a velocidade e o estado do corpo.
  c.physics = w.get<ed_::PhysicsComp>(e);
  for (int k = 0; k < 3; ++k) c.worldGravity[k] = static_cast<float>(ed.sceneInfo.physics.gravity[k]);
  if (ed.playing && ed.play.built) {
    auto it = ed.play.indexOf.find(e.index);
    if (it != ed.play.indexOf.end()) {
      const auto& rb = ed.play.rb[it->second];
      c.playPhysics = F("Play: velocity %.2f, %.2f, %.2f  %s%s", static_cast<double>(rb.velocity.x), static_cast<double>(rb.velocity.y),
                        static_cast<double>(rb.velocity.z), rb.grounded ? "grounded " : "", rb.isSleeping ? "asleep" : "");
    }
  }
  // Tags: todas as da cena e quais o objeto tem (secao Tags do painel SpriteUI).
  const tool::TagMask mask = w.tagMask(e);
  for (tool::TagId i = 0; i < ed.scene.tags().size(); ++i) {
    c.tagNames.emplace_back(ed.scene.tags().name(i));
    c.tagOn.push_back(mask.test(i));
    c.tagBuiltin.push_back(ed_::isBuiltinTag(std::string(ed.scene.tags().name(i))));
  }
  return c;
}

// Acoes com efeito fora do objeto. Cada uma faz o mesmo que o ImGui fazia antes da troca.
editor::inspector_panel::Hooks inspectorHooks(Editor& ed) {
  editor::inspector_panel::Hooks h;
  h.assets = [&ed](ed_::AssetKind k) { return assetsOfKind(ed, k); };
  h.assetChosen = [&ed](ed_::AssetKind, const std::string& rel) {   // mesma regra do assetField do ImGui
    if (!rel.empty()) ed.assignAsset(rel);
    ed.lib.invalidate(rel);
  };
  h.previewSound = [&ed](const std::string& rel) { ed.previewSound(rel); };
  h.stopSound = [&ed] { ed.mixer.stop(ed.previewVoice); };
  h.hasNamed = [&ed](const std::string& name) { tool::Entity found = ed.selected; return ed.findNamed(name, found); };
  h.setActive = [&ed](bool on) {
    tool::World& w = ed.scene.world();
    if (on) { if (w.has<ed_::Inactive>(ed.selected)) w.remove<ed_::Inactive>(ed.selected); }
    else if (!w.has<ed_::Inactive>(ed.selected)) w.tryAdd<ed_::Inactive>(ed.selected, ed_::Inactive{});
  };
  h.setLocked = [&ed](bool on) {
    tool::World& w = ed.scene.world();
    if (on) w.tryAdd<ed_::Locked>(ed.selected, ed_::Locked{});
    else w.remove<ed_::Locked>(ed.selected);
  };
  h.setPrincipal = [&ed](bool on) { ed.setPrincipal(ed.selected, on); };
  h.setSplitVertical = [&ed](bool vertical) { ed.sceneInfo.splitVertical = vertical; };
  h.addTagNamed = [&ed](const std::string& name) {   // mesma regra do "Add tag" do ImGui
    const tool::TagId id = ed.scene.tag(name.c_str());
    if (id == tool::kInvalidIndex) return false;
    ed.scene.world().addTag(ed.selected, id);
    return true;
  };
  h.addTagIndex = [&ed](std::size_t i) { ed.scene.world().addTag(ed.selected, static_cast<tool::TagId>(i)); };
  h.removeTagIndex = [&ed](std::size_t i) { ed.scene.world().removeTag(ed.selected, static_cast<tool::TagId>(i)); };
  h.renameTagIndex = [&ed](std::size_t i, const std::string& to) { return ed.renameTag(static_cast<tool::TagId>(i), to); };
  h.openScriptHelp = [] { editor::openHelp("script_examples"); };   // mesmas acoes do scriptUi (ImGui)
  h.addScript = [&ed] { ed.attachScript(ed.selected); };
  h.openScriptExternal = [&ed](const std::string& rel) { editor::openInExternalEditor(ed, rel); };
  h.openScriptWindow = [&ed] { if (gScriptPanel) gScriptPanel->open(ed); };
  h.reloadScripts = [&ed] { ed.startScripts(); };
  h.removePhysics = [&ed] { ed.scene.world().remove<ed_::PhysicsComp>(ed.selected); };
  h.actionNames = [&ed](int player, int kind) {   // mesma lista do combo de acao do ImGui (scheme do jogador)
    std::vector<std::string> out;
    const std::size_t si = static_cast<std::size_t>(std::clamp(player, 1, 8) - 1);
    if (si < ed.inputMap.schemes.size()) {
      for (const auto& r : ed.inputMap.schemes[si].rows) {
        if (r.kind == kind) out.emplace_back(r.action);
      }
    }
    return out;
  };
  h.openToolsHelp = [] { editor::openHelp("jslib"); };
  h.voxelPaint = [&ed](int x, int layer, int z, int brush) {   // cada acao discreta no grid cria um passo de undo
    ed.undoTick(true);
    if (auto* vc = ed.scene.world().get<ed_::VoxelChunk>(ed.selected)) editor::setBlock(*vc, x, layer, z, static_cast<unsigned char>(brush));
    ed.undoTick(false);
  };
  h.voxelFill = [&ed] {   // mesmo calculo do ImGui: o terreno acompanha a posicao do objeto
    ed.undoTick(true);
    auto& w = ed.scene.world();
    auto* vc = w.get<ed_::VoxelChunk>(ed.selected);
    if (vc) {
      const auto* tr = w.get<ed_::Transform>(ed.selected);
      const float span = vc->cell * ed_::kChunkSize;
      editor::fillTerrain(*vc, tr ? static_cast<int>(std::lround(tr->pos[0] / span)) : 0, tr ? static_cast<int>(std::lround(tr->pos[2] / span)) : 0);
    }
    ed.undoTick(false);
  };
  h.voxelClear = [&ed] {
    ed.undoTick(true);
    if (auto* vc = ed.scene.world().get<ed_::VoxelChunk>(ed.selected)) editor::clearChunk(*vc);
    ed.undoTick(false);
  };
  h.removeUi = [&ed] { ed.scene.world().remove<ed_::UiCanvas>(ed.selected); };
  h.addResource = [&ed](int id) {   // mesmas regras do menu "+ Add Resource" do ImGui
    using R = editor::inspector_model::Resource;
    tool::World& w = ed.scene.world();
    const tool::Entity sel = ed.selected;
    const R r = static_cast<R>(id);
    if (r == R::Physics) {
      ed_::PhysicsComp pc;
      if (auto* m = w.get<ed_::MeshRenderer>(sel)) if (m->shape == 2) pc.body = 0;
      w.add<ed_::PhysicsComp>(sel, pc);
    } else if (r == R::Mesh) {
      w.add<ed_::MeshRenderer>(sel, ed_::MeshRenderer{});
    } else if (r == R::PointLight) {
      tool::Light l; l.type = tool::LightType::Point; l.range = 12.f; l.intensity = 18.f; l.color = {1.f, 0.7f, 0.4f}; l.castShadow = true;
      w.add<ed_::LightComp>(sel, ed_::LightComp{l});
    } else if (r == R::Camera) {
      w.add<ed_::CameraComp>(sel);
    } else if (r == R::Audio) {
      w.add<ed_::AudioSource>(sel);
    } else if (r == R::Particles) {
      w.add<ed_::ParticleComp>(sel);
    } else if (r == R::Script) {
      ed.attachScript(sel);
    } else if (r == R::GameUi) {
      ed_::UiCanvas u; ed_::UiElem& e = u.items[u.count++];
      std::snprintf(e.text, sizeof e.text, "%s", "Hello UI");
      w.add<ed_::UiCanvas>(sel, u);
    } else if (r == R::Voxel) {
      ed_::VoxelChunk vc; editor::fillTerrain(vc, 0, 0);
      w.add<ed_::VoxelChunk>(sel, vc);
    } else if (static_cast<int>(r) >= static_cast<int>(R::ToolJump) && static_cast<int>(r) <= static_cast<int>(R::ToolScriptLib)) {
      const int k = static_cast<int>(r) - static_cast<int>(R::ToolJump);   // tipo da ferramenta (ToolKind)
      auto* tl = w.get<ed_::ToolsComp>(sel);
      if (!tl) { w.add<ed_::ToolsComp>(sel, ed_::ToolsComp{}); tl = w.get<ed_::ToolsComp>(sel); }
      if (tl->count >= ed_::kMaxTools) return;
      ed_::ToolItem& it = tl->items[tl->count++];
      it = ed_::ToolItem{};
      it.kind = k;
      if (k == 0) { std::snprintf(it.action, sizeof it.action, "%s", "jump"); it.a = 1.5f; }
      if (k == 1) { std::snprintf(it.action, sizeof it.action, "%s", "moveX"); std::snprintf(it.target, sizeof it.target, "%s", "moveY"); it.a = 5.f; it.b = 1.f; }
      if (k == 2) { it.b = 4.f; it.c = 8.f; it.d = 6.f; it.mode = 1; }
      if (k == 3) it.b = 90.f;
      if (k == 5) {   // cria um arquivo de biblioteca no projeto
        static int n = 0;
        const std::string rel = "Assets/Scripts/Libs/Lib" + std::to_string(++n + static_cast<int>(ed.created)) + ".js";
        if (editor::scriptPathOk(ed.project, rel) &&
            editor::writeScript(ed.project, rel, "// Library for this object. Functions here become part of its code once the script runtime is on.\nexport function hello(obj) {\n  console.log(\"hello from a library\", obj.name);\n}\n"))
          std::snprintf(it.action, sizeof it.action, "%s", rel.c_str());
      }
    }
  };
  h.removeResource = [&ed](int id) {   // mesmas regras do menu "- Remove Resource" do ImGui
    using R = editor::inspector_model::Resource;
    tool::World& w = ed.scene.world();
    const tool::Entity sel = ed.selected;
    switch (static_cast<R>(id)) {
      case R::Physics: w.remove<ed_::PhysicsComp>(sel); break;
      case R::ToolsAll: w.remove<ed_::ToolsComp>(sel); break;
      case R::Mesh: w.remove<ed_::MeshRenderer>(sel); break;
      case R::PointLight: w.remove<ed_::LightComp>(sel); break;
      case R::GlobalLight: w.remove<ed_::GlobalLight>(sel); break;
      case R::Camera: w.remove<ed_::CameraComp>(sel); break;
      case R::Audio:
        if (auto* a = w.get<ed_::AudioSource>(sel)) ed.mixer.stop(a->voice);
        w.remove<ed_::AudioSource>(sel);
        break;
      case R::Particles: w.remove<ed_::ParticleComp>(sel); break;
      case R::GameUi: w.remove<ed_::UiCanvas>(sel); break;
      case R::Voxel: w.remove<ed_::VoxelChunk>(sel); break;
      case R::Script: w.remove<ed_::ScriptComp>(sel); break;
      default: break;
    }
  };
  h.smokePreset = [&ed] {
    if (auto* p = ed.scene.world().get<ed_::ParticleComp>(ed.selected)) editor::applySmokePreset(*p);
  };
  h.removeParticles = [&ed] { ed.scene.world().remove<ed_::ParticleComp>(ed.selected); };
  h.addUiElem = [&ed](int kind) {   // valores iniciais de cada tipo: os mesmos do botao "+" do ImGui
    auto* u = ed.scene.world().get<ed_::UiCanvas>(ed.selected);
    if (!u || u->count >= ed_::kMaxUi) return;
    ed_::UiElem& e = u->items[u->count++];
    e = ed_::UiElem{};
    if (kind == 0) {   // texto
      e.y = 0.05f + 0.07f * static_cast<float>(u->count);
      std::snprintf(e.text, sizeof e.text, "%s", "Score: {score}");
    } else if (kind == 2) {   // botao
      e.kind = 2; e.x = 0.4f; e.y = 0.8f;
      e.color[0] = 0.9f; e.color[1] = 0.8f; e.color[2] = 0.3f;
      std::snprintf(e.text, sizeof e.text, "%s", "Play");
      std::snprintf(e.action, sizeof e.action, "%s", "play_clicked");
    } else if (kind == 3) {   // barra
      e.kind = 3; e.w = 0.3f; e.h = 0.03f; e.y = 0.92f;
      std::snprintf(e.action, sizeof e.action, "%s", "health");
      e.color[1] = 0.8f; e.color[2] = 0.2f;
    } else {   // painel
      e.kind = 1;
      e.color[0] = e.color[1] = e.color[2] = 0.1f; e.color[3] = 0.6f;
      e.text[0] = 0;
    }
  };
  h.removeUiElem = [&ed](std::size_t i) {   // desloca os elementos seguintes, como o ImGui fazia
    auto* u = ed.scene.world().get<ed_::UiCanvas>(ed.selected);
    if (!u || static_cast<int>(i) >= u->count) return;
    for (int k = static_cast<int>(i); k + 1 < u->count; ++k) u->items[k] = u->items[k + 1];
    --u->count;
  };
  h.removeTool = [&ed](std::size_t i) {   // desloca os itens seguintes, como o ImGui fazia
    auto* tl = ed.scene.world().get<ed_::ToolsComp>(ed.selected);
    if (!tl || static_cast<int>(i) >= tl->count) return;
    for (int k = static_cast<int>(i); k + 1 < tl->count; ++k) tl->items[k] = tl->items[k + 1];
    --tl->count;
  };
  h.setPartOverride = [&ed](int pi, bool on) {   // material editado por parte de um modelo importado
    tool::World& w = ed.scene.world();
    const auto* mr = w.get<ed_::MeshRenderer>(ed.selected);
    if (!mr) return;
    const auto* lm = ed.lib.model(mr->model);
    if (!lm || pi < 0 || static_cast<size_t>(pi) >= lm->data.parts.size()) return;
    const auto& part = lm->data.parts[static_cast<size_t>(pi)];
    auto* po = w.get<ed_::PartOverrides>(ed.selected);
    if (on) {
      if (!po) { po = w.tryAdd<ed_::PartOverrides>(ed.selected, ed_::PartOverrides{}); if (!po) return; }
      for (const auto& it : po->items) if (it.part == pi) return;
      ed_::PartOverride ov;
      ov.part = pi;
      ov.color[0] = part.material.albedo.x; ov.color[1] = part.material.albedo.y; ov.color[2] = part.material.albedo.z;
      ov.roughness = part.material.roughness; ov.metallic = part.material.metallic;
      po->items.push_back(ov);
    } else if (po) {
      po->items.erase(std::remove_if(po->items.begin(), po->items.end(), [&](const ed_::PartOverride& it) { return it.part == pi; }), po->items.end());
    }
  };
  h.setPartTexture = [&ed](int pi, const std::string& texture) {
    auto* overrides = ed.scene.world().get<ed_::PartOverrides>(ed.selected);
    if (!overrides) return;
    for (auto& item : overrides->items) if (item.part == pi) {
      ed_::ser::copyStr(item.texture, sizeof item.texture, texture);
      return;
    }
  };
  return h;
}

// Bloco SpriteUI: prepara os controles, reserva a altura no ImGui e guarda a parte visivel dentro da janela.
bool gDockSaveRequested = false;   // Janela > Salvar layout: o host salva no proximo quadro
constexpr const char* kDockLayoutFile = "editor_dock_layout.txt";
editor::sui::Box gInspectorBody{}, gSettingsBody{}, gCodingBody{};   // corpo (abaixo do cabecalho) da moldura SpriteUI neste quadro
// Corpo de uma janela de moldura SpriteUI neste quadro. Escondida (ou sem painel): a moldura some e o corpo e vazio.
editor::sui::Box framedBody(editor::framed::Chrome* chrome, bool shown, int winW, int winH, float w, float h) {
  if (!chrome) return {};
  if (!shown) {
    chrome->hide();
    return {};
  }
  return chrome->prepare(static_cast<float>(winW), static_cast<float>(winH), w, h);
}



// LEGADO (nao chamado desde a migracao do Console para a moldura SpriteUI). Remover na etapa 7 do
// docs/planos/05_REMOVER_IMGUI.md, so depois da aprovacao visual do usuario.
bool gConsoleFrame = false;
editor::sui::Box gConsoleRect;


// Clicar numa linha "[Nome] ..." seleciona o objeto Nome na cena (nome vazio: nada).
void selectObjectNamed(Editor& ed, const std::string& name) {
  if (name.empty()) return;
  const tool::Entity e = ed.findByName(name);
  if (ed.scene.world().alive(e)) ed.selected = e;
}


editor::statusbar::Info statusBarInfo(Editor& ed) {
  editor::statusbar::Info info;
  info.api = ed.apiStatus;
  info.frameMs = tool::debug().lastFrameMs();
  info.scene = ed.sceneInfo.name;
  info.entities = ed.scene.world().liveCount();
  info.errors = tool::debug().count(tool::Severity::Error);
  info.warnings = tool::debug().count(tool::Severity::Warning);
  info.statusLine = ed.statusLine;
  info.language = editor::I18n::get().code();
  return info;
}

// Pedidos do painel SpriteUI do Console (o painel nao conhece o Editor). Aplicados depois do desenho.
void applyConsoleRequests(Editor& ed, const editor::console_panel::Requests& req) {
  if (req.clear) {
    ed.console.clear();
    ++ed.consoleSerial;
  }
  if (req.checkScripts) ed.checkAllScripts();
  if (!req.command.empty()) editor::runConsoleCommand(ed, req.command);
  selectObjectNamed(ed, req.select);
}

// Dados que o Profiler nao conhece (API, luz, GPU, cena): entram no modelo do painel SpriteUI a cada refresh.
editor::profiler_model::Extras fillProfilerExtras(Editor& ed) {
  editor::profiler_model::Extras x;
  x.api = ed.apiStatus;
  x.adapter = ed.gpuDev ? ed.gpuDev->deviceName() : std::string("(software renderer)");
  x.hasGpu = ed.gpuDev != nullptr;
  x.renderStats = ed.renderFrameStats;
  if (ed.gpuDev) {
    const auto g = ed.renderFrameStats.backendTotal();
    x.gpuDraws = static_cast<unsigned>(g.backendDrawCalls);
    x.gpuTriangles = static_cast<unsigned>(g.backendTriangles);
    x.gpuUploads = static_cast<unsigned>(g.backendBufferUploads);
    x.gpuPipelineSwitches = static_cast<unsigned>(g.backendPipelineSwitches);
  }
  const auto q = tool::makeQuality(ed.sceneInfo.render.lighting);
  const auto& lf = ed.lightRig.frame();
  x.lightShadows = lf.shadows();
  x.lightCascades = static_cast<unsigned>(q.csmCascades);
  x.lightShadowRes = static_cast<unsigned>(q.shadowMapResolution);
  x.lightDropped = static_cast<unsigned>(lf.droppedLights);
  x.postOn = ed.sceneInfo.render.post.active();
  const auto s = ed.scene.stats();
  x.entities = ed.scene.world().liveCount();
  x.dormant = ed.scene.dormantCount();
  x.reused = s.reused;
  x.freed = s.freed;
  x.meshes = ed.lib.meshCount();
  x.sounds = ed.lib.soundCount();
  x.tags = ed.scene.tags().size();
  x.errors = static_cast<unsigned>(tool::debug().count(tool::Severity::Error));
  x.warnings = static_cast<unsigned>(tool::debug().count(tool::Severity::Warning));
  return x;
}

// --- Janelas de visualizacao (Scene e Game) -------------------------------------------------------------------


// Janelas Scene e Jogo em SpriteUI (scene_view_panel.hpp, game_view_panel.hpp). O ImGui cria a janela (titulo, foco,
// docking, o "?") e mede a area; o painel desenha imagem, barra e sobreposicoes depois do ImGui. Sem a fonte, a
// janela nao aparece (como as outras janelas SpriteUI).
//
// Exemplo: a Scene calcula a selecao e a grade no quadro, o painel desenha e devolve o que o usuario pediu
// (`Requests`), e applySceneRequests aplica no Editor. Assim a regra de edicao fica em um lugar so.
namespace scene_panel = editor::scene_view_panel;
namespace game_panel = editor::game_view_panel;
namespace vm = editor::scene_view_model;
namespace gm = editor::game_view_model;
namespace vo = editor::view_overlays;
using ImageFit = ToolEngine::SpriteUi::Elements::ImageFit;

editor::scene_view_panel::ScenePanel* gScenePanel = nullptr;   // dono: main (unique_ptr)
editor::game_view_panel::GamePanel* gGamePanel = nullptr;
bool gSceneFrame = false;   // a janela Scene foi desenhada neste quadro
bool gGameFrame = false;
editor::dock_host::Host* gDock = nullptr;   // dono: main (unique_ptr); docking das janelas SpriteUI
// Janela visivel: o host decide (aberta e aba ativa, ou flutuante). Sem host, vale a flag do Editor.
bool dockShown(editor::dock_host::Kind k, bool open) { return gDock ? gDock->visible(editor::dock_host::idOf(k), open) : open; }
// Registra uma janela no host. `get` devolve o objeto com frameRect/gripRect/setDock/floatAt, ou nullptr.
template <class GetFn>
void addDockWindow(editor::dock_host::Host& host, editor::dock_host::Kind k, std::function<std::string()> title,
                   std::function<bool&()> flag, GetFn get) {
  editor::dock_host::Window w;
  w.id = editor::dock_host::idOf(k);
  w.title = std::move(title);
  w.open = [flag] { return flag(); };
  w.close = [flag] { flag() = false; };
  w.frame = [get]() -> std::optional<ToolEngine::SpriteUi::Rect> {
    auto* p = get();
    if (!p) return std::nullopt;
    return p->frameRect();
  };
  w.grip = [get]() { auto* p = get(); return p ? p->gripRect() : ToolEngine::SpriteUi::Rect{}; };
  w.setDock = [get](std::optional<ToolEngine::SpriteUi::Rect> d) { if (auto* p = get()) p->setDock(d); };
  w.floatAt = [get](ToolEngine::SpriteUi::Vec2 at) { if (auto* p = get()) p->floatAt(at); };
  host.add(std::move(w));
}
editor::sui::Box gSceneRect;   // area da janela (pixels da janela)
editor::sui::Box gGameRect;
scene_panel::Values gSceneValues;   // montado no quadro, desenhado depois do ImGui
game_panel::Values gGameValues;

void beginViewsFrame() { gSceneFrame = gGameFrame = false; }

// Entidade <-> id de 64 bits (o painel so guarda o id).
std::uint64_t packEntity(tool::Entity e) { return (static_cast<std::uint64_t>(e.index) << 32) | e.generation; }
tool::Entity unpackEntity(std::uint64_t id) {
  return tool::Entity{static_cast<std::uint32_t>(id >> 32), static_cast<std::uint32_t>(id & 0xffffffffu)};
}

scene_panel::Selection sceneSelection(Editor& ed) {
  scene_panel::Selection s;
  auto& w = ed.scene.world();
  if (!w.alive(ed.selected)) return s;
  const auto* t = w.get<ed_::Transform>(ed.selected);
  if (!t) return s;
  s.present = true;
  s.locked = w.has<ed_::Locked>(ed.selected);
  for (std::size_t i = 0; i < 3; ++i) {
    s.pos[i] = t->pos[i];
    s.rot[i] = t->rot[i];
    s.scale[i] = t->scale[i];
  }
  s.worldPos = ed.worldPos(ed.selected);
  s.world = ed.worldMat(ed.selected);
  const tool::Entity par = ed.parentOf(ed.selected);
  s.hasParent = w.alive(par);
  s.parentInv = s.hasParent ? editor::inverseAffine(ed.worldMat(par)) : tool::Mat4{};
  return s;
}

// Objeto sob o mouse: raio; sem acerto, o centro mais proximo (luzes, cameras, vazios). Mesma regra de antes.
tool::Entity pickSceneAt(Editor& ed, const tool::Camera& cam, editor::sui::Box img, vm::Pixel mp) {
  const editor::Px origin{img.x, img.y}, size{img.w, img.h}, at{mp.x, mp.y};
  tool::Entity hit = editor::pickByRay(ed, editor::rayFromScreen(cam, origin, size, at));
  if (ed.scene.world().alive(hit)) return hit;
  const vm::Projection proj = vm::Projection::make(cam, {img.x, img.y, img.w, img.h});
  float best = 28.f;
  ed.scene.world().eachEntity([&](tool::Entity e) {
    const auto* t = ed.scene.world().get<ed_::Transform>(e);
    if (!t) return;
    const auto* m = ed.scene.world().get<ed_::MeshRenderer>(e);
    if (m && m->shape == 2) return;   // o chao nao disputa o clique
    vm::Pixel sp{};
    if (!proj.point({t->pos[0], t->pos[1], t->pos[2]}, sp)) return;
    const float d = std::hypot(sp.x - mp.x, sp.y - mp.y);
    if (d < best) { best = d; hit = e; }
  });
  return hit;
}

// O que tem sob o mouse na Scene (botao direito): id, nome e marcas do objeto.
scene_panel::Probe sceneProbe(Editor& ed, const tool::Camera& cam, editor::sui::Box img, vm::Pixel p) {
  scene_panel::Probe r;
  const tool::Entity e = pickSceneAt(ed, cam, img, p);
  auto& w = ed.scene.world();
  if (!w.alive(e)) return r;
  r.found = true;
  r.id = packEntity(e);
  if (const auto* nm = w.get<ed_::Name>(e)) r.name = nm->text;
  r.locked = w.has<ed_::Locked>(e);
  r.hidden = w.has<ed_::Inactive>(e);
  return r;
}

// Monta os dados da Scene do quadro: flags, selecao, particulas (abaixo), grade, cortados e icones (acima).
void buildSceneValues(Editor& ed, const tool::Camera& cam, editor::sui::Box img, SDL_Texture* tex, int tw, int th) {
  scene_panel::Values v;
  v.shown = true;
  v.cam = cam;
  v.texture = tex;
  v.texW = static_cast<float>(tw);
  v.texH = static_cast<float>(th);
  v.playing = ed.playing;
  v.paused = ed.paused;
  v.freeFly = ed.freeFly;
  v.ctrlDown = editor::frameInput().ctrl;
  v.showGrid = ed.showGridScene;
  v.showGizmos = ed.showGizmos;
  v.showCulled = ed.showCulled;
  v.showPostFx = ed.showPostInScene;
  v.gizmoMode = ed.gizmoMode;
  v.localSpace = ed.gizmoLocal;
  v.snapOn = ed.snapOn;
  v.snapStep = ed.snapStep;
  v.sel = sceneSelection(ed);
  v.probe = [&ed, cam, img](vm::Pixel p) { return sceneProbe(ed, cam, img, p); };
  const vm::Projection proj = vm::Projection::make(cam, {img.x, img.y, img.w, img.h});
  vo::buildParticles(ed, cam, img, ed.selected, v.below);
  if (ed.showGridScene) vo::buildGrid(cam, proj, v.grid);
  if (ed.showCulled) vo::buildCulled(ed, proj, img, v.above);
  if (ed.showGizmos) {
    vo::buildSceneIcons(ed, proj, v.above);
    vo::buildPhysics(ed, proj, v.above);
  }
  gSceneValues = std::move(v);
}

// Item do menu de contexto da Scene (botao direito sobre um objeto).
void applySceneContext(Editor& ed, scene_panel::CtxAct act, tool::Entity e) {
  auto& w = ed.scene.world();
  if (!w.alive(e)) return;
  const bool locked = w.has<ed_::Locked>(e), hidden = w.has<ed_::Inactive>(e);
  switch (act) {
    case scene_panel::CtxAct::Inspector: ed.showInspector = true; break;
    case scene_panel::CtxAct::Focus: ed.focusOn(e); break;
    case scene_panel::CtxAct::Duplicate: ed.selected = ed.duplicate(e); break;
    case scene_panel::CtxAct::Delete:
      if (locked) ed.statusLine = T("Object is locked: unlock it (padlock) before deleting.");
      else ed.destroy(e);
      break;
    case scene_panel::CtxAct::SnapToContact:
      if (!locked && !snapToContact(ed, e)) ed.statusLine = T("Nothing to snap to here");
      break;
    case scene_panel::CtxAct::ToggleHidden:
      if (hidden) w.remove<ed_::Inactive>(e); else w.add<ed_::Inactive>(e, ed_::Inactive{});
      break;
    case scene_panel::CtxAct::ToggleLocked:
      if (locked) w.remove<ed_::Locked>(e); else w.add<ed_::Locked>(e, ed_::Locked{});
      break;
  }
}

// Pedidos da Scene: aplicados no Editor com as mesmas regras de antes (botoes, flags, arrasto, menu de contexto).
void applySceneRequests(Editor& ed, const scene_panel::Requests& r) {
  auto& w = ed.scene.world();
  if (r.close) ed.showScene = false;
  if (r.help) editor::openHelp("scene");
  if (r.playToggle) { if (ed.playing) ed.stopPlay(); else ed.startPlay(); }
  if (r.pauseToggle) ed.paused = !ed.paused;
  if (r.stepOnce) ed.stepOnce = true;
  if (r.grid) ed.showGridScene = *r.grid;
  if (r.gizmos) ed.showGizmos = *r.gizmos;
  if (r.culled) ed.showCulled = *r.culled;
  if (r.postFx) ed.showPostInScene = *r.postFx;
  if (r.gizmoMode) ed.gizmoMode = *r.gizmoMode;
  if (r.localSpace) ed.gizmoLocal = *r.localSpace;
  if (r.snapOn) ed.snapOn = *r.snapOn;
  if (r.snapStep) ed.snapStep = *r.snapStep;
  if (r.select && w.alive(unpackEntity(*r.select))) ed.selected = unpackEntity(*r.select);
  if (w.alive(ed.selected)) {
    if (auto* t = w.get<ed_::Transform>(ed.selected)) {
      for (std::size_t i = 0; i < 3; ++i) {
        if (r.pos) t->pos[i] = (*r.pos)[i];
        if (r.rot) t->rot[i] = (*r.rot)[i];
        if (r.scale) t->scale[i] = (*r.scale)[i];
      }
    }
  }
  if (r.snapToContact && !snapToContact(ed, ed.selected)) ed.statusLine = T("Nothing to snap to here");
  if (r.ctx) applySceneContext(ed, r.ctx->first, unpackEntity(r.ctx->second));
}

void applyGameRequests(Editor& ed, const game_panel::Requests& r) {
  if (r.close) ed.showGame = false;
  if (r.help) editor::openHelp("start");
  if (r.playToggle) { if (ed.playing) ed.stopPlay(); else ed.startPlay(); }
  if (r.pauseToggle) ed.paused = !ed.paused;
  if (r.stepOnce) ed.stepOnce = true;
  for (const std::string& a : r.uiActions) ed.uiEvents.push_back(a);
}

// Teclas do menu da Scene: W, E, R trocam o modo do gizmo; Q alterna mundo e local. A regra fica em scene_transform_menu.
void sceneMenuKeys(Editor& ed) {
  namespace stm = editor::scene_transform_menu;
  const std::pair<SDL_Scancode, stm::Action> keys[] = {
      {SDL_SCANCODE_W, stm::Action::Move}, {SDL_SCANCODE_E, stm::Action::Rotate}, {SDL_SCANCODE_R, stm::Action::Scale}, {SDL_SCANCODE_Q, stm::Action::ToggleSpace}};
  const editor::FrameInput& in = editor::frameInput();
  for (const auto& [key, action] : keys) {
    if (in.pressed[key]) stm::applyAction(action, ed.gizmoMode, ed.gizmoLocal);
  }
}

// Teclado e navegacao da Scene. Alt = navegacao livre (WASD); fora dela, W/E/R/Q mudam o menu do gizmo e o botao
// direito/meio movem a camera. Quem consome o clique no gizmo e o painel, entao aqui so vale fora do arrasto.
void sceneKeysAndNavigation(Editor& ed, bool hovered) {
  editor::FrameInput& in = editor::frameInput();
  const bool typing = in.typing;
  const bool focused = hovered;
  if (!typing && focused && in.pressed[SDL_SCANCODE_LALT]) { ed.toggleFreeFly(); ed.flyChanged = true; }
  if (ed.freeFly) {
    if (in.pressed[SDL_SCANCODE_ESCAPE]) { ed.toggleFreeFly(); ed.flyChanged = true; return; }
    ed.flyLook(ed.mouseDX, ed.mouseDY);
    ed.mouseDX = ed.mouseDY = 0.f;
    auto key = [&in](SDL_Scancode k) { return in.down(k) ? 1.f : 0.f; };
    ed.flyMove(in.dt, key(SDL_SCANCODE_W) - key(SDL_SCANCODE_S), key(SDL_SCANCODE_D) - key(SDL_SCANCODE_A), key(SDL_SCANCODE_E) - key(SDL_SCANCODE_Q), in.shift);
    if (in.wheel != 0.f) ed.flySpeed = std::clamp(ed.flySpeed * (1.f + in.wheel * 0.15f), 0.5f, 80.f);
    return;
  }
  if (!typing && focused) sceneMenuKeys(ed);
  // O arrasto da camera so vale se o botao foi pressionado DENTRO desta vista (um clique que vem de outro painel
  // nao gira a camera sozinho).
  static bool navRight = false, navMid = false;
  if (hovered && in.clicked[2]) navRight = true;
  if (hovered && in.clicked[1]) navMid = true;
  if (!in.held[2]) navRight = false;
  if (!in.held[1]) navMid = false;
  if (!hovered || gScenePanel->dragging()) return;
  if (navRight && in.held[2]) {
    ed.yaw -= in.mdx * 0.008f;
    ed.pitch = std::clamp(ed.pitch + in.mdy * 0.008f, -1.5f, 1.5f);
  }
  if (navMid && in.held[1]) {
    const tool::Camera cam = ed.sceneCamera();
    const tool::Vec3 f = tool::normalize(cam.target - cam.eye), r = tool::normalize(tool::cross(f, {0, 1, 0})), u = tool::cross(r, f);
    const float k = ed.dist * 0.0016f;
    ed.target = ed.target - r * (in.mdx * k) + u * (in.mdy * k);
  }
  ed.dist = std::clamp(ed.dist - in.wheel * ed.dist * 0.1f, 1.5f, 200.f);
}

// Textura da Scene: renderiza (ou reusa, com a cena ociosa) no tamanho da imagem.
SDL_Texture* updateSceneTexture(Editor& ed, SDL_Renderer* sdl, const tool::Camera& cam, int w, int h, int& tw, int& th) {
  const int rw = std::max(64, static_cast<int>(static_cast<float>(w) * ed.renderScale));
  const int rh = std::max(36, static_cast<int>(static_cast<float>(h) * ed.renderScale));
  const bool soft = ed.softScaling();   // upscaling/AA por software: a textura tem o tamanho da janela
  tw = soft ? w : rw;
  th = soft ? h : rh;
  const bool reuse = ed.viewsIdle() && ed.sceneTex.tex && ed.sceneTex.w == tw && ed.sceneTex.h == th;
  if (!reuse) {
    const tool::u8* raw = ed.renderView(cam, rw, rh, true, editor::Zone::SceneRender, ed.showPostInScene ? &ed.sceneInfo.render.post : nullptr);
    const tool::u8* pixels = soft ? ed.finishView(ed.sceneUp, raw, rw, rh, w, h) : raw;
    ed.sceneTex.ensure(sdl, tw, th);
    if (ed.sceneTex.tex) SDL_UpdateTexture(ed.sceneTex.tex, nullptr, pixels, tw * 4);
  }
  if (ed.sceneTex.tex) SDL_SetTextureScaleMode(ed.sceneTex.tex, soft ? SDL_SCALEMODE_NEAREST : (ed.pixelFilter ? SDL_SCALEMODE_NEAREST : SDL_SCALEMODE_LINEAR));
  return ed.sceneTex.tex;
}

void drawSceneSui(Editor& ed, SDL_Renderer* sdl) {
  // A janela (moldura, titulo, X, "?") e do painel SpriteUI: aqui so pegamos a area da cena e montamos os Values.
  int winW = 0, winH = 0;
  SDL_GetWindowSize(SDL_GetRenderWindow(sdl), &winW, &winH);
  if (winW <= 0 || winH <= 0) return;
  gSceneFrame = true;
  gSceneRect = gScenePanel->prepare(static_cast<float>(winW), static_cast<float>(winH));
  const editor::sui::Box img = scene_panel::imageArea(gSceneRect);
  const tool::Camera cam = ed.sceneCamera();
  sceneKeysAndNavigation(ed, gScenePanel->hovered());
  int tw = 0, th = 0;
  SDL_Texture* tex = updateSceneTexture(ed, sdl, cam, std::clamp(static_cast<int>(img.w), 64, 1920), std::clamp(static_cast<int>(img.h), 64, 1080), tw, th);
  buildSceneValues(ed, cam, img, tex, tw, th);
}

// Textura de uma camera do jogo: renderiza (ou reusa) e sobe para o SDL_Texture da celula.
SDL_Texture* updateGameTexture(Editor& ed, SDL_Renderer* sdl, std::size_t i, const tool::Camera& cam, const tool::PostFx& fx,
                               int w, int h, int rw, int rh, bool soft, int& tw, int& th) {
  tw = soft ? w : rw;
  th = soft ? h : rh;
  const bool reuse = ed.viewsIdle() && ed.gameTex[i].tex && ed.gameTex[i].w == tw && ed.gameTex[i].h == th;
  if (!reuse) {
    const tool::u8* raw = ed.renderView(cam, rw, rh, false, editor::Zone::GameRender, &fx, i);
    const tool::u8* pixels = soft ? ed.finishView(ed.gameUp[i], raw, rw, rh, w, h) : raw;
    ed.gameTex[i].ensure(sdl, tw, th);
    if (ed.gameTex[i].tex) SDL_UpdateTexture(ed.gameTex[i].tex, nullptr, pixels, tw * 4);
  }
  if (ed.gameTex[i].tex) SDL_SetTextureScaleMode(ed.gameTex[i].tex, soft ? SDL_SCALEMODE_NEAREST : (ed.pixelFilter ? SDL_SCALEMODE_NEAREST : SDL_SCALEMODE_LINEAR));
  return ed.gameTex[i].tex;
}

// Uma celula (uma camera principal): imagem, grade e sobreposicoes. Com resolucao fixa, a imagem ocupa so o
// retangulo de letreiro dentro da celula (sem barras pretas no desenho, o painel faz o Contain).
void buildGameCell(Editor& ed, SDL_Renderer* sdl, std::size_t i, tool::Entity e, const gm::Rect& cell, bool fill, game_panel::Cell& out) {
  auto* t = ed.scene.world().get<ed_::Transform>(e);
  auto* c = ed.scene.world().get<ed_::CameraComp>(e);
  if (!t || !c) return;
  const int w = std::clamp(static_cast<int>(cell.w), 32, 1920), h = std::clamp(static_cast<int>(cell.h), 32, 1080);
  int rw = std::max(32, static_cast<int>(static_cast<float>(w) * ed.renderScale));
  int rh = std::max(18, static_cast<int>(static_cast<float>(h) * ed.renderScale));
  const bool fixedRes = !fill && ed.fixedRes(rw, rh);   // resolucao fixa: a imagem mantem a proporcao (Contain)
  const tool::Camera cam = ed.cameraFor(e);
  const tool::PostFx fx = tool::combinePostFx(ed.sceneInfo.render.post, c->post);
  int tw = 0, th = 0;
  out.texture = updateGameTexture(ed, sdl, i, cam, fx, w, h, rw, rh, ed.softScaling() && !fixedRes, tw, th);
  out.box = cell;
  out.texW = static_cast<float>(fixedRes ? rw : tw);
  out.texH = static_cast<float>(fixedRes ? rh : th);
  out.fit = fixedRes ? ImageFit::Contain : ImageFit::Stretch;
  const gm::Rect img = fixedRes ? gm::letterbox(cell, static_cast<float>(rw), static_cast<float>(rh)) : cell;
  const editor::sui::Box imgBox{img.x, img.y, img.w, img.h};
  vo::buildParticles(ed, cam, imgBox, tool::Entity{}, out.overlay);
  vo::buildUi(ed, e, imgBox, ed.playing, gGamePanel->hoverHit(i), gGamePanel->pressedHit(i), out);
}

void drawGameSui(Editor& ed, SDL_Renderer* sdl, bool player) {
  editor::sui::Box rect{};
  if (player) {   // jogo exportado (--player): janela cheia, sem barra e sem moldura
    int winW = 0, winH = 0;
    SDL_GetWindowSize(SDL_GetRenderWindow(sdl), &winW, &winH);
    if (winW <= 0 || winH <= 0) return;
    rect = {0.f, 0.f, static_cast<float>(winW), static_cast<float>(winH)};
    gGamePanel->setBody(rect);
  } else {   // janela SpriteUI com moldura (dock, arraste e X): o painel devolve a area do corpo
    int winW = 0, winH = 0;
    SDL_GetWindowSize(SDL_GetRenderWindow(sdl), &winW, &winH);
    if (winW <= 0 || winH <= 0) return;
    rect = gGamePanel->prepare(static_cast<float>(winW), static_cast<float>(winH));
  }
  gGameFrame = true;
  gGameRect = rect;
  const editor::sui::Box area = player ? rect
      : editor::sui::Box{rect.x, rect.y + game_panel::kToolbarH, rect.w, std::max(0.f, rect.h - game_panel::kToolbarH)};
  const auto cams = ed.principalCameras();
  game_panel::Values v;
  v.shown = true;
  v.player = player;
  v.playing = ed.playing;
  v.paused = ed.paused;
  if (cams.empty()) {
    v.message = T("No camera has the 'Principal' tag. Create a camera (Hierarchy > + Create > Main Camera) or tick 'Principal' on a camera in the Inspector.");
  } else {
    const std::size_t n = std::min<std::size_t>(2, cams.size());
    const bool vertical = ed.sceneInfo.splitVertical;
    v.cameraLabel = F("Principal cameras: %zu%s", cams.size(), cams.size() >= 2 ? (vertical ? " (vertical split)" : " (horizontal split)") : "");
    const std::vector<gm::Rect> cells = gm::splitCells({area.x, area.y, area.w, area.h}, n, vertical, 2.f);
    const bool fill = n == 2 && ed.sceneInfo.splitFill;   // split "preencher": cada metade usa a propria area
    for (std::size_t i = 0; i < n; ++i) {
      game_panel::Cell out;
      buildGameCell(ed, sdl, i, cams[i], cells[i], fill, out);
      v.cells.push_back(std::move(out));
    }
  }
  gGameValues = std::move(v);
}

// Na build SpriteUI, a janela so existe com o painel (a fonte carregou). Sem fonte, nada e desenhado.
void drawSceneWindow(Editor& ed, SDL_Renderer* sdl) {
  if (gScenePanel) drawSceneSui(ed, sdl);
}
void drawGameWindow(Editor& ed, SDL_Renderer* sdl, bool player) {
  if (gGamePanel) drawGameSui(ed, sdl, player);
}

// Depois do RenderDrawData: os paineis desenham por cima e devolvem os pedidos do quadro. Sem quadro (janela
// fechada), o painel recebe Values vazio e so solta o estado.
void drawViewPanels(Editor& ed, SDL_Renderer* sdl) {
  const scene_panel::Values noScene;
  const game_panel::Values noGame;
  if (gGamePanel) {   // Jogo primeiro: a janela Scene desenha por cima dela (como as janelas ImGui)
    game_panel::Requests req;
    if (!gGamePanel->draw(sdl, gGameFrame ? gGameValues : noGame, req)) tool::logError(std::string("Game SpriteUI render failed: ") + SDL_GetError());
    applyGameRequests(ed, req);
  }
  if (gScenePanel) {
    scene_panel::Requests req;
    if (!gScenePanel->draw(sdl, gSceneFrame ? gSceneValues : noScene, req)) tool::logError(std::string("Scene SpriteUI render failed: ") + SDL_GetError());
    applySceneRequests(ed, req);
  }
}

// Eventos: os paineis veem tudo, sem consumir (o ImGui continua recebendo o mouse e o teclado).
bool forwardViewEvent(const SDL_Event& e, bool sceneShown, bool gameShown) {
  const bool sceneFrame = sceneShown && gScenePanel && gScenePanel->handleEvent(e);
  const bool gameFrame = gameShown && gGamePanel && gGamePanel->handleEvent(e);
  return sceneFrame || gameFrame;
}








// Janela Configuracoes em SpriteUI (settings_panel.hpp): a faixa de abas e a aba Graficos. As outras abas seguem
// no ImGui, abaixo da faixa. Os pedidos do painel sao gravados no Editor aqui (mesmas regras da aba no ImGui).
bool gSettingsSpriteOn = false;   // o painel SpriteUI existe
bool gSettingsFrame = false;      // a janela ImGui esta aberta neste quadro
editor::sui::Box gSettingsRect;   // area da janela (faixa e conteudo), em pixels
editor::panel_router::PanelRouter gPanelRouter;   // quem recebe ponteiro quando paineis SpriteUI se sobrepoem

// Eventos de ponteiro (os que o roteador decide). Teclado nao passa por ele.
inline bool isPointerEvent(const SDL_Event& e) {
  return e.type == SDL_EVENT_MOUSE_BUTTON_DOWN || e.type == SDL_EVENT_MOUSE_BUTTON_UP || e.type == SDL_EVENT_MOUSE_MOTION ||
         e.type == SDL_EVENT_MOUSE_WHEEL;
}
inline float pointerX(const SDL_Event& e) {
  if (e.type == SDL_EVENT_MOUSE_MOTION) return e.motion.x;
  if (e.type == SDL_EVENT_MOUSE_WHEEL) return e.wheel.mouse_x;
  return e.button.x;
}
inline float pointerY(const SDL_Event& e) {
  if (e.type == SDL_EVENT_MOUSE_MOTION) return e.motion.y;
  if (e.type == SDL_EVENT_MOUSE_WHEEL) return e.wheel.mouse_y;
  return e.button.y;
}

// Quais APIs de render estao compiladas (o radio so mostra essas).
std::array<bool, editor::settings_bridge::kApiCount> settingsApiBuilt() {
  std::array<bool, editor::settings_bridge::kApiCount> built{};
  built[0] = true;   // Software (CPU) existe sempre
#ifdef TOOL_EDITOR_GPU
  for (auto b : tool::rhi::compiledBackends()) {
    if (b == tool::rhi::Backend::Vulkan) built[1] = true;
    else if (b == tool::rhi::Backend::GLES) built[2] = true;
    else if (b == tool::rhi::Backend::D3D12) built[3] = true;
    else if (b == tool::rhi::Backend::D3D11) built[4] = true;
  }
#endif
  return built;
}

// ====================================================================================================== LINHAS DAS SECOES
// Secoes Game, Physics, Optimizing, Language, Lighting, Tags e Project da janela Configuracoes em SpriteUI. Cada
// controle do ImGui antigo (drawGameTab, drawLightingTab, drawTagsTab...) virou uma linha de settings_form.hpp: os
// construtores buildXRows leem o Editor, e applyFormRequest (chamado por SettingsEditorTarget::setForm) grava o pedido
// com a mesma regra que o ImGui usava. Inventario (controle: tipo, faixa, efeito). Nenhum controle ficou de fora.
//  Game (drawGameTab):
//    Grid and gizmos: 3 caixas (grade da Scene, grade do Game, gizmos) e nota da grade.
//    Split screen: combo de 2 opcoes (splitVertical); caixa Fill each half (splitFill); duas notas.
//    Skybox: caixa (sky.enabled); com ela ligada, 6 combos de imagem (um por lado; "(none)" = cor solida),
//      cor solida em 3 deslizantes R/G/B (0 a 1, no lugar do seletor de cor) e nota.
//    Audio: nota com o estado da saida de audio.
//  Optimizing (drawSettingsTabBody, aba 4): combo Frame rate limit (fpsMode); deslizante Mesh chunk cache (256 a
//    32768, inteiro); deslizante UI scale (0,70 a 2,00); caixa Idle rendering.
//  Language (drawLanguageTab): radio dos pacotes; notas de traducao e da pasta; botoes Reload packs e Export
//    template; nota de status.
//  Physics (drawPhysicsTab, extra_ui.hpp): caixa Physics enabled; gravidade X/Y/Z (-1000 a 1000, no lugar do
//    DragFloat3); 7 botoes de gravidade pronta; Solver iterations (1 a 30); Steps per second (15 a 240); caixa Draw
//    colliders; Simulation radius (0 a 1000); notas; no Play, o bloco Live com 2 notas de estatisticas.
//  Lighting (drawLightingTab): Frustum culling, Occlusion culling e Draw distance (com as dicas); Sun: cor (3
//    deslizantes), Intensity, Direction, Height, Ambient (3), Sun casts shadows; Light Prism: Quality preset,
//    Custom values e, com custom, Shadow cascades, Shadow map resolution, Edge softness, Max lights per cluster,
//    Light indices per cluster, Auto radius cutoff (o resto fica apagado sem custom); Light budget (2); Shadows:
//    Enable shadows e 7 controles dele; Point and spot shadow lights e resolucao; SSAO (caixa e 4 deslizantes);
//    Ray tracing (caixa, dica), RT AO (caixa, dica, rays, raio); Bloom (caixa, dica, forca); Ambient intensity;
//    Pre-exposure; Global post-processing: Enable, Exposure, Contrast, Saturation, Tint (3), Tint strength,
//    Vignette, Vignette size (so com vinheta), Sharpen e Reset effect.
//  Tags (drawTagsTab): lista de tags internas (notas); ligacoes (uma nota e um botao Remove link cada); campo da tag,
//    combo do alvo e botao Link; tags do projeto com contagem, botao Rename (campo New name e cancel, so ao renomear);
//    mensagem de erro; nota final.
//  Project (drawProjectTab): campo Editor command (texto, com dica); notas de exemplos e de exportacao.
// Nao portado como controle: o arrastar de asset do painel Assets para o combo da skybox (o combo ja escolhe o arquivo).
// Cores: o seletor (ColorEdit) virou 3 deslizantes; o DragFloat virou deslizante com a mesma faixa.

namespace formrow {
using editor::settings_form::Row;
using editor::settings_form::RowKind;
using editor::settings_form::Rows;

// Subtitulo (SeparatorText).
inline Row Head(std::string text) { Row r; r.kind = RowKind::Heading; r.label = std::move(text); return r; }
// Texto normal (TextWrapped).
inline Row Note(std::string text) { Row r; r.kind = RowKind::Note; r.label = std::move(text); return r; }
// Texto apagado (TextDisabled): fica em `note`, em tom menor e apagado.
inline Row Dim(std::string text) { Row r; r.kind = RowKind::Note; r.note = std::move(text); return r; }
// Deslizante: `note` vazio mostra o valor; com texto, mostra esse texto (ex.: "30 deg").
inline Row Slide(int id, std::string label, float value, float lo, float hi, bool asInt = false, std::string note = {}, bool enabled = true) {
  Row r; r.kind = RowKind::Slider; r.id = id; r.label = std::move(label); r.value = value; r.lo = lo; r.hi = hi;
  r.isInt = asInt; r.note = std::move(note); r.enabled = enabled; return r;
}
inline Row Check(int id, std::string label, bool on, bool enabled = true) {
  Row r; r.kind = RowKind::Check; r.id = id; r.label = std::move(label); r.on = on; r.enabled = enabled; return r;
}
inline Row Drop(int id, std::string label, std::vector<std::string> options, int index, bool enabled = true) {
  Row r; r.kind = RowKind::Dropdown; r.id = id; r.label = std::move(label); r.options = std::move(options); r.index = index;
  r.enabled = enabled; return r;
}
inline Row Radio(int id, std::string label, std::vector<std::string> options, int index) {
  Row r; r.kind = RowKind::Radio; r.id = id; r.label = std::move(label); r.options = std::move(options); r.index = index; return r;
}
inline Row Btn(int id, std::string label, bool enabled = true) {
  Row r; r.kind = RowKind::Button; r.id = id; r.label = std::move(label); r.enabled = enabled; return r;
}
inline Row Txt(int id, std::string label, std::string text) {
  Row r; r.kind = RowKind::Text; r.id = id; r.label = std::move(label); r.text = std::move(text); return r;
}
// "Rotulo (R)" para cada canal de uma cor.
inline std::string channel(const char* key, const char* c) { return std::string(T(key)) + " (" + c + ")"; }
} // namespace formrow

// Ids das linhas genericas (settings_form.hpp). Cada campo editavel e cada botao tem um id; as faixas de cada secao
// ficam juntas. Os ids de Tags com varios itens (Remove link, Rename) somam o indice da ligacao ou da tag.
enum FormId : int {
    // Game (100)
    kGameGridScene = 100, kGameGridGame, kGameGizmos, kGameSplit, kGameSplitFill, kGameSkyEnabled,
    kGameSkyFace0 = 110,                       // 110 a 115: um combo por lado do cubo
    kGameSkyRed = 120, kGameSkyGreen, kGameSkyBlue,
    // Physics (200)
    kPhysEnabled = 200, kPhysGravX, kPhysGravY, kPhysGravZ,
    kPhysPreset0 = 210,                        // 210 a 216: gravidades prontas
    kPhysIterations = 220, kPhysStepHz, kPhysColliders, kPhysSimRadius,
    // Optimizing (300)
    kOptFps = 300, kOptChunkCache, kOptUiScale, kOptIdle, kOptSimplify, kOptSimplifyDist, kOptSimplifyAmount,
    // Language (400)
    kLangPack = 400, kLangReload, kLangExport,
    // Tags (500)
    kTagTarget = 500, kTagAlias, kTagLink, kTagRenameText, kTagRenameCancel,
    kTagRemoveBase = 1000,                     // 1000 + indice da ligacao: Remove link
    kTagRenameBase = 2000,                     // 2000 + indice da tag: Rename
    // Project (600)
    kProjEditor = 600,
    // Lighting (700)
    kLitFrustum = 700, kLitOcclusion, kLitDrawDistance,
    kLitSunR, kLitSunG, kLitSunB, kLitSunIntensity, kLitSunAzimuth, kLitSunElevation,
    kLitAmbR, kLitAmbG, kLitAmbB, kLitSunShadow,
    kLitTier = 720, kLitCustom, kLitCascades, kLitShadowRes, kLitPcf, kLitMaxLightsCluster, kLitIndicesCluster, kLitAutoRadius,
    kLitMaxPoint = 730, kLitMaxSpot,
    kLitShadows = 740, kLitShadowDistance, kLitSplitLambda, kLitBlend, kLitCasterExt, kLitDepthBias, kLitNormalBias, kLitSnap,
    kLitPointShadowLights = 750, kLitPointShadowRes,
    kLitSsao = 760, kLitSsaoKernel, kLitSsaoRadius, kLitSsaoBias, kLitSsaoPower,
    kLitRayTracing = 770, kLitRtAo, kLitRtAoRays, kLitRtAoRadius,
    kLitBloom = 780, kLitBloomStrength,
    kLitAmbient = 790, kLitPreExposure,
    kLitPostEnabled = 800, kLitPostExposure, kLitPostContrast, kLitPostSaturation, kLitPostTintR, kLitPostTintG,
    kLitPostTintB, kLitPostTintStrength, kLitPostVignette, kLitPostVignetteSize, kLitPostSharpen, kLitPostReset,
};

// Estado que o ImGui guardava em variaveis static na aba Tags (alias digitado, alvo, renomeacao, mensagem).
struct TagsFormState {
    std::string msg;
    std::string alias;
    int target = 1;              // indice em ed_::kBuiltinTags
    int renaming = -1;           // indice da tag em renomeacao, ou -1
    std::string renameBuf;
};
TagsFormState gTagsForm;

// Caminho do Sol (Global Light) desta cena, ou nullptr. Mesma busca do ImGui: a primeira entidade com o componente.
ed_::GlobalLight* settingsSunLight(Editor& ed) {
    tool::Entity sun;
    ed.scene.world().each<ed_::GlobalLight>([&](tool::Entity e, ed_::GlobalLight&) { if (!ed.scene.world().alive(sun)) sun = e; });
    return ed.scene.world().alive(sun) ? ed.scene.world().get<ed_::GlobalLight>(sun) : nullptr;
}

// Opcoes do combo de imagem de um lado da skybox: "(none)" e os arquivos de imagem de Assets. Se o arquivo atual nao
// estiver na pasta, entra no fim para o combo continuar mostrando o que a cena usa.
std::vector<std::string> skyFaceOptions(Editor& ed, int face) {
    std::vector<std::string> opts{std::string(T("(none)"))};
    const std::string cur = ed.sceneInfo.sky.face[face];
    const std::vector<std::string> list = assetsOfKind(ed, ed_::AssetKind::Image);
    opts.insert(opts.end(), list.begin(), list.end());
    if (!cur.empty() && std::find(list.begin(), list.end(), cur) == list.end()) opts.push_back(cur);
    return opts;
}

// Linhas da secao Game (drawGameTab). O ImGui marcava settingsDirty em quase tudo.
void addGameRows(Editor& ed, editor::settings_form::Rows& rows) {
    using namespace formrow;
    rows.push_back(Head(T("Grid and gizmos")));
    rows.push_back(Check(kGameGridScene, T("Show grid in the Scene window"), ed.showGridScene));
    rows.push_back(Check(kGameGridGame, T("Show grid in the Game window"), ed.showGridGame));
    rows.push_back(Check(kGameGizmos, T("Show gizmos (cameras, lights, sounds)"), ed.showGizmos));
    rows.push_back(Dim(T("The grid is an editor aid: it only appears in the Game window if you turn it on here.")));
    rows.push_back(Head(T("Split screen")));
    rows.push_back(Drop(kGameSplit, T("Split screen"),
                        {std::string(T("Vertical split (side by side)")), std::string(T("Horizontal split (top / bottom)"))},
                        ::editor::settings_model::SplitIndex(ed.sceneInfo.splitVertical)));
    rows.push_back(Dim(T("Used when two cameras have the 'Principal' tag. Saved with the scene.")));
    rows.push_back(Check(kGameSplitFill, T("Fill each half (no black bars)"), ed.sceneInfo.splitFill));
    rows.push_back(Dim(T("Each camera renders into its whole half, so a 16:9 resolution no longer leaves black bars. The camera keeps its vertical field of view; the sides are cropped.")));
    rows.push_back(Dim(T("Off: the whole camera image fits the half, with black bars when the proportions differ.")));
    rows.push_back(Head(T("Skybox")));
    rows.push_back(Check(kGameSkyEnabled, T("Skybox (6 images)"), ed.sceneInfo.sky.enabled));
    if (ed.sceneInfo.sky.enabled) {
        for (int i = 0; i < 6; ++i) {   // um combo por lado do cubo; "(none)" = cor solida
            const auto opts = skyFaceOptions(ed, i);
            const std::string cur = ed.sceneInfo.sky.face[i];
            int idx = 0;
            for (std::size_t k = 1; k < opts.size(); ++k) if (opts[k] == cur) { idx = static_cast<int>(k); break; }
            rows.push_back(Drop(kGameSkyFace0 + i, T(editor::kSkyFaceNames[static_cast<size_t>(i)]), opts, idx));
        }
        rows.push_back(Slide(kGameSkyRed, channel("Solid color (sides without image)", "R"), ed.sceneInfo.sky.solid[0], 0.f, 1.f));
        rows.push_back(Slide(kGameSkyGreen, channel("Solid color (sides without image)", "G"), ed.sceneInfo.sky.solid[1], 0.f, 1.f));
        rows.push_back(Slide(kGameSkyBlue, channel("Solid color (sides without image)", "B"), ed.sceneInfo.sky.solid[2], 0.f, 1.f));
        rows.push_back(Dim(T("Each side shows its image in the direction you look at. Used by the Scene and Game views; 2D cameras have no skybox.")));
    }
    rows.push_back(Head(T("Audio")));
    rows.push_back(Note(F("Audio output: %s", ed.audioTried ? (ed.audioOk ? "ready" : "not available") : "starts on Play")));
}

// Linhas da secao Optimizing (aba 4 do drawSettingsTabBody).
void addOptimizingRows(Editor& ed, editor::settings_form::Rows& rows) {
    using namespace formrow;
    rows.push_back(Head(T("Frame and cache")));
    rows.push_back(Drop(kOptFps, T("Frame rate limit"), {"VSync", "30 FPS", "60 FPS", "120 FPS", "Unlimited"}, ed.fpsMode));
    rows.push_back(Slide(kOptChunkCache, T("Mesh chunk cache"), static_cast<float>(ed.chunkCacheMax),
                         static_cast<float>(::editor::settings_model::kChunkCacheMin), static_cast<float>(::editor::settings_model::kChunkCacheMax), true));
    rows.push_back(Head(T("Interface")));
    rows.push_back(Slide(kOptUiScale, T("UI scale"), ed.theme.scale, ::editor::settings_model::kInterfaceScaleMin, ::editor::settings_model::kInterfaceScaleMax));
    rows.push_back(Check(kOptIdle, T("Idle rendering"), ed.idleRendering));
    rows.push_back(Head(T("Mesh simplification")));
    rows.push_back(Check(kOptSimplify, T("Simplify distant meshes"), ed.simplifyFar));
    rows.push_back(Slide(kOptSimplifyDist, T("Simplify beyond (m)"), ed.simplifyDistance,
                         ::editor::settings_model::kSimplifyDistanceMin, ::editor::settings_model::kSimplifyDistanceMax));
    rows.push_back(Slide(kOptSimplifyAmount, T("Faces removed"), ed.simplifyAmount, 0.f, ::editor::settings_model::kSimplifyAmountMax));
}

// Linhas da secao Language (drawLanguageTab).
void addLanguageRows(Editor& ed, editor::settings_form::Rows& rows) {
    using namespace formrow;
    auto& i18n = editor::I18n::get();
    rows.push_back(Head(T("Language packs")));
    std::vector<std::string> names;
    int current = -1;
    for (std::size_t k = 0; k < i18n.packs().size(); ++k) {
        const auto& p = i18n.packs()[k];
        names.push_back(p.name + "  [" + p.code + "]");
        if (i18n.code() == p.code) current = static_cast<int>(k);
    }
    rows.push_back(Radio(kLangPack, T("Language"), names, current));
    if (i18n.code() != "en") rows.push_back(Dim(F("%zu of %zu phrases seen so far are translated in this language.", i18n.translatedCount(), i18n.phraseCount())));
    else rows.push_back(Dim(T("Base language (English): nothing to translate.")));
    rows.push_back(Head(T("Create or edit a pack")));
    rows.push_back(Note(F("Packs are JSON files in the '%s' folder. Untranslated phrases appear in English.", i18n.dir().c_str())));
    rows.push_back(Btn(kLangReload, T("Reload packs")));
    rows.push_back(Btn(kLangExport, T("Export template")));
    if (!ed.langStatus.empty()) rows.push_back(Dim(ed.langStatus));
}

// Linhas da secao Physics (drawPhysicsTab, extra_ui.hpp). Como o ImGui, nao marca nada como sujo.
void addPhysicsRows(Editor& ed, editor::settings_form::Rows& rows) {
    using namespace formrow;
    const auto& ps = ed.sceneInfo.physics;
    rows.push_back(Note(T("World physics of this scene (saved with the scene). Objects with 'Vector physics' ignore the world gravity.")));
    rows.push_back(Check(kPhysEnabled, T("Physics enabled"), ps.enabled));
    rows.push_back(Head(T("World gravity (m/s2) - any axis")));
    rows.push_back(Slide(kPhysGravX, T("Gravity X"), ps.gravity[0], -1000.f, 1000.f));
    rows.push_back(Slide(kPhysGravY, T("Gravity Y"), ps.gravity[1], -1000.f, 1000.f));
    rows.push_back(Slide(kPhysGravZ, T("Gravity Z"), ps.gravity[2], -1000.f, 1000.f));
    static const char* kPresets[7] = {"Earth", "Moon", "Mars", "Zero-G", "Sideways (+X)", "Toward -Z", "Upside down"};
    for (int i = 0; i < 7; ++i) rows.push_back(Btn(kPhysPreset0 + i, T(kPresets[i])));
    rows.push_back(Head(T("Simulation")));
    rows.push_back(Slide(kPhysIterations, T("Solver iterations"), static_cast<float>(ps.solverIterations), 1.f, 30.f, true));
    rows.push_back(Slide(kPhysStepHz, T("Steps per second"), static_cast<float>(ps.stepHz), 15.f, 240.f, true));
    rows.push_back(Dim(T("More iterations stack better; more steps per second is more precise. Both cost CPU.")));
    rows.push_back(Check(kPhysColliders, T("Draw colliders in the Scene view"), ps.drawColliders));
    rows.push_back(Slide(kPhysSimRadius, T("Simulation radius (0 = everything)"), ps.simRadius, 0.f, 1000.f));
    rows.push_back(Dim(T("Bodies outside the radius around the camera stop moving until the camera gets closer. Saves CPU in large scenes.")));
    if (ed.playing && ed.play.built) {
        const auto& s = ed.play.world.stats;
        rows.push_back(Head(T("Live")));
        rows.push_back(Note(F("bodies %d   awake %d   pairs %d   contacts %d   steps/frame %d", s.bodies, s.awake, s.pairs, s.contacts, s.steps)));
        rows.push_back(Note(F("%s: %d   %s: %d", T("Bodies simulated"), static_cast<int>(s.bodies - ed.play.culledCount),
                              T("Bodies outside the simulation radius"), static_cast<int>(ed.play.culledCount))));
    }
}

// Linhas da secao Lighting (drawLightingTab). A ordem e a mesma do ImGui; o que ficava dentro de BeginDisabled vira
// enabled = false (mostrado apagado, sem interacao).
void addLightingRows(Editor& ed, editor::settings_form::Rows& rows) {
    using namespace formrow;
    auto& r = ed.sceneInfo.render;
    auto& L = r.lighting;
    auto u32f = [](tool::u32 v) { return static_cast<float>(v); };
    rows.push_back(Dim(F("These settings belong to the scene '%s' and are saved with it.", ed.sceneInfo.name.c_str())));
    rows.push_back(Check(kLitFrustum, T("Frustum culling (skip objects outside the camera view)"), r.frustumCull));
    rows.push_back(Check(kLitOcclusion, T("Occlusion culling (skip objects hidden behind big objects)"), r.occlusionCull));
    rows.push_back(Dim(T("Objects completely behind a large object from the camera are not drawn. Works with perspective cameras. Objects that cast shadows are always drawn.")));
    rows.push_back(Slide(kLitDrawDistance, T("Draw distance (0 = each camera's Far)"), r.drawDistance, 0.f, 20000.f));
    rows.push_back(Dim(T("Objects farther than this are not drawn in the Scene and Game views. 0 = use the Far value of each camera. Very large values make distant objects flicker (depth precision).")));

    // Sol (Global Light): so existe se a cena tiver um GlobalLight.
    rows.push_back(Head(T("Sun (Global Light)")));
    if (const ed_::GlobalLight* g = settingsSunLight(ed)) {
        rows.push_back(Slide(kLitSunR, channel("Light color", "R"), g->color[0], 0.f, 1.f));
        rows.push_back(Slide(kLitSunG, channel("Light color", "G"), g->color[1], 0.f, 1.f));
        rows.push_back(Slide(kLitSunB, channel("Light color", "B"), g->color[2], 0.f, 1.f));
        rows.push_back(Slide(kLitSunIntensity, T("Intensity"), g->intensity, 0.f, 20.f));
        rows.push_back(Slide(kLitSunAzimuth, T("Direction (azimuth)"), g->azimuth, 0.f, 360.f, false, F("%.0f deg", g->azimuth)));
        rows.push_back(Slide(kLitSunElevation, T("Height (elevation)"), g->elevation, -10.f, 90.f, false, F("%.0f deg", g->elevation)));
        rows.push_back(Slide(kLitAmbR, channel("Ambient light", "R"), g->ambient[0], 0.f, 1.f));
        rows.push_back(Slide(kLitAmbG, channel("Ambient light", "G"), g->ambient[1], 0.f, 1.f));
        rows.push_back(Slide(kLitAmbB, channel("Ambient light", "B"), g->ambient[2], 0.f, 1.f));
        rows.push_back(Check(kLitSunShadow, T("Sun casts shadows"), g->castShadow));
    } else rows.push_back(Dim(T("No Global Light in this scene (Hierarchy > + Create > Global Light).")));

    // Light Prism: qualidade (preset ou custom).
    rows.push_back(Head(T("Light Prism: quality")));
    rows.push_back(Dim(T("The lighting of the engine (lights, clusters, shadows and ambient) is computed by the Light Prism library, on the CPU and on the GPU.")));
    rows.push_back(Drop(kLitTier, T("Quality preset"), {std::string(T("Mobile")), std::string(T("Low")), std::string(T("Medium")), std::string(T("High"))}, L.tier));
    rows.push_back(Check(kLitCustom, T("Custom values (override the preset)"), L.custom));
    if (!L.custom) {
        const auto q = tool::makeQuality(L);
        rows.push_back(Dim(F("Preset: %u shadow cascades, %u px per cascade, %u PCF taps, %u lights per cluster",
                             static_cast<unsigned>(q.csmCascades), static_cast<unsigned>(q.shadowMapResolution),
                             static_cast<unsigned>(q.pcfTaps), static_cast<unsigned>(q.maxLightsPerCluster))));
    }
    rows.push_back(Slide(kLitCascades, T("Shadow cascades"), u32f(L.csmCascades), 1.f, 4.f, true, {}, L.custom));
    {
        const int qi = L.shadowMapResolution <= 512 ? 0 : L.shadowMapResolution <= 1024 ? 1 : L.shadowMapResolution <= 2048 ? 2 : 3;
        rows.push_back(Drop(kLitShadowRes, T("Shadow map resolution (per cascade)"),
                            {std::string(T("Low (512)")), std::string(T("Medium (1024)")), std::string(T("High (2048)")), std::string(T("Ultra (4096)"))},
                            qi, L.custom));
    }
    {
        const int pi = L.pcfTaps <= 1 ? 0 : (L.pcfTaps <= 4 ? 1 : 2);
        rows.push_back(Drop(kLitPcf, T("Edge softness (PCF)"),
                            {std::string(T("Hard edges (1 tap)")), std::string(T("Soft 2x2 (4 taps)")), std::string(T("Softer 3x3 (9 taps)"))},
                            pi, L.custom));
    }
    rows.push_back(Slide(kLitMaxLightsCluster, T("Max lights per cluster"), u32f(L.maxLightsPerCluster), 1.f, 256.f, true, {}, L.custom));
    rows.push_back(Slide(kLitIndicesCluster, T("Light indices per cluster"), u32f(L.indicesPerCluster), 1.f, 64.f, true, {}, L.custom));
    rows.push_back(Slide(kLitAutoRadius, T("Auto radius cutoff"), L.autoRadiusCutoff, 0.f, 0.2f, false, {}, L.custom));

    // Orcamento de luzes (sempre editavel).
    rows.push_back(Head(T("Light budget")));
    rows.push_back(Slide(kLitMaxPoint, T("Max point lights"), u32f(L.maxPointLights), 1.f, 16384.f, true));
    rows.push_back(Slide(kLitMaxSpot, T("Max spot lights"), u32f(L.maxSpotLights), 1.f, 4096.f, true));
    rows.push_back(Dim(T("Lights beyond the budget are dropped (the scene reports how many in Profiler > Rendering).")));

    // Sombras do sol (cascatas).
    rows.push_back(Head(T("Shadows (sun cascades)")));
    rows.push_back(Check(kLitShadows, T("Enable shadows"), L.shadows));
    rows.push_back(Slide(kLitShadowDistance, T("Max shadow distance (m, 0 = camera far)"), L.shadowDistance, 0.f, 500.f, false, {}, L.shadows));
    rows.push_back(Slide(kLitSplitLambda, T("Cascade split (0 uniform - 1 logarithmic)"), L.splitLambda, 0.f, 1.f, false, {}, L.shadows));
    rows.push_back(Slide(kLitBlend, T("Cascade blend"), L.blendFraction, 0.f, 0.5f, false, {}, L.shadows));
    rows.push_back(Slide(kLitCasterExt, T("Caster extension (m)"), L.casterExtension, 0.f, 200.f, false, {}, L.shadows));
    rows.push_back(Slide(kLitDepthBias, T("Depth bias (m)"), L.depthBias, 0.f, 0.5f, false, {}, L.shadows));
    rows.push_back(Slide(kLitNormalBias, T("Normal bias (texels)"), L.normalBias, 0.f, 4.f, false, {}, L.shadows));
    rows.push_back(Check(kLitSnap, T("Snap to texels (stable shadows, no shimmer)"), L.snapToTexels, L.shadows));
    // Sombras de luzes pontuais/spot: feitas pela engine (a Light Prism so tem CSM do sol).
    rows.push_back(Slide(kLitPointShadowLights, T("Point and spot shadow lights"), u32f(L.pointShadowLights), 0.f, 4.f, true));
    {
        const int pi = L.pointShadowResolution <= 128 ? 0 : L.pointShadowResolution <= 256 ? 1 : L.pointShadowResolution <= 512 ? 2 : 3;
        rows.push_back(Drop(kLitPointShadowRes, T("Point/spot shadow resolution (per face)"),
                            {"128", "256", "512", "1024"}, pi));
    }
    rows.push_back(Dim(T("Point and spot shadows: CPU and GPU (Vulkan, D3D12). OpenGL uses the CPU renderer.")));
    // SSAO (Light Prism Post/Ssao).
    rows.push_back(Check(kLitSsao, T("Ambient occlusion (SSAO)"), L.ssao));
    rows.push_back(Slide(kLitSsaoKernel, T("AO kernel samples"), u32f(L.ssaoKernel), 1.f, 32.f, true, {}, L.ssao));
    rows.push_back(Slide(kLitSsaoRadius, T("AO radius (m)"), L.ssaoRadius, 0.05f, 3.f, false, {}, L.ssao));
    rows.push_back(Slide(kLitSsaoBias, T("AO bias (m)"), L.ssaoBias, 0.f, 0.2f, false, {}, L.ssao));
    rows.push_back(Slide(kLitSsaoPower, T("AO power (contrast)"), L.ssaoPower, 0.1f, 4.f, false, {}, L.ssao));
    // Ray tracing (CPU).
    rows.push_back(Check(kLitRayTracing, T("Enable ray tracing"), L.rayTracing));
    rows.push_back(Dim(T("Master switch for CPU ray tracing. Disabling it skips BVH construction and ray-traced effects.")));
    rows.push_back(Check(kLitRtAo, T("Ray-traced ambient occlusion (CPU)"), L.rtAo, L.rayTracing));
    rows.push_back(Dim(T("Casts real rays from each visible surface to find occluders. Replaces the SSAO above. Software renderer only (the GPU keeps the SSAO); sun shadows and voxels are not ray traced yet.")));
    rows.push_back(Slide(kLitRtAoRays, T("RT AO rays per pixel"), u32f(L.rtAoRays), 1.f, 64.f, true, {}, L.rayTracing && L.rtAo));
    rows.push_back(Slide(kLitRtAoRadius, T("RT AO radius (m)"), L.rtAoRadius, 0.05f, 5.f, false, {}, L.rayTracing && L.rtAo));
    // Bloom.
    rows.push_back(Check(kLitBloom, T("Bloom (glow of emissive objects)"), L.bloom));
    rows.push_back(Dim(T("Only objects with Enable emission glow (Inspector > Emission). Other bright surfaces do not bloom. GPU and software renderer.")));
    rows.push_back(Slide(kLitBloomStrength, T("Bloom strength"), L.bloomStrength, 0.f, 4.f, false, {}, L.bloom));
    // Ambiente e exposicao.
    rows.push_back(Head(T("Ambient and exposure")));
    rows.push_back(Slide(kLitAmbient, T("Ambient intensity"), L.ambientIntensity, 0.f, 4.f));
    rows.push_back(Slide(kLitPreExposure, T("Pre-exposure"), L.preExposure, 0.1f, 4.f));
    rows.push_back(Dim(T("Each object chooses Cast shadow / Receive shadow in the Inspector. The sun and the nearest point/spot lights that have Cast shadow on cast shadows. Lower quality = more FPS.")));
    // Pos-processamento global (postFxUi): so mostra os controles com o efeito ligado.
    rows.push_back(Head(T("Global post-processing")));
    const tool::PostFx& p = r.post;
    rows.push_back(Check(kLitPostEnabled, T("Enable post-processing"), p.enabled));
    if (p.enabled) {
        rows.push_back(Slide(kLitPostExposure, T("Exposure (EV)"), p.exposure, -3.f, 3.f));
        rows.push_back(Slide(kLitPostContrast, T("Contrast"), p.contrast, 0.5f, 1.8f));
        rows.push_back(Slide(kLitPostSaturation, T("Saturation"), p.saturation, 0.f, 2.f));
        rows.push_back(Slide(kLitPostTintR, channel("Tint color", "R"), p.tint[0], 0.f, 1.f));
        rows.push_back(Slide(kLitPostTintG, channel("Tint color", "G"), p.tint[1], 0.f, 1.f));
        rows.push_back(Slide(kLitPostTintB, channel("Tint color", "B"), p.tint[2], 0.f, 1.f));
        rows.push_back(Slide(kLitPostTintStrength, T("Tint strength"), p.tintStrength, 0.f, 1.f));
        rows.push_back(Slide(kLitPostVignette, T("Vignette"), p.vignette, 0.f, 1.f));
        if (p.vignette > 0.f) rows.push_back(Slide(kLitPostVignetteSize, T("Vignette size"), p.vignetteSize, 0.2f, 1.4f));
        rows.push_back(Slide(kLitPostSharpen, T("Sharpen"), p.sharpen, 0.f, 1.f));
        rows.push_back(Btn(kLitPostReset, T("Reset effect")));
    }
    rows.push_back(Dim(T("Applies to every camera. A camera can add to it or replace it (Inspector > Camera).")));
}

// Linhas da secao Tags (drawTagsTab). Tags internas, ligacoes, criar ligacao, tags do projeto e renomeacao.
void addTagsRows(Editor& ed, editor::settings_form::Rows& rows) {
    using namespace formrow;
    rows.push_back(Head(T("Built-in tags (used by the engine)")));
    for (auto& b : ed_::kBuiltinTags) rows.push_back(Note(std::string(b.name) + "  -  " + T(b.meaning)));
    rows.push_back(Dim(T("Built-in tags cannot be renamed. To use your own name, link it below.")));
    rows.push_back(Head(T("Links (your tag -> built-in tag)")));
    for (std::size_t i = 0; i < ed.projSettings.links.size(); ++i) {
        const auto& l = ed.projSettings.links[i];
        rows.push_back(Note("\"" + l.alias + "\"  ->  \"" + l.target + "\""));
        rows.push_back(Btn(kTagRemoveBase + static_cast<int>(i), T("Remove link")));
    }
    rows.push_back(Txt(kTagAlias, T("your tag (e.g. jogador)"), gTagsForm.alias));
    {
        std::vector<std::string> names;
        for (auto& b : ed_::kBuiltinTags) names.push_back(b.name);
        rows.push_back(Drop(kTagTarget, T("Link to"), names, gTagsForm.target));
    }
    rows.push_back(Btn(kTagLink, T("Link")));
    rows.push_back(Head(T("All tags in this project")));
    for (tool::TagId i = 0; i < ed.scene.tags().size(); ++i) {
        const std::string name(ed.scene.tags().name(i));
        const bool builtin = ed_::isBuiltinTag(name);
        size_t users = 0;
        ed.scene.world().eachEntity([&](tool::Entity e) { if (ed.scene.world().tagMask(e).test(i)) ++users; });
        Row line = Note(name + (builtin ? "  [built-in]" : ""));
        line.note = std::to_string(users) + (users == 1 ? " object" : " objects");
        rows.push_back(line);
        if (gTagsForm.renaming == static_cast<int>(i)) {
            rows.push_back(Txt(kTagRenameText, T("New name"), gTagsForm.renameBuf));
            rows.push_back(Btn(kTagRenameCancel, T("cancel")));
        } else if (!builtin) {
            rows.push_back(Btn(kTagRenameBase + static_cast<int>(i), T("Rename")));
        }
    }
    if (!gTagsForm.msg.empty()) rows.push_back(Note(gTagsForm.msg));
    rows.push_back(Dim(T("Renaming a tag changes it in every object and in the other scene files.")));
}

// Linhas da secao Project (drawProjectTab).
void addProjectRows(Editor& ed, editor::settings_form::Rows& rows) {
    using namespace formrow;
    rows.push_back(Head(T("External code editor")));
    rows.push_back(Txt(kProjEditor, T("Editor command"), ed.projSettings.externalEditor));
    rows.push_back(Dim(T("empty = system default (Notepad on Windows)")));
    rows.push_back(Dim(T("Examples: code   |   notepad++.exe   |   C:\\Program Files\\Notepad++\\notepad++.exe. Run directly, never through a shell.")));
    rows.push_back(Head(T("Export")));
    rows.push_back(Dim(T("Fill in File > Editor info, then use File > Export game.")));
}

// Aplica um pedido de linha (settings_form.hpp) no Editor: grava o que o ImGui gravava no mesmo controle.
// Slider usa value; Check usa on; Dropdown e Radio usam index; Text usa text; Button ignora os outros campos.
void applyFormRequest(Editor& ed, int id, float value, bool on, int index, const std::string& text) {
    auto& r = ed.sceneInfo.render;
    auto& L = r.lighting;
    auto u32v = [](float v) { return static_cast<tool::u32>(v < 0.f ? 0.f : v + 0.5f); };
    switch (id) {
        // Game
        case kGameGridScene: ed.showGridScene = on; ed.settingsDirty = true; break;
        case kGameGridGame: ed.showGridGame = on; ed.settingsDirty = true; break;
        case kGameGizmos: ed.showGizmos = on; ed.settingsDirty = true; break;
        case kGameSplit:
            ed.sceneInfo.splitVertical = ::editor::settings_model::SplitIsVertical(index);
            ed.settingsDirty = true;
            break;
        case kGameSplitFill: ed.sceneInfo.splitFill = on; ed.settingsDirty = true; break;
        case kGameSkyEnabled: ed.sceneInfo.sky.enabled = on; ed.settingsDirty = true; break;
        case kGameSkyRed: ed.sceneInfo.sky.solid[0] = value; ed.settingsDirty = true; break;
        case kGameSkyGreen: ed.sceneInfo.sky.solid[1] = value; ed.settingsDirty = true; break;
        case kGameSkyBlue: ed.sceneInfo.sky.solid[2] = value; ed.settingsDirty = true; break;
        // Physics (sem marca de sujo, como o ImGui)
        case kPhysEnabled: ed.sceneInfo.physics.enabled = on; break;
        case kPhysGravX: ed.sceneInfo.physics.gravity[0] = value; break;
        case kPhysGravY: ed.sceneInfo.physics.gravity[1] = value; break;
        case kPhysGravZ: ed.sceneInfo.physics.gravity[2] = value; break;
        case kPhysIterations: ed.sceneInfo.physics.solverIterations = static_cast<int>(value + 0.5f); break;
        case kPhysStepHz: ed.sceneInfo.physics.stepHz = static_cast<int>(value + 0.5f); break;
        case kPhysColliders: ed.sceneInfo.physics.drawColliders = on; break;
        case kPhysSimRadius: ed.sceneInfo.physics.simRadius = value; break;
        // Optimizing
        case kOptFps: ed.fpsMode = index; ed.fpsDirty = true; ed.settingsDirty = true; break;
        case kOptChunkCache:
            ed.chunkCacheMax = static_cast<size_t>(::editor::settings_model::ClampChunkCacheSize(static_cast<int>(value + 0.5f)));
            ed.settingsDirty = true;
            break;
        case kOptUiScale:
            ed.theme.scale = ::editor::settings_model::ClampInterfaceScale(value);
            editor::ui_style::interfaceFactor() = ed.theme.scale / editor::Theme::kDefaultScale;
            editor::ui_style::refreshScale();
            ed.settingsDirty = true;
            break;
        case kOptIdle: ed.idleRendering = on; ed.settingsDirty = true; break;
        case kOptSimplify: ed.simplifyFar = on; ed.settingsDirty = true; break;
        case kOptSimplifyDist: ed.simplifyDistance = ::editor::settings_model::ClampSimplifyDistance(value); ed.settingsDirty = true; break;
        case kOptSimplifyAmount: ed.simplifyAmount = ::editor::settings_model::ClampSimplifyAmount(value); ed.settingsDirty = true; break;
        // Language
        case kLangPack: {
            auto& i18n = editor::I18n::get();
            if (index < 0 || index >= static_cast<int>(i18n.packs().size())) break;
            const std::string code = i18n.packs()[static_cast<std::size_t>(index)].code;
            if (i18n.select(code)) ed.langDirty = true;
            break;
        }
        case kLangReload: {
            auto& i18n = editor::I18n::get();
            const std::string cur = i18n.code();
            i18n.scan(i18n.dir());
            i18n.select(cur);
            break;
        }
        case kLangExport: {
            auto& i18n = editor::I18n::get();
            ed.langStatus = i18n.exportTemplate(i18n.dir() + "/modelo.json") ? F("template written to %s/modelo.json", i18n.dir().c_str()) : std::string(T("failed to save"));
            break;
        }
        // Project
        case kProjEditor:
            ed_::ser::copyStr(ed.projSettings.externalEditor, sizeof ed.projSettings.externalEditor, text);
            ed.projSettings.save(ed.project);
            break;
        // Tags
        case kTagTarget: gTagsForm.target = ::editor::settings_model::ClampChoice(index, static_cast<int>(ed_::kBuiltinTags.size())); break;
        case kTagAlias: gTagsForm.alias = text; break;
        case kTagLink:
            if (gTagsForm.alias.empty()) break;
            {
                const std::string alias = gTagsForm.alias.substr(0, 47);   // o ImGui tinha um campo de 48 bytes
                const int target = ::editor::settings_model::ClampChoice(gTagsForm.target, static_cast<int>(ed_::kBuiltinTags.size()));
                if (ed.projSettings.link(alias, ed_::kBuiltinTags[static_cast<size_t>(target)].name)) {
                    ed.scene.tag(alias);
                    ed.projSettings.save(ed.project);
                    gTagsForm.msg.clear();
                    gTagsForm.alias.clear();
                } else {
                    gTagsForm.msg = T("Could not link: the name is empty, too long, or is itself a built-in tag.");
                }
            }
            break;
        case kTagRenameText:
            if (gTagsForm.renaming >= 0) {
                gTagsForm.msg = ed.renameTag(static_cast<tool::TagId>(gTagsForm.renaming), text) ? "" : T("Could not rename: the name is empty, too long, already used, or a built-in tag.");
                gTagsForm.renaming = -1;
            }
            break;
        case kTagRenameCancel: gTagsForm.renaming = -1; break;
        // Lighting: cena
        case kLitFrustum: r.frustumCull = on; ed.settingsDirty = true; break;
        case kLitOcclusion: r.occlusionCull = on; ed.settingsDirty = true; break;
        case kLitDrawDistance: r.drawDistance = value; ed.settingsDirty = true; break;
        // Lighting: sol (so existe com um GlobalLight na cena)
        case kLitSunR: case kLitSunG: case kLitSunB: case kLitSunIntensity: case kLitSunAzimuth: case kLitSunElevation:
        case kLitAmbR: case kLitAmbG: case kLitAmbB: case kLitSunShadow: {
            ed_::GlobalLight* g = settingsSunLight(ed);
            if (!g) break;
            switch (id) {
                case kLitSunR: g->color[0] = value; break;
                case kLitSunG: g->color[1] = value; break;
                case kLitSunB: g->color[2] = value; break;
                case kLitSunIntensity: g->intensity = value; break;
                case kLitSunAzimuth: g->azimuth = value; break;
                case kLitSunElevation: g->elevation = value; break;
                case kLitAmbR: g->ambient[0] = value; break;
                case kLitAmbG: g->ambient[1] = value; break;
                case kLitAmbB: g->ambient[2] = value; break;
                default: g->castShadow = on; break;   // kLitSunShadow
            }
            ed.settingsDirty = true;
            break;
        }
        // Lighting: qualidade
        case kLitTier: L.tier = index; ed.settingsDirty = true; break;
        case kLitCustom: L.custom = on; ed.settingsDirty = true; break;
        case kLitCascades: L.csmCascades = u32v(value); ed.settingsDirty = true; break;
        case kLitShadowRes: L.shadowMapResolution = 512u << ::editor::settings_model::ClampChoice(index, 4); ed.settingsDirty = true; break;
        case kLitPcf: L.pcfTaps = index == 0 ? 1u : (index == 1 ? 4u : 9u); ed.settingsDirty = true; break;
        case kLitMaxLightsCluster: L.maxLightsPerCluster = u32v(value); ed.settingsDirty = true; break;
        case kLitIndicesCluster: L.indicesPerCluster = u32v(value); ed.settingsDirty = true; break;
        case kLitAutoRadius: L.autoRadiusCutoff = value; ed.settingsDirty = true; break;
        // Lighting: orcamento
        case kLitMaxPoint: L.maxPointLights = u32v(value); ed.settingsDirty = true; break;
        case kLitMaxSpot: L.maxSpotLights = u32v(value); ed.settingsDirty = true; break;
        // Lighting: sombras do sol
        case kLitShadows: L.shadows = on; ed.settingsDirty = true; break;
        case kLitShadowDistance: L.shadowDistance = value; ed.settingsDirty = true; break;
        case kLitSplitLambda: L.splitLambda = value; ed.settingsDirty = true; break;
        case kLitBlend: L.blendFraction = value; ed.settingsDirty = true; break;
        case kLitCasterExt: L.casterExtension = value; ed.settingsDirty = true; break;
        case kLitDepthBias: L.depthBias = value; ed.settingsDirty = true; break;
        case kLitNormalBias: L.normalBias = value; ed.settingsDirty = true; break;
        case kLitSnap: L.snapToTexels = on; ed.settingsDirty = true; break;
        case kLitPointShadowLights: L.pointShadowLights = u32v(value); ed.settingsDirty = true; break;
        case kLitPointShadowRes: L.pointShadowResolution = 128u << ::editor::settings_model::ClampChoice(index, 4); ed.settingsDirty = true; break;
        // Lighting: SSAO, ray tracing, bloom, ambiente
        case kLitSsao: L.ssao = on; ed.settingsDirty = true; break;
        case kLitSsaoKernel: L.ssaoKernel = u32v(value); ed.settingsDirty = true; break;
        case kLitSsaoRadius: L.ssaoRadius = value; ed.settingsDirty = true; break;
        case kLitSsaoBias: L.ssaoBias = value; ed.settingsDirty = true; break;
        case kLitSsaoPower: L.ssaoPower = value; ed.settingsDirty = true; break;
        case kLitRayTracing: L.rayTracing = on; ed.settingsDirty = true; break;
        case kLitRtAo: L.rtAo = on; ed.settingsDirty = true; break;
        case kLitRtAoRays: L.rtAoRays = u32v(value); ed.settingsDirty = true; break;
        case kLitRtAoRadius: L.rtAoRadius = value; ed.settingsDirty = true; break;
        case kLitBloom: L.bloom = on; ed.settingsDirty = true; break;
        case kLitBloomStrength: L.bloomStrength = value; ed.settingsDirty = true; break;
        case kLitAmbient: L.ambientIntensity = value; ed.settingsDirty = true; break;
        case kLitPreExposure: L.preExposure = value; ed.settingsDirty = true; break;
        // Lighting: pos-processamento global
        case kLitPostEnabled: r.post.enabled = on; ed.settingsDirty = true; break;
        case kLitPostExposure: r.post.exposure = value; ed.settingsDirty = true; break;
        case kLitPostContrast: r.post.contrast = value; ed.settingsDirty = true; break;
        case kLitPostSaturation: r.post.saturation = value; ed.settingsDirty = true; break;
        case kLitPostTintR: r.post.tint[0] = value; ed.settingsDirty = true; break;
        case kLitPostTintG: r.post.tint[1] = value; ed.settingsDirty = true; break;
        case kLitPostTintB: r.post.tint[2] = value; ed.settingsDirty = true; break;
        case kLitPostTintStrength: r.post.tintStrength = value; ed.settingsDirty = true; break;
        case kLitPostVignette: r.post.vignette = value; ed.settingsDirty = true; break;
        case kLitPostVignetteSize: r.post.vignetteSize = value; ed.settingsDirty = true; break;
        case kLitPostSharpen: r.post.sharpen = value; ed.settingsDirty = true; break;
        case kLitPostReset: {
            const bool add = r.post.additive;
            r.post = tool::PostFx{};
            r.post.enabled = true;
            r.post.additive = add;
            ed.settingsDirty = true;
            break;
        }
        default:
            // Ids com faixa de itens (Remove link, Rename, sombra de cor do sol etc.) tratados abaixo.
            if (id >= kTagRemoveBase && id < kTagRemoveBase + 1000) {
                const std::size_t i = static_cast<std::size_t>(id - kTagRemoveBase);
                if (i < ed.projSettings.links.size()) {
                    const std::string alias = ed.projSettings.links[i].alias;
                    ed.projSettings.unlink(alias);
                    ed.projSettings.save(ed.project);
                }
            } else if (id >= kTagRenameBase && id < kTagRenameBase + 1000) {
                const std::size_t i = static_cast<std::size_t>(id - kTagRenameBase);
                if (i < ed.scene.tags().size()) {
                    gTagsForm.renaming = static_cast<int>(i);
                    gTagsForm.renameBuf = std::string(ed.scene.tags().name(static_cast<tool::TagId>(i)));
                }
            } else if (id >= kGameSkyFace0 && id < kGameSkyFace0 + 6) {
                const int face = id - kGameSkyFace0;
                const auto opts = skyFaceOptions(ed, face);
                if (index < 0 || index >= static_cast<int>(opts.size())) break;
                const std::string before = ed.sceneInfo.sky.face[face];
                if (index == 0) {
                    ed.sceneInfo.sky.face[face][0] = 0;
                } else {
                    ed_::ser::copyStr(ed.sceneInfo.sky.face[face], sizeof ed.sceneInfo.sky.face[face], opts[static_cast<std::size_t>(index)]);
                    ed.assignAsset(opts[static_cast<std::size_t>(index)]);
                }
                ed.lib.invalidate(before);
                ed.lib.invalidate(ed.sceneInfo.sky.face[face]);
                ed.settingsDirty = true;
            } else if (id >= kPhysPreset0 && id < kPhysPreset0 + 7) {
                static const float kPresetG[7][3] = {{0, -9.81f, 0}, {0, -1.62f, 0}, {0, -3.71f, 0}, {0, 0, 0}, {9.81f, 0, 0}, {0, 0, -9.81f}, {0, 9.81f, 0}};
                const int k = id - kPhysPreset0;
                for (int a = 0; a < 3; ++a) ed.sceneInfo.physics.gravity[a] = kPresetG[k][a];
            }
            break;
    }
}

editor::settings_panel::Values settingsValues(Editor& ed) {
  editor::settings_panel::Values v;
  auto& L = ed.sceneInfo.render.lighting;
  v.tab = ed.settingsTab < 0 ? 1 : std::clamp(ed.settingsTab, 0, editor::settings_model::kTabCount - 1);
  v.palette = static_cast<int>(editor::palette::activeIndex());
  {   // paleta Custom: cores-base (0..4) e tokens (5..12), cada um R, G, B
    const auto& c = editor::palette::custom();
    for (std::size_t k = 0; k < 13; ++k) {
      const editor::palette::Rgb& col = k < 5 ? c.base[k] : c.token[k - 5];
      v.customColor[k * 3] = col.r; v.customColor[k * 3 + 1] = col.g; v.customColor[k * 3 + 2] = col.b;
    }
  }
  for (const auto& f : ed.theme.files) v.themeFiles.push_back(f.name);
  v.buttonRadius = editor::ui_style::buttonRadius();
  v.windowRadius = editor::ui_style::windowRadius();
  v.textScale = editor::ui_style::textSetting();
  v.api = ed.apiRequested;
  v.apiBuilt = settingsApiBuilt();
  v.apiStatus = F("Active API: %s", ed.apiStatus.c_str());
  // Cada dominio tem sua própria seção; física não fica misturada com a API gráfica.
  v.form[2] = [&] { editor::settings_form::Rows rows; addGameRows(ed, rows); return rows; }();
  v.form[3] = [&] { editor::settings_form::Rows rows; addPhysicsRows(ed, rows); return rows; }();
  v.form[4] = [&] { editor::settings_form::Rows rows; addLightingRows(ed, rows); return rows; }();
  v.form[5] = [&] { editor::settings_form::Rows rows; addOptimizingRows(ed, rows); return rows; }();
  v.form[6] = [&] { editor::settings_form::Rows rows; addLanguageRows(ed, rows); return rows; }();
  {
    editor::settings_form::Rows& g = v.form[editor::settings_panel::kGraphicsTab];
    addTagsRows(ed, g);
    addProjectRows(ed, g);
  }
  v.resMode = ed.resMode;
  v.customW = ed.customW;
  v.customH = ed.customH;
  int fw = 0, fh = 0;
  v.fixedSize = ed.fixedRes(fw, fh);
  v.fixedW = fw;
  v.fixedH = fh;
  v.renderScale = ed.renderScale;
  v.upscale = ed.upscaleMode;
  v.aa = ed.aaMode;
  v.pixelFilter = ed.pixelFilter;
  v.fps = ed.fpsMode;
  v.shadows = L.shadows;
  v.shadowQuality = ::editor::settings_model::ShadowQualityIndex(L.shadowMapResolution);
  v.rayTracing = L.rayTracing;
  v.rtAo = L.rtAo;
  v.adapter = F("Adapter: %s", ed.gpuDev ? ed.gpuDev->deviceName().c_str() : "(software renderer)");
  v.pixelsLine = F("Pixels per frame: %.2f M (all views)", static_cast<double>(ed.prof.last.pixels) / 1e6);
  v.gpuErrorLine = F("Consecutive GPU error frames: %d / 5 (then the editor falls back to the CPU renderer)", ed.gpuBadFrames);
  if (ed.gpuDev) {
    const auto s = ed.renderFrameStats.backendTotal();
    v.gpuStatsLine = F("GPU: %u draws, %u triangles, %u uploads/frame",
                       static_cast<unsigned>(s.backendDrawCalls), static_cast<unsigned>(s.backendTriangles),
                       static_cast<unsigned>(s.backendBufferUploads));
  }
  return v;
}

// Alvo dos pedidos da janela Configuracoes (settings_changes.hpp): cada metodo grava um campo no Editor com a mesma
// regra que a aba tinha no ImGui (marca o modo de janela como sujo, troca de API pelo applyApi etc.).
// Cor-base da paleta Custom para o tema do editor (editor_theme.json), que guarda as 5 cores-base.
void syncThemeBaseFrom(Editor& ed) {
  const auto& c = editor::palette::custom();
  ed.theme.base.bg = {c.base[0].r, c.base[0].g, c.base[0].b, 1.f};
  ed.theme.base.surface = {c.base[1].r, c.base[1].g, c.base[1].b, 1.f};
  ed.theme.base.text = {c.base[2].r, c.base[2].g, c.base[2].b, 1.f};
  ed.theme.base.accent = {c.base[3].r, c.base[3].g, c.base[3].b, 1.f};
  ed.theme.base.border = {c.base[4].r, c.base[4].g, c.base[4].b, 1.f};
}

struct SettingsEditorTarget {
  Editor& ed;
  int apiRequested() const { return ed.apiRequested; }
  void setTab(int i) { ed.settingsTab = i; }
  void setPalette(int i) {
    editor::palette::activeIndex() = static_cast<std::size_t>(std::clamp(i, 0, static_cast<int>(editor::palette::kCustomIndex)));
    ed.settingsDirty = true;
  }
  // Grava uma cor da paleta Custom. Trocar uma cor-base recalcula os tokens (como o ImGui); trocar um token so muda ele.
  void setCustomColor(int index, float value) {
    auto& c = editor::palette::custom();
    const int slot = std::clamp(index / 3, 0, 12);
    const int ch = index % 3;
    editor::palette::Rgb& col = slot < 5 ? c.base[slot] : c.token[slot - 5];
    (ch == 0 ? col.r : ch == 1 ? col.g : col.b) = std::clamp(value, 0.f, 1.f);
    if (slot < 5) editor::palette::deriveTokens(c);
    syncThemeBase();
    ed.settingsDirty = true;
    ed.themeDirty = true;
  }
  void syncThemeBase() { syncThemeBaseFrom(ed); }
  // Aplica um arquivo de tema da lista: as cores-base vem do arquivo, a paleta vira Custom.
  void setThemeFile(int i) {
    if (i < 0 || i >= static_cast<int>(ed.theme.files.size())) return;
    if (!ed.theme.load(ed.theme.files[static_cast<std::size_t>(i)].path)) return;
    auto& c = editor::palette::custom();
    c.base[0] = {ed.theme.base.bg.x, ed.theme.base.bg.y, ed.theme.base.bg.z};
    c.base[1] = {ed.theme.base.surface.x, ed.theme.base.surface.y, ed.theme.base.surface.z};
    c.base[2] = {ed.theme.base.text.x, ed.theme.base.text.y, ed.theme.base.text.z};
    c.base[3] = {ed.theme.base.accent.x, ed.theme.base.accent.y, ed.theme.base.accent.z};
    c.base[4] = {ed.theme.base.border.x, ed.theme.base.border.y, ed.theme.base.border.z};
    editor::palette::deriveTokens(c);
    editor::palette::activeIndex() = editor::palette::kCustomIndex;
    ed.settingsDirty = true;
    ed.themeDirty = true;
  }
  void applyApi(int api) { ed.requestApi(api); ed.settingsDirty = true; }   // pedido: a troca acontece no inicio do proximo quadro
  void setResMode(int i) { ed.resMode = i; ed.settingsDirty = true; }
  void setCustomW(int w) { ed.customW = w; ed.settingsDirty = true; }
  void setCustomH(int h) { ed.customH = h; ed.settingsDirty = true; }
  void setRenderScale(float s) { ed.renderScale = s; ed.renderScaleSet = true; ed.settingsDirty = true; }
  void setUpscale(int i) { ed.upscaleMode = i; ed.settingsDirty = true; }
  void setAa(int i) { ed.aaMode = i; ed.settingsDirty = true; }
  void setPixelFilter(bool on) { ed.pixelFilter = on; ed.settingsDirty = true; }
  void setFps(int i) { ed.fpsMode = i; ed.fpsDirty = true; ed.settingsDirty = true; }
  void setShadows(bool on) { ed.sceneInfo.render.lighting.shadows = on; ed.settingsDirty = true; }
  void setRayTracing(bool on) { ed.sceneInfo.render.lighting.rayTracing = on; ed.settingsDirty = true; }
  void setRtAo(bool on) { ed.sceneInfo.render.lighting.rtAo = on; ed.settingsDirty = true; }
  void setShadowQuality(int i) {
    auto& L = ed.sceneInfo.render.lighting;
    L.shadowMapResolution = ::editor::settings_model::ShadowResolution(i);
    L.custom = true;
    ed.settingsDirty = true;
  }
  void setChunkCacheMax(int value) {
    ed.chunkCacheMax = ::editor::settings_model::ClampChunkCacheSize(value);
    ed.settingsDirty = true;
  }
  void setButtonRadius(float value) {
    editor::ui_style::buttonRadius() = ::editor::settings_model::ClampButtonRadius(value);
    ed.settingsDirty = true;
  }
  void setWindowRadius(float value) {
    editor::ui_style::windowRadius() = ::editor::settings_model::ClampWindowRadius(value);
    ed.settingsDirty = true;
  }
  // Linhas genericas (settings_form.hpp): Game, Physics, Optimizing, Language, Lighting, Tags e Project.
  void setForm(int id, float value, bool on, int index, const std::string& text) { applyFormRequest(ed, id, value, on, index, text); }
  void setTextScale(float value) {
    editor::ui_style::textSetting() = ::editor::settings_model::ClampTextScale(value);
    editor::ui_style::refreshScale();
    ed.settingsDirty = true;
  }
  void setInterfaceScale(float value) {
    ed.theme.scale = ::editor::settings_model::ClampInterfaceScale(value);
    editor::ui_style::interfaceFactor() = ed.theme.scale / editor::Theme::kDefaultScale;
    editor::ui_style::refreshScale();
    ed.settingsDirty = true;
  }
  void setIdleRendering(bool on) {
    ed.idleRendering = on;
    ed.settingsDirty = true;
  }
};




void openSettings(Editor& ed, int tab) { ed.showSettings = true; ed.settingsTab = tab; ed.focusSettings = 3; }

// Estado da barra de menus principal SpriteUI. Cada marca le um flag do Editor.
editor::mainmenu::MenuState menuStateOf(Editor& ed) {
  editor::mainmenu::MenuState s;
  s.playing = ed.playing;
  s.paused = ed.paused;
  s.canUndo = ed.undoStack.canUndo();
  s.canRedo = ed.undoStack.canRedo();
  s.hierarchy = ed.showHierarchy;
  s.inspector = ed.showInspector;
  s.console = ed.showConsole;
  s.profiler = ed.showProfiler;
  s.editMesh = ed.showEditMesh;
  s.paintMesh = ed.showPaintMesh;
  s.scene = ed.showScene;
  s.game = ed.showGame;
  s.assets = ed.showAssets;
  s.scenes = ed.showScenes;
  s.coding = ed.showCoding;
  s.settings = ed.showSettings;
  s.narrative = ed.showNarrative;
  s.inputMap = ed.showInputMap;
  s.spriteUiDiagnostics = ed.showSpriteUiDiagnostics;
  s.help = editor::helpState().open;
  s.lockInterface = ed.lockInterface;
  s.dockWithShift = ed.dockWithShift;
  s.logKeys = ed.logKeys;
  s.language = editor::I18n::get().code();
  return s;
}


// Janelas e opcoes que a barra liga e desliga. false se o pedido nao for uma delas.
bool toggleFromMenu(Editor& ed, editor::mainmenu::Command c) {
  using C = editor::mainmenu::Command;
  bool* flag = nullptr;
  switch (c) {
    case C::ToggleHierarchy: flag = &ed.showHierarchy; break;
    case C::ToggleInspector: flag = &ed.showInspector; break;
    case C::ToggleConsole: flag = &ed.showConsole; break;
    case C::ToggleProfiler: flag = &ed.showProfiler; break;
    case C::ToggleEditMesh: flag = &ed.showEditMesh; break;
    case C::TogglePaintMesh: flag = &ed.showPaintMesh; break;
    case C::ToggleScene: flag = &ed.showScene; break;
    case C::ToggleGame: flag = &ed.showGame; break;
    case C::ToggleAssets: flag = &ed.showAssets; break;
    case C::ToggleScenes: flag = &ed.showScenes; break;
    case C::ToggleCoding: flag = &ed.showCoding; break;
    case C::ToggleSettings: flag = &ed.showSettings; break;
    case C::ToggleNarrative: flag = &ed.showNarrative; break;
    case C::ToggleInputMap: flag = &ed.showInputMap; break;
    case C::ToggleSpriteUiDiagnostics: flag = &ed.showSpriteUiDiagnostics; break;
    case C::ToggleHelp: flag = &editor::helpState().open; break;
    case C::ToggleLockInterface: flag = &ed.lockInterface; break;
    case C::ToggleDockWithShift: flag = &ed.dockWithShift; break;
    case C::ToggleLogKeys: flag = &ed.logKeys; break;
    default: return false;
  }
  *flag = !*flag;
  return true;
}

// Executa o pedido de um item ou botao da barra de menus.
void applyMenuRequest(Editor& ed, bool& quit, const editor::mainmenu::Request& req) {
  using C = editor::mainmenu::Command;
  if (toggleFromMenu(ed, req.command)) return;
  switch (req.command) {
    case C::NewScene:
      ed.showScenes = true;
      if (gScenesPanel) gScenesPanel->beginNew();
      break;
    case C::SaveScene: ed.saveScene(); break;
    case C::ImportAsset: SDL_ShowOpenFileDialog([](void*, const char* const* files, int) { if (files) for (; *files; ++files) editor::queueImport(*files); }, nullptr, nullptr, nullptr, 0, nullptr, true); break;
    case C::NewNarrative: ed.narrative.newGraph(); break;
    case C::SaveNarrative: ed.narrative.save(); break;
    case C::Settings: openSettings(ed, req.tab); break;
    case C::EditorInfo: ed.showEditorInfo = true; break;
    case C::ExportGame: ed.showExport = true; break;
    case C::Quit: quit = true; break;
    case C::Undo: ed.undo(); break;
    case C::Redo: ed.redo(); break;
    case C::ResetLayout: ed.resetLayout = true; break;
    case C::SaveLayout: gDockSaveRequested = true; break;
    case C::Play: ed.startPlay(); break;
    case C::Stop: ed.stopPlay(); break;
    case C::Pause: ed.paused = !ed.paused; break;
    default: break;
  }
}






// --- persistencia (editor_settings.json + editor_theme.json) ---------------------------------------------------
void saveSettings(Editor& ed) {
  tool::Json j = tool::Json::object();
  j.set("language", editor::I18n::get().code());
  j.set("showGridScene", ed.showGridScene); j.set("showGridGame", ed.showGridGame); j.set("showGizmos", ed.showGizmos);
  j.set("graphicsApi", ed.apiRequested);
  j.set("resMode", ed.resMode); j.set("customW", ed.customW); j.set("customH", ed.customH); j.set("pixelFilter", ed.pixelFilter); j.set("fpsMode", ed.fpsMode);
  j.set("upscaleMode", ed.upscaleMode); j.set("aaMode", ed.aaMode);
  j.set("chunkCacheMax", static_cast<double>(ed.chunkCacheMax));
  j.set("idleRendering", ed.idleRendering);
  j.set("simplifyFar", ed.simplifyFar); j.set("simplifyDistance", static_cast<double>(ed.simplifyDistance)); j.set("simplifyAmount", static_cast<double>(ed.simplifyAmount));
  j.set("interfaceScale", static_cast<double>(ed.theme.scale));
  j.set("buttonRadius", static_cast<double>(editor::ui_style::buttonRadius())); j.set("windowRadius", static_cast<double>(editor::ui_style::windowRadius())); j.set("textScale", static_cast<double>(editor::ui_style::textSetting())); j.set("palette", static_cast<double>(editor::palette::activeIndex()));
  { tool::Json pc = tool::Json::array(); const auto& c = editor::palette::custom();   // paleta Custom: 5 cores-base e 8 tokens
    for (std::size_t k = 0; k < 13; ++k) { const editor::palette::Rgb& col = k < 5 ? c.base[k] : c.token[k - 5]; pc.push(col.r); pc.push(col.g); pc.push(col.b); }
    j.set("paletteCustom", pc); }
  j.set("lockInterface", ed.lockInterface); j.set("dockWithShift", ed.dockWithShift);
  j.set("dockLockV2", true); j.set("dockLockV3", true);
  j.set("renderScaleSet", ed.renderScaleSet); if (ed.renderScaleSet) j.set("renderScale", static_cast<double>(ed.renderScale));
  j.set("lastScene", ed.sceneInfo.name);
  { std::error_code ec; j.set("lastProject", std::filesystem::absolute(ed.project.root, ec).string()); }
  std::ofstream f("editor_settings.json", std::ios::binary | std::ios::trunc); f << j.dump();
  ed.theme.save("editor_theme.json", ed.theme.current);
}
// Retorna a API grafica salva (0 = CPU).
int loadSettings(Editor& ed, std::string& lastScene, std::string& lastProject) {
  std::ifstream f("editor_settings.json", std::ios::binary);
  int api = 0;
  bool interfaceScaleSet = false;
  float interfaceScale = ed.theme.scale;
  if (f) {
    std::stringstream ss; ss << f.rdbuf();
    if (auto j = tool::Json::parse(ss.str())) {
      editor::I18n::get().selectOrDefault((*j)["language"].asString());   // sem idioma salvo: ingles
      ed.showGridScene = editor::ser::flag(*j, "showGridScene", true); ed.showGridGame = editor::ser::flag(*j, "showGridGame", false); ed.showGizmos = editor::ser::flag(*j, "showGizmos", true);
      ed.resMode = std::clamp(static_cast<int>(editor::ser::num(*j, "resMode", 0.f)), 0, 5);
      ed.customW = static_cast<int>(editor::ser::num(*j, "customW", 1280.f)); ed.customH = static_cast<int>(editor::ser::num(*j, "customH", 720.f));
      ed.pixelFilter = editor::ser::flag(*j, "pixelFilter", false); ed.fpsMode = std::clamp(static_cast<int>(editor::ser::num(*j, "fpsMode", 4.f)), 0, 4);
      ed.upscaleMode = std::clamp(static_cast<int>(editor::ser::num(*j, "upscaleMode", 2.f)), 0, 3); ed.aaMode = std::clamp(static_cast<int>(editor::ser::num(*j, "aaMode", 0.f)), 0, 1);
      ed.chunkCacheMax = ::editor::settings_model::ClampChunkCacheSize(static_cast<int>(editor::ser::num(*j, "chunkCacheMax", 8192.f)));
      ed.idleRendering = editor::ser::flag(*j, "idleRendering", true);
      ed.simplifyFar = editor::ser::flag(*j, "simplifyFar", false);
      ed.simplifyDistance = ::editor::settings_model::ClampSimplifyDistance(editor::ser::num(*j, "simplifyDistance", 60.f));
      ed.simplifyAmount = ::editor::settings_model::ClampSimplifyAmount(editor::ser::num(*j, "simplifyAmount", 0.5f));
      editor::ui_style::buttonRadius() = ::editor::settings_model::ClampButtonRadius(editor::ser::num(*j, "buttonRadius", 3.f));
      editor::ui_style::windowRadius() = ::editor::settings_model::ClampWindowRadius(editor::ser::num(*j, "windowRadius", 6.f));
      editor::ui_style::textSetting() = ::editor::settings_model::ClampTextScale(editor::ser::num(*j, "textScale", 1.f));
      editor::palette::activeIndex() = static_cast<std::size_t>(std::clamp(static_cast<int>(editor::ser::num(*j, "palette", 0.f)), 0, static_cast<int>(editor::palette::kCustomIndex)));
      if ((*j)["paletteCustom"].isArray() && (*j)["paletteCustom"].items().size() >= 39) {   // paleta Custom salva
        auto& c = editor::palette::custom();
        const auto& it = (*j)["paletteCustom"].items();
        for (std::size_t k = 0; k < 13; ++k) {
          editor::palette::Rgb& col = k < 5 ? c.base[k] : c.token[k - 5];
          col.r = static_cast<float>(it[k * 3].asNumber(col.r)); col.g = static_cast<float>(it[k * 3 + 1].asNumber(col.g)); col.b = static_cast<float>(it[k * 3 + 2].asNumber(col.b));
        }
        syncThemeBaseFrom(ed);
      }
      if ((*j)["interfaceScale"].isNumber()) {
        interfaceScaleSet = true;
        interfaceScale = static_cast<float>((*j)["interfaceScale"].asNumber(ed.theme.scale));
      }
      // Arquivos anteriores podiam gravar a trava automaticamente; abre destravado uma vez para permitir resize/docking.
      ed.lockInterface = editor::ser::flag(*j, "dockLockV3", false)
          ? editor::ser::flag(*j, "lockInterface", false)
          : false;
      ed.dockWithShift = editor::ser::flag(*j, "dockWithShift", false);
      if (editor::ser::flag(*j, "renderScaleSet", false)) { ed.renderScaleSet = true; ed.renderScale = std::clamp(editor::ser::num(*j, "renderScale", 1.f), 0.25f, 2.f); }
      api = static_cast<int>(editor::ser::num(*j, "graphicsApi", 0.f)); lastScene = (*j)["lastScene"].asString(); lastProject = (*j)["lastProject"].asString();
    }
  }
  ed.theme.load("editor_theme.json");
  if (interfaceScaleSet) ed.theme.scale = ::editor::settings_model::ClampInterfaceScale(interfaceScale);
  editor::ui_style::interfaceFactor() = ed.theme.scale / editor::Theme::kDefaultScale;
  editor::ui_style::refreshScale();
  return api;
}

// Renderer SDL da interface (SpriteUI). E independente da API da Cena e do Jogo (Diligent).
// No Windows, Direct3D vem antes de OpenGL para evitar que o contexto OpenGL oculto da cena
// substitua o contexto usado pelo SDL_Renderer da interface. Vulkan fica de fora porque a cena ja o usa.
SDL_Renderer* createEditorRenderer(SDL_Window* window, bool forceSoftware) {
  if (!window) return nullptr;
  if (forceSoftware) return SDL_CreateRenderer(window, "software");
#if defined(_WIN32)
  static constexpr const char* kPreferred[] = {"direct3d11", "direct3d12", "opengl", "opengles2"};
#else
  static constexpr const char* kPreferred[] = {"opengl", "metal", "opengles2", "direct3d11", "direct3d12"};
#endif
  auto accept = [&](const char* driver) -> SDL_Renderer* {
    SDL_Renderer* renderer = SDL_CreateRenderer(window, driver);
    if (renderer) tool::logInfo(std::string("Interface SpriteUI: driver de render ") + SDL_GetRendererName(renderer));
    return renderer;
  };
  for (const char* driver : kPreferred) {
    if (SDL_Renderer* renderer = accept(driver)) return renderer;
  }
  for (int i = 0; i < SDL_GetNumRenderDrivers(); ++i) {
    const char* driver = SDL_GetRenderDriver(i);
    if (!driver || std::strcmp(driver, "vulkan") == 0 || std::strcmp(driver, "software") == 0) continue;
    if (SDL_Renderer* renderer = accept(driver)) return renderer;
  }
  tool::logWarn("Interface SpriteUI: nenhum driver de GPU abriu; usando software (CPU)");
  return SDL_CreateRenderer(window, "software");
}

} // namespace

// O editor le assets/ (shaders, idiomas, temas) por caminho relativo. Para funcionar com dois cliques no .exe
// (ou pelo Visual Studio), procuramos a raiz do projeto: pasta atual -> pastas acima do executavel -> pasta
// de origem gravada na compilacao (TOOL_SOURCE_DIR). Ao achar, ela vira a pasta de trabalho.
void enterProjectRoot() {
  namespace fs = std::filesystem;
  std::error_code ec;
  auto isRoot = [&](const fs::path& p) { return fs::exists(p / "assets" / "shaders" / "hlsl" / "pbr.hlsl", ec); };
  if (isRoot(fs::current_path(ec))) return;
  if (const char* base = SDL_GetBasePath()) {
    fs::path p = fs::path(base);
    for (int i = 0; i < 8 && !p.empty(); ++i, p = p.parent_path()) {
      if (isRoot(p)) { fs::current_path(p, ec); return; }
      if (p == p.parent_path()) break;
    }
  }
#ifdef TOOL_SOURCE_DIR
  if (isRoot(fs::path(TOOL_SOURCE_DIR))) fs::current_path(fs::path(TOOL_SOURCE_DIR), ec);
#endif
}


// Sem isto, qualquer excecao nao tratada (ex.: falta de memoria com muitas esferas) fecha o editor sem aviso.
// O relatorio vai para stderr e para crash_report.txt, na pasta de trabalho.
static int runEditorMain(int argc, char** argv);

int main(int argc, char** argv) {
  try {
    return runEditorMain(argc, argv);
  } catch (const std::bad_alloc&) {
    std::fprintf(stderr, "ToolEditor: memoria insuficiente (bad_alloc). Veja crash_report.txt.\n");
    if (FILE* f = std::fopen("crash_report.txt", "w")) { std::fprintf(f, "Memoria insuficiente (std::bad_alloc). Reduza o teste ECS ou feche outros programas.\n"); std::fclose(f); }
  } catch (const std::exception& e) {
    std::fprintf(stderr, "ToolEditor: erro nao tratado: %s. Veja crash_report.txt.\n", e.what());
    if (FILE* f = std::fopen("crash_report.txt", "w")) { std::fprintf(f, "Erro nao tratado: %s\n", e.what()); std::fclose(f); }
  } catch (...) {
    std::fprintf(stderr, "ToolEditor: erro nao tratado desconhecido. Veja crash_report.txt.\n");
  }
  return 1;
}

static int runEditorMain(int argc, char** argv) {
  int maxFrames = 0; int startApi = -1; bool startPlaying = false, selfTest = false, playerMode = false;
  long long ecsCount = 0;   // --ecs N: benchmark de N esferas (ver abaixo)
  long long voxelCount = 0; // --voxel N: benchmark de N chunks de terreno (16x16x16 blocos cada)
  int benchFrames = 300;    // --bench-frames N: quadros medidos apos aquecimento
  int mapSide = 0;          // --map N: mapa de N x N chunks de terreno, centrado na origem (ver Criar > Test map of chunks)
  for (int i = 1; i + 1 < argc; ++i) if (std::strcmp(argv[i], "--map") == 0) mapSide = std::atoi(argv[i + 1]);
  for (int i = 1; i + 1 < argc; ++i) if (std::strcmp(argv[i], "--frames") == 0) maxFrames = std::atoi(argv[i + 1]);
  // --screenshot <arquivo.ppm> (com --frames N): grava o ultimo quadro, antes de apresentar. Headless, sem GPU.
  std::string screenshotPath;
  for (int i = 1; i + 1 < argc; ++i) if (std::strcmp(argv[i], "--screenshot") == 0) screenshotPath = argv[i + 1];
  for (int i = 1; i + 1 < argc; ++i) if (std::strcmp(argv[i], "--ecs") == 0) ecsCount = std::atoll(argv[i + 1]);
  for (int i = 1; i + 1 < argc; ++i) if (std::strcmp(argv[i], "--voxel") == 0) voxelCount = std::atoll(argv[i + 1]);
  for (int i = 1; i + 1 < argc; ++i) if (std::strcmp(argv[i], "--bench-frames") == 0) benchFrames = std::clamp(std::atoi(argv[i + 1]), 1, 10000);
  const bool headless = maxFrames > 0 || ecsCount > 0 || voxelCount > 0;   // sem dialogo de projeto, sem gravar cena/configuracoes
  std::string projectArg;   // --project <pasta>: abre esse projeto direto, sem a janela de inicio
  for (int i = 1; i + 1 < argc; ++i) if (std::strcmp(argv[i], "--project") == 0) projectArg = argv[i + 1];
  if ((ecsCount > 0 || voxelCount > 0 || mapSide > 0) && projectArg.empty()) { std::fprintf(stderr, "--ecs/--voxel precisam de --project <pasta de teste> (nao grava o projeto)\n"); return 1; }
  for (int i = 1; i + 1 < argc; ++i) if (std::strcmp(argv[i], "--api") == 0) startApi = std::strcmp(argv[i + 1], "vulkan") == 0 ? 1 : std::strcmp(argv[i + 1], "opengl") == 0 ? 2 : std::strcmp(argv[i + 1], "d3d12") == 0 ? 3 : std::strcmp(argv[i + 1], "d3d11") == 0 ? 4 : 0;
  for (int i = 1; i < argc; ++i) { if (std::strcmp(argv[i], "--test-map") == 0) ed_::openTestMap(); }   // --test-map: captura de tela
  for (int i = 1; i < argc; ++i) { if (std::strcmp(argv[i], "--play") == 0) startPlaying = true; if (std::strcmp(argv[i], "--selftest") == 0) selfTest = true; if (std::strcmp(argv[i], "--player") == 0) playerMode = true; }
  bool openScriptAtStart = false;   // --edit-script: abre a janela de script do objeto selecionado (capturas)
  for (int i = 1; i < argc; ++i) if (std::strcmp(argv[i], "--edit-script") == 0) openScriptAtStart = true;
  std::string startLang, startTheme, startScene, focusWin, selectName; int startTab = -1; bool showInputMapAtStart = false; bool showExportAtStart = false; std::string narrativeFile;
  for (int i = 1; i + 1 < argc; ++i) {
    if (std::strcmp(argv[i], "--lang") == 0) startLang = argv[i + 1];
    if (std::strcmp(argv[i], "--theme") == 0) startTheme = argv[i + 1];
    if (std::strcmp(argv[i], "--palette") == 0) editor::palette::activeIndex() = static_cast<std::size_t>(std::atoi(argv[i + 1]));   // captura: paleta de Appearance pelo indice
    if (std::strcmp(argv[i], "--tab") == 0) startTab = std::atoi(argv[i + 1]);
    if (std::strcmp(argv[i], "--scene") == 0) startScene = argv[i + 1];
    if (std::strcmp(argv[i], "--focus") == 0) focusWin = argv[i + 1];   // para capturas de tela: traz uma janela (hier, game, assets...) para frente
    if (std::strcmp(argv[i], "--select") == 0) selectName = argv[i + 1]; // seleciona um objeto pelo nome
    if (std::strcmp(argv[i], "--help-topic") == 0) editor::openHelp(argv[i + 1]); // capturas de tela: abre a janela Help neste topico
    if (std::strcmp(argv[i], "--show-inputmap") == 0) showInputMapAtStart = true;
    if (std::strcmp(argv[i], "--show-export") == 0) showExportAtStart = true;   // captura: abre Export game
    if (std::strcmp(argv[i], "--narrative") == 0) narrativeFile = argv[i + 1];   // abre um grafo no Narrative Controller (capturas de tela)
  }
  if (selfTest && !std::getenv("SDL_VIDEODRIVER")) SDL_setenv_unsafe("SDL_VIDEODRIVER", "dummy", 1);
  if (maxFrames > 0 && startApi != 2 && !std::getenv("SDL_VIDEODRIVER")) SDL_setenv_unsafe("SDL_VIDEODRIVER", "dummy", 1);

  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) { std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1; }
  enterProjectRoot();
  if (selfTest) { const int rc = editor::runSelfTest("assets/samples"); SDL_Quit(); return rc; } // sem janela
  SDL_Window* window = SDL_CreateWindow("ToolEngine Editor", 1600, 900, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
  SDL_Renderer* renderer = createEditorRenderer(window, maxFrames > 0);
  if (!window || !renderer) { std::fprintf(stderr, "window/renderer: %s\n", SDL_GetError()); return 1; }
  SDL_SetRenderVSync(renderer, 0); // o limite so e ativado se o usuario selecionar VSync ou um FPS fixo

  Editor ed;
  editor::I18n::get().scan("assets/lang");
  editor::I18n::get().select(editor::kDefaultLanguage);   // ingles sem configuracao salva
  ed.theme.scan("assets/themes");
  
  std::string lastScene, lastProject; int savedApi = 0;
  if (!headless) savedApi = loadSettings(ed, lastScene, lastProject);
  if (!startLang.empty()) editor::I18n::get().select(startLang);
  if (startTab >= 0) openSettings(ed, startTab);
  if (!startTheme.empty()) {
    const auto& pr = editor::builtinThemes();
    for (size_t i = 0; i < pr.size(); ++i) if (startTheme == pr[i].key) ed.theme.setPreset(i);
    for (auto& f : ed.theme.files) if (startTheme == f.name) ed.theme.load(f.path);
  }
  tool::Logger::get().setSink([&](tool::LogLevel l, std::string_view m) {
    ed.console.emplace_back(l, std::string(m));
    if (ed.console.size() > editor::console_model::kMaxEntries) ed.console.pop_front();
    ++ed.consoleSerial;
    std::fprintf(l >= tool::LogLevel::Warn ? stderr : stdout, "[%s] %.*s\n", tool::toString(l), static_cast<int>(m.size()), m.data());
  });
  tool::logInfo("Editor started.");
  if (startApi < 0) startApi = savedApi;
  if (startApi > 0) ed.applyApi(startApi);
  tool::logInfo("Graphics API: " + ed.apiStatus);

  // projeto: cria Project/Assets e Project/Scenes; abre a ultima cena (ou cria "Main" na primeira vez)
  // Projeto: --project <pasta> abre direto; senao (uso normal) a janela de inicio pede nome, autor e pasta.
  if (playerMode) { // jogo exportado: o executavel fica na pasta do jogo, com Data/Project ao lado
    std::error_code pe;
    const std::filesystem::path base = editor::editorFolder();
    std::filesystem::current_path(base, pe);
    projectArg = (base / "Data" / "Project").string();
    startPlaying = true;
  }
  if (!projectArg.empty()) {
    std::string err;
    std::error_code mk;
    std::filesystem::create_directories(projectArg, mk);   // --project cria a pasta se ela nao existir (uso de linha de comando)
    const std::filesystem::path root = editor::openProjectFolder(projectArg, err);
    if (root.empty()) { std::fprintf(stderr, "--project: %s (%s)\n", err.c_str(), projectArg.c_str()); SDL_Quit(); return 1; }
    ed.project.root = root;
  } else if (!headless) {
    // Tela de inicio (SpriteUI): criar ou abrir o projeto antes do editor.
    editor::sui::TextAtlasSet startText;   // um atlas por tamanho de texto (como o resto da UI)
    startText.load("assets/fonts/Xmind-Regular.ttf", SDL_GetWindowDisplayScale(window));   // sem a pasta, usa a fonte embutida
    bool closed = false;
    bool startDone = false;
    editor::start::Model model(lastProject, editor::defaultProjectsFolder(lastProject));
    startDone = editor::start::RunStartScreen(window, renderer, model, startText, editor::defaultProjectsFolder(lastProject), closed);
    if (!startDone) { // fechou na tela de inicio: nada foi criado
      SDL_DestroyRenderer(renderer); SDL_DestroyWindow(window); SDL_Quit();
      return 0;
    }
    ed.project.root = model.result();
    saveSettings(ed); // lembra o projeto para o proximo "Abrir ultimo projeto"
  }
  ed.project.ensure();
  ed.projSettings.load(ed.project);
  if (!ed.projSettings.projectName.empty()) SDL_SetWindowTitle(window, ("ToolEngine Editor - " + ed.projSettings.projectName).c_str()); // Project/project.json (ligacoes de tags, preset de exportacao); ausente = padrao
  if (!ed.inputMap.load(ed.project)) ed.inputMap = editor::InputMap::defaults(); // Project/input_map.json; ausente = mapa padrao
  ed.applyInputMap();
  ed.refreshScenes();
  {
    const std::string want = !startScene.empty() ? startScene : lastScene;
    const bool exists = std::find(ed.sceneList.begin(), ed.sceneList.end(), want) != ed.sceneList.end();
    if (!want.empty() && exists && ed.openScene(want, false)) {}
    else if (!ed.sceneList.empty() && ed.openScene(ed.sceneList.front(), false)) {}
    else { ed.sceneInfo.name = "Main"; ed.createDefaultContent(); ed.saveScene(); ed.undoReset(); }
  }
  if (showInputMapAtStart) ed.showInputMap = true;
  if (showExportAtStart) ed.showExport = true;
  if (!narrativeFile.empty()) ed.narrative.open(narrativeFile);
  if (startPlaying) ed.startPlay();
  // --ecs N: benchmark sem vsync. Cria N esferas (como "test ECS: N"), mede o tempo de cada quadro depois do nascimento
  // e imprime media, p50, p95 e maximo. Sai sozinho ao fim de benchFrames quadros medidos.
  if (ecsCount > 0) ed.startEcsTest(ecsCount);
  if (mapSide > 0) ed.generateChunkMap(mapSide);
  if (voxelCount > 0) {   // chunks em grade quadrada, todos com terreno
    const long long side = static_cast<long long>(std::ceil(std::sqrt(static_cast<double>(voxelCount))));
    for (long long i = 0; i < voxelCount; ++i) ed.makeVoxel("Chunk", static_cast<int>(i % side), static_cast<int>(i / side), true);
  }
  if (!selectName.empty()) ed.scene.world().eachEntity([&](tool::Entity e) { auto* n = ed.scene.world().get<editor::Name>(e); if (n && selectName == n->text) ed.selected = e; });

  bool quit = false;
  int frame = 0, layoutFocus = 0;
  const Uint64 t0 = SDL_GetTicks();
  const bool bench = ecsCount > 0 || voxelCount > 0;
  std::vector<double> benchMs;
  std::array<double, static_cast<size_t>(editor::Zone::Count)> benchZoneMs{};
  // TOOL_BENCH_CSV=<arquivo>: grava cada quadro medido (ms total e de cada zona) para achar a origem dos picos.
  std::FILE* benchCsv = nullptr;
  if (const char* csvPath = std::getenv("TOOL_BENCH_CSV")) {
    benchCsv = std::fopen(csvPath, "w");
    if (benchCsv) { std::fprintf(benchCsv, "frame,total_ms"); for (int z = 0; z < static_cast<int>(editor::Zone::Count); ++z) std::fprintf(benchCsv, ",z%d", z); std::fprintf(benchCsv, "\n"); }
  }
  bool benchArmed = false;
  editor::SpriteUiDiagnostics spriteUiDiagnostics;
  // Fonte e renderizador unicos das janelas SpriteUI. Sem a fonte, as janelas ficam na versao ImGui.
  editor::sui::TextAtlasSet uiText;   // atlas de glifos por tamanho (texto nitido em qualquer tamanho)
  editor::sui::SpriteUiSdlRenderer uiRenderer;
  // O modo --player tambem carrega a fonte: a janela do jogo desenha o texto da UI com ela (mesma fonte do editor).
  // Em --frames as janelas SpriteUI nao aparecem, salvo com --screenshot (print com as janelas reais).
  const bool fontReady = (!headless || !screenshotPath.empty()) &&
      uiText.load("assets/fonts/Xmind-Regular.ttf", SDL_GetWindowDisplayScale(window));
  uiRenderer.setTextSet(&uiText);
  const bool uiReady = fontReady && !playerMode;
  // Janelas do editor em SpriteUI. Sem a fonte, elas nao aparecem (nao ha caminho ImGui de reserva).
  std::unique_ptr<editor::profiler_panel::ProfilerPanel> profPanel;
  if (uiReady) profPanel = std::make_unique<editor::profiler_panel::ProfilerPanel>(uiText, uiRenderer);
  std::unique_ptr<editor::statusbar::StatusBarPanel> statusPanel;
  if (uiReady) statusPanel = std::make_unique<editor::statusbar::StatusBarPanel>(uiText, uiRenderer);
  const bool statusSpriteOn = statusPanel != nullptr;
  std::unique_ptr<editor::mainmenu::MainMenuPanel> menuPanel;
  if (uiReady) menuPanel = std::make_unique<editor::mainmenu::MainMenuPanel>(uiText, uiRenderer);
  const bool menuSpriteOn = menuPanel != nullptr;
  std::unique_ptr<editor::help::HelpPanel> helpPanel;
  if (uiReady) helpPanel = std::make_unique<editor::help::HelpPanel>(uiText, uiRenderer);
  std::unique_ptr<editor::testmap::TestMapPanel> testMapPanel;
  if (uiReady) testMapPanel = std::make_unique<editor::testmap::TestMapPanel>(uiText, uiRenderer);
  std::unique_ptr<editor::exportui::Panel> exportPanel;   // Export game em SpriteUI (exportacao em export_panel.hpp)
  if (uiReady) exportPanel = std::make_unique<editor::exportui::Panel>(uiText, uiRenderer);
  std::unique_ptr<editor::inputui::Panel> inputPanel;     // Input Map em SpriteUI (dados em input_map.hpp)
  if (uiReady) inputPanel = std::make_unique<editor::inputui::Panel>(uiText, uiRenderer);
  bool testMapGenerate = false;   // o botao "Generate map" so marca; o mapa e gerado depois do quadro
  bool exportRequested = false;   // o botao "Export game" so marca; a exportacao roda depois do quadro
  std::unique_ptr<editor::console_panel::ConsolePanel> consolePanel;
  if (uiReady) consolePanel = std::make_unique<editor::console_panel::ConsolePanel>(uiText, uiRenderer);
  SDL_Cursor* spriteTextCursor = uiReady ? SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_TEXT) : nullptr;
  if (uiReady && !spriteTextCursor) {
    static const Uint8 iBeamData[32] = {
      0x18,0x00, 0x18,0x00, 0x18,0x00, 0x18,0x00,
      0x18,0x00, 0x18,0x00, 0x18,0x00, 0x18,0x00,
      0x18,0x00, 0x18,0x00, 0x18,0x00, 0x18,0x00,
      0x18,0x00, 0x18,0x00, 0x18,0x00, 0x18,0x00
    };
    static const Uint8 iBeamMask[32] = {
      0x3C,0x00, 0x7E,0x00, 0xFF,0x00, 0xFF,0x00,
      0x7E,0x00, 0x3C,0x00, 0x18,0x00, 0x18,0x00,
      0x18,0x00, 0x18,0x00, 0x18,0x00, 0x18,0x00,
      0x18,0x00, 0x18,0x00, 0x18,0x00, 0x18,0x00
    };
    spriteTextCursor = SDL_CreateCursor(iBeamData, iBeamMask, 16, 16, 7, 7);
  }
  bool cursorWarningLogged = false;
  if (uiReady && !spriteTextCursor) tool::logWarn(std::string("Could not create text cursor: ") + SDL_GetError());
  std::unique_ptr<editor::hierarchy_panel::HierarchyPanel> hierPanel;
  if (uiReady) hierPanel = std::make_unique<editor::hierarchy_panel::HierarchyPanel>(uiText, uiRenderer);
  gHierarchySpriteOn = hierPanel != nullptr;
  std::unique_ptr<editor::settings_panel::SettingsPanel> settingsPanel;
  if (uiReady) settingsPanel = std::make_unique<editor::settings_panel::SettingsPanel>(uiText, uiRenderer);
  gSettingsSpriteOn = settingsPanel != nullptr;
  std::unique_ptr<editor::inspector_panel::InspectorPanel> inspPanel;
  if (uiReady) inspPanel = std::make_unique<editor::inspector_panel::InspectorPanel>(uiText, uiRenderer);
  std::unique_ptr<editor::coding::Panel> codingPanel;
  if (uiReady) codingPanel = std::make_unique<editor::coding::Panel>(uiText, uiRenderer);
  std::unique_ptr<editor::script_window::Panel> scriptPanel;
  if (uiReady) scriptPanel = std::make_unique<editor::script_window::Panel>(uiText, uiRenderer);
  gScriptPanel = scriptPanel.get();
  if (openScriptAtStart && gScriptPanel) {   // so para captura: anexa um script ja existente ao objeto (em memoria)
    auto& w = ed.scene.world();
    if (!w.has<editor::ScriptComp>(ed.selected)) { editor::ScriptComp sc; editor::ser::copyStr(sc.path, sizeof sc.path, "Assets/Scripts/Scenes/Main.scene.tool.js"); w.add<editor::ScriptComp>(ed.selected, sc); }
    gScriptPanel->open(ed);
  }
  gInspectorPanel = inspPanel.get();
  std::unique_ptr<editor::scenesui::Panel> scenesPanel;
  if (uiReady) scenesPanel = std::make_unique<editor::scenesui::Panel>(uiText, uiRenderer);
  gScenesPanel = scenesPanel.get();
  std::unique_ptr<editor::NarrativePanel> narrativePanel;   // Narrative Controller em SpriteUI (dados: ed.narrative)
  if (uiReady) narrativePanel = std::make_unique<editor::NarrativePanel>(uiText, uiRenderer, ed.narrative);
  std::unique_ptr<editor::infoui::Panel> editorInfoPanel;   // Editor info em SpriteUI (rascunho em editor_info_model.hpp)
  if (uiReady) editorInfoPanel = std::make_unique<editor::infoui::Panel>(uiText, uiRenderer);
  bool editorInfoWasOpen = false;
  std::unique_ptr<editor::assetsui::Panel> assetsPanel;   // Assets em SpriteUI (sem a fonte, a versao ImGui)
  if (uiReady) assetsPanel = std::make_unique<editor::assetsui::Panel>(uiText, uiRenderer);
  std::unique_ptr<editor::scene_view_panel::ScenePanel> scenePanel;
  std::unique_ptr<editor::game_view_panel::GamePanel> gamePanel;
  if (uiReady) scenePanel = std::make_unique<editor::scene_view_panel::ScenePanel>(uiText, uiRenderer);
  if (fontReady) gamePanel = std::make_unique<editor::game_view_panel::GamePanel>(uiText, uiRenderer);
  gScenePanel = scenePanel.get();
  gGamePanel = gamePanel.get();
  std::unique_ptr<editor::dock_host::Host> dockOwner;   // antes do laço: destruido antes dos paineis
  if (uiReady) {   // docking: as janelas SpriteUI acopláveis; o host decide area, aba e encaixe
    using editor::dock_host::Kind;
    dockOwner = std::make_unique<editor::dock_host::Host>(uiText, uiRenderer);
    gDock = dockOwner.get();
    addDockWindow(*dockOwner, Kind::Hierarchy, [] { return std::string(T("Hierarchy")); }, [&ed]() -> bool& { return ed.showHierarchy; }, [&] { return hierPanel.get(); });
    addDockWindow(*dockOwner, Kind::Scene, [] { return std::string(T("Scene")); }, [&ed]() -> bool& { return ed.showScene; }, [&] { return scenePanel.get(); });
    addDockWindow(*dockOwner, Kind::Game, [] { return std::string(T("Game")); }, [&ed]() -> bool& { return ed.showGame; }, [&] { return gamePanel.get(); });
    addDockWindow(*dockOwner, Kind::Scenes, [] { return std::string(T("Scenes")); }, [&ed]() -> bool& { return ed.showScenes; }, [&] { return scenesPanel ? &scenesPanel->view() : nullptr; });
    addDockWindow(*dockOwner, Kind::Assets, [] { return std::string(T("Assets")); }, [&ed]() -> bool& { return ed.showAssets; }, [&] { return assetsPanel ? &assetsPanel->view() : nullptr; });
    addDockWindow(*dockOwner, Kind::Console, [] { return std::string(T("Console")); }, [&ed]() -> bool& { return ed.showConsole; }, [&] { return consolePanel.get(); });
    addDockWindow(*dockOwner, Kind::Profiler, [] { return std::string(T("Profiler")); }, [&ed]() -> bool& { return ed.showProfiler; }, [&] { return profPanel.get(); });
    addDockWindow(*dockOwner, Kind::Inspector, [] { return std::string(T("Inspector")); }, [&ed]() -> bool& { return ed.showInspector; }, [&] { return inspPanel ? &inspPanel->chrome() : nullptr; });
    addDockWindow(*dockOwner, Kind::Settings, [] { return std::string(T("Settings")); }, [&ed]() -> bool& { return ed.showSettings; }, [&] { return settingsPanel ? &settingsPanel->chrome() : nullptr; });
    addDockWindow(*dockOwner, Kind::Coding, [] { return std::string(T("Coding")); }, [&ed]() -> bool& { return ed.showCoding; }, [&] { return codingPanel ? &codingPanel->chrome() : nullptr; });
    addDockWindow(*dockOwner, Kind::Narrative, [] { return std::string(T("Narrative Controller")); }, [&ed]() -> bool& { return ed.showNarrative; }, [&] { return narrativePanel ? &narrativePanel->chrome() : nullptr; });
    dockOwner->load(kDockLayoutFile);   // organizacao salva na sessao anterior (se houver e for valida)
  }
  gInspectorSpriteOn = inspPanel != nullptr;
  while (!quit) {
    const Uint64 frameStart = SDL_GetTicksNS();
    ed.applyPendingApi();   // troca de API pedida no quadro anterior (com o aviso ja desenhado)
    if (ed.fpsDirty && renderer) { SDL_SetRenderVSync(renderer, ed.fpsMode == 0 && !headless ? 1 : 0); ed.fpsDirty = false; }
    tool::debug().beginFrame();
    ed.prof.beginFrame();
    // Areas dos paineis SpriteUI para o roteador: vem do quadro anterior (o layout so muda ao redimensionar).
    gPanelRouter.beginFrame();
    namespace pr = editor::panel_router;
    if (menuPanel) {
      int screenW = 0, screenH = 0;
      SDL_GetWindowSize(window, &screenW, &screenH);
      gPanelRouter.add(pr::kMenuBarHandle, {0.f, 0.f, static_cast<float>(screenW), editor::ui_style::barHeight()}, ToolEngine::SpriteUi::UILayer::Docked);
      if (menuPanel->popupOpen()) {
        // Lista aberta: fundo de captura (cliques fora dela nao chegam as janelas de tras) e a propria lista por cima.
        gPanelRouter.add(pr::kMenuBarHandle, {0.f, 0.f, static_cast<float>(screenW), static_cast<float>(screenH)}, ToolEngine::SpriteUi::UILayer::Popup);
        const editor::sui::Box pb = menuPanel->popupBox();
        gPanelRouter.add(pr::kMenuBarHandle, {pb.x, pb.y, pb.w, pb.h}, ToolEngine::SpriteUi::UILayer::Popup);
      }
    }
    if (settingsPanel && ed.showSettings && gSettingsFrame)
      gPanelRouter.add(pr::kSettingsHandle, {gSettingsRect.x, gSettingsRect.y, gSettingsRect.w, gSettingsRect.h},
                       ToolEngine::SpriteUi::UILayer::Docked);
    if (assetsPanel && ed.showAssets) {   // Assets: idem; cobre o ponteiro por cima do ImGui
      const auto ar = assetsPanel->rect();
      gPanelRouter.add(pr::kAssetsHandle, {ar.position.x, ar.position.y, ar.size.x, ar.size.y},
                       ToolEngine::SpriteUi::UILayer::Docked);
    }
    if (scenesPanel && ed.showScenes) {   // Scenes: a janela SpriteUI cobre o ponteiro por cima do ImGui
      const auto sr = scenesPanel->rect();
      gPanelRouter.add(pr::kScenesHandle, {sr.position.x, sr.position.y, sr.size.x, sr.size.y},
                       ToolEngine::SpriteUi::UILayer::Docked);
    }
    if (codingPanel && ed.showCoding && gCodingBody.h > 0.f) {   // Coding: corpo SpriteUI cobre o ponteiro por cima do ImGui
      gPanelRouter.add(pr::kCodingHandle, {gCodingBody.x, gCodingBody.y, gCodingBody.w, gCodingBody.h},
                       ToolEngine::SpriteUi::UILayer::Docked);
    }
    if (narrativePanel && ed.showNarrative && narrativePanel->chrome().visible() && !narrativePanel->chrome().minimized()) {   // Narrative: moldura SpriteUI
      const auto nr = narrativePanel->chrome().frameRect();
      gPanelRouter.add(pr::kNarrativeHandle, {nr.position.x, nr.position.y, nr.size.x, nr.size.y}, ToolEngine::SpriteUi::UILayer::Docked);
    }
    SDL_Event e;
    // Ocioso: espera ate 16 ms pelo proximo evento (em vez de girar o quadro sem parar). O evento volta para a fila.
    // A espera vem ANTES da zona de medicao: nao e trabalho e nao deve aparecer como "Events + input".
    if (ed.viewsIdle() && !headless) {
      const auto waitStart = editor::FrameProfiler::Clock::now();
      SDL_Event waited;
      if (SDL_WaitEventTimeout(&waited, 16)) SDL_PushEvent(&waited);
      ed.prof.addWait(std::chrono::duration<float, std::milli>(editor::FrameProfiler::Clock::now() - waitStart).count());
    }
    editor::ZoneTimer zone(ed.prof, editor::Zone::Events);
    int nEvents = 0;
    spriteUiDiagnostics.beginFrame();
    while (SDL_PollEvent(&e)) {
      editor::frameInput().onEvent(e);   // entrada do quadro (SDL), para a Scene e o Game
      // Movimento do mouse sem botao (so passar por cima) nao conta como atividade: senao a cena nunca fica ociosa
      // e cada pixel de mouse refaz as duas vistas por 3 quadros. A interface continua redesenhando a cada quadro.
      if (!(e.type == SDL_EVENT_MOUSE_MOTION && e.motion.state == 0 && !ed.freeFly)) ++nEvents;
      // Docking primeiro: aba ou grip consumidos aqui nao chegam as janelas (o arraste e do host).
      const bool dockHandled = !playerMode && gDock && gDock->onEvent(e);
      const bool spriteUiHandled =
          spriteUiDiagnostics.handleEvent(e, ed.showSpriteUiDiagnostics);
      const bool profHandled = !dockHandled && profPanel && profPanel->handleEvent(e, dockShown(editor::dock_host::Kind::Profiler, ed.showProfiler));
      const bool helpHandled = helpPanel && helpPanel->handleEvent(e, window, editor::helpState().open);   // janela Help SpriteUI
      const editor::testmap::Action testMapAction = testMapPanel ? testMapPanel->handleEvent(e, editor::testMapState().open, editor::testMapState().side) : editor::testmap::Action::None;
      if (testMapAction == editor::testmap::Action::Generate) testMapGenerate = true;
      const bool inputHandled = inputPanel && inputPanel->handleEvent(e, ed);   // Input Map (SpriteUI) antes do ImGui
      const editor::exportui::Action exportAction = exportPanel ? exportPanel->handleEvent(e, ed.showExport, ed.projSettings.exportPreset.platform) : editor::exportui::Action::None;
      if (exportAction == editor::exportui::Action::Platform) ed.projSettings.save(ed.project);   // a escolha de plataforma vai para o projeto
      if (exportAction == editor::exportui::Action::Export) exportRequested = true;
      const bool menuHandled = menuPanel && menuPanel->handleEvent(e);
      const bool consoleHandled = !dockHandled && consolePanel && consolePanel->handleEvent(e, dockShown(editor::dock_host::Kind::Console, ed.showConsole));
      const bool hierHandled = !dockHandled && hierPanel && dockShown(editor::dock_host::Kind::Hierarchy, ed.showHierarchy) && hierPanel->handleEvent(e);
      // Pontos cobertos por outro painel SpriteUI (ex.: lista de menu aberta) nao chegam ao Settings.
      const bool pointerEvt = isPointerEvent(e);
      const float ptrX = pointerEvt ? pointerX(e) : 0.f;
      const float ptrY = pointerEvt ? pointerY(e) : 0.f;
      const bool settingsMayReceive = !pointerEvt || gPanelRouter.mayReceive(editor::panel_router::kSettingsHandle, ptrX, ptrY);
      // Teclas com Ctrl, Alt ou Gui (atalhos) nunca vao ao campo de texto da janela: o painel consome toda tecla com o
      // campo em foco, e o atalho precisa chegar ao ImGui e aos outros paineis. Texto digitado (TEXT_INPUT) passa
      // sempre: sem campo em foco, o painel devolve false e o ImGui recebe.
      const bool shortcutKey = e.type == SDL_EVENT_KEY_DOWN && (e.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI)) != 0;
      // Editor info (janela flutuante): recebe teclado e ponteiro enquanto esta aberta e ativa o campo clicado.
      const bool eiHandled = !dockHandled && editorInfoPanel && ed.showEditorInfo && editorInfoPanel->handleEvent(e, true);
      // Narrative vem antes dos outros: um clique em outra janela solta o teclado dele (releaseKeyboard).
      bool narHandled = false;
      if (!dockHandled && narrativePanel && dockShown(editor::dock_host::Kind::Narrative, ed.showNarrative)) {
        const bool narrativeMayReceive = !pointerEvt || gPanelRouter.mayReceive(pr::kNarrativeHandle, ptrX, ptrY);
        if (pointerEvt && !narrativeMayReceive) narrativePanel->releaseKeyboard();
        else narHandled = !shortcutKey && narrativePanel->handleEvent(e);
      }
      const bool setHandled = !dockHandled && settingsPanel && dockShown(editor::dock_host::Kind::Settings, ed.showSettings) && settingsMayReceive && !shortcutKey && settingsPanel->handleEvent(e);
      const bool inspHandled = !dockHandled && inspPanel && dockShown(editor::dock_host::Kind::Inspector, ed.showInspector) && inspPanel->handleEvent(e);
      const bool scrHandled = !dockHandled && gScriptPanel && gScriptPanel->handleEvent(e, ed);
      const bool codHandled = !dockHandled && codingPanel && dockShown(editor::dock_host::Kind::Coding, ed.showCoding) && codingPanel->handleEvent(e);
      const bool scnHandled = !dockHandled && scenesPanel && scenesPanel->handleEvent(e, dockShown(editor::dock_host::Kind::Scenes, ed.showScenes));
      const bool asstHandled = !dockHandled && assetsPanel && assetsPanel->handleEvent(e, dockShown(editor::dock_host::Kind::Assets, ed.showAssets));
      const bool sceneFrameHandled = !dockHandled && forwardViewEvent(e, dockShown(editor::dock_host::Kind::Scene, ed.showScene), dockShown(editor::dock_host::Kind::Game, ed.showGame));   // Scene e Jogo veem o evento; a moldura da Scene consome o clique

      if (ed.logKeys && (e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_KEY_UP))
        tool::logInfo(std::string("[keys] ") + (e.type == SDL_EVENT_KEY_DOWN ? "down " : "up ") + SDL_GetKeyName(e.key.key) + (e.key.repeat ? " (repeat)" : "") + (SDL_TextInputActive(window) ? " [text field]" : ""));
      // Atalhos globais (antes vinham do ImGui): so quando nenhum campo de texto esta digitando.
      if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat && !SDL_TextInputActive(window)) {
        const bool ctrl = (e.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0, shift = (e.key.mod & SDL_KMOD_SHIFT) != 0;
        if (playerMode && e.key.key == SDLK_ESCAPE) quit = true;   // jogo exportado: Esc fecha
        if (!playerMode && ctrl && e.key.key == SDLK_S) ed.saveScene();
        if (ctrl && !shift && e.key.key == SDLK_Z) ed.undo();
        if (ctrl && (e.key.key == SDLK_Y || (shift && e.key.key == SDLK_Z))) ed.redo();
        if (e.key.key == SDLK_F1) editor::openHelp("start");   // ajuda geral (o topico por janela vem do "?" de cada moldura)
      }
      if (ed.logKeys && e.type == SDL_EVENT_TEXT_INPUT) tool::logInfo(std::string("[keys] text '") + (e.text.text ? e.text.text : "") + "'");
      if (e.type == SDL_EVENT_MOUSE_MOTION && ed.freeFly) { ed.mouseDX += e.motion.xrel; ed.mouseDY += e.motion.yrel; } // navegacao livre: mouse capturado
      if (e.type == SDL_EVENT_QUIT || (e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && e.window.windowID == SDL_GetWindowID(window))) quit = true;
      if (e.type == SDL_EVENT_DROP_FILE && e.drop.data) editor::queueImport(e.drop.data); // arrastar arquivos do sistema para a janela
    }
    ed.idleFrames = nEvents > 0 ? 0 : ed.idleFrames + 1;
    zone.restart(editor::Zone::Update);
    ed.time = static_cast<double>(SDL_GetTicks() - t0) / 1000.0;
    ed.scene.setClock(ed.time);
    ed.scene.collectPool(ed.time);
    ed.uiTyping = SDL_TextInputActive(window);
    ed.tick(static_cast<double>(tool::debug().lastFrameMs()) / 1000.0);
    ed.beginRenderStatsFrame();
    if (!ed.paused) ed.updateParticles(static_cast<float>(tool::debug().lastFrameMs()) / 1000.f);
    editor::processImports(ed);
    { // navegacao livre (Alt): o mouse fica preso na janela (modo relativo) e a UI ignora o mouse ate sair
      if (ed.flyChanged) {
        ed.flyChanged = false; ed.mouseDX = ed.mouseDY = 0.f;
        SDL_SetWindowRelativeMouseMode(window, ed.freeFly);
      }
    }
    zone.stop();
    const auto panelsStart = editor::FrameProfiler::Clock::now();

    editor::FrameInput& frameIn = editor::frameInput();
    frameIn.refresh();
    frameIn.typing = SDL_TextInputActive(window);
    frameIn.dt = std::min(0.1f, static_cast<float>(tool::debug().lastFrameMs()) / 1000.f);
    const bool mouseHeld = frameIn.held[0];
    const bool deleteHeld = SDL_GetKeyboardState(nullptr)[SDL_SCANCODE_DELETE] != 0;
    ed.undoTick(mouseHeld || deleteHeld || ed.freeFly); // fim de interacao = novo passo do historico
    beginViewsFrame();
    if (playerMode) {
      drawGameWindow(ed, renderer, true);
    } else {
      const float barH = editor::ui_style::barHeight();   // barras de menu e de status (SpriteUI)
      const float menuH = menuSpriteOn ? barH : 0.f;
      const float statusH = statusSpriteOn ? barH : 0.f;
      if (gDock) {   // area do dock: entre a barra de menu e a de status
        int dockW = 0, dockH = 0;
        SDL_GetWindowSize(window, &dockW, &dockH);
        if (ed.resetLayout) gDock->resetDefault();   // Janela > Restaurar layout
        if (gDockSaveRequested) {
          gDockSaveRequested = false;
          tool::logInfo(gDock->save(kDockLayoutFile) ? std::string("Layout das janelas salvo") : std::string("Nao foi possivel salvar o layout"));
        }
        gDock->beginFrame({{0.f, menuH}, {static_cast<float>(dockW), static_cast<float>(dockH) - menuH - statusH}}, dockW, dockH);
        gInspectorBody = framedBody(inspPanel ? &inspPanel->chrome() : nullptr, dockShown(editor::dock_host::Kind::Inspector, ed.showInspector), dockW, dockH, 340.f, 520.f);
        gSettingsBody = framedBody(settingsPanel ? &settingsPanel->chrome() : nullptr, dockShown(editor::dock_host::Kind::Settings, ed.showSettings), dockW, dockH, 460.f, 480.f);
        gCodingBody = framedBody(codingPanel ? &codingPanel->chrome() : nullptr, dockShown(editor::dock_host::Kind::Coding, ed.showCoding), dockW, dockH, 640.f, 420.f);
        // Geometria dos corpos SpriteUI (Inspector e Settings): corpo vazio (h <= 0) = janela escondida ou minimizada.
        gInspectorOrigin = gInspectorBody;
        gInspectorVisible = gInspectorBody;
        gInspectorFrame = gInspectorBody.h > 0.f;
        if (inspPanel && gInspectorFrame) inspPanel->prepare(inspectorContext(ed), inspectorHooks(ed), gInspectorBody.w);   // monta os controles do quadro
        gSettingsRect = gSettingsBody;
        gSettingsFrame = gSettingsBody.h > 0.f;
      }
      ed.resetLayout = false;
      if (dockShown(editor::dock_host::Kind::Scene, ed.showScene)) drawSceneWindow(ed, renderer);
      if (dockShown(editor::dock_host::Kind::Game, ed.showGame)) drawGameWindow(ed, renderer, false);
    }
    { // "Panels" = tempo dos paineis SEM as renderizacoes 3D (essas ja foram contadas nas suas proprias zonas)
      const float total = std::chrono::duration<float, std::milli>(editor::FrameProfiler::Clock::now() - panelsStart).count();
      const float inner = ed.prof.cur[static_cast<int>(editor::Zone::SceneRender)] + ed.prof.cur[static_cast<int>(editor::Zone::GameRender)] + ed.prof.cur[static_cast<int>(editor::Zone::PostFx)] + ed.prof.cur[static_cast<int>(editor::Zone::Sky)];
      ed.prof.add(editor::Zone::Hierarchy, std::max(0.f, total - inner));
    }
    zone.restart(editor::Zone::Ui);
    if (!headless && !playerMode && (ed.themeDirty || ed.langDirty || ed.settingsDirty) && !mouseHeld) { saveSettings(ed); ed.themeDirty = ed.langDirty = ed.settingsDirty = false; }

    { const auto bg = editor::palette::active().panel; SDL_SetRenderDrawColor(renderer, static_cast<Uint8>(bg.r * 255), static_cast<Uint8>(bg.g * 255), static_cast<Uint8>(bg.b * 255), 255); }
    SDL_RenderClear(renderer);
    drawViewPanels(ed, renderer);   // Scene e Jogo em SpriteUI
    const double nowSec = static_cast<double>(SDL_GetTicksNS()) * 1e-9;
    if (profPanel) {   // Profiler: moldura SpriteUI com o botao Exportar (sem janela ImGui)
      if (profPanel->consumeExportRequest()) profPanel->beginExport(nowSec);
      const bool profilerVisible = dockShown(editor::dock_host::Kind::Profiler, ed.showProfiler);
      if (profilerVisible || profPanel->exporting()) {
        if (profPanel->due(nowSec)) profPanel->refresh(nowSec, ed.prof, fillProfilerExtras(ed));   // so 4 vezes por segundo
        profPanel->tickExport(nowSec);
      }
      if (profilerVisible && !profPanel->draw(renderer, window)) {
        tool::logError(std::string("Profiler SpriteUI render failed: ") + SDL_GetError());
        ed.showProfiler = false;
      }
      if (profPanel->consumeClose()) ed.showProfiler = false;   // X da moldura
      if (profPanel->consumeHelp()) editor::openHelp("profiler");   // ? da moldura
    }
    if (hierPanel) {   // Hierarchy: janela SpriteUI com moldura propria (sem janela ImGui)
      const bool shown = dockShown(editor::dock_host::Kind::Hierarchy, ed.showHierarchy);
      editor::hierarchy_panel::Requests requests;
      const editor::hierarchy_panel::Source source = shown ? hierarchySource(ed) : editor::hierarchy_panel::Source{};
      if (!hierPanel->draw(renderer, window, shown, source, requests)) {
        tool::logError(std::string("Hierarchy SpriteUI render failed: ") + SDL_GetError());
        hierPanel.reset();
        gHierarchySpriteOn = false;
      }
      if (hierPanel && hierPanel->consumeClose()) ed.showHierarchy = false;   // X da moldura
      if (hierPanel && hierPanel->consumeHelp()) editor::openHelp("hierarchy");   // ? da moldura
      applyHierarchyRequests(ed, requests);
    }
    if (inspPanel) {   // Inspector: componentes do objeto, no bloco reservado pelo ImGui (drawInspectorBlock)
      const bool shown = ed.showInspector && gInspectorFrame;
      if (!inspPanel->draw(renderer, gInspectorOrigin, gInspectorVisible, shown)) {
        tool::logError(std::string("Inspector SpriteUI render failed: ") + SDL_GetError());
        inspPanel.reset();
        gInspectorPanel = nullptr;
        gInspectorSpriteOn = false;
      }
    }
    if (codingPanel) {   // moldura da janela Coding (SpriteUI): titulo, X, "?" e grip
      if (!codingPanel->chrome().draw(renderer, T("Coding"))) tool::logError(std::string("Coding frame render failed: ") + SDL_GetError());
      if (codingPanel->chrome().consumeClose()) ed.showCoding = false;
      codingPanel->chrome().consumeHelp();
      if (dockShown(editor::dock_host::Kind::Coding, ed.showCoding) &&
          !codingPanel->drawBody(renderer, gCodingBody, ed.project.root)) {
        tool::logError(std::string("Coding render failed: ") + SDL_GetError());
      }
    }
    if (assetsPanel && dockShown(editor::dock_host::Kind::Assets, ed.showAssets)) {   // Assets: janela SpriteUI flutuante (lista, grade, acoes e texto)
      syncAssetsPanel(ed, *assetsPanel);
      std::vector<editor::assetsui::View::Action> assetsActs;
      int winW = 0, winH = 0;
      SDL_GetWindowSize(window, &winW, &winH);
      if (!assetsPanel->draw(renderer, static_cast<float>(winW), static_cast<float>(winH), assetsActs)) {
        tool::logError(std::string("Assets SpriteUI render failed: ") + SDL_GetError());
        assetsPanel.reset();
        ed.showAssets = false;
      } else {
        applyAssetsActions(ed, *assetsPanel, assetsActs);
      }
    }
    if (scenesPanel && dockShown(editor::dock_host::Kind::Scenes, ed.showScenes)) {   // Scenes: janela SpriteUI flutuante; o ImGui antigo fica so como legado
      syncScenesPanel(ed, *scenesPanel);
      std::vector<editor::scenesui::View::Action> scenesActs;
      int winW = 0, winH = 0;
      SDL_GetWindowSize(window, &winW, &winH);
      if (!scenesPanel->draw(renderer, static_cast<float>(winW), static_cast<float>(winH), scenesActs)) {
        tool::logError(std::string("Scenes SpriteUI render failed: ") + SDL_GetError());
        scenesPanel.reset();
        gScenesPanel = nullptr;
        ed.showScenes = false;
      } else {
        applyScenesActions(ed, *scenesPanel, scenesActs);
      }
    }
    if (narrativePanel) {   // Narrative Controller (SpriteUI): moldura, grafo, variaveis e propriedades
      int nw = 0, nh = 0;
      SDL_GetWindowSize(window, &nw, &nh);
      if (!narrativePanel->draw(renderer, static_cast<float>(nw), static_cast<float>(nh), dockShown(editor::dock_host::Kind::Narrative, ed.showNarrative))) tool::logError(std::string("Narrative render failed: ") + SDL_GetError());
      if (narrativePanel->chrome().consumeClose()) ed.showNarrative = false;
      if (narrativePanel->chrome().consumeHelp()) editor::openHelp("narrative");
    }
    if (helpPanel) {   // por cima das outras janelas; o "?" e F1 abrem o topico em helpState().pending
      editor::HelpState& help = editor::helpState();
      if (!help.pending.empty()) {
        helpPanel->openTopic(help.pending);
        help.pending.clear();
      }
      if (!helpPanel->draw(renderer, window, help.open, editor::helpLanguage())) {
        tool::logError(std::string("Help SpriteUI render failed: ") + SDL_GetError());
        helpPanel.reset();
      }
    }
    if (testMapPanel) {
      editor::TestMapState& testMap = editor::testMapState();
      if (!testMapPanel->draw(renderer, window, testMap.open, testMap.side, editor::I18n::get().code())) {
        tool::logError(std::string("Test map SpriteUI render failed: ") + SDL_GetError());
        testMapPanel.reset();
      }
      if (testMapGenerate) {
        testMapGenerate = false;
        const int made = ed.generateChunkMap(testMap.side);
        tool::logInfo("Test map: " + std::to_string(made) + " chunks");
      }
    }
    if (editorInfoPanel) {   // Editor info (SpriteUI): abre com o que esta salvo; Save e Close viram acoes do host
      if (ed.showEditorInfo && !editorInfoWasOpen) editorInfoPanel->open(ed.projSettings);
      editorInfoWasOpen = ed.showEditorInfo;
      if (ed.showEditorInfo) {
        std::vector<editor::infoui::View::Action> eiActs;
        int ew = 0, eh = 0;
        SDL_GetWindowSize(window, &ew, &eh);
        if (!editorInfoPanel->draw(renderer, static_cast<float>(ew), static_cast<float>(eh), eiActs)) tool::logError(std::string("Editor info SpriteUI render failed: ") + SDL_GetError());
        else applyEditorInfoActions(ed, *editorInfoPanel, eiActs);
      }
    }
    if (inspPanel) {   // moldura do Inspector (SpriteUI): titulo, X, "?" e grip
      if (!inspPanel->chrome().draw(renderer, T("Inspector"))) tool::logError(std::string("Inspector frame render failed: ") + SDL_GetError());
      if (inspPanel->chrome().consumeClose()) ed.showInspector = false;
      if (inspPanel->chrome().consumeHelp()) editor::openHelp("objects");
    }
    if (settingsPanel) {   // moldura do Settings (SpriteUI)
      if (!settingsPanel->chrome().draw(renderer, T("Settings"))) tool::logError(std::string("Settings frame render failed: ") + SDL_GetError());
      if (settingsPanel->chrome().consumeClose()) ed.showSettings = false;
      if (settingsPanel->chrome().consumeHelp()) editor::openHelp("profiler");
    }
    if (settingsPanel) {   // Configuracoes: faixa de abas e aba Graficos por cima da janela ImGui
      const bool shown = ed.showSettings && gSettingsFrame;
      editor::settings_changes::Requests requests;
      const editor::settings_panel::Values values = shown ? settingsValues(ed) : editor::settings_panel::Values{};
      if (!settingsPanel->draw(renderer, gSettingsRect, shown, values, requests)) {
        tool::logError(std::string("Settings SpriteUI render failed: ") + SDL_GetError());
        settingsPanel.reset();
        gSettingsSpriteOn = false;
      }
      SettingsEditorTarget target{ed};
      editor::settings_changes::ApplyChanges(target, requests);
    }
    if (menuPanel) {
      std::vector<editor::mainmenu::Request> menuRequests;
      menuPanel->setState(menuStateOf(ed), [](const char* text) { return T(text); });
      if (!menuPanel->draw(renderer, window, editor::ui_style::barHeight(), menuRequests)) {
        tool::logError(std::string("Menu bar SpriteUI render failed: ") + SDL_GetError());
        menuPanel.reset();
      }
      for (const editor::mainmenu::Request& request : menuRequests) applyMenuRequest(ed, quit, request);
    }
    if (consolePanel) {
      const editor::console_panel::Source source{ed.console, ed.consoleSerial, tool::debug().count(tool::Severity::Error),
                                                 tool::debug().count(tool::Severity::Warning)};
      editor::console_panel::Requests requests;
      if (!consolePanel->draw(renderer, window, dockShown(editor::dock_host::Kind::Console, ed.showConsole), source, requests)) {
        tool::logError(std::string("Console SpriteUI render failed: ") + SDL_GetError());
        ed.showConsole = false;
      }
      if (requests.close) ed.showConsole = false;   // X da moldura SpriteUI
      if (!requests.help.empty()) editor::openHelp(requests.help);   // ? da moldura SpriteUI
      applyConsoleRequests(ed, requests);
    }
    if (inputPanel && !inputPanel->draw(renderer, window, ed)) {   // Input Map (SpriteUI)
      tool::logError(std::string("Input map SpriteUI render failed: ") + SDL_GetError());
      inputPanel.reset();
    }
    if (exportPanel) {   // Export game (SpriteUI): plataforma indisponivel volta para a primeira disponivel
      auto& ex = ed.projSettings.exportPreset;
      if (!editor::platformAvailable(ex.platform)) {
        for (int i = 0; i < static_cast<int>(editor::kExportPlatforms.size()); ++i) if (editor::platformAvailable(i)) { ex.platform = i; break; }
      }
      if (!exportPanel->draw(renderer, window, ed.showExport, ex.platform, ed.projSettings.missingInfo(), editor::I18n::get().code())) {
        tool::logError(std::string("Export SpriteUI render failed: ") + SDL_GetError());
        exportPanel.reset();
      } else if (exportRequested) {
        exportPanel->setResult(editor::exportui::runExport(ed));
      }
    }
    exportRequested = false;
    if (statusPanel) {   // por ultimo: a barra fica por cima do Profiler e da Ajuda (mesmo renderer, depois do RenderDrawData)
      int viewportW = 0, viewportH = 0;
      SDL_GetWindowSize(window, &viewportW, &viewportH);
      if (!statusPanel->draw(renderer, statusBarInfo(ed), [](const char* s) { return T(s); }, nowSec, static_cast<float>(viewportW), static_cast<float>(viewportH), editor::ui_style::barHeight())) {
        tool::logError(std::string("Status bar SpriteUI render failed: ") + SDL_GetError());
        statusPanel.reset();
      }
    }
    if (!headless && !playerMode && ed.showSpriteUiDiagnostics) {
      const auto stats = ed.prof.stats();
      if (!spriteUiDiagnostics.draw(
              renderer, window, ed.showSpriteUiDiagnostics, stats.fps,
              ed.prof.cur[static_cast<int>(editor::Zone::SceneRender)],
              ed.prof.cur[static_cast<int>(editor::Zone::PostFx)])) {
        tool::logError(std::string("SpriteUI diagnostics render failed: ") + SDL_GetError());
        ed.showSpriteUiDiagnostics = false;
      }
    }
    float mouseX = 0.f, mouseY = 0.f;
    SDL_GetMouseState(&mouseX, &mouseY);
    const bool textFieldHovered = SDL_GetMouseFocus() == window && (
        (hierPanel && ed.showHierarchy && hierPanel->wantsTextCursor(mouseX, mouseY)) ||
        (consolePanel && ed.showConsole && consolePanel->wantsTextCursor(mouseX, mouseY)) ||
        (inspPanel && ed.showInspector && gInspectorFrame && inspPanel->wantsTextCursor(mouseX, mouseY)) ||
        (codingPanel && ed.showCoding && codingPanel->wantsTextCursor(mouseX, mouseY)) ||
        (helpPanel && editor::helpState().open && helpPanel->wantsTextCursor(mouseX, mouseY)));
    if (textFieldHovered && spriteTextCursor) {
      if (SDL_GetCursor() != spriteTextCursor && !SDL_SetCursor(spriteTextCursor) && !cursorWarningLogged) {
        tool::logWarn(std::string("Could not activate text cursor: ") + SDL_GetError());
        cursorWarningLogged = true;
      }
    } else {
      SDL_Cursor* defaultCursor = SDL_GetDefaultCursor();
      if (SDL_GetCursor() != defaultCursor && !SDL_SetCursor(defaultCursor) && !cursorWarningLogged) {
        tool::logWarn(std::string("Could not restore default cursor: ") + SDL_GetError());
        cursorWarningLogged = true;
      }
    }
    if (gDock && !playerMode) gDock->overlay(renderer);   // faixas de abas e guias do arraste: por cima de tudo
    if (gScriptPanel && !gScriptPanel->render(renderer, window, ed, SDL_GetTicks() / 1000.0)) tool::logError(std::string("Script render failed: ") + SDL_GetError());   // janela flutuante: por cima das docadas
    if (!screenshotPath.empty() && maxFrames > 0 && frame + 1 >= maxFrames) {
      if (SDL_Surface* shot = SDL_RenderReadPixels(renderer, nullptr)) {
        if (SDL_Surface* rgb = SDL_ConvertSurface(shot, SDL_PIXELFORMAT_RGB24)) {
          if (FILE* f = std::fopen(screenshotPath.c_str(), "wb")) {
            std::fprintf(f, "P6\n%d %d\n255\n", rgb->w, rgb->h);
            const auto* base = static_cast<const unsigned char*>(rgb->pixels);
            for (int y = 0; y < rgb->h; ++y)
              std::fwrite(base + static_cast<std::size_t>(y) * static_cast<std::size_t>(rgb->pitch), 1,
                          static_cast<std::size_t>(rgb->w) * 3, f);
            std::fclose(f);
          }
          SDL_DestroySurface(rgb);
        }
        SDL_DestroySurface(shot);
      }
    }
    ed.endRenderStatsFrame();
    zone.restart(editor::Zone::Present);
    SDL_RenderPresent(renderer);
    editor::frameInput().endFrame();   // bordas do quadro (pressed, clicked, roda) valem so aqui
    zone.stop();
    tool::debug().endFrame();
    { static editor::PerfWarnings perfWarn; static unsigned seenLoads = 0;
      if (ed.sceneLoads != seenLoads) { seenLoads = ed.sceneLoads; perfWarn.quietFor(240); } // cena nova: compila shaders e cria pipelines
      perfWarn.check(ed.prof); }   // avisos de recurso no Console (a cada 30 quadros)
    if (ed.fpsMode >= 1 && ed.fpsMode <= 3 && !headless) { // limite manual de FPS (30/60/120): espera o que sobrar do quadro
      static const Uint64 target[4] = {0, 1000000000ull / 30, 1000000000ull / 60, 1000000000ull / 120};
      const Uint64 spent = SDL_GetTicksNS() - frameStart;
      if (spent < target[ed.fpsMode]) {
        const auto waitStart = editor::FrameProfiler::Clock::now();
        SDL_DelayNS(target[ed.fpsMode] - spent);
        ed.prof.addWait(std::chrono::duration<float, std::milli>(editor::FrameProfiler::Clock::now() - waitStart).count());
      }
    }
    ++frame; if (maxFrames > 0 && frame >= maxFrames) quit = true;
    if (bench) {
      bool armed = false;
      ed.scene.world().each<editor::SpawnerComp>([&](tool::Entity, editor::SpawnerComp& s) { if (s.armed) armed = true; });
      if (armed) benchArmed = true;
      if (voxelCount > 0 && frame == 10) benchArmed = true;   // chunks nao tem nascimento: mede a partir do 10o quadro
      if (benchArmed && !armed) {   // nascimento terminou: mede os proximos quadros
        benchMs.push_back(static_cast<double>(SDL_GetTicksNS() - frameStart) / 1e6);
        for (size_t z = 0; z < benchZoneMs.size(); ++z) benchZoneMs[z] += ed.prof.cur[z];
        if (benchCsv) { std::fprintf(benchCsv, "%zu,%.3f", benchMs.size(), benchMs.back()); for (size_t z = 0; z < benchZoneMs.size(); ++z) std::fprintf(benchCsv, ",%.3f", ed.prof.cur[z]); std::fprintf(benchCsv, "\n"); }
        if (benchMs.size() >= static_cast<size_t>(benchFrames)) {
          std::vector<double> s = benchMs; std::sort(s.begin(), s.end());
          if (benchCsv) { std::fclose(benchCsv); benchCsv = nullptr; }
          double sum = 0; for (double v : s) sum += v;
          std::printf("ECS bench: %s, %zu bodies, %zu frames\n", ecsCount > 0 ? (std::to_string(ecsCount) + " spheres").c_str() : (std::to_string(voxelCount) + " voxel chunks").c_str(), ed.play.ents.size(), s.size());
          std::printf("ECS bench: avg %.2f ms (%.1f fps), p50 %.2f ms, p95 %.2f ms, max %.2f ms\n", sum / s.size(), 1000.0 * s.size() / sum,
                      s[s.size() / 2], s[s.size() * 95 / 100], s.back());
          for (int z = 0; z < static_cast<int>(editor::Zone::Count); ++z) std::printf("ECS bench:   %-34s %7.2f ms\n", editor::zoneName(static_cast<editor::Zone>(z)), benchZoneMs[static_cast<size_t>(z)] / s.size());
          std::printf("ECS bench:   last frame: %llu draws, %llu triangles, %llu culled\n", static_cast<unsigned long long>(ed.prof.last.draws), static_cast<unsigned long long>(ed.prof.last.tris), static_cast<unsigned long long>(ed.prof.last.culled));
          quit = true;
        }
      }
    }
  }
  if (!headless && !playerMode) { ed.saveScene(); saveSettings(ed); } // ao sair, grava a cena e as configuracoes (nunca no jogo exportado)
  ed.stopPlay();
  tool::Logger::get().setSink(nullptr);
  ed.sceneTex.destroy(); ed.gameTex[0].destroy(); ed.gameTex[1].destroy();
  ed.shutdownGpu();
  SDL_SetCursor(SDL_GetDefaultCursor());
  profPanel.reset();
  menuPanel.reset();
  statusPanel.reset();
  helpPanel.reset();
  testMapPanel.reset();
  if (spriteTextCursor) SDL_DestroyCursor(spriteTextCursor);
  uiRenderer.destroy();   // a textura do atlas e destruida antes do renderer SDL
  SDL_DestroyRenderer(renderer); SDL_DestroyWindow(window); SDL_Quit();
  std::printf("editor exited after %d frames\n", frame);
  {
    const auto calls = editor::sui::SpriteUiDrawCalls();
    std::printf("SpriteUI: %llu SDL_RenderGeometry no total, %.1f por quadro\n", static_cast<unsigned long long>(calls),
                frame > 0 ? static_cast<double>(calls) / frame : 0.0);
  }
  return 0;
}
