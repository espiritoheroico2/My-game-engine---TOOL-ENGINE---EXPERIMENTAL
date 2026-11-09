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
#include "voxel.hpp"
#include "narrative_panel.hpp"
#include "profiler.hpp"
#include "perf_warnings.hpp"
#include "project.hpp"
#include "project_settings.hpp"
#include "script.hpp"
#include "script_host.hpp"
#include "script_check.hpp"
#include "input_map.hpp"
#include "input_sampler.hpp"
#include "play_runtime.hpp"
#include "theme.hpp"
#include "undo.hpp"
#include "tool/audio/decode.hpp"
#include "tool/audio/miniaudio_device.hpp"
#include "tool/render/gpu_renderer.hpp"
#include "tool/render/occlusion.hpp"
#include "tool/render/postfx.hpp"
#include "tool/render/bloom.hpp"
#include "tool/render/upscale.hpp"
#include "tool/render/lighting.hpp"
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
#include <map>
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
  bool showEditMesh = false, showPaintMesh = false;   // janelas de recursos futuros (so aviso de "em desenvolvimento")
  bool showAssets = true, showScenes = true, showNarrative = true, showSettings = true, showInputMap = false;
  bool showEditorInfo = false, showExport = false;   // File > Editor info / Export game
  bool resetLayout = false;
  unsigned sceneLoads = 0;          // conta aberturas de cena: os avisos de desempenho esperam o carregamento acabar
  bool lockInterface = false;  // Interface travada: abas/janelas nao saem do encaixe e os divisores nao se mexem (menu Interface)
  bool dockWithShift = false;  // encaixar/desencaixar janelas so segurando Shift (evita encaixes acidentais)
  bool showCulled = false;     // Scene: mostra em vermelho os objetos que a camera de jogo nao desenha
  bool logKeys = false;        // depuracao: escreve no Console cada tecla e texto digitado
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
  tool::SoftRenderer rendererScene{640, 360}, rendererGame{640, 360};   // um por vista (ver renderView)
  tool::LightRig lightRig;       // Light Prism: luzes, clusters, cascatas e ambiente (um quadro para CPU e GPU)
  tool::Mesh cube = tool::Mesh::cube(0.5f), sphere = tool::Mesh::sphere(0.5f), plane = tool::Mesh::plane(0.5f);
  // Nivel de detalhe: esfera de baixa resolucao (8x5 = ~80 triangulos contra ~576) para objetos longe da camera.
  tool::Mesh sphereFar = tool::Mesh::sphere(0.5f, 8, 5);
  // Malhas dos chunks de blocos, por entidade. Refeitas so quando a revisao do chunk muda.
  std::unordered_map<uint64_t, std::unique_ptr<ChunkMeshes>> chunkCache_;
  // A malha e refeita quando o proprio chunk OU um vizinho muda (o carimbo junta as revisoes de todos).
  ChunkMeshes& chunkMeshesOf(tool::Entity e, const VoxelChunk& c, const ChunkNeighbors& nb = ChunkNeighbors{}) {
    if (chunkCache_.size() > 8192) chunkCache_.clear();   // teto: entidades apagadas nao vazam para sempre
    const uint64_t key = (static_cast<uint64_t>(e.generation) << 32) | e.index;
    auto& p = chunkCache_[key];
    if (!p) p = std::make_unique<ChunkMeshes>();
    uint64_t stamp = c.revision * 1000003ull + static_cast<uint64_t>(c.cell * 1000.f);
    for (const VoxelChunk* n : nb) stamp = stamp * 1315423911ull + (n ? n->revision + 1ull : 0ull);
    if (!p->built || p->stamp != stamp || p->cell != c.cell) { buildChunkMeshes(c, *p, nb); p->stamp = stamp; p->cell = c.cell; }
    return *p;
  }
  // Cria um chunk de blocos. `terrain` = preenche com o terreno de teste. A origem fica na quina (0,0,0) do chunk.
  tool::Entity makeVoxel(const char* name, int cx, int cz, bool terrain) {
    tool::Entity e = make(name);
    VoxelChunk vc; if (terrain) fillTerrain(vc, cx, cz);
    const float span = vc.cell * kChunkSize;
    Transform* t = scene.world().get<Transform>(e); t->pos[0] = static_cast<float>(cx) * span; t->pos[2] = static_cast<float>(cz) * span;
    scene.world().add<VoxelChunk>(e, vc);
    return e;
  }
  // Mapa de teste: side x side chunks de terreno (16x16x16 blocos cada), centrado na origem. Substitui os chunks que
  // ja existem. Cada chunk e uma entidade: o custo cresce com side^2. Use a Draw distance para ver so o que esta perto.
  int generateChunkMap(int side) {
    side = std::clamp(side, 1, 256);
    std::vector<tool::Entity> old;
    scene.world().each<VoxelChunk>([&](tool::Entity e, VoxelChunk&) { old.push_back(e); });
    for (tool::Entity e : old) scene.world().destroy(e);
    selected = {};
    const int half = side / 2;
    for (int z = 0; z < side; ++z)
      for (int x = 0; x < side; ++x) makeVoxel("Chunk", x - half, z - half, true);
    return side * side;
  }
  static constexpr float kSphereFarDistance = 25.f;   // metros; alem disso a esfera usa a malha simples

  tool::Mesh cylinder = tool::Mesh::cylinder(0.5f, 1.f), cone = tool::Mesh::cone(0.5f, 1.f), capsule = tool::Mesh::capsule(0.35f, 1.f),
           wheel = tool::Mesh::wheel(), triPrism = tool::Mesh::prism(3, 0.5f, 1.f), hexPrism = tool::Mesh::prism(6, 0.5f, 1.f); // formas extras (shape 4..9)
  ViewTex sceneTex, gameTex[2];
  float yaw = 0.6f, pitch = 0.35f, dist = 9.f;
  tool::Vec3 target{0, 0.5f, 0};
  bool showGridScene = true, showGridGame = false, showGizmos = true;
  bool showPostInScene = true; // aplicar o pos-processamento GLOBAL tambem na janela Scene
  // Gizmo de transformacao (Scene): 0 = mover (W), 1 = rotacionar (E), 2 = escalar (R); espaco global/local nao e usado (eixos do mundo)
  int gizmoMode = 0;
  bool snapOn = false;       // ima da barra flutuante: o gizmo encaixa na grade de snapStep (igual a Ctrl ao arrastar)
  float snapStep = 0.25f;    // passo da grade do ima, em metros
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
  std::uint64_t sigGpu = 0, sigCpu[2] = {0, 0}; bool sigGpuValid = false, sigCpuValid[2] = {false, false};   // assinatura (casters + luz) do ultimo shadow map, por renderer/vista (sleeper de sombras)
  std::unordered_map<std::string, float> vars;
  // Eventos enviados a objetos (acao "send" do Narrative / futuro script): fila por objeto, limpa a cada Play.
  std::unordered_map<uint32_t, std::vector<std::string>> objectInbox;
  std::vector<std::string> narrPending;   // eventos do Narrative a disparar DEPOIS do quadro (evita reentrar no runtime)
  // Scripts ToolObject (QuickJS): so ativos durante o Play. Pedidos dos scripts entram na fila e sao aplicados depois do quadro.
  ScriptHost scriptHost;
  std::vector<std::pair<std::string, float>> scriptDelayed;   // destroy(nome, segundos) pendentes
  ScriptHost::Entry scriptEntryFor(tool::Entity e);
  void startScripts(); void checkAllScripts(); void tickScripts(float dt); void applyScriptCmds(); void fillScriptInput();
  void scriptCall(ScriptHost::Inst& in, int what, float dt, const std::string& ev);   // what: 1 evento, 2 update
  // copia um objeto (sem gravar arquivos de script novos). newName: nome da copia ("" = mantem o nome);
  // pos: posicao local da copia (nullptr = mantem a do original).
  tool::Entity spawnCopy(const std::string& name, const std::string& newName = std::string(), const float* pos = nullptr);
  void stopScripts() { scriptHost.close(); scriptDelayed.clear(); }
  // Teste de ECS (ecs_test.hpp): cria/atualiza o objeto ECScall com N esferas e da Play.
  bool pushRuntimeBody(tool::Entity e);   // corpo fisico para um objeto criado durante o Play (sem Rebuild)
  void tickSpawners(float dt);
  void startEcsTest(long long n);
  void sendEvent(const std::string& target, const std::string& ev); void setActiveByName(const std::string& n, bool on);
  struct Bounds { tool::Vec3 center{}; float radius = 0.f; };
  struct BoundsEntry { size_t n = 0; const void* data = nullptr; Bounds b; };
  std::unordered_map<const tool::Mesh*, BoundsEntry> boundsCache_;   // esfera de cada malha (para o frustum culling)
  std::unique_ptr<LightPrism::Rt::Bvh> rtBvh_;                         // BVH da cena para o RT AO (CPU); refeito so se a cena mudou
  std::uint64_t rtSig_ = 0; bool rtHave_ = false;
  std::uint64_t psigCpu[2] = {0, 0}; bool psigCpuValid[2] = {false, false};   // assinatura das sombras de pontual/spot (CPU), por vista (0 = Scene, 1 = Game)
  std::uint64_t psigGpu = 0; bool psigGpuValid = false;   // a mesma, para a GPU
 std::vector<std::string> uiEvents;
  struct Particle { tool::Vec3 p, v; float age = 0.f, life = 1.f; float phase = 0.f; tool::Vec3 jit{}; };  // jit: deslocamento do nascimento (local)
  struct ParticleSim { std::vector<Particle> list; float acc = 0.f; uint32_t seed = 12345u; uint32_t touched = 0; };
  uint32_t particleFrame = 0;
  int particleBudget = 100000;                         // maximo de particulas vivas somando todos os emissores
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
  int apiChoice = 0, apiActive = 0;   // apiActive: API em uso; apiChoice: o que esta ativo agora (0 = CPU)
  int apiRequested = 0;               // o que o usuario escolheu (radio e projeto); sobrevive a uma falha de inicializacao
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
  std::vector<tool::u8> gpuPixels[2];          // ultimo quadro lido da GPU, por vista (0 = Scene, 1 = Game)
  int gpuPixW[2] = {0, 0}, gpuPixH[2] = {0, 0};

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
    apiRequested = choice;
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
    if (!gpuDev || !gpu.init(*gpuDev, tool::rhi::createLightPrismDevice(*gpuDev), "assets/shaders", 640, 360)) {
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
    tool::Light l; l.type = tool::LightType::Point; l.range = 12.f; l.intensity = 18.f; l.color = {1.f, 0.7f, 0.4f}; l.castShadow = true; scene.world().add<LightComp>(e, LightComp{l});
    return e;
  }
  tool::Entity makeAudio(const std::string& clipRel) {
    tool::Entity e = make("Audio Source");
    AudioSource a; ser::copyStr(a.clip, sizeof a.clip, clipRel); scene.world().add<AudioSource>(e, a);
    if (!clipRel.empty()) ser::copyStr(scene.world().get<Name>(e)->text, sizeof(Name::text), fs::path(clipRel).stem().string());
    return e;
  }
  // Modelo importado: um objeto "pai" e um objeto por no do arquivo, ligados por Parent (editor_hier.hpp).
  tool::Entity makeModel(const std::string& rel);
  void assignAsset(const std::string& rel) { if (std::find(sceneInfo.assets.begin(), sceneInfo.assets.end(), rel) == sceneInfo.assets.end()) sceneInfo.assets.push_back(rel); }
  tool::Entity duplicate(tool::Entity e) {
    // A copia fica no MESMO pai (o save/load nao leva a ligacao de pai). keepWorld=false: a posicao local e a copiada.
    const tool::Entity par = parentOf(e);
    tool::Entity c = loadEntity(scene, saveEntity(scene, e));
    cloneScript(c);
    if (auto* t = scene.world().get<Transform>(c)) t->pos[0] += 0.5f;
    if (auto* a = scene.world().get<AudioSource>(c)) a->voice = {};
    if (scene.world().alive(par)) setParent(c, par, false);
    return c;
  }
  void destroy(tool::Entity e);                     // apaga o objeto e todos os seus filhos (editor_hier.hpp)
  // --- hierarquia pai/filho (editor_hier.hpp) ---
  tool::Entity parentOf(tool::Entity e);            // pai valido ou {}
  tool::Mat4 worldMat(tool::Entity e);              // matriz de mundo (compoe os pais; sem pai = a propria)
  tool::Vec3 worldPos(tool::Entity e);
  bool findNamed(const std::string& name, tool::Entity& out);  // primeiro objeto com esse nome (Name)
  tool::Vec3 lightDir(tool::Entity le, const LightComp& lc);   // direcao efetiva: sincronizada com o Transform ou a propria
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
    ++sceneLoads;
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
  // Distancia de desenho da cena (Settings > Lighting and shadows): sobrescreve o Far de cada camera quando > 0.
  tool::Camera withDrawDistance(tool::Camera c) const { if (sceneInfo.render.drawDistance > 0.f) c.farZ = sceneInfo.render.drawDistance; return c; }
  tool::Camera sceneCamera() const {
    tool::Camera cam; cam.farZ = sceneInfo.render.drawDistance > 0.f ? sceneInfo.render.drawDistance : 500.f;
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
      tool::Light l = sunLight(g, sunRuntime); l.castShadow = g.castShadow && sceneInfo.render.lighting.shadows;
      if (sunIdx) *sunIdx = static_cast<int>(lights.size());
      lights.push_back(l); ambient = {g.ambient[0], g.ambient[1], g.ambient[2]};
    });
    scene.world().each<LightComp, Transform>([&](tool::Entity le, LightComp& lc, Transform&) { if (scene.world().has<Inactive>(le)) return; tool::Light l = lc.light; l.position = worldPos(le); if (lc.syncRot) l.direction = lightDir(le, lc); lights.push_back(l); }); // castShadow vem do inspector: as N luzes mais proximas recebem sombra (LightRig)
  }
  const tool::Mesh* meshOf(const MeshRenderer& mr, tool::Vec3* tint = nullptr) {
    if (mr.shape == 3) return mr.node >= 0 ? lib.nodeMesh(mr.model, mr.node) : lib.mesh(mr.model, tint);
    switch (mr.shape) {
      case 1: return &sphere;
      case 2: return &plane;
      case 4: return &cylinder;
      case 5: return &cone;
      case 6: return &capsule;
      case 7: return &wheel;
      case 8: return &triPrism;
      case 9: return &hexPrism;
      default: return &cube;
    }
  }

  // Malha de um MeshRenderer escolhida pela distancia da camera. So a esfera tem nivel de detalhe.
  const tool::Mesh* lodMeshOf(tool::Entity e, const MeshRenderer& mr, const tool::Camera& cam) {
    if (mr.shape == 1) {
      const tool::Mat4 wm = worldMat(e);
      const float dx = wm.m[3][0] - cam.eye.x, dy = wm.m[3][1] - cam.eye.y, dz = wm.m[3][2] - cam.eye.z;
      if (dx * dx + dy * dy + dz * dz > kSphereFarDistance * kSphereFarDistance) return &sphereFar;
    }
    return meshOf(mr);
  }

  // Assinatura da geometria que o RT enxerga: objetos visiveis, malhas e transformacoes (muda = BVH refeito).
  std::uint64_t rtSignature() {
    std::uint64_t hs = 1469598103934665603ull;
    auto mixu = [&](std::uint64_t v) { hs ^= v; hs *= 1099511628211ull; };
    auto mixf = [&](float f) { std::uint32_t b; std::memcpy(&b, &f, 4); mixu(b); };
    scene.world().each<MeshRenderer, Transform>([&](tool::Entity e, MeshRenderer& mr, Transform&) {
      if (scene.world().has<Inactive>(e)) return;
      const tool::Mesh* m = meshOf(mr); if (!m) return;
      mixu(e.index); mixu(e.generation); mixu(reinterpret_cast<std::uintptr_t>(m)); mixu(m->idx.size());
      const tool::Mat4 wm = worldMat(e);
      for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r) mixf(wm.m[c][r]);
    });
    return hs;
  }

  // Assinatura do que define o mapa de sombras: sol, objetos que projetam, camera e ajustes da Light Prism.
  // Igual ao quadro anterior = os mapas de sombra sao reaproveitados (ver renderScene).
  std::uint64_t shadowSignature(const tool::Camera& cam, int w, int h, const std::vector<tool::Light>& lights) {
    std::uint64_t hs = 1469598103934665603ull;
    auto mixu = [&](std::uint64_t v) { hs ^= v; hs *= 1099511628211ull; };
    auto mixf = [&](float f) { std::uint32_t b; std::memcpy(&b, &f, 4); mixu(b); };
    const tool::LightingSettings& st = sceneInfo.render.lighting;
    mixu(st.csmCascades); mixu(st.shadowMapResolution); mixu(st.pcfTaps); mixu(st.snapToTexels ? 1u : 0u);
    mixf(st.splitLambda); mixf(st.shadowDistance); mixf(st.blendFraction); mixf(st.casterExtension); mixf(st.depthBias); mixf(st.normalBias);
    for (const tool::Light& l : lights) if (l.type == tool::LightType::Directional) {
      mixf(l.direction.x); mixf(l.direction.y); mixf(l.direction.z); mixf(l.intensity); mixu(l.castShadow ? 1u : 0u);
    }
    for (const tool::Vec3* v : {&cam.eye, &cam.target, &cam.up}) { mixf(v->x); mixf(v->y); mixf(v->z); }
    mixf(cam.fovY); mixf(cam.nearZ); mixf(cam.farZ); mixu(cam.ortho ? 1u : 0u); mixf(cam.orthoHeight);
    mixu(static_cast<std::uint64_t>(w)); mixu(static_cast<std::uint64_t>(h));
    hashCasters(cam, mixu, mixf);
    return hs;
  }

  // Quem projeta sombra (objetos com castShadow e chunks de bloco), com a malha de LOD deste quadro e a transformacao.
  template <class MU, class MF>
  void hashCasters(const tool::Camera& cam, MU&& mixu, MF&& mixf) {
    scene.world().each<MeshRenderer, Transform>([&](tool::Entity e, MeshRenderer& mr, Transform&) {
      if (!mr.castShadow) return;
      const tool::Mesh* m = lodMeshOf(e, mr, cam); if (!m) return;
      mixu(e.index); mixu(e.generation); mixu(reinterpret_cast<std::uintptr_t>(m));
      const tool::Mat4 wm = worldMat(e);
      for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r) mixf(wm.m[c][r]);
    });
    scene.world().each<VoxelChunk, Transform>([&](tool::Entity ce, VoxelChunk& vc, Transform&) {
      if (scene.world().has<Inactive>(ce)) return;
      mixu(ce.index); mixu(ce.generation); mixu(vc.revision); mixf(vc.cell);
    });
  }

  // Assinatura dos mapas de face das luzes pontuais/spot. NAO inclui a camera: so as luzes escolhidas (vp de cada
  // face), a resolucao e os casters. Assim, mover a camera nao refaz essas sombras.
  std::uint64_t pointShadowSignature(const tool::Camera& cam, const tool::LightFrame& lf) {
    std::uint64_t hs = 1469598103934665603ull;
    auto mixu = [&](std::uint64_t v) { hs ^= v; hs *= 1099511628211ull; };
    auto mixf = [&](float f) { std::uint32_t b; std::memcpy(&b, &f, 4); mixu(b); };
    mixu(lf.pointShadowResolution); mixu(lf.shadowLights.size());
    for (const tool::ShadowLight& sl : lf.shadowLights) {
      mixu(static_cast<std::uint64_t>(sl.type)); mixu(sl.faces); mixf(sl.range); mixf(sl.nearZ);
      for (tool::u32 f = 0; f < sl.faces; ++f) for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r) mixf(sl.vp[f].m[c][r]);
    }
    hashCasters(cam, mixu, mixf);
    return hs;
  }

  // Renderiza a cena para um buffer RGBA w*h. `highlight` realca o objeto selecionado (so na janela Scene).
  // `zone` = onde contar o tempo no Profiler; `fx` = pos-processamento a aplicar na imagem pronta (nullptr = nenhum).
  const tool::u8* renderView(const tool::Camera& cam, int w, int h, bool highlight, Zone zone = Zone::SceneRender, const tool::PostFx* fx = nullptr) {
    const tool::u8* out = nullptr;
    const int vi = zone == Zone::GameRender ? 1 : 0;                       // vista: 0 = Scene, 1 = Game
    tool::SoftRenderer& renderer = vi ? rendererGame : rendererScene;      // cada vista tem o seu renderizador CPU
    {
      ZoneTimer zt(prof, zone);
      out = renderScene(cam, w, h, highlight, vi);
    }
    if (out && sceneInfo.sky.enabled && !cam.ortho) { // skybox de imagens: preenche so o fundo (alfa 0), antes do pos-processamento
      ZoneTimer zt(prof, Zone::Sky);
      tool::u8* px = (apiActive != 0 && out == gpuPixels[vi].data()) ? gpuPixels[vi].data() : renderer.pixelsMutable();
      SkyTextures skyTex{};
      for (size_t i = 0; i < 6; ++i) if (sceneInfo.sky.face[i][0]) skyTex[i] = lib.texture(sceneInfo.sky.face[i]); // cache no AssetLib
      applySkybox(px, w, h, cam, sceneInfo.sky, skyTex);
      out = px;
    }
    // Bloom da CPU (o da GPU roda dentro do GpuRenderer, em end()). Depois do ceu e antes do pos-processamento.
    if (out && sceneInfo.render.lighting.bloom && !(apiActive != 0 && out == gpuPixels[vi].data())) {
      ZoneTimer zt(prof, Zone::PostFx);
      const tool::LightingSettings& L = sceneInfo.render.lighting;
      tool::u8* px = renderer.pixelsMutable();
      tool::applyGlowRgba8(px, static_cast<unsigned>(w), static_cast<unsigned>(h), renderer.glowLinear(), L.bloomStrength); // so o que emite brilha
      out = px;
    }
    if (out && fx && fx->active()) {
      ZoneTimer zt(prof, Zone::PostFx);
      tool::u8* px = (apiActive != 0 && out == gpuPixels[vi].data()) ? gpuPixels[vi].data() : renderer.pixelsMutable();
      tool::applyPostFx(px, w, h, *fx);
      out = px;
    }
    return out;
  }

