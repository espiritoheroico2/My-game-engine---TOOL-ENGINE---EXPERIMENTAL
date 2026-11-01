// ToolEngine Editor - estado central do editor (cena, projeto, renderizacao, audio) sem nenhum codigo de janela.
// As janelas (main.cpp, assets_panel.hpp) so leem e alteram este objeto.
//
// COMO O EDITOR DESENHA UM QUADRO
//   1. As janelas "Scene" e "Game" pedem uma imagem: renderView(camera, largura, altura).
//   2. renderView escolhe o caminho: GPU (Diligent -> readback) ou CPU (SoftRenderer, multithread).
//   3. A imagem vira uma textura SDL e o ImGui a mostra. Grid e gizmos sao linhas do ImGui por cima
//      (projetadas com a mesma matriz da camera), entao funcionam igual em qualquer API grafica.
#pragma once
#include "components.hpp"
#include "narrative_panel.hpp"
#include "profiler.hpp"
#include "project.hpp"
#include "project_settings.hpp"
#include "script.hpp"
#include "script_host.hpp"
#include "input_map.hpp"
#include "input_sampler.hpp"
#include "play_runtime.hpp"
#include "theme.hpp"
#include "undo.hpp"
#include "tool/audio/decode.hpp"
#include "tool/audio/miniaudio_device.hpp"
#include "tool/render/gpu_renderer.hpp"
#include "tool/render/postfx.hpp"
#include "tool/render/upscale.hpp"
#include "tool/render/shadow.hpp"
#include "tool/render/soft_renderer.hpp"
#ifdef TOOL_EDITOR_GPU
#include "tool/rhi/diligent_device.hpp"
#endif
#include "tool/rhi/null_device.hpp"
#include "tool/scene/scene.hpp"

#include <SDL3/SDL.h>

#include <cstring>
#include <array>
#include <deque>
#include <memory>
#include <unordered_map>
#include <functional>
#include <string>
#include <vector>

namespace editor {

struct ViewTex {
  SDL_Texture* tex = nullptr;
  int w = 0, h = 0;
  void ensure(SDL_Renderer* r, int nw, int nh) {
    if (tex && w == nw && h == nh) return;
    if (tex) SDL_DestroyTexture(tex);
    tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, nw, nh);
    if (tex) SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_LINEAR); // o modo e reaplicado todo quadro (opcao "pixel filter")
    w = nw; h = nh;
  }
  void destroy() { if (tex) SDL_DestroyTexture(tex); tex = nullptr; w = h = 0; }
};

struct Editor {
  tool::Scene scene;
  tool::Entity selected;
  std::deque<std::pair<tool::LogLevel, std::string>> console;
  std::vector<float> frameHistory = std::vector<float>(120, 0.f);
  ::editor::NarrativePanel narrative;
  std::unique_ptr<tool::rhi::IDevice> device = tool::rhi::createNullDevice();

  // janelas
  bool showHierarchy = true, showInspector = true, showConsole = true, showProfiler = true, showScene = true, showGame = true;
  bool showAssets = true, showScenes = true, showNarrative = true, showSettings = true, showInputMap = false;
  bool resetLayout = false;
  int focusSettings = 0, settingsTab = -1;

  // projeto / cena
  Project project;
  AssetLib lib{&project};
  SceneFile sceneInfo;           // nome da cena atual, orientacao do split, assets alocados, render (sombras/pos-proc.)
  ProjectSettings projSettings;  // Project/project.json: ligacoes de tags e preset de exportacao
  FrameProfiler prof;            // dados do Profiler (tempos por zona, contadores)
  std::vector<std::string> sceneList;
  std::string statusLine;        // ultima mensagem de acao (salvou, importou...)
  int created = 0;
  double time = 0;

  // visualizacao
  tool::SoftRenderer renderer{640, 360};
  tool::Mesh cube = tool::Mesh::cube(0.5f), sphere = tool::Mesh::sphere(0.5f), plane = tool::Mesh::plane(0.5f);
  ViewTex sceneTex, gameTex[2];
  float yaw = 0.6f, pitch = 0.35f, dist = 9.f;
  tool::Vec3 target{0, 0.5f, 0};
  bool showGridScene = true, showGridGame = false, showGizmos = true;
  bool showPostInScene = true; // aplicar o pos-processamento GLOBAL tambem na janela Scene
  // Gizmo de transformacao (Scene): 0 = mover (W), 1 = rotacionar (E), 2 = escalar (R); espaco global/local nao e usado (eixos do mundo)
  int gizmoMode = 0;
  // Desfazer/Refazer (undo.hpp). `interactingPrev` detecta o fim de uma interacao do usuario.
  UndoStack undoStack; bool interactingPrev = false;
  // Navegacao livre (tecla Alt): camera em primeira pessoa na janela Scene. WASD anda, Q/E desce/sobe, Shift acelera.
  bool freeFly = false;
  tool::Vec3 flyPos{0, 2, 6};
  float flyYaw = 0.f, flyPitch = 0.f;   // yaw 0 = olhando para -Z
  float flySpeed = 6.f;                 // m/s
  bool flyChanged = false;              // a UI pediu para entrar/sair da navegacao livre: main.cpp ajusta o mouse (modo relativo)
  float mouseDX = 0.f, mouseDY = 0.f;   // movimento bruto do mouse acumulado no quadro (so usado na navegacao livre)

  // execucao (Play)
  bool playing = false;
  // Variaveis do jogo (SavePoint/UI/Narrative) e eventos de UI da ultima passada (limpos a cada quadro de Play).
  std::uint64_t sigGpu = 0, sigCpu = 0; bool sigGpuValid = false, sigCpuValid = false;   // assinatura (casters + luz) do ultimo shadow map de cada renderer (sleeper de sombras)
  std::unordered_map<std::string, float> vars;
  // Eventos enviados a objetos (acao "send" do Narrative / futuro script): fila por objeto, limpa a cada Play.
  std::unordered_map<uint32_t, std::vector<std::string>> objectInbox;
  std::vector<std::string> narrPending;   // eventos do Narrative a disparar DEPOIS do quadro (evita reentrar no runtime)
  // Scripts ToolObject (QuickJS): so ativos durante o Play. Pedidos dos scripts entram na fila e sao aplicados depois do quadro.
  ScriptHost scriptHost;
  std::vector<std::pair<std::string, float>> scriptDelayed;   // destroy(nome, segundos) pendentes
  ScriptHost::Entry scriptEntryFor(tool::Entity e);
  void startScripts(); void tickScripts(float dt); void applyScriptCmds();
  void scriptCall(ScriptHost::Inst& in, int what, float dt, const std::string& ev);   // what: 1 evento, 2 update
  tool::Entity spawnCopy(const std::string& name);                                    // copia um objeto (sem gravar arquivos de script novos)
  void stopScripts() { scriptHost.close(); scriptDelayed.clear(); }
  // Teste de ECS (ecs_test.hpp): cria/atualiza o objeto ECScall com N esferas e da Play.
  bool pushRuntimeBody(tool::Entity e);   // corpo fisico para um objeto criado durante o Play (sem Rebuild)
  void tickSpawners(float dt);
  void startEcsTest(long long n);
  void sendEvent(const std::string& target, const std::string& ev); void setActiveByName(const std::string& n, bool on);
  struct Bounds { tool::Vec3 center{}; float radius = 0.f; };
  struct BoundsEntry { size_t n = 0; const void* data = nullptr; Bounds b; };
  std::unordered_map<const tool::Mesh*, BoundsEntry> boundsCache_;   // esfera de cada malha (para o frustum culling)
 std::vector<std::string> uiEvents;
  struct Particle { tool::Vec3 p, v; float age = 0.f, life = 1.f; };
  struct ParticleSim { std::vector<Particle> list; float acc = 0.f; uint32_t seed = 12345u; uint32_t touched = 0; };
  uint32_t particleFrame = 0;
  int particleBudget = 100000;                         // maximo de particulas vivas somando todos os emissores
  std::array<tool::Mesh, 64> partBatch;               // lotes por cor (malhas dinamicas, reaproveitadas)
  std::array<tool::Vec3, 64> partColor{};             // cor de cada lote
  void buildParticleBatches(const tool::Camera& cam, int w, int h);
  std::unordered_map<uint32_t, ParticleSim> particles;   // por indice de entidade
  void updateParticles(float dt);
  // Narrative Controller em execucao (so durante o Play) + codigo da cena
  struct NarrRun { tool::narr::Program prog; std::unique_ptr<tool::narr::Runtime> rt; };
  std::unique_ptr<NarrRun> narrRun;
  bool ensureSceneCode(); void openSceneNarrative(); tool::Entity findByName(const std::string& n);
  void startNarrative(); void syncNarrativeVars(); void tickNarrative(float dt, const std::vector<std::string>& events);
  void narrativeAction(const std::string& name, const std::string& arg); void onCreated(tool::Entity e, const std::string& templateName = "");
  bool paused = false, stepOnce = false;   // Pause: congela fisica/tools/sol; Step avanca 1/60 s
  // Entrada (Input Map) e Play
  InputMap inputMap = InputMap::defaults();   // Project/input_map.json
  tool::InputSystem input;                    // acoes "Player 1/jump"... (preenchido por tick() durante o Play)
  tool::RawInput rawInput;
  InputSampler sampler;
  bool injectInput = false;                   // testes: nao ler o SDL, usar rawInput como esta
  bool uiTyping = false;                      // a UI esta recebendo texto: o jogo nao le o teclado
  PlayRuntime play;                           // fisica + estado do Play (so durante o Play)
  void applyInputMap() { inputMap.apply(input); }
  float sunRuntime = 0.f;
  tool::Mixer mixer;
  std::unique_ptr<tool::MiniaudioDevice> audioDev;
  bool audioTried = false, audioOk = false;
  tool::VoiceId previewVoice{};

