#include <tool/fidelityfx/fidelityfx.hpp>

#include <tool/debug/debug.hpp>

#if TOOL_WITH_FIDELITYFX
// Os headers do SDK so entram aqui dentro do guard. Usamos a API C (ffx_api.h) e carregamos o loader por LoadLibrary,
// assim o executavel nao depende da DLL para iniciar: sem ela, initVulkan() apenas reporta o motivo.
#include <ffx_api.h>
#include <ffx_api_types.h>
#include <ffx_upscale.h>
#include <vk/ffx_api_vk.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdio>
#include <vector>
#endif

namespace tool::fidelityfx {

namespace {

// Estado do modulo: so existe uma instancia de contexto por processo (suficiente para o editor/teste).
struct State {
  std::string status = "FSR 4: nao inicializado (initVulkan nao foi chamado)";
  std::string version;      // versao em uso
  std::string allVersions;  // todas as versoes listadas pelo driver
  bool fsr4 = false;        // resultado da consulta E contexto criado
  bool nonLinear = true;    // cor de entrada perceptual (sRGB) em vez de linear
#if TOOL_WITH_FIDELITYFX
  HMODULE loader = nullptr;
  PfnFfxCreateContext createContext = nullptr;
  PfnFfxDestroyContext destroyContext = nullptr;
  PfnFfxConfigure configure = nullptr;
  PfnFfxQuery query = nullptr;
  PfnFfxDispatch dispatch = nullptr;
  ffxContext ctx = nullptr;
  VkDeviceContext vkdc{};  // precisa viver tanto quanto o contexto (ponteiros ficam guardados na descricao)
#endif
};

State g;

// Guarda o texto de status e registra no log da engine.
void setStatus(std::string s, bool ok) {
  g.status = std::move(s);
  debug().report(ok ? "TOOL-I210" : "TOOL-W210", ok ? Severity::Info : Severity::Warning, g.status);
}

} // namespace

#if TOOL_WITH_FIDELITYFX

namespace {

// Recebe as mensagens do runtime do FFX e repassa para o log da engine.
void ffxMessage(std::uint32_t type, const wchar_t* msg) {
  char buf[512];
  std::snprintf(buf, sizeof(buf), "[ffx] %ls", msg ? msg : L"");
  debug().report("TOOL-W211", type == FFX_API_MESSAGE_TYPE_ERROR ? Severity::Error : Severity::Warning, buf);
}

// true se o nome da versao comeca (apos qualquer prefixo nao numerico) com o numero 4: "4.1.2", "FSR 4.0.3"...
bool isFsr4Name(const char* name) {
  if (!name) return false;
  for (const char* p = name; *p; ++p)
    if (*p >= '0' && *p <= '9') return *p == '4' && (p[1] == '.' || p[1] == 0 || p[1] == ' ');
  return false;
}

// Descreve uma imagem Vulkan para o FFX (textura 2D de 1 mip).
FfxApiResource makeResource(const NativeImage& im, std::uint32_t state, bool uav) {
  FfxApiResource r{};
  r.resource = im.image;
  r.state = state;
  r.description.type = FFX_API_RESOURCE_TYPE_TEXTURE2D;
  r.description.format = ffxApiGetSurfaceFormatVK(static_cast<VkFormat>(im.vkFormat));
  r.description.width = im.width;
  r.description.height = im.height;
  r.description.depth = 1;
  r.description.mipCount = 1;
  r.description.flags = FFX_API_RESOURCE_FLAGS_NONE;
  r.description.usage = uav ? FFX_API_RESOURCE_USAGE_UAV : FFX_API_RESOURCE_USAGE_READ_ONLY;
  return r;
}

} // namespace

void shutdown() {
  if (g.ctx && g.destroyContext) g.destroyContext(&g.ctx, nullptr);
  g.ctx = nullptr;
  if (g.loader) FreeLibrary(g.loader);
  g = State{};
}

bool initVulkan(const VulkanHandles& h, std::uint32_t maxRenderW, std::uint32_t maxRenderH,
                std::uint32_t maxOutputW, std::uint32_t maxOutputH, bool nonLinearColor) {
  shutdown();
  if (!h.device || !h.physicalDevice) { setStatus("FSR 4: nao suportado, sem VkDevice/VkPhysicalDevice (device nao e Vulkan)", false); return false; }

  // 1) Carrega o loader. Procura primeiro ao lado do executavel e, na falta, na pasta bin do SDK (SetDllDirectory
  // tambem vale para a DLL do upscaler que o loader carrega por dentro).
#ifdef TOOL_FIDELITYFX_BIN_DIR
  SetDllDirectoryA(TOOL_FIDELITYFX_BIN_DIR);
#endif
  g.loader = LoadLibraryA("amd_fidelityfx_loader_vk.dll");
  if (!g.loader) { setStatus("FSR 4: nao suportado, amd_fidelityfx_loader_vk.dll nao encontrada (third_party/fidelityfx/bin)", false); return false; }
  g.createContext = reinterpret_cast<PfnFfxCreateContext>(GetProcAddress(g.loader, "ffxCreateContext"));
  g.destroyContext = reinterpret_cast<PfnFfxDestroyContext>(GetProcAddress(g.loader, "ffxDestroyContext"));
  g.query = reinterpret_cast<PfnFfxQuery>(GetProcAddress(g.loader, "ffxQuery"));
  g.dispatch = reinterpret_cast<PfnFfxDispatch>(GetProcAddress(g.loader, "ffxDispatch"));
  g.configure = reinterpret_cast<PfnFfxConfigure>(GetProcAddress(g.loader, "ffxConfigure"));
  if (!g.createContext || !g.destroyContext || !g.query || !g.dispatch) { setStatus("FSR 4: nao suportado, o loader nao exporta a API ffx", false); return false; }
  if (g.configure) { // mensagens detalhadas do runtime (erros de parametro etc.) vao para o log da engine
    ffxConfigureDescGlobalDebug gd{};
    gd.header.type = FFX_API_CONFIGURE_DESC_TYPE_GLOBALDEBUG;
    gd.effectId = FFX_API_EFFECT_ID_UPSCALE;
    gd.fpMessage = &ffxMessage;
    gd.debugLevel = FFX_API_CONFIGURE_GLOBALDEBUG_LEVEL_VERBOSE;
    g.configure(nullptr, &gd.header);
  }

  // 2) vkGetDeviceProcAddr vem do mesmo vulkan-1.dll que o Diligent ja carregou.
  HMODULE vk = GetModuleHandleA("vulkan-1.dll");
  auto getDevProc = vk ? reinterpret_cast<PFN_vkGetDeviceProcAddr>(GetProcAddress(vk, "vkGetDeviceProcAddr")) : nullptr;
  if (!getDevProc) { setStatus("FSR 4: nao suportado, vkGetDeviceProcAddr indisponivel (vulkan-1.dll nao carregada)", false); return false; }
  g.vkdc.vkDevice = static_cast<VkDevice>(h.device);
  g.vkdc.vkPhysicalDevice = static_cast<VkPhysicalDevice>(h.physicalDevice);
  g.vkdc.vkDeviceProcAddr = getDevProc;

  // 3) Consulta REAL ao driver: quais versoes de upscaler existem neste device. Duas chamadas: contagem e nomes.
  std::uint64_t count = 0;
  ffxQueryDescGetVersions qv{};
  qv.header.type = FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;
  qv.createDescType = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
  qv.device = &g.vkdc;
  qv.outputCount = &count;
  ffxReturnCode_t rc = g.query(nullptr, &qv.header);
  if (rc != FFX_API_RETURN_OK) {
    setStatus("FSR 4: nao suportado, a consulta de versoes falhou (codigo ffx " + std::to_string(rc) + ")", false);
    return false;
  }
  if (count == 0) { setStatus("FSR 4: nao suportado nesta GPU, o driver nao listou nenhuma versao de upscaler", false); return false; }
  std::vector<std::uint64_t> ids(static_cast<size_t>(count));
  std::vector<const char*> names(static_cast<size_t>(count), nullptr);
  qv.versionIds = ids.data();
  qv.versionNames = names.data();
  rc = g.query(nullptr, &qv.header);
  if (rc != FFX_API_RETURN_OK) {
    setStatus("FSR 4: nao suportado, a leitura dos nomes de versao falhou (codigo ffx " + std::to_string(rc) + ")", false);
    return false;
  }
  std::uint64_t chosenId = 0;
  std::string chosenName;
  for (std::uint64_t i = 0; i < count; ++i) {
    const std::string n = names[i] ? names[i] : "?";
    g.allVersions += (i ? ", " : "") + n + " [id " + std::to_string(ids[i]) + "]";
    if (chosenName.empty() && isFsr4Name(names[i])) { chosenId = ids[i]; chosenName = n; }
  }
  if (chosenName.empty()) {
    setStatus("FSR 4: nao suportado nesta GPU; o driver listou apenas: " + g.allVersions, false);
    return false;
  }

  // 4) Cria o contexto de upscale com a versao FSR 4 escolhida. Cadeia: upscale -> backend VK -> override de versao.
  ffxOverrideVersion ov{};
  ov.header.type = FFX_API_DESC_TYPE_OVERRIDE_VERSION;
  ov.versionId = chosenId;
  ffxCreateBackendVKDesc be{};
  be.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_VK;
  be.header.pNext = &ov.header;
  be.deviceContext = g.vkdc;
  // Obrigatorio para usar a API nova do upscaler (FSR 4): informa contra qual versao do header o app foi compilado.
  ffxCreateContextDescUpscaleVersion uv{};
  uv.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE_VERSION;
  uv.header.pNext = &be.header;
  uv.version = FFX_UPSCALER_VERSION;
  ffxCreateContextDescUpscale cd{};
  cd.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
  cd.header.pNext = &uv.header;
  // Auto exposicao: o renderer ainda nao fornece um recurso de exposicao 1x1 (pendencia da proxima rodada).
  cd.flags = FFX_UPSCALE_ENABLE_DEBUG_CHECKING | FFX_UPSCALE_ENABLE_AUTO_EXPOSURE;
  // MEDIDO nesta GPU: o FSR 4 devolve tudo zero se a cor perceptual (RGBA8 UNORM) for declarada como linear.
  if (nonLinearColor) cd.flags |= FFX_UPSCALE_ENABLE_NON_LINEAR_COLORSPACE;
  g.nonLinear = nonLinearColor;
  cd.maxRenderSize = {maxRenderW, maxRenderH};
  cd.maxUpscaleSize = {maxOutputW, maxOutputH};
  cd.fpMessage = &ffxMessage;
  rc = g.createContext(&g.ctx, &cd.header, nullptr);
  if (rc != FFX_API_RETURN_OK || !g.ctx) {
    g.ctx = nullptr;
    setStatus("FSR 4: a consulta listou " + chosenName + " mas ffxCreateContext falhou (codigo ffx " + std::to_string(rc) + ")", false);
    return false;
  }
  g.version = chosenName;
  g.fsr4 = true;
  { // quais recursos esta versao exige / aceita (bits de FfxApiQueryResourceIdentifiers): color=1 depth=2 mv=4 exposure=8 reactive=16 t&c=32
    ffxQueryDescUpscaleGetResourceRequirements rq{};
    rq.header.type = FFX_API_QUERY_DESC_TYPE_UPSCALE_GET_RESOURCE_REQUIREMENTS;
    if (g.query(&g.ctx, &rq.header) == FFX_API_RETURN_OK)
      debug().report("TOOL-I212", Severity::Info, "FSR upscaler: recursos obrigatorios=0x" + std::to_string(rq.required_resources) + " opcionais=0x" + std::to_string(rq.optional_resources));
  }
  setStatus("FSR 4: suportado nesta GPU (versao " + chosenName + ")", true);
  return true;
}

bool supportsFsr4() { return g.fsr4 && g.ctx != nullptr; }

bool queryJitterOffset(int index, int phaseCount, float& x, float& y) {
  if (!g.ctx) return false;
  ffxQueryDescUpscaleGetJitterOffset q{};
  q.header.type = FFX_API_QUERY_DESC_TYPE_UPSCALE_GETJITTEROFFSET;
  q.index = index; q.phaseCount = phaseCount; q.pOutX = &x; q.pOutY = &y;
  return g.query(&g.ctx, &q.header) == FFX_API_RETURN_OK;
}

bool queryJitterPhaseCount(std::uint32_t renderWidth, std::uint32_t outputWidth, int& count) {
  if (!g.ctx) return false;
  std::int32_t c = 0;
  ffxQueryDescUpscaleGetJitterPhaseCount q{};
  q.header.type = FFX_API_QUERY_DESC_TYPE_UPSCALE_GETJITTERPHASECOUNT;
  q.renderWidth = renderWidth; q.displayWidth = outputWidth; q.pOutPhaseCount = &c;
  if (g.query(&g.ctx, &q.header) != FFX_API_RETURN_OK) return false;
  count = c;
  return true;
}

std::uint32_t dispatchUpscale(const UpscalerConfig& c) {
  if (!g.ctx || !g.dispatch) return 0xFFFFFFFFu;
  ffxDispatchDescUpscale d{};
  d.header.type = FFX_API_DISPATCH_DESC_TYPE_UPSCALE;
  d.commandList = c.commandBuffer;
  d.color = makeResource(c.color, FFX_API_RESOURCE_STATE_COMPUTE_READ, false);
  d.depth = makeResource(c.depth, FFX_API_RESOURCE_STATE_COMPUTE_READ, false);
  d.motionVectors = makeResource(c.motionVectors, FFX_API_RESOURCE_STATE_COMPUTE_READ, false);
  d.output = makeResource(c.output, FFX_API_RESOURCE_STATE_UNORDERED_ACCESS, true);
  d.jitterOffset = {c.jitterX, c.jitterY};
  d.motionVectorScale = {c.motionScaleX, c.motionScaleY};
  d.renderSize = {c.renderWidth, c.renderHeight};
  d.upscaleSize = {c.outputWidth, c.outputHeight};
  d.enableSharpening = false;
  d.sharpness = 0.f;
  d.frameTimeDelta = c.deltaTimeMs;
  d.preExposure = 1.f;
  d.reset = c.reset;
  d.cameraNear = c.cameraNear;
  d.cameraFar = c.cameraFar;
  d.cameraFovAngleVertical = c.cameraFovY;
  d.viewSpaceToMetersFactor = 1.f;
  d.flags = g.nonLinear ? FFX_UPSCALE_FLAG_NON_LINEAR_COLOR_SRGB : 0;
  return g.dispatch(&g.ctx, &d.header);
}

#else // sem SDK: tudo desligado

void shutdown() { g = State{}; }
bool initVulkan(const VulkanHandles&, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, bool) {
  setStatus("FSR 4: nao suportado, a build foi feita sem o SDK FidelityFX (third_party/fidelityfx ausente)", false);
  return false;
}
bool supportsFsr4() { return false; }
bool queryJitterOffset(int, int, float&, float&) { return false; }
bool queryJitterPhaseCount(std::uint32_t, std::uint32_t, int&) { return false; }
std::uint32_t dispatchUpscale(const UpscalerConfig&) { return 0xFFFFFFFFu; }

#endif

const std::string& fsr4Status() { return g.status; }
const std::string& fsr4Version() { return g.version; }
const std::string& allUpscalerVersions() { return g.allVersions; }
bool supportsFrameGen() { return false; } // frame generation ainda nao e consultado (proxima rodada)

} // namespace tool::fidelityfx
