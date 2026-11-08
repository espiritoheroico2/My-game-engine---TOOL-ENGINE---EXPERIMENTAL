// ToolEngine - iluminacao, agora baseada na biblioteca Light Prism (third_party/LightPrism).
//
// O modelo antigo (Forward+ proprio, atenuacao, cascatas e espaco de clusters) foi REMOVIDO: quem calcula luzes,
// clusters, cascatas de sombra e a BRDF e a LightPrism. Este arquivo so faz a ponte:
//   - Light / LightingSettings: o que a engine guarda na cena e nas configuracoes (Settings > Lighting);
//   - LightRig: converte a cena para as descricoes da LightPrism, monta o quadro (BuildFrame) e as cascatas (ComputeCsm)
//     e calcula a luz ambiente (SH9 da LightPrism).
// Os renderizadores (SoftRenderer na CPU e GpuRenderer no Diligent) recebem um LightFrame e so consomem o resultado.
#pragma once
#include "tool/math/math.hpp"

#include "LightPrism/Ibl/Ibl.hpp"
#include "LightPrism/LightingSystem.hpp"
#include "LightPrism/Quality.hpp"
#include "LightPrism/Post/Ssao.hpp"
#include "LightPrism/Shadow/Csm.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <vector>

namespace tool {

// Tipo de luz da cena (componente LightComp e Global Light). Mesmos valores de antes, para a serializacao continuar lendo.
enum class LightType : u32 { Directional = 0, Point = 1, Spot = 2 };

struct Light {
  LightType type = LightType::Point;
  Vec3 position{}, direction{0, -1, 0}, color{1, 1, 1};
  f32 intensity = 1.f, range = 10.f;   // range = raio em metros (LightPrism: PointLightDesc::radius)
  f32 innerAngle = 0.4f, outerAngle = 0.6f; // radianos (spot)
  bool castShadow = false;             // o sol projeta sombras em cascatas; pontual/spot projetam (ate pointShadowLights, as mais proximas)
};

// LUT da BRDF (split-sum) da LightPrism, 32x32, calculada uma vez. Compartilhada pelo renderizador CPU e pela GPU
// (na GPU vira textura RGBA8: A em R, B em G).
inline const LightPrism::Ibl::BrdfLut& sharedBrdfLut() {
  static const LightPrism::Ibl::BrdfLut lut = LightPrism::Ibl::BuildBrdfLut(32, 512);
  return lut;
}

// Configuracao de iluminacao da CENA (Settings > Lighting, salva no project.json em render.lighting).
// Presets: tier 0 Mobile, 1 Baixo, 2 Medio, 3 Alto (MakeQualitySettings da LightPrism). Com custom = true, os campos
// abaixo valem no lugar do preset (cada um e um parametro direto da LightPrism).
struct LightingSettings {
  int tier = 2;
  bool custom = false;
  bool shadows = true;                  // liga as cascatas de sombra do sol
  // Orcamento e culling (LightingConfig / QualitySettings)
  u32 maxPointLights = 4096, maxSpotLights = 1024;
  f32 autoRadiusCutoff = 0.02f;         // 0 = desligado: encolhe o raio onde I/d^2 cai abaixo deste limiar
  u32 maxLightsPerCluster = 64;
  u32 indicesPerCluster = 8;
  // Sombras em cascata (CsmConfig)
  u32 csmCascades = 3;                  // 1..4
  u32 shadowMapResolution = 2048;       // por cascata, potencia de 2
  u32 pcfTaps = 4;                      // 1, 4 ou 9
  f32 splitLambda = 0.7f;               // 0 = divisao uniforme, 1 = logaritmica
  f32 shadowDistance = 0.f;             // metros; 0 = ate o far da camera
  f32 blendFraction = 0.1f;             // fade entre cascatas (fracao da cascata)
  f32 casterExtension = 60.f;           // metros alem da esfera, na direcao da luz, para incluir quem projeta sombra
  f32 depthBias = 0.05f;                // metros
  f32 normalBias = 1.5f;                // texels da cascata
  bool snapToTexels = true;             // so desligue para depurar (causa tremor nas sombras)
  // Sombras de luzes pontuais e spot (a LightPrism so tem CSM do sol; estas sombras sao da engine).
  // Cada luz com castShadow ganha um mapa de profundidade por face: 6 faces (cubo) para pontual, 1 para spot.
  u32 pointShadowLights = 2;            // 0..kMaxPointShadowLights: quantas luzes projetam sombra (as mais proximas da camera)
  u32 pointShadowResolution = 512;      // por face, potencia de 2 entre 64 e 1024 (GPU: 6 faces x R x R por luz)
  // SSAO = Light Prism Post/Ssao (oclusao ambiente em espaco de tela). Escurece so a luz ambiente, como na Light Prism.
  bool ssao = true;
  u32 ssaoKernel = 16;                  // 1..LightPrism::Ssao::kMaxKernel
  f32 ssaoRadius = 0.5f;                // metros
  f32 ssaoBias = 0.025f;                // metros (mais o biasPerMeter da Light Prism)
  f32 ssaoPower = 1.f;                  // contraste: ao = ao^power
  // Ray tracing (CPU): oclusao ambiente por raios reais (BVH da LightPrism) no lugar do SSAO. So o renderizador de software;
  // sombras do sol e objetos de voxel ainda nao sao ray traced.
  bool rtAo = false;
  u32 rtAoRays = 16;                    // raios por pixel (1..64)
  // Bloom (halo): o brilho acima do limiar (objetos emissivos e superficies muito claras) vira um halo ao redor.
  bool bloom = false;
  f32 bloomThreshold = 0.75f;           // limiar de luminancia linear (0..1)
  f32 bloomStrength = 0.8f;             // quanto do halo soma na imagem
  f32 rtAoRadius = 0.6f;                // metros: objetos mais longe que isso nao escurecem o pixel
  // Luz ambiente (irradiancia uniforme em SH9, pela LightPrism)
  f32 ambientIntensity = 1.f;
  f32 preExposure = 1.f;
};

// Converte as configuracoes da engine nas da LightPrism.
inline LightPrism::QualitySettings makeQuality(const LightingSettings& s) {
  const auto tier = static_cast<LightPrism::QualityTier>(s.tier < 0 ? 0 : (s.tier > 3 ? 3 : s.tier));
  LightPrism::QualitySettings q = LightPrism::MakeQualitySettings(tier);
  if (s.custom) {
    q.csmCascades = std::clamp<u32>(s.csmCascades, 1u, LightPrism::kMaxCsmCascades);
    q.shadowMapResolution = s.shadowMapResolution;
    q.pcfTaps = s.pcfTaps;
    q.maxLightsPerCluster = s.maxLightsPerCluster;
    q.indicesPerCluster = s.indicesPerCluster;
    q.autoRadiusCutoff = s.autoRadiusCutoff;
  }
  return q;
}

inline LightPrism::LightingConfig makeLightingConfig(const LightingSettings& s) {
  LightPrism::LightingConfig c;
  c.maxPointLights = s.maxPointLights;
  c.maxSpotLights = s.maxSpotLights;
  c.quality = makeQuality(s);
  c.autoRadiusCutoff = c.quality.autoRadiusCutoff;
  return c;
}

inline LightPrism::Ssao::SsaoConfig makeSsaoConfig(const LightingSettings& s) {
  LightPrism::Ssao::SsaoConfig c;
  c.kernelSize = std::clamp<u32>(s.ssaoKernel, 1u, LightPrism::Ssao::kMaxKernel);
  c.radius = std::max(0.01f, s.ssaoRadius);
  c.bias = std::max(0.f, s.ssaoBias);
  c.power = std::max(0.1f, s.ssaoPower);
  return c;
}

inline LightPrism::CsmConfig makeCsmConfig(const LightingSettings& s) {
  const LightPrism::QualitySettings q = makeQuality(s);
  LightPrism::CsmConfig c = LightPrism::MakeCsmConfig(q);
  c.splitLambda = s.splitLambda;
  c.shadowDistance = s.shadowDistance;
  c.blendFraction = s.blendFraction;
  c.casterExtension = s.casterExtension;
  c.depthBias = s.depthBias;
  c.normalBias = s.normalBias;
  c.snapToTexels = s.snapToTexels;
  return c;
}

constexpr u32 kMaxPointShadowLights = 4;
constexpr u32 kMaxPointShadowFaces = 6;

// Luz pontual ou spot com sombra neste quadro. vp[f] = projecao * view da face f (cubo: 6 faces, eixos +X,-X,+Y,-Y,+Z,-Z;
// spot: so a face 0). Convencao de profundidade: perspectiva de Mat4::perspective, z/w em 0..1.
struct ShadowLight {
  LightType type = LightType::Point;
  Vec3 position{};
  f32 range = 1.f;
  f32 nearZ = 0.05f;
  u32 faces = 6;
  Mat4 vp[kMaxPointShadowFaces];
};

// Quadro pronto para os renderizadores: copias dos dados da LightPrism (independentes do LightRig).
struct LightFrame {
  std::vector<LightPrism::PointLightGPU> points;
  std::vector<LightPrism::SpotLightGPU> spots;
  LightPrism::DirectionalLightGPU sun{};
  bool hasSun = false;
  LightPrism::GlobalLightingGPU globals{};
  LightPrism::CsmResult csm{};          // csm.gpu.cascadeCount == 0: sem sombras neste quadro
  LightPrism::Ibl::Sh9 ambient{};       // irradiancia da luz ambiente (E/pi, convoluida): multiplicar por albedo
  u32 droppedLights = 0;                // luzes descartadas pelo orcamento
  std::vector<ShadowLight> shadowLights; // luzes pontuais/spot com sombra (engine, fora da LightPrism)
  bool ssaoOn = false;                   // SSAO ligado neste quadro (LightingSettings::ssao)
  LightPrism::Ssao::SsaoConfig ssao{};
  u32 pointShadowResolution = 512;
  bool shadows() const { return csm.gpu.cascadeCount > 0; }
};

// Ponte entre a cena da engine e a LightPrism. Guarda o LightingSystem (orcamento, clusters, handles) entre quadros.
class LightRig {
public:
  LightRig() { rebuild(LightingSettings{}); }