  // API grafica: 0 = Software (CPU); 1 = Vulkan; 2 = OpenGL; 3 = D3D12 (so Windows)
  int apiChoice = 0, apiActive = 0;
  float renderScale = 0.5f;
  bool renderScaleSet = false; // o usuario mexeu na escala: nao sobrescrever ao trocar de API
  // Upscaling e antialiasing POR SOFTWARE (CPU, tool/render/upscale.hpp), aplicados no tamanho da janela.
  // upscaleMode: 0 = desligado (SDL escala linear), 1 = bilinear, 2 = bicubic, 3 = Lanczos2. aaMode: 0 = desligado, 1 = FXAA.
  int upscaleMode = 2, aaMode = 0;
  std::vector<tool::u8> sceneUp, gameUp[2], aaScratch;
  bool softScaling() const { return upscaleMode != 0 || aaMode != 0; }
  // Converte a imagem renderizada (rw x rh) para a janela (w x h): upscaling + AA. Devolve o buffer final (w x h).
  const tool::u8* finishView(std::vector<tool::u8>& dst, const tool::u8* src, int rw, int rh, int w, int h) {
    const size_t n = static_cast<size_t>(w) * static_cast<size_t>(h) * 4;
    dst.resize(n);
    if (rw == w && rh == h) std::memcpy(dst.data(), src, n);
    else {
      static const tool::UpscaleFilter filters[4] = {tool::UpscaleFilter::Bilinear, tool::UpscaleFilter::Bilinear, tool::UpscaleFilter::Bicubic, tool::UpscaleFilter::Lanczos2};
      ZoneTimer zt(prof, Zone::PostFx);
      tool::upscaleRgba(src, rw, rh, dst.data(), w, h, filters[std::clamp(upscaleMode, 0, 3)]);
    }
    if (aaMode == 1) { ZoneTimer zt(prof, Zone::PostFx); tool::fxaaRgba(dst.data(), w, h, aaScratch); }
    return dst.data();
  }
  // Resolucao do jogo (janela Game): 0 = acompanha a janela x escala; 1..4 = fixa (640x360, 1280x720, 1920x1080, 2560x1440); 5 = personalizada
  int resMode = 0, customW = 1280, customH = 720;
  bool pixelFilter = false; // ampliar a imagem com pixels nitidos (nearest) em vez de suave (linear)
  int fpsMode = 0;          // 0 = VSync; 1 = 30; 2 = 60; 3 = 120; 4 = sem limite
  // Ocioso: sem evento (mouse, teclado, janela) ha 3 quadros e sem Play, as vistas 3D nao sao redesenhadas (economiza GPU/CPU).
  int idleFrames = 0;
  // Ocioso = nada muda na tela: sem Play, sem emissor de particulas animando (ou pausado) e sem interacao ha 3 quadros.
  bool fxAnimating = false;   // definido por updateParticles: ha ParticleComp na cena
  bool viewsIdle() const { return !playing && !(fxAnimating && !paused) && idleFrames > 3; }
  bool fpsDirty = true;
  int gpuBadFrames = 0;
  // Tamanho fixo escolhido (retorna false se o modo e "acompanha a janela").
  bool fixedRes(int& w, int& h) const {
    static const int P[4][2] = {{640, 360}, {1280, 720}, {1920, 1080}, {2560, 1440}};
    if (resMode >= 1 && resMode <= 4) { w = P[resMode - 1][0]; h = P[resMode - 1][1]; return true; }
    if (resMode == 5) { w = std::clamp(customW, 160, 3840); h = std::clamp(customH, 90, 2160); return true; }
    return false;
  }
  float defaultScale() const { return apiActive == 0 ? 0.5f : 1.f; }
  std::string apiStatus = "Software (CPU)";
  std::unique_ptr<tool::rhi::IDevice> gpuDev;
  tool::GpuRenderer gpu;
  SDL_Window* gpuWin = nullptr;
  SDL_GLContext glCtx = nullptr; // so Linux/OpenGL: contexto criado pelo SDL, ativado apenas durante o desenho da GPU
  std::vector<tool::u8> gpuPixels;

  // aparencia/idioma
  ::editor::Theme theme;
  char themeName[48] = "meu-tema";
  std::string themeStatus, langStatus;
  bool themeDirty = false, langDirty = false, settingsDirty = false;

  Editor() { lib.onMeshForget = [this](const tool::Mesh* m) { if (apiActive != 0) gpu.forget(*m); }; }