private:
  const tool::u8* renderScene(const tool::Camera& cam, int w, int h, bool highlight, int vi) {
    tool::SoftRenderer& renderer = vi ? rendererGame : rendererScene;   // renderizador e assinatura da vista (0 = Scene, 1 = Game)
    std::vector<tool::Light> lights; tool::Vec3 ambient; int sunIdx = -1;
    // Com skybox ligado, o fundo sai com alfa 0 (o skybox e pintado depois, em renderView). 2D nao usa skybox.
    const float bgAlpha = (sceneInfo.sky.enabled && !cam.ortho) ? 0.f : 1.f;
    collectLights(lights, ambient, &sunIdx);
    // Light Prism: um unico quadro de iluminacao (luzes, ambiente e cascatas de sombra do sol) para CPU e GPU.
    // As cascatas usam a mesma view/projecao da camera que os renderizadores.
    lightRig.configure(sceneInfo.render.lighting);
    const tool::Mat4 view = tool::Mat4::lookAt(cam.eye, cam.target, cam.up);
    const tool::Mat4 proj = cam.projection(static_cast<tool::f32>(w), static_cast<tool::f32>(h));
    const tool::LightFrame& lf = lightRig.build(lights, ambient, cam.eye, view, proj.m[0][0], proj.m[1][1], cam.nearZ, cam.farZ);
    const bool shadows = lf.shadows();
    // "Sleeper" de sombras: se a assinatura do shadow map e igual a do quadro anterior, o renderizador reaproveita os mapas.
    const std::uint64_t sigNow = shadows ? shadowSignature(cam, w, h, lights) : 0;
    if (!shadows) { sigGpuValid = false; sigCpuValid[vi] = false; }   // (renderView tambem usa vi nas assinaturas da GPU)
    // Sombras de pontual/spot: assinatura sem a camera (ver pointShadowSignature). Igual para a GPU e a CPU.
    const bool pointShadows = !lf.shadowLights.empty();
    const std::uint64_t psigNow = pointShadows ? pointShadowSignature(cam, lf) : 0;
    if (!pointShadows) { psigGpuValid = false; psigCpuValid[vi] = false; }
    tool::u64 tris = 0; unsigned draws = 0, casters = 0;
    // Emissao do objeto: cor x intensidade (linear). Vale para todas as partes de um modelo.
    // Sem "Enable emission" o objeto nao emite: nem cor extra nem brilho (bloom).
    auto emissionOf = [](const MeshRenderer& mr) { const float k = mr.emissionOn ? mr.emissiveIntensity : 0.f; return tool::Vec3{mr.emissive[0] * k, mr.emissive[1] * k, mr.emissive[2] * k}; };
    auto material = [&](tool::Entity e, const MeshRenderer& mr) {
      tool::Material m{{mr.color[0], mr.color[1], mr.color[2]}, mr.roughness, mr.metallic, mr.castShadow, mr.receiveShadow};
      m.emissive = emissionOf(mr);
      if (mr.texture[0]) m.albedoMap = lib.texture(mr.texture);
      if (highlight && e == selected) m.albedo = m.albedo * 1.25f;
      return m;
    };
    // Pedacos desenhados por um objeto: um por parte (no de modelo, cada parte com o seu material; a cor editada
    // no inspector sobrepoe o material do arquivo) ou o objeto inteiro (formas e modelos antigos).
    auto pieces = [&](tool::Entity e, const MeshRenderer& mr, auto&& fn) {
      if (mr.shape == 3 && mr.node >= 0) {
        const auto* lm = lib.model(mr.model); if (!lm) return;
        if (static_cast<size_t>(mr.node) >= lm->data.nodes.size()) return;
        const PartOverrides* po = scene.world().get<PartOverrides>(e);
        for (int pi : lm->data.nodes[static_cast<size_t>(mr.node)].parts) {
          const tool::assets::ModelPart& part = lm->data.parts[static_cast<size_t>(pi)];
          tool::Material m = part.material;
          m.castShadow = mr.castShadow; m.receiveShadow = mr.receiveShadow;
          m.emissive = emissionOf(mr);
          if (po) for (const PartOverride& ov : po->items) if (ov.part == pi) { m.albedo = {ov.color[0], ov.color[1], ov.color[2]}; m.roughness = ov.roughness; m.metallic = ov.metallic; }
          if (mr.texture[0]) m.albedoMap = lib.texture(mr.texture);
          if (highlight && e == selected) m.albedo = m.albedo * 1.25f;
          fn(part.mesh, m);
        }
        return;
      }
      if (const tool::Mesh* m = lodMeshOf(e, mr, cam)) fn(*m, material(e, mr));
    };
    // Frustum culling: pula objetos fora da visao. Oclusao: pula objetos inteiramente atras de objetos grandes
    // (so camera em perspectiva; ver tool/render/occlusion.hpp). Objeto que projeta sombra nunca e pulado.
    const bool doCull = sceneInfo.render.frustumCull;
    const std::array<tool::Vec4, 6> planes = doCull ? framePlanes(cam, w, h) : std::array<tool::Vec4, 6>{};
    tool::render::OcclusionBuffer occ;
    if (sceneInfo.render.occlusionCull && !cam.ortho) {
      // Oclusores: triangulos de todos os objetos visiveis. Malhas muito densas ficam de fora (custo por quadro).
      constexpr size_t kOccluderMaxTris = 2048;
      occ.begin(static_cast<tool::u32>(w), static_cast<tool::u32>(h), proj * view);
      scene.world().each<MeshRenderer, Transform>([&](tool::Entity e, MeshRenderer& mr, Transform&) {
        if (scene.world().has<Inactive>(e)) return;
        const tool::Mesh* m = lodMeshOf(e, mr, cam);
        if (!m || m->idx.size() / 3 > kOccluderMaxTris) return;
        const tool::Mat4 wm = worldMat(e);
        for (size_t i = 0; i + 2 < m->idx.size(); i += 3) {
          // Dois triangulos de uma face (a,b,c)+(a,c,d), como em Mesh::cube e Mesh::plane, viram um quad.
          if (i + 5 < m->idx.size() && m->idx[i + 3] == m->idx[i] && m->idx[i + 4] == m->idx[i + 2]) {
            occ.addQuad(wm.transformPoint(m->pos[m->idx[i]]), wm.transformPoint(m->pos[m->idx[i + 1]]), wm.transformPoint(m->pos[m->idx[i + 2]]), wm.transformPoint(m->pos[m->idx[i + 5]]));
            i += 3;
            continue;
          }
          occ.addTriangle(wm.transformPoint(m->pos[m->idx[i]]), wm.transformPoint(m->pos[m->idx[i + 1]]), wm.transformPoint(m->pos[m->idx[i + 2]]));
        }
      });
    }
    auto skipCulled = [&](tool::Entity e, const tool::Mesh& m, const MeshRenderer& mr) -> bool {
      // Quem projeta sombra nunca e pulado: vale para o sol (shadows) e para as sombras de pontual/spot.
      if (!(doCull || occ.active()) || ((shadows || !lf.shadowLights.empty()) && mr.castShadow)) return false;
      const Bounds b = meshBounds(m); const tool::Mat4 wm = worldMat(e);
      const float sc = std::max({std::hypot(wm.m[0][0], wm.m[0][1], wm.m[0][2]), std::hypot(wm.m[1][0], wm.m[1][1], wm.m[1][2]), std::hypot(wm.m[2][0], wm.m[2][1], wm.m[2][2])});
      const tool::Vec3 c = wm.transformPoint(b.center); const float r = b.radius * sc;
      if (doCull) for (const tool::Vec4& p : planes) if (p.x * c.x + p.y * c.y + p.z * c.z + p.w < -r) { ++prof.counters.culled; return true; }
      if (occ.active()) {
        // Caixa do tamanho da esfera (contem a esfera, entao "escondida" aqui vale para o objeto inteiro).
        tool::Vec3 box[8]; int k = 0;
        for (float x : {-r, r}) for (float y : {-r, r}) for (float z : {-r, r}) box[k++] = c + tool::Vec3{x, y, z};
        if (occ.hidden(box)) { ++prof.counters.culled; return true; }
      }
      return false;
    };
    auto count = [&](const tool::Mesh& m, const tool::Material& mat) { tris += m.idx.size() / 3; ++draws; if (shadows && mat.castShadow) ++casters; };
    // Chunks de blocos: uma malha por tipo de bloco, desenhada uma vez. Culling pela esfera do chunk.
    auto drawChunks = [&](auto& target) {
      // Indice da grade: chunk alinhado a um multiplo do seu tamanho tem vizinhos (para o culling de borda).
      std::map<std::array<int, 3>, const VoxelChunk*> grid;
      scene.world().each<VoxelChunk>([&](tool::Entity ce, VoxelChunk& vc) {
        if (scene.world().has<Inactive>(ce)) return;
        const tool::Mat4 wm = worldMat(ce);
        const float s = vc.cell * kChunkSize;
        const float px = wm.m[3][0] / s, py = wm.m[3][1] / s, pz = wm.m[3][2] / s;
        const int gx = static_cast<int>(std::lround(px)), gy = static_cast<int>(std::lround(py)), gz = static_cast<int>(std::lround(pz));
        if (std::fabs(px - gx) < 1e-3f && std::fabs(py - gy) < 1e-3f && std::fabs(pz - gz) < 1e-3f) grid[{gx, gy, gz}] = &vc;
      });
      scene.world().each<VoxelChunk>([&](tool::Entity ce, VoxelChunk& vc) {
        if (scene.world().has<Inactive>(ce)) return;
        const tool::Mat4 wm = worldMat(ce);
        const float s = vc.cell * kChunkSize;
        const int gx = static_cast<int>(std::lround(wm.m[3][0] / s)), gy = static_cast<int>(std::lround(wm.m[3][1] / s)), gz = static_cast<int>(std::lround(wm.m[3][2] / s));
        auto neighborAt = [&](int dx, int dy, int dz) -> const VoxelChunk* { auto it = grid.find({gx + dx, gy + dy, gz + dz}); return it == grid.end() ? nullptr : it->second; };
        const ChunkNeighbors nb = {neighborAt(1, 0, 0), neighborAt(-1, 0, 0), neighborAt(0, 1, 0), neighborAt(0, -1, 0), neighborAt(0, 0, 1), neighborAt(0, 0, -1)};
        if (doCull && !shadows) {
          const float half = vc.cell * kChunkSize * 0.5f, r = half * 1.7321f;   // meia diagonal do chunk
          const tool::Vec3 c = wm.transformPoint({half, half, half});
          for (const tool::Vec4& p : planes) if (p.x * c.x + p.y * c.y + p.z * c.z + p.w < -r) { ++prof.counters.culled; return; }
        }
        ChunkMeshes& cm = chunkMeshesOf(ce, vc, nb);
        for (int t = 1; t < kBlockTypes; ++t) {
          const tool::Mesh& m = cm.byType[t]; if (m.idx.empty()) continue;
          const tool::Material mat{{vc.palette[t][0], vc.palette[t][1], vc.palette[t][2]}, 0.8f, 0.f, true, true};
          count(m, mat); target.draw(m, wm, mat);
        }
      });
    };
    ++prof.counters.views; prof.counters.pixels += static_cast<tool::u64>(w) * static_cast<tool::u64>(h);
    if (apiActive != 0) {
      // Protecao: se a GPU reportar erros em quadros seguidos (ex.: memoria esgotada, driver instavel), volta para a CPU
      // em vez de congelar o editor com milhares de mensagens. O motivo fica em Settings > Graphics e no Console.
      const size_t errBefore = tool::debug().count(tool::Severity::Error);
      if (glCtx) SDL_GL_MakeCurrent(gpuWin, glCtx);
      struct Release { Editor& e; ~Release() { if (e.glCtx && e.gpuWin) SDL_GL_MakeCurrent(e.gpuWin, nullptr); } } release{*this};
      gpu.resize(static_cast<tool::u32>(w), static_cast<tool::u32>(h));
      gpu.begin(cam, {0.02f, 0.025f, 0.035f}, bgAlpha);
      const bool reGpu = shadows && sigGpuValid && sigGpu == sigNow;
      const bool rePointGpu = pointShadows && psigGpuValid && psigGpu == psigNow;
      if (!gpu.setLighting(lightRig, view, proj, reGpu, rePointGpu)) {
        // Light Prism nao conseguiu montar a iluminacao na GPU: a CPU desenha este quadro e os seguintes.
        const std::string why = gpu.lastError();
        applyApi(0); apiStatus = "Software (CPU) - " + why;
      } else {
        if (shadows) { sigGpu = sigNow; sigGpuValid = true; if (gpu.shadowPassSkipped()) ++prof.counters.shadowReused; }
        if (pointShadows) { psigGpu = psigNow; psigGpuValid = true; }
        scene.world().each<MeshRenderer, Transform>([&](tool::Entity e, MeshRenderer& mr, Transform&) {
          if (scene.world().has<Inactive>(e)) return;
          pieces(e, mr, [&](const tool::Mesh& m, const tool::Material& mat) { if (skipCulled(e, m, mr)) return; count(m, mat); gpu.draw(m, worldMat(e), mat); });
        });
        drawChunks(gpu);
        gpu.setBloom(sceneInfo.render.lighting.bloom, sceneInfo.render.lighting.bloomThreshold, sceneInfo.render.lighting.bloomStrength);
        gpu.end();
        gpuDev->present();
        tool::u32 rw = 0, rh = 0;
        // Leitura sem esperar a GPU: o quadro lido pode ser de 1 ou 2 quadros atras (ver readTextureAsync).
        const int rd = gpu.readbackAsync(vi, gpuPixels[vi], rw, rh);
        if (rd == 1) { gpuPixW[vi] = static_cast<int>(rw); gpuPixH[vi] = static_cast<int>(rh); }
        if (tool::debug().count(tool::Severity::Error) > errBefore) {
          if (++gpuBadFrames >= 5) { applyApi(0); apiStatus = "Software (CPU) - the GPU reported errors in several frames in a row (see Console)"; gpuBadFrames = 0; }
        } else gpuBadFrames = 0;
        const bool haveFrame = rd >= 0 && gpuPixW[vi] == w && gpuPixH[vi] == h && !gpuPixels[vi].empty();
        if (apiActive != 0 && haveFrame) {
          if (std::getenv("TOOL_DUMP")) { // depuracao: media de brilho do quadro (para comparar APIs sem olhar a tela)
            const auto& gp = gpuPixels[vi];
            unsigned long long sum = 0; for (size_t i = 0; i < gp.size(); i += 4) sum += gp[i] + gp[i + 1] + gp[i + 2];
            std::fprintf(stderr, "GPU frame mean brightness: %.1f\n", static_cast<double>(sum) / (static_cast<double>(gp.size()) / 4.0 * 3.0));
          }
          prof.counters.tris += tris; prof.counters.draws += draws; prof.counters.shadowCasters += casters;
          return gpuPixels[vi].data();
        }
        // Sem quadro da GPU ainda (primeiros quadros ou tamanho mudou): este quadro sai pela CPU e a API continua.
        if (apiActive != 0 && rd < 0) { applyApi(0); apiStatus = "Software (CPU) - GPU readback failed"; }
      }
      tris = 0; draws = 0; casters = 0; // a CPU vai contar de novo
    }
    // Um renderizador e uma assinatura de sombra por vista: Scene e Game nao apagam o shadow map uma da outra.
    if (static_cast<int>(renderer.width()) != w || static_cast<int>(renderer.height()) != h) renderer.resize(static_cast<tool::u32>(w), static_cast<tool::u32>(h));
    renderer.begin(cam, {0.08f, 0.09f, 0.11f}, bgAlpha);
    const bool reCpu = shadows && sigCpuValid[vi] && sigCpu[vi] == sigNow;
    const bool rePoint = pointShadows && psigCpuValid[vi] && psigCpu[vi] == psigNow;
    renderer.setLighting(lf, reCpu, rePoint);
    if (shadows) { sigCpu[vi] = sigNow; sigCpuValid[vi] = true; if (renderer.shadowPassSkipped()) ++prof.counters.shadowReused; }
    if (pointShadows) { psigCpu[vi] = psigNow; psigCpuValid[vi] = true; } else psigCpuValid[vi] = false;
    scene.world().each<MeshRenderer, Transform>([&](tool::Entity e, MeshRenderer& mr, Transform&) {
      if (scene.world().has<Inactive>(e)) return;
      pieces(e, mr, [&](const tool::Mesh& m, const tool::Material& mat) { if (skipCulled(e, m, mr)) return; count(m, mat); renderer.draw(m, worldMat(e), mat); });
    });
    drawChunks(renderer);
    // Ray tracing (CPU): BVH da LightPrism com os triangulos dos objetos visiveis (voxels de fora, por enquanto).
    // Vive ate o pixels() abaixo, que conclui o desenho e aplica a oclusao.
    const tool::LightingSettings& lset = sceneInfo.render.lighting;
    if (lset.rtAo) {
      const std::uint64_t sig = rtSignature();
      if (!rtBvh_ || sig != rtSig_) {
        std::vector<LightPrism::Rt::Triangle> rtTris;
        scene.world().each<MeshRenderer, Transform>([&](tool::Entity e, MeshRenderer& mr, Transform&) {
          if (scene.world().has<Inactive>(e)) return;
          const tool::Mesh* m = meshOf(mr);
          if (!m) return;
          const tool::Mat4 wm = worldMat(e);
          for (size_t i = 0; i + 2 < m->idx.size(); i += 3) {
            const tool::Vec3 a = wm.transformPoint(m->pos[m->idx[i]]), b = wm.transformPoint(m->pos[m->idx[i + 1]]), c = wm.transformPoint(m->pos[m->idx[i + 2]]);
            rtTris.push_back({{a.x, a.y, a.z}, {b.x, b.y, b.z}, {c.x, c.y, c.z}});
          }
        });
        rtBvh_ = std::make_unique<LightPrism::Rt::Bvh>();
        rtHave_ = !rtTris.empty();
        if (rtHave_) rtBvh_->Build(rtTris);
        rtSig_ = sig;
      }
    } else { rtBvh_.reset(); rtHave_ = false; rtSig_ = 0; }
    LightPrism::Rt::RtAoConfig rtCfg; rtCfg.rays = lset.rtAoRays; rtCfg.radius = lset.rtAoRadius;
    renderer.setRayTracedAo(rtHave_ ? rtBvh_.get() : nullptr, rtCfg);
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
    try { tickScripts(static_cast<float>(dt)); }   // uma falha do sistema de scripts para os scripts, nao o editor
    catch (...) { tool::debug().report("TOOL-S009", tool::Severity::Error, "script system stopped after an internal error; the editor keeps running"); scriptHost.close(); }
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