  // Aplica as configuracoes. Recria o LightingSystem so quando o orcamento muda (ele fixa o tamanho na criacao).
  void configure(const LightingSettings& s) {
    const bool budgetChanged = !built_ || s.maxPointLights != cfg_.maxPointLights || s.maxSpotLights != cfg_.maxSpotLights;
    cfg_ = s;
    if (budgetChanged) rebuild(s);
    else sys_->SetQuality(makeQuality(s));
  }
  const LightingSettings& settings() const { return cfg_; }

  // Monta o quadro: luzes da cena + sol + cascatas + ambiente. Chamar uma vez por quadro.
  // `ambient` = cor da luz ambiente (cor * intensidade). `view` = mundo -> camera; `p00`/`p11` = P[0][0], P[1][1] da camera.
  const LightFrame& build(const std::vector<Light>& lights, const Vec3& ambient, const Vec3& cameraPos, const tool::Mat4& view,
                          f32 p00, f32 p11, f32 zNear, f32 zFar) {
    // Luzes pontuais/spot so sao removidas e readicionadas quando a LISTA muda (posicao, cor, raio...). Com a cena
    // parada, os handles do quadro anterior continuam validos e o custo de registro some. O sol e refeito sempre (e barato).
    const u64 lsig = lightsSignature(lights);
    const bool relist = !lightsValid_ || lsig != lightsSig_;
    if (relist) {
      for (auto h : handles_) (void)sys_->RemoveLight(h);
      handles_.clear();
    }
    sys_->ClearDirectionalLight();
    bool haveSun = false; Vec3 sunDir{0, -1, 0};
    for (const Light& l : lights) {
      switch (l.type) {
      case LightType::Point: {
        LightPrism::PointLightDesc d;
        d.position = lp(l.position); d.radius = l.range; d.color = lp(l.color); d.intensity = l.intensity;
        if (relist) handles_.push_back(sys_->AddPointLight(d));
        break;
      }
      case LightType::Spot: {
        LightPrism::SpotLightDesc d;
        d.position = lp(l.position); d.radius = l.range; d.direction = lp(normalize(l.direction));
        d.innerAngleRad = l.innerAngle; d.outerAngleRad = l.outerAngle; d.color = lp(l.color); d.intensity = l.intensity;
        if (relist) handles_.push_back(sys_->AddSpotLight(d));
        break;
      }
      case LightType::Directional:
        if (!haveSun) { // so o primeiro sol vale (a cena tem uma Global Light)
          haveSun = true; sunDir = normalize(l.direction);
          LightPrism::DirectionalLightDesc d;
          d.direction = lp(sunDir); d.color = lp(l.color); d.intensity = l.intensity;
          d.flags = l.castShadow ? LightPrism::LightFlag_CastShadows : LightPrism::LightFlag_None;
          sys_->SetDirectionalLight(d);
        }
        break;
      }
    }
    lightsSig_ = lsig; lightsValid_ = true;
    LightPrism::FrameParams fp;
    fp.cameraPosition = lp(cameraPos);
    fp.preExposure = cfg_.preExposure;
    const LightPrism::FrameLightView& v = sys_->BuildFrame(fp);

    out_.points.assign(v.points, v.points + v.pointCount);
    out_.spots.assign(v.spots, v.spots + v.spotCount);
    out_.sun = v.sun; out_.hasSun = haveSun && v.sun.intensity > 0.f;
    out_.globals = v.globals; out_.droppedLights = v.droppedLights;
    out_.ssaoOn = cfg_.ssao; out_.ssao = makeSsaoConfig(cfg_);
    lastView_ = v;

    // Sombras: so o sol que projeta, com a camera vinda da view e da projecao (a LightPrism so precisa de P00/P11).
    out_.csm = LightPrism::CsmResult{};
    if (cfg_.shadows && out_.hasSun && (v.sun.flags & LightPrism::LightFlag_CastShadows)) {
      LightPrism::CsmCameraInput ci;
      ci.view = toLp(view); ci.proj00 = p00; ci.proj11 = p11; ci.zNear = zNear; ci.zFar = zFar;
      out_.csm = LightPrism::ComputeCsm(makeCsmConfig(cfg_), ci, lp(sunDir));
    }

    // Sombras de luzes pontuais e spot (fora da LightPrism): as N luzes com castShadow mais proximas da camera.
    out_.shadowLights.clear();
    out_.pointShadowResolution = cfg_.pointShadowResolution;
    if (cfg_.pointShadowLights > 0) buildPointShadows(lights, cameraPos);

    // Ambiente: luz uniforme de cor `ambient` vira SH9 de irradiancia (E/pi) pela propria LightPrism. Recalcula so
    // quando a cor muda (ProjectToSh integra o ceu inteiro).
    if (!ambValid_ || ambient.x != ambCache_.x || ambient.y != ambCache_.y || ambient.z != ambCache_.z || cfg_.ambientIntensity != ambIntensityCache_) {
      const LightPrism::Vec3 c = lp(ambient) * cfg_.ambientIntensity;
      out_.ambient = LightPrism::Ibl::ConvolveCosine(LightPrism::Ibl::ProjectToSh([c](const LightPrism::Vec3&) { return c; }, 16));
      ambCache_ = ambient; ambIntensityCache_ = cfg_.ambientIntensity; ambValid_ = true;
    }
    return out_;
  }