  // ---------------------------------------------------------------- API grafica
  void shutdownGpu() {
    if (glCtx && gpuWin) SDL_GL_MakeCurrent(gpuWin, glCtx); // o OpenGL precisa do contexto ativo para liberar recursos
    gpu.shutdown();  // libera recursos antes do dispositivo
    gpuDev.reset();
    if (glCtx) { SDL_GL_DestroyContext(glCtx); glCtx = nullptr; }
    if (gpuWin) { SDL_DestroyWindow(gpuWin); gpuWin = nullptr; }
    apiActive = 0;
  }
  // Troca a API grafica em execucao; se falhar, volta para o renderizador em CPU e mostra o motivo.
  void applyApi(int choice) {
    shutdownGpu();
    apiChoice = choice;
    if (choice == 0) { apiStatus = "Software (CPU)"; if (!renderScaleSet) renderScale = 0.5f; return; }
#ifdef TOOL_EDITOR_GPU
    tool::rhi::DeviceInfo info; info.width = 640; info.height = 360;
    info.validation = std::getenv("TOOL_GPU_VALIDATION") != nullptr; // depuracao: TOOL_GPU_VALIDATION=1 liga as camadas de validacao (mais lento)
    info.backend = choice == 1 ? tool::rhi::Backend::Vulkan : choice == 2 ? tool::rhi::Backend::GLES : tool::rhi::Backend::D3D12;
    if (info.backend == tool::rhi::Backend::GLES) {
#if defined(_WIN32)
      // Windows: o Diligent cria o contexto OpenGL sozinho a partir do HWND. A janela NAO pode ter sido
      // preparada para OpenGL pelo SDL (o formato de pixel so pode ser definido uma vez por janela).
      gpuWin = SDL_CreateWindow("gpu", 64, 64, SDL_WINDOW_HIDDEN);
#else
      // Linux: o Diligent usa o contexto GL que ESTIVER ativo; por isso o criamos aqui (4.3 core) via SDL.
      SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4); SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
      SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
      gpuWin = SDL_CreateWindow("gpu", 64, 64, SDL_WINDOW_HIDDEN | SDL_WINDOW_OPENGL);
      if (gpuWin) { glCtx = SDL_GL_CreateContext(gpuWin); if (!glCtx) std::fprintf(stderr, "SDL_GL_CreateContext: %s\n", SDL_GetError()); }
#endif
      if (!gpuWin) { apiStatus = std::string("OpenGL: could not create the context window: ") + SDL_GetError(); apiChoice = 0; return; }
    }
    if (info.backend == tool::rhi::Backend::D3D12) {
#if defined(_WIN32)
      gpuWin = SDL_CreateWindow("gpu", 64, 64, SDL_WINDOW_HIDDEN);
#endif
    }
    if (gpuWin) {
      const SDL_PropertiesID p = SDL_GetWindowProperties(gpuWin);
#if defined(_WIN32)
      info.nativeWindow = SDL_GetPointerProperty(p, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
#else
      info.nativeWindow = reinterpret_cast<void*>(static_cast<uintptr_t>(SDL_GetNumberProperty(p, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0)));
      info.nativeDisplay = SDL_GetPointerProperty(p, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr);
#endif
    }
    std::string err;
    gpuDev = tool::rhi::createDiligentDevice(info, &err);
    if (!gpuDev || !gpu.init(*gpuDev, "assets/shaders", 640, 360)) {
      apiStatus = "FAILED (" + (err.empty() ? std::string("shaders/pipeline") : err) + "): using CPU";
      shutdownGpu(); apiChoice = 0; return;
    }
    if (glCtx) SDL_GL_MakeCurrent(gpuWin, nullptr); // solta o contexto: ele so e usado dentro de renderView
    apiActive = choice; if (!renderScaleSet) renderScale = 1.f;
    apiStatus = std::string(tool::rhi::toString(gpuDev->backend())) + " - " + gpuDev->deviceName();
    lib.clear(); // as malhas importadas sobem para a GPU de novo (os ponteiros mudam)
#else
    apiStatus = "This build has no GPU backends (use the editor-gpu preset)"; apiChoice = 0;
#endif
  }

  // ---------------------------------------------------------------- objetos
  tool::Entity make(const char* name, bool withTransform = true) {
    tool::World& w = scene.world();
    tool::Entity e = w.create();
    Name n; std::snprintf(n.text, sizeof n.text, "%s", name); w.add<Name>(e, n);
    if (withTransform) w.add<Transform>(e);
    return e;   // sem script: o script ToolObject so existe quando o usuario clica em "Add Script"
  }
  // Script ToolObject: opcional. Criado sob demanda (botao "Add Script") como arquivo em Assets/Scripts.
  void attachScript(tool::Entity e) {
    tool::World& w = scene.world();
    const std::string rel = createScriptFile(project, w.get<Name>(e)->text);
    if (rel.empty()) { tool::logWarn("Could not create the script file for a new object."); return; }
    ScriptComp sc; ser::copyStr(sc.path, sizeof sc.path, rel); w.add<ScriptComp>(e, sc);
  }
  // Ao duplicar, o clone recebe uma COPIA do script (editar um nao altera o outro).
  void cloneScript(tool::Entity c) {
    tool::World& w = scene.world();
    auto* sc = w.get<ScriptComp>(c);
    if (!sc) return;   // o clone so ganha script se o original tiver
    std::string text; const bool had = readScript(project, sc->path, text);
    const std::string rel = createScriptFile(project, w.get<Name>(c)->text);
    sc->path[0] = 0;
    if (rel.empty()) return;
    if (had) writeScript(project, rel, text);
    ser::copyStr(sc->path, sizeof sc->path, rel);
  }
  // Ao apagar um objeto, o script some junto SE ainda for o texto inicial (nunca apagamos codigo que o usuario escreveu)
  // e se nenhum outro objeto usa o mesmo arquivo.
  void releaseScript(tool::Entity e) {
    tool::World& w = scene.world();
    auto* sc = w.get<ScriptComp>(e); auto* n = w.get<Name>(e);
    if (!sc || !n || !sc->path[0]) return;
    bool shared = false;
    w.each<ScriptComp>([&](tool::Entity o, ScriptComp& other) { if (o != e && std::strcmp(other.path, sc->path) == 0) shared = true; });
    std::string text;
    if (!shared && readScript(project, sc->path, text) && text == scriptTemplate(n->text)) { std::error_code ec; fs::remove(project.resolve(sc->path), ec); }
  }
  tool::Entity makeMesh(const char* name, int shape, float x, float y, float z) {
    tool::Entity e = make(name);
    scene.world().get<Transform>(e)->pos[0] = x; scene.world().get<Transform>(e)->pos[1] = y; scene.world().get<Transform>(e)->pos[2] = z;
    MeshRenderer m; m.shape = shape; scene.world().add<MeshRenderer>(e, m);
    return e;
  }
  tool::Entity makeCamera(bool principal) {
    tool::Entity e = make(principal ? "Main Camera" : "Camera");
    auto* t = scene.world().get<Transform>(e); t->pos[1] = 2.5f; t->pos[2] = 7.f; t->rot[0] = -15.f;
    scene.world().add<CameraComp>(e);
    if (principal) { scene.world().addTag(e, scene.tag("Principal")); scene.world().get<CameraComp>(e)->soundEar = true; }
    return e;
  }
  tool::Entity makeGlobalLight() { tool::Entity e = make("Global Light"); scene.world().get<Transform>(e)->pos[1] = 6.f; scene.world().add<GlobalLight>(e); return e; }
  tool::Entity makePointLight() {
    tool::Entity e = make("Point Light"); scene.world().get<Transform>(e)->pos[1] = 2.5f;
    tool::Light l; l.type = tool::LightType::Point; l.range = 12.f; l.intensity = 18.f; l.color = {1.f, 0.7f, 0.4f}; scene.world().add<LightComp>(e, LightComp{l});
    return e;
  }
  tool::Entity makeAudio(const std::string& clipRel) {
    tool::Entity e = make("Audio Source");
    AudioSource a; ser::copyStr(a.clip, sizeof a.clip, clipRel); scene.world().add<AudioSource>(e, a);
    if (!clipRel.empty()) ser::copyStr(scene.world().get<Name>(e)->text, sizeof(Name::text), fs::path(clipRel).stem().string());
    return e;
  }
  tool::Entity makeModel(const std::string& rel) {
    tool::Vec3 tint{0.8f, 0.8f, 0.8f};
    tool::Entity e = make(fs::path(rel).stem().string().c_str());
    MeshRenderer m; m.shape = 3; ser::copyStr(m.model, sizeof m.model, rel);
    if (lib.mesh(rel, &tint)) { m.color[0] = tint.x; m.color[1] = tint.y; m.color[2] = tint.z; }
    scene.world().add<MeshRenderer>(e, m);
    assignAsset(rel);
    return e;
  }
  void assignAsset(const std::string& rel) { if (std::find(sceneInfo.assets.begin(), sceneInfo.assets.end(), rel) == sceneInfo.assets.end()) sceneInfo.assets.push_back(rel); }
  tool::Entity duplicate(tool::Entity e) {
    tool::Entity c = loadEntity(scene, saveEntity(scene, e));
    cloneScript(c);
    if (auto* t = scene.world().get<Transform>(c)) t->pos[0] += 0.5f;
    if (auto* a = scene.world().get<AudioSource>(c)) a->voice = {};
    return c;
  }
  void destroy(tool::Entity e);                     // apaga o objeto e todos os seus filhos (editor_hier.hpp)
  // --- hierarquia pai/filho (editor_hier.hpp) ---
  tool::Entity parentOf(tool::Entity e);            // pai valido ou {}
  tool::Mat4 worldMat(tool::Entity e);              // matriz de mundo (compoe os pais; sem pai = a propria)
  tool::Vec3 worldPos(tool::Entity e);
  bool isDescendant(tool::Entity e, tool::Entity anc);
  bool setParent(tool::Entity child, tool::Entity parent, bool keepWorld = true); // parent invalido = solta (raiz)
  void bakeWorld(tool::Entity e);                   // grava o transform de mundo no local e solta do pai
  tool::Camera cameraFor(tool::Entity e);           // camera do renderizador a partir do objeto (considera os pais)

  tool::Blueprint enemyBlueprint() {
    tool::Blueprint bp; bp.tag(scene.tag("enemy")).with<Transform>().with<Name>().with<MeshRenderer>();
    return bp;
  }
  void spawnEnemy() {
    auto o = scene.spawn(enemyBlueprint(), time);
    ++created;
    std::snprintf(scene.world().get<Name>(o.entity())->text, sizeof(Name::text), "Enemy %d", created);
    auto* t = scene.world().get<Transform>(o.entity()); t->pos[0] = static_cast<float>((created % 5) - 2) * 1.6f; t->pos[1] = 0.5f; t->pos[2] = static_cast<float>((created / 5) % 3) * -1.6f;
    auto* m = scene.world().get<MeshRenderer>(o.entity()); m->shape = created % 2; m->color[0] = 0.3f + 0.15f * static_cast<float>(created % 5); m->color[1] = 0.8f - 0.1f * static_cast<float>(created % 4);
    selected = o.entity();
    if (playing) onCreated(o.entity(), "Enemy");
  }

  // Cena nova padrao: chao, sol, camera Principal e um cubo.
  void createDefaultContent() {
    sceneInfo.sky.enabled = true;   // cena nova: skybox de montanhas e mar (cenas antigas continuam como estavam)
    tool::Entity g = makeMesh("Ground", 2, 0, 0, 0);
    scene.world().get<Transform>(g)->scale[0] = scene.world().get<Transform>(g)->scale[2] = 14.f;
    { PhysicsComp pc; pc.body = 0; scene.world().add<PhysicsComp>(g, pc); } // o chao e estatico
    auto* gm = scene.world().get<MeshRenderer>(g); gm->color[0] = gm->color[1] = gm->color[2] = 0.55f;
    makeGlobalLight();
    makeCamera(true);
    { tool::Entity cubeE = makeMesh("Cube", 0, 0, 0.5f, 0); scene.world().add<PhysicsComp>(cubeE, PhysicsComp{}); } // cai e colide ao apertar Play
    { tool::Entity fx = make("Particles"); scene.world().get<Transform>(fx)->pos[0] = 3.f; scene.world().get<Transform>(fx)->pos[1] = 0.2f; scene.world().add<ParticleComp>(fx); } // exemplo: fogo
    { tool::Entity ui = make("UI"); UiCanvas u; // exemplo de UI: texto com variavel, barra e botao (camada sobre a camera Principal)
      UiElem& t = u.items[u.count++]; ser::copyStr(t.text, sizeof t.text, "Score: {score}"); t.x = 0.03f; t.y = 0.03f; t.fontScale = 1.4f;
      UiElem& b = u.items[u.count++]; b.kind = 3; b.x = 0.03f; b.y = 0.1f; b.w = 0.25f; b.h = 0.03f; ser::copyStr(b.action, sizeof b.action, "score"); b.maxValue = 10.f; b.color[0] = 0.9f; b.color[1] = 0.2f; b.color[2] = 0.2f;
      UiElem& k = u.items[u.count++]; k.kind = 2; k.x = 0.03f; k.y = 0.16f; k.w = 0.16f; k.h = 0.06f; ser::copyStr(k.text, sizeof k.text, "Add score"); ser::copyStr(k.action, sizeof k.action, "add_score"); k.color[0] = 0.9f; k.color[1] = 0.8f; k.color[2] = 0.3f;
      scene.world().add<UiCanvas>(ui, u); }
    selected = {};
  }

  // ---------------------------------------------------------------- cenas
  void refreshScenes() { sceneList = project.listScenes(); }
  bool saveScene() {
    ensureSceneCode(); openSceneNarrative();
    if (narrative.canvasReady() && !sceneInfo.narrative.empty() && narrative.path() == project.resolve(sceneInfo.narrative).string()) narrative.save();
    const bool ok = saveSceneFile(project, scene, sceneInfo);
    statusLine = ok ? "Scene saved: " + sceneInfo.name : "Failed to save scene";
    if (ok) tool::logInfo(statusLine); else tool::logError(statusLine);
    refreshScenes();
    return ok;
  }
  void preloadSceneAssets() {
    for (auto& a : sceneInfo.assets) {
      const auto k = tool::assets::isModelExtension(a) ? 1 : tool::isAudioExtension(fs::path(a).extension().string()) ? 2 : 0;
      if (k == 1) lib.mesh(a); else if (k == 2) lib.sound(a);
    }
  }
  bool openScene(const std::string& name, bool saveCurrent = true) {
    boundsCache_.clear();   // malhas da cena anterior nao valem mais (o cache e por endereco)
    if (playing) stopPlay();
    if (saveCurrent && !sceneInfo.name.empty() && sceneInfo.name != name) saveScene();  // troca de cena = salva a atual (nao ha "sem salvar")
    SceneFile info; std::string err;
    if (!loadSceneFile(project, scene, name, info, &err)) { statusLine = err; tool::logError(err); return false; }
    sceneInfo = info; selected = {};
    preloadSceneAssets();
    ensureSceneCode(); openSceneNarrative();
    undoReset();
    statusLine = "Scene opened: " + name; tool::logInfo(statusLine);
    return true;
  }
  bool newScene(const std::string& name) {
    if (!safeName(name)) { statusLine = "Invalid scene name"; return false; }
    if (fs::exists(project.scenePath(name))) { statusLine = "A scene with this name already exists"; return false; }
    if (!sceneInfo.name.empty()) saveScene();
    clearScene(scene);
    sceneInfo = SceneFile{}; sceneInfo.name = name;
    createDefaultContent();
    ensureSceneCode(); openSceneNarrative();
    undoReset();
    return saveScene();
  }