  const LightFrame& frame() const { return out_; }
  LightPrism::LightingSystem& system() { return *sys_; }
  // FrameLightView do ultimo build(): ponteiros validos ate o proximo build (usado pelos clusters na GPU).
  const LightPrism::FrameLightView& lastView() const { return lastView_; }

  // Conversoes (tool <-> LightPrism). A engine guarda Mat4 por COLUNA (m[coluna][linha], translacao em m[3][0]);
  // a Light Prism guarda por LINHA (m[linha][coluna], translacao em m[0][3]). Por isso a conversao transpoe.
  static LightPrism::Vec3 lp(const Vec3& v) { return {v.x, v.y, v.z}; }
  static LightPrism::Mat4 toLp(const tool::Mat4& m) {
    LightPrism::Mat4 r;
    for (int c = 0; c < 4; ++c) for (int k = 0; k < 4; ++k) r.m[k][c] = m.m[c][k];
    return r;
  }

private:
  // Monta as faces de cada luz com sombra. Ordem das faces = a de SoftRenderer::shade() (eixo dominante).
  void buildPointShadows(const std::vector<Light>& lights, const Vec3& cameraPos) {
    std::vector<const Light*> cand;
    for (const Light& l : lights)
      if (l.castShadow && l.type != LightType::Directional) cand.push_back(&l);
    std::sort(cand.begin(), cand.end(), [&](const Light* a, const Light* b) {
      const Vec3 da = a->position - cameraPos, db = b->position - cameraPos;
      return dot(da, da) < dot(db, db);
    });
    const u32 limit = std::min<u32>(std::min<u32>(cfg_.pointShadowLights, kMaxPointShadowLights), static_cast<u32>(cand.size()));
    static const Vec3 kDir[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    static const Vec3 kUp[6] = {{0, -1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}, {0, -1, 0}, {0, -1, 0}};
    const f32 fovCube = 1.5707964f; // 90 graus: cada face cobre um eixo
    for (u32 i = 0; i < limit; ++i) {
      const Light& l = *cand[i];
      ShadowLight sl;
      sl.type = l.type; sl.position = l.position; sl.range = std::max(0.05f, l.range); sl.nearZ = 0.05f;
      if (l.type == LightType::Point) {
        sl.faces = 6;
        for (u32 f = 0; f < 6; ++f)
          sl.vp[f] = Mat4::perspective(fovCube, 1.f, sl.nearZ, sl.range) * Mat4::lookAt(l.position, l.position + kDir[f], kUp[f]);
      } else { // spot: uma face com o cone do spot (um pouco mais larga para a borda ficar dentro)
        sl.faces = 1;
        const Vec3 d = normalize(l.direction);
        const Vec3 up = std::abs(d.y) > 0.99f ? Vec3{1, 0, 0} : Vec3{0, 1, 0};
        const f32 fov = std::clamp(2.f * l.outerAngle * 1.05f, 0.1f, 2.9f);
        sl.vp[0] = Mat4::perspective(fov, 1.f, sl.nearZ, sl.range) * Mat4::lookAt(l.position, l.position + d, up);
      }
      out_.shadowLights.push_back(sl);
    }
  }

  void rebuild(const LightingSettings& s) {
    cfg_ = s;
    sys_ = std::make_unique<LightPrism::LightingSystem>(makeLightingConfig(s));
    handles_.clear(); built_ = true; ambValid_ = false; lightsValid_ = false;
  }

  // Assinatura da lista de luzes (tudo que entra na LightPrism por luz pontual/spot).
  static u64 lightsSignature(const std::vector<Light>& lights) {
    u64 h = 1469598103934665603ull;
    auto mixu = [&](u64 v) { h ^= v; h *= 1099511628211ull; };
    auto mixf = [&](f32 f) { u32 b; std::memcpy(&b, &f, 4); mixu(b); };
    for (const Light& l : lights) {
      if (l.type == LightType::Directional) continue;   // o sol nao entra na lista de handles
      mixu(static_cast<u64>(l.type));
      mixf(l.position.x); mixf(l.position.y); mixf(l.position.z);
      mixf(l.direction.x); mixf(l.direction.y); mixf(l.direction.z);
      mixf(l.color.x); mixf(l.color.y); mixf(l.color.z);
      mixf(l.intensity); mixf(l.range); mixf(l.innerAngle); mixf(l.outerAngle);
    }
    mixu(lights.size());
    return h;
  }

  LightingSettings cfg_;
  std::unique_ptr<LightPrism::LightingSystem> sys_;
  std::vector<LightPrism::LightHandle> handles_;
  LightFrame out_;
  LightPrism::FrameLightView lastView_{};
  u64 lightsSig_ = 0; bool lightsValid_ = false;   // lista de luzes registrada na LightPrism (ver build)
  Vec3 ambCache_{};
  f32 ambIntensityCache_ = 0.f;
  bool ambValid_ = false, built_ = false;
};

} // namespace tool