  // ---------------------------------------------------------------- cameras
  // --- tags com significado para a engine ("Principal", "player", "enemy") e seus apelidos (Settings > Tags) ---
  // Ids de TODAS as tags que a engine deve tratar como `builtin`: ela mesma + as ligadas a ela. So consulta: nao cria tags.
  std::vector<tool::TagId> tagIdsFor(const std::string& builtin) {
    std::vector<tool::TagId> ids;
    for (auto& n : projSettings.namesFor(builtin)) { const tool::TagId id = scene.tags().find(n); if (id != tool::kInvalidIndex) ids.push_back(id); }
    return ids;
  }
  // O objeto tem a tag pre-definida OU algum apelido ligado a ela? (ex.: hasTagLinked(e, "player") vale para "jogador")
  bool hasTagLinked(tool::Entity e, const std::string& builtin) {
    const tool::TagMask m = scene.world().tagMask(e);
    for (auto id : tagIdsFor(builtin)) if (m.test(id)) return true;
    return false;
  }
  // Renomeia uma tag em todos os objetos (a cena aberta) e nos arquivos das outras cenas; atualiza as ligacoes.
  // Tags pre-definidas ("Principal", "player", "enemy") nao podem ser renomeadas.
  bool renameTag(tool::TagId id, const std::string& to) {
    const std::string from(scene.tags().name(id));
    if (from.empty() || ed_isBuiltin(from)) return false;
    if (!scene.tags().rename(id, to)) return false;
    projSettings.renameAlias(from, to);
    const int n = renameTagInSceneFiles(project, sceneInfo.name, from, to);
    projSettings.save(project);
    statusLine = "Tag renamed: " + from + " -> " + to + (n ? " (" + std::to_string(n) + " other scene file(s) updated)" : "");
    return true;
  }
  static bool ed_isBuiltin(const std::string& n) { return isBuiltinTag(n); }
  std::vector<tool::Entity> principalCameras() {
    std::vector<tool::Entity> out;
    const auto ids = tagIdsFor("Principal");
    scene.world().each<CameraComp, Transform>([&](tool::Entity e, CameraComp&, Transform&) {
      const tool::TagMask m = scene.world().tagMask(e);
      for (auto id : ids) if (m.test(id)) { out.push_back(e); return; }
    });
    std::sort(out.begin(), out.end(), [](tool::Entity a, tool::Entity b) { return a.index < b.index; });
    return out;
  }
  bool isPrincipal(tool::Entity e) { return hasTagLinked(e, "Principal"); }
  void setPrincipal(tool::Entity e, bool on) {
    if (on) { scene.world().addTag(e, scene.tag("Principal")); return; }
    for (auto id : tagIdsFor("Principal")) scene.world().removeTag(e, id); // desligar remove tambem os apelidos
  }

  // ---------------------------------------------------------------- camera da janela Scene (orbita ou navegacao livre)
  tool::Vec3 flyForward() const { return {std::sin(flyYaw) * std::cos(flyPitch) * -1.f, std::sin(flyPitch), -std::cos(flyYaw) * std::cos(flyPitch)}; }
  tool::Camera sceneCamera() const {
    tool::Camera cam; cam.farZ = 500.f;
    if (freeFly) { cam.eye = flyPos; cam.target = flyPos + flyForward(); return cam; }
    cam.target = target;
    cam.eye = {target.x + dist * std::cos(pitch) * std::sin(yaw), target.y + dist * std::sin(pitch), target.z + dist * std::cos(pitch) * std::cos(yaw)};
    return cam;
  }
  // Alt: entra/sai da navegacao livre. Ao entrar, a camera livre comeca exatamente onde a de orbita esta; ao sair, a
  // orbita passa a girar em torno de um ponto a frente (nada "pula").
  void toggleFreeFly() {
    if (!freeFly) {
      const tool::Camera c = sceneCamera(); flyPos = c.eye;
      const tool::Vec3 f = tool::normalize(c.target - c.eye);
      flyPitch = std::asin(std::clamp(f.y, -1.f, 1.f)); flyYaw = std::atan2(-f.x, -f.z);
      freeFly = true;
    } else {
      const tool::Vec3 f = flyForward(); dist = std::max(dist, 3.f);
      target = flyPos + f * dist;
      yaw = std::atan2(-f.x, -f.z); pitch = std::asin(std::clamp(-f.y, -1.f, 1.f)); // eye = target - f*dist
      freeFly = false;
    }
  }
  void flyLook(float dx, float dy) { flyYaw += dx * 0.003f; flyPitch = std::clamp(flyPitch - dy * 0.003f, -1.5f, 1.5f); }
  void flyMove(float dt, float fwd, float right, float up, bool fast) {
    const tool::Vec3 f = flyForward(), r = tool::normalize(tool::cross(f, {0, 1, 0}));
    const float v = flySpeed * (fast ? 3.f : 1.f) * dt;
    flyPos = flyPos + f * (fwd * v) + r * (right * v) + tool::Vec3{0, up * v, 0};
  }
  // Duplo clique na hierarquia: leva a camera do editor ate o objeto (tamanho do objeto define a distancia).
  void focusOn(tool::Entity e) {
    auto* t = scene.world().get<Transform>(e); if (!t) return;
    const tool::Mat4 wm = worldMat(e); float sz = 0.f; for (int c = 0; c < 3; ++c) sz = std::max(sz, std::sqrt(wm.m[c][0] * wm.m[c][0] + wm.m[c][1] * wm.m[c][1] + wm.m[c][2] * wm.m[c][2]));
    const bool big = scene.world().has<MeshRenderer>(e) && scene.world().get<MeshRenderer>(e)->shape == 2; // plano grande: nao afasta demais
    const float radius = std::clamp(big ? 3.f : sz * 0.9f, 0.5f, 30.f);
    const tool::Vec3 p = worldPos(e);
    dist = std::clamp(radius * 3.f, 2.5f, 80.f);
    if (freeFly) flyPos = p - flyForward() * dist; else target = p;
  }

  // ---------------------------------------------------------------- renderizacao
  // Junta as luzes da cena. A primeira Global Light e o "sol": e a unica que projeta sombras.
  // `sunIdx` = posicao do sol em `lights` (-1 se nao ha sol).
  // Esfera (centro + raio) da malha, em espaco local. Cacheada por malha (refeita se a malha for recarregada).
  Bounds meshBounds(const tool::Mesh& m) {
    const size_t n = m.pos.size(); const void* dp = m.pos.data();
    auto it = boundsCache_.find(&m);
    if (it != boundsCache_.end() && it->second.n == n && it->second.data == dp) return it->second.b;
    // Teto de tamanho: o cache indexa pelo endereco da malha; enderecos reaproveitados sao cobertos pela checagem acima.
    if (boundsCache_.size() > 4096) boundsCache_.clear();
    Bounds b;
    if (n) {
      tool::Vec3 lo = m.pos[0], hi = m.pos[0];
      for (const tool::Vec3& p : m.pos) { lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)}; hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)}; }
      b.center = (lo + hi) * 0.5f; float r2 = 0.f;
      for (const tool::Vec3& p : m.pos) { const tool::Vec3 d = p - b.center; r2 = std::max(r2, d.x * d.x + d.y * d.y + d.z * d.z); }
      b.radius = std::sqrt(r2);
    }
    boundsCache_[&m] = BoundsEntry{n, dp, b};
    return b;
  }
  // 6 planos do frustum (normalizados) a partir de view*proj. Usa o plano "near" de GL (-1..1): conservador tambem para D3D (0..1).
  static std::array<tool::Vec4, 6> framePlanes(const tool::Camera& cam, int w, int h) {
    const tool::Mat4 vp = tool::Mat4::perspective(cam.fovY, static_cast<float>(w) / static_cast<float>(std::max(1, h)), cam.nearZ, cam.farZ) * tool::Mat4::lookAt(cam.eye, cam.target, cam.up);
    auto row = [&](int r) { return tool::Vec4{vp.m[0][r], vp.m[1][r], vp.m[2][r], vp.m[3][r]}; };
    const tool::Vec4 r0 = row(0), r1 = row(1), r2 = row(2), r3 = row(3);
    auto mk = [](const tool::Vec4& a, const tool::Vec4& b, float s) { return tool::Vec4{a.x + s * b.x, a.y + s * b.y, a.z + s * b.z, a.w + s * b.w}; };
    std::array<tool::Vec4, 6> pl{mk(r3, r0, 1.f), mk(r3, r0, -1.f), mk(r3, r1, 1.f), mk(r3, r1, -1.f), mk(r3, r2, 1.f), mk(r3, r2, -1.f)};
    for (tool::Vec4& p : pl) { const float l = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z); if (l > 1e-8f) { p.x /= l; p.y /= l; p.z /= l; p.w /= l; } }
    return pl;
  }
  void collectLights(std::vector<tool::Light>& lights, tool::Vec3& ambient, int* sunIdx = nullptr) {
    ambient = {0.03f, 0.03f, 0.04f};
    bool haveSun = false;
    if (sunIdx) *sunIdx = -1;
    scene.world().each<GlobalLight>([&](tool::Entity ge, GlobalLight& g) {
      if (haveSun || scene.world().has<Inactive>(ge)) return; // so a primeira Global Light vale
      haveSun = true;
      tool::Light l = sunLight(g, sunRuntime); l.castShadow = g.castShadow && sceneInfo.render.shadow.enabled;
      if (sunIdx) *sunIdx = static_cast<int>(lights.size());
      lights.push_back(l); ambient = {g.ambient[0], g.ambient[1], g.ambient[2]};
    });
    scene.world().each<LightComp, Transform>([&](tool::Entity le, LightComp& lc, Transform&) { if (scene.world().has<Inactive>(le)) return; tool::Light l = lc.light; l.position = worldPos(le); l.castShadow = false; lights.push_back(l); });
  }
  const tool::Mesh* meshOf(const MeshRenderer& mr, tool::Vec3* tint = nullptr) {
    if (mr.shape == 3) return lib.mesh(mr.model, tint);
    return mr.shape == 1 ? &sphere : mr.shape == 2 ? &plane : &cube;
  }

  // Esfera que envolve todos os objetos que PROJETAM sombra (planos grandes, como o chao, ficam de fora: so recebem).
  // O mapa de sombra cobre so essa regiao -> mais nitidez por texel e zero custo se ninguem projeta sombra.
  // Retorna false se nao ha nenhum projetor.
  bool shadowCasterBounds(tool::Vec3& center, float& radius) {
    tool::Vec3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f}; int n = 0;
    scene.world().each<MeshRenderer, Transform>([&](tool::Entity me, MeshRenderer& mr, Transform& t) {
      if (!mr.castShadow || mr.shape == 2) return;
      const tool::Mat4 wm = worldMat(me); const tool::Vec3 p{wm.m[3][0], wm.m[3][1], wm.m[3][2]};
      float sc = 0.f; for (int c = 0; c < 3; ++c) sc = std::max(sc, std::sqrt(wm.m[c][0] * wm.m[c][0] + wm.m[c][1] * wm.m[c][1] + wm.m[c][2] * wm.m[c][2]));
      (void)t; const float r = sc * 0.9f; // 0.87 = meia diagonal do cubo unitario
      lo = {std::min(lo.x, p.x - r), std::min(lo.y, p.y - r), std::min(lo.z, p.z - r)};
      hi = {std::max(hi.x, p.x + r), std::max(hi.y, p.y + r), std::max(hi.z, p.z + r)};
      ++n;
    });
    if (n == 0) return false;
    center = (lo + hi) * 0.5f;
    radius = std::clamp(tool::length(hi - lo) * 0.5f + 3.f, 4.f, sceneInfo.render.shadow.distance); // +3 m de folga: sombras longas nao cortam na borda
    return true;
  }

  // Renderiza a cena para um buffer RGBA w*h. `highlight` realca o objeto selecionado (so na janela Scene).
  // `zone` = onde contar o tempo no Profiler; `fx` = pos-processamento a aplicar na imagem pronta (nullptr = nenhum).
  const tool::u8* renderView(const tool::Camera& cam, int w, int h, bool highlight, Zone zone = Zone::SceneRender, const tool::PostFx* fx = nullptr) {
    const tool::u8* out = nullptr;
    {
      ZoneTimer zt(prof, zone);
      out = renderScene(cam, w, h, highlight);
    }
    if (out && sceneInfo.sky.enabled && !cam.ortho) { // skybox de imagens: preenche so o fundo (alfa 0), antes do pos-processamento
      ZoneTimer zt(prof, Zone::Sky);
      tool::u8* px = (apiActive != 0 && out == gpuPixels.data()) ? gpuPixels.data() : renderer.pixelsMutable();
      SkyTextures skyTex{};
      for (size_t i = 0; i < 6; ++i) if (sceneInfo.sky.face[i][0]) skyTex[i] = lib.texture(sceneInfo.sky.face[i]); // cache no AssetLib
      applySkybox(px, w, h, cam, sceneInfo.sky, skyTex);
      out = px;
    }
    if (out && fx && fx->active()) {
      ZoneTimer zt(prof, Zone::PostFx);
      tool::u8* px = (apiActive != 0 && out == gpuPixels.data()) ? gpuPixels.data() : renderer.pixelsMutable();
      tool::applyPostFx(px, w, h, *fx);
      out = px;
    }
    return out;
  }

private:
  const tool::u8* renderScene(const tool::Camera& cam, int w, int h, bool highlight) {
    std::vector<tool::Light> lights; tool::Vec3 ambient; int sunIdx = -1;
    // Com skybox ligado, o fundo sai com alfa 0 (o skybox e pintado depois, em renderView). 2D nao usa skybox.
    const float bgAlpha = (sceneInfo.sky.enabled && !cam.ortho) ? 0.f : 1.f;
    collectLights(lights, ambient, &sunIdx);
    // sombras do sol: so se o sol projeta E existe pelo menos um objeto que projeta
    tool::ShadowFrustum fr; bool shadows = false;
    if (sunIdx >= 0 && lights[static_cast<size_t>(sunIdx)].castShadow) {
      tool::Vec3 c; float r = 0;
      if (shadowCasterBounds(c, r)) { fr = tool::makeSunShadow(lights[static_cast<size_t>(sunIdx)].direction, c, r, sceneInfo.render.shadow.resolution); shadows = fr.valid; }
    }
    if (!shadows) for (auto& l : lights) l.castShadow = false;
    // "Sleeper" de sombras: se luz e todos os objetos que projetam sombra estao IGUAIS ao quadro anterior, reaproveita o shadow map.
    std::uint64_t sigNow = 0;
    if (shadows) {
      std::uint64_t hs = 1469598103934665603ull;
      auto mixu = [&](std::uint64_t v) { hs ^= v; hs *= 1099511628211ull; };
      auto mixf = [&](float f) { std::uint32_t b; std::memcpy(&b, &f, 4); mixu(b); };
      for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r) mixf(fr.viewProj.m[c][r]);
      mixu(static_cast<std::uint64_t>(sceneInfo.render.shadow.resolution));
      scene.world().each<MeshRenderer, Transform>([&](tool::Entity e, MeshRenderer& mr, Transform&) {
        if (!mr.castShadow) return;
        const tool::Mesh* m = meshOf(mr); if (!m) return;
        mixu(e.index); mixu(e.generation); mixu(reinterpret_cast<std::uintptr_t>(m)); const tool::Mat4 wm = worldMat(e);
        for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r) mixf(wm.m[c][r]);
      });
      sigNow = hs;
    } else { sigGpuValid = sigCpuValid = false; }
    tool::u64 tris = 0; unsigned draws = 0, casters = 0;
    auto material = [&](tool::Entity e, const MeshRenderer& mr) {
      tool::Material m{{mr.color[0], mr.color[1], mr.color[2]}, mr.roughness, mr.metallic, mr.castShadow, mr.receiveShadow};
      if (mr.texture[0]) m.albedoMap = lib.texture(mr.texture);
      if (highlight && e == selected) m.albedo = m.albedo * 1.25f;
      return m;
    };
    // Frustum culling: pula objetos fora da visao. Objeto que projeta sombra nunca e pulado (a sombra dele pode cair na tela).
    const bool doCull = sceneInfo.render.frustumCull;
    const std::array<tool::Vec4, 6> planes = doCull ? framePlanes(cam, w, h) : std::array<tool::Vec4, 6>{};
    auto skipCulled = [&](tool::Entity e, const tool::Mesh& m, const MeshRenderer& mr) -> bool {
      if (!doCull || (shadows && mr.castShadow)) return false;
      const Bounds b = meshBounds(m); const tool::Mat4 wm = worldMat(e);
      const float sc = std::max({std::hypot(wm.m[0][0], wm.m[0][1], wm.m[0][2]), std::hypot(wm.m[1][0], wm.m[1][1], wm.m[1][2]), std::hypot(wm.m[2][0], wm.m[2][1], wm.m[2][2])});
      const tool::Vec3 c = wm.transformPoint(b.center); const float r = b.radius * sc;
      for (const tool::Vec4& p : planes) if (p.x * c.x + p.y * c.y + p.z * c.z + p.w < -r) { ++prof.counters.culled; return true; }
      return false;
    };
    auto count = [&](const tool::Mesh& m, const tool::Material& mat) { tris += m.idx.size() / 3; ++draws; if (shadows && mat.castShadow) ++casters; };
    ++prof.counters.views; prof.counters.pixels += static_cast<tool::u64>(w) * static_cast<tool::u64>(h);
    // Particulas: em LOTES (buildParticleBatches): quads voltados para a camera, agrupados por cor.
    // Um lote = uma malha dinamica = uma chamada de desenho, com teste de profundidade (fica atras dos objetos).
    buildParticleBatches(cam, w, h);
    const tool::Mat4 identity{};
    if (apiActive != 0) {
      // Protecao: se a GPU reportar erros em quadros seguidos (ex.: memoria esgotada, driver instavel), volta para a CPU
      // em vez de congelar o editor com milhares de mensagens. O motivo fica em Settings > Graphics e no Console.
      const size_t errBefore = tool::debug().count(tool::Severity::Error);
      if (glCtx) SDL_GL_MakeCurrent(gpuWin, glCtx);
      struct Release { Editor& e; ~Release() { if (e.glCtx && e.gpuWin) SDL_GL_MakeCurrent(e.gpuWin, nullptr); } } release{*this};
      gpu.resize(static_cast<tool::u32>(w), static_cast<tool::u32>(h));
      gpu.begin(cam, lights, ambient, {0.02f, 0.025f, 0.035f}, bgAlpha);
      if (shadows) { const bool re = sigGpuValid && sigGpu == sigNow; gpu.setShadow(sceneInfo.render.shadow, fr, re); sigGpu = sigNow; sigGpuValid = true; if (re) ++prof.counters.shadowReused; }
      scene.world().each<MeshRenderer, Transform>([&](tool::Entity e, MeshRenderer& mr, Transform&) {
        if (scene.world().has<Inactive>(e)) return;
        if (const tool::Mesh* m = meshOf(mr)) { if (skipCulled(e, *m, mr)) return; const tool::Material mat = material(e, mr); count(*m, mat); gpu.draw(*m, worldMat(e), mat); }
      });
      for (tool::u32 b = 0; b < 64; ++b) if (!partBatch[b].idx.empty()) { const tool::Material pm{partColor[b], 1.f, 0.f, false, false}; count(partBatch[b], pm); gpu.drawDynamic(0x40000000u + b, partBatch[b], identity, pm); }
      gpu.end();
      gpuDev->present();
      tool::u32 rw = 0, rh = 0;
      const bool readOk = gpu.readback(gpuPixels, rw, rh);
      if (tool::debug().count(tool::Severity::Error) > errBefore) {
        if (++gpuBadFrames >= 5) { applyApi(0); apiStatus = "Software (CPU) - the GPU reported errors in several frames in a row (see Console)"; gpuBadFrames = 0; }
      } else gpuBadFrames = 0;
      if (apiActive != 0 && readOk && static_cast<int>(rw) == w && static_cast<int>(rh) == h) {
        if (std::getenv("TOOL_DUMP")) { // depuracao: media de brilho do quadro (para comparar APIs sem olhar a tela)
          unsigned long long sum = 0; for (size_t i = 0; i < gpuPixels.size(); i += 4) sum += gpuPixels[i] + gpuPixels[i + 1] + gpuPixels[i + 2];
          std::fprintf(stderr, "GPU frame mean brightness: %.1f\n", static_cast<double>(sum) / (static_cast<double>(gpuPixels.size()) / 4.0 * 3.0));
        }
        prof.counters.tris += tris; prof.counters.draws += draws; prof.counters.shadowCasters += casters;
        return gpuPixels.data();
      }
      if (apiActive != 0) { applyApi(0); apiStatus = "Software (CPU) - GPU readback failed"; }
      tris = 0; draws = 0; casters = 0; // a CPU vai contar de novo
    }
    if (static_cast<int>(renderer.width()) != w || static_cast<int>(renderer.height()) != h) renderer.resize(static_cast<tool::u32>(w), static_cast<tool::u32>(h));
    renderer.begin(cam, {0.08f, 0.09f, 0.11f}, bgAlpha);
    renderer.setLights(lights, ambient);
    if (shadows) { const bool re = sigCpuValid && sigCpu == sigNow; renderer.setShadow(sceneInfo.render.shadow, fr, re); sigCpu = sigNow; sigCpuValid = true; if (re) ++prof.counters.shadowReused; }
    scene.world().each<MeshRenderer, Transform>([&](tool::Entity e, MeshRenderer& mr, Transform&) {
      if (scene.world().has<Inactive>(e)) return;
        if (const tool::Mesh* m = meshOf(mr)) { if (skipCulled(e, *m, mr)) return; const tool::Material mat = material(e, mr); count(*m, mat); renderer.draw(*m, worldMat(e), mat); }
    });
    for (tool::u32 b = 0; b < 64; ++b) if (!partBatch[b].idx.empty()) { const tool::Material pm{partColor[b], 1.f, 0.f, false, false}; count(partBatch[b], pm); renderer.draw(partBatch[b], identity, pm); }
    prof.counters.tris += tris; prof.counters.draws += draws; prof.counters.shadowCasters += casters; prof.counters.shadowTris += renderer.shadowTriangles();
    return renderer.pixels();
  }

public:
  // ---------------------------------------------------------------- audio / Play
  void ensureAudio() {
    if (audioTried) return;
    audioTried = true;
    audioDev = std::make_unique<tool::MiniaudioDevice>();
    audioOk = audioDev->start(mixer);
    if (!audioOk) tool::logWarn("No audio output device available: sound is disabled.");
  }
  // O "ouvido": camera Principal marcada como Sound Ear, senao qualquer camera com Sound Ear, senao a camera da janela Scene.
  void updateListener() {
    tool::Entity ear;
    for (auto e : principalCameras()) if (scene.world().get<CameraComp>(e)->soundEar) { ear = e; break; }
    if (!scene.world().alive(ear)) scene.world().each<CameraComp, Transform>([&](tool::Entity e, CameraComp& c, Transform&) { if (!scene.world().alive(ear) && c.soundEar) ear = e; });
    tool::Listener l; float vol = 1.f;
    if (scene.world().alive(ear)) {
      const tool::Camera ec = cameraFor(ear); l.x = ec.eye.x; l.y = ec.eye.y; l.z = ec.eye.z;
      const tool::Vec3 f = tool::normalize(ec.target - ec.eye); const float n = std::sqrt(f.x * f.x + f.z * f.z);
      if (n > 1e-4f) { l.rightX = -f.z / n; l.rightZ = f.x / n; }
      vol = scene.world().get<CameraComp>(ear)->earVolume;
    } else {
      const tool::Camera c = sceneCamera(); l.x = c.eye.x; l.y = c.eye.y; l.z = c.eye.z;
    }
    mixer.setListener(l);
    mixer.setBusVolume(tool::BusId::Master, vol);
  }
  void playSource(tool::Entity e) {
    auto* a = scene.world().get<AudioSource>(e); auto* t = scene.world().get<Transform>(e);
    if (!a || !t || !a->clip[0]) return;
    tool::SoundPtr s = lib.sound(a->clip);
    if (!s) return;
    tool::PlayParams p; p.volume = a->volume; p.loop = a->loop; p.spatial = a->spatial; { const tool::Vec3 wp = worldPos(e); p.x = wp.x; p.y = wp.y; p.z = wp.z; }
    p.minDistance = a->minDistance; p.maxDistance = a->maxDistance;
    mixer.stop(a->voice);
    a->voice = mixer.play(s, p);
  }
  // ---------------------------------------------------------------- desfazer / refazer
  UndoState captureState() {
    int sel = -1, i = 0; scene.world().eachEntity([&](tool::Entity e) { if (e == selected) sel = i; ++i; });
    return {saveAllEntities(scene).dump(), sel};
  }
  void undoReset() { undoStack.reset(captureState()); interactingPrev = false; }
  // Chamado todo quadro com "o usuario esta no meio de uma interacao?" (mouse apertado, campo ativo, gizmo arrastando).
  void undoTick(bool interacting) {
    if (playing) { interactingPrev = false; return; }   // o Play altera a cena: nao entra no historico
    if (interactingPrev && !interacting) undoStack.commit(captureState());
    interactingPrev = interacting;
  }
  void restoreState(const UndoState& st) {
    clearScene(scene); selected = {};
    if (auto j = tool::Json::parse(st.data); j && j->isArray()) {
      const auto ents = loadAllEntities(scene, *j);
      if (st.selected >= 0 && static_cast<size_t>(st.selected) < ents.size()) selected = ents[static_cast<size_t>(st.selected)];
    }
    interactingPrev = false;
    undoStack.rebase(captureState());
  }
  bool undo() { if (playing) return false; undoStack.commit(captureState()); const UndoState* s = undoStack.undo(); if (!s) return false; restoreState(*s); statusLine = "Undo"; return true; }
  bool redo() { if (playing) return false; const UndoState* s = undoStack.redo(); if (!s) return false; restoreState(*s); statusLine = "Redo"; return true; }

  // --- Play: fisica Newtoon + Tools + Input Map (implementado em editor_play.hpp) ---
  Newtoon::Collider colliderFor(tool::Entity e);   // collider de um objeto com PhysicsComp (tamanho = escala)
  void startPhysics();                              // cria o mundo de fisica a partir da cena
  void stepPlay(float dt);                          // entrada -> Tools -> fisica -> sincroniza Transforms -> eventos

  void startPlay() {
    ensureAudio();
    bool logic = false;
    scene.world().each<PhysicsComp>([&](tool::Entity, PhysicsComp&) { logic = true; });
    scene.world().each<ToolsComp>([&](tool::Entity, ToolsComp&) { logic = true; });
    scene.world().each<SpawnerComp>([&](tool::Entity, SpawnerComp&) { logic = true; });   // o Stop precisa desfazer as esferas criadas
    if (logic) { /* snapshot abaixo cobre o bake */ }
    play.clear(); vars.clear(); uiEvents.clear();
    if (logic) { const UndoState st = captureState(); play.snapshot = st.data; play.snapshotSel = st.selected; play.hasSnapshot = true; } // o Play pode mover/apagar objetos: ao parar, volta tudo
    { std::vector<tool::Entity> bodies; scene.world().each<PhysicsComp>([&](tool::Entity e, PhysicsComp&) { if (scene.world().alive(parentOf(e))) bodies.push_back(e); });
      for (tool::Entity e : bodies) bakeWorld(e); } // corpo fisico dentro de um pai: solta do pai (a fisica trabalha em espaco de mundo); Stop restaura
    applyInputMap();
    startPhysics();
    playing = true; paused = false; stepOnce = false; sunRuntime = 0.f; objectInbox.clear(); narrPending.clear();
    updateListener();
    scene.world().each<AudioSource>([&](tool::Entity e, AudioSource& a) { if (a.playOnStart) playSource(e); });
    startNarrative();
    startScripts();
  }
  void stopPlay() {
    narrRun.reset();
    stopScripts();
    playing = false; paused = false; stepOnce = false; sunRuntime = 0.f;
    scene.world().each<AudioSource>([&](tool::Entity, AudioSource& a) { mixer.stop(a.voice); a.voice = {}; });
    if (play.hasSnapshot) { restoreState(UndoState{play.snapshot, play.snapshotSel}); statusLine = "Play stopped: scene restored"; }
    play.clear();
  }
  // Ouve um arquivo de audio do projeto (pre-visualizacao no painel Assets), sem espacializacao.
  void previewSound(const std::string& rel) {
    ensureAudio();
    mixer.stop(previewVoice);
    if (tool::SoundPtr s = lib.sound(rel)) { tool::PlayParams p; mixer.setBusVolume(tool::BusId::Master, 1.f); previewVoice = mixer.play(s, p); }
  }
  void tick(double dt) {
    if (!playing) return;
    if (paused) { if (!stepOnce) return; stepOnce = false; dt = 1.0 / 60.0; }
    float speed = 0.f;
    scene.world().each<GlobalLight>([&](tool::Entity, GlobalLight& g) { if (speed == 0.f && g.dynamic) speed = g.speed; });
    sunRuntime += speed * static_cast<float>(dt);
    // Eventos de UI embutidos: "add_<var>" soma 1 a variavel, "sub_<var>" subtrai 1, "reset_<var>" zera. Narrative/scripts leem os mesmos eventos/variaveis.
    for (const std::string& ev : uiEvents) {
      if (ev.rfind("add_", 0) == 0) vars[ev.substr(4)] += 1.f; else if (ev.rfind("sub_", 0) == 0) vars[ev.substr(4)] -= 1.f; else if (ev.rfind("reset_", 0) == 0) vars[ev.substr(6)] = 0.f;
    }
    std::vector<std::string> evs; evs.swap(uiEvents);
    tickNarrative(static_cast<float>(dt), evs);
    tickScripts(static_cast<float>(dt));
    tickSpawners(static_cast<float>(dt));
    stepPlay(static_cast<float>(dt));
    updateListener();
  }
};

} // namespace editor
#include "editor_hier.hpp"
#include "editor_play.hpp"
#include "scene_logic.hpp"
#include "editor_scripts.hpp"
#include "ecs_test.hpp"
