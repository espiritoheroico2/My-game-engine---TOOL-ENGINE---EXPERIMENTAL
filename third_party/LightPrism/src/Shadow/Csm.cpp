#include "LightPrism/Shadow/Csm.hpp"

#include <algorithm>
#include <cmath>

namespace LightPrism {

CsmResult ComputeCsm(const CsmConfig& cfg, const CsmCameraInput& cam, const Vec3& sunTravelDir) {
    CsmResult out{};
    const float n = cam.zNear;
    const float f = (cfg.shadowDistance > 0.0f) ? std::min(cfg.shadowDistance, cam.zFar) : cam.zFar;
    if (!(n > 0.0f) || !(f > n) || !(cam.proj00 > 0.0f) || !(cam.proj11 > 0.0f) || cfg.shadowMapSize < 2) return out;

    const uint32_t count = std::clamp(cfg.cascadeCount, 1u, kMaxCsmCascades);
    const Vec3 dir = Normalize(sunTravelDir);

    // Splits "práticos": mistura de uniforme e logarítmico.
    float split[kMaxCsmCascades + 1];
    split[0] = n;
    for (uint32_t i = 1; i <= count; ++i) {
        const float p = float(i) / float(count);
        const float logS = n * std::pow(f / n, p);
        const float uniS = n + (f - n) * p;
        split[i] = Lerp(uniS, logS, std::clamp(cfg.splitLambda, 0.0f, 1.0f));
    }
    split[count] = f;

    // Câmera no mundo: view = [R | t]  =>  eye = -Rᵀ t ; forward = -R.row2.
    const Mat4& v = cam.view;
    const Vec3 r0{v.m[0][0], v.m[0][1], v.m[0][2]}, r1{v.m[1][0], v.m[1][1], v.m[1][2]}, r2{v.m[2][0], v.m[2][1], v.m[2][2]};
    const Vec3 t{v.m[0][3], v.m[1][3], v.m[2][3]};
    const Vec3 eye = -(r0 * t.x + r1 * t.y + r2 * t.z);
    const Vec3 fwd = -r2;

    // Rotação mundo->luz FIXA (origem no mundo): o snapping precisa de uma grade que não dependa da câmera.
    const Mat4 Rl = LookAtRH({0, 0, 0}, dir);
    const float invAspect2 = 1.0f / (cam.proj00 * cam.proj00) + 1.0f / (cam.proj11 * cam.proj11);

    CsmGPU& g = out.gpu;
    g.cascadeCount = count;
    g.shadowMapSize = float(cfg.shadowMapSize);
    g.depthBias = cfg.depthBias;
    g.normalBias = cfg.normalBias;
    g.blendFraction = std::clamp(cfg.blendFraction, 0.0f, 0.5f);
    g.pcfTaps = cfg.pcfTaps;

    for (uint32_t i = 0; i < count; ++i) {
        // A cascata i também cobre a faixa de blend da cascata anterior.
        const float prevLen = i > 0 ? split[i] - split[i - 1] : 0.0f;
        const float sn = split[i] - g.blendFraction * prevLen;
        const float sf = split[i + 1];

        // Esfera mínima centrada no eixo da câmera que contém os 8 cantos da fatia [sn, sf].
        const float rn2 = sn * sn * invAspect2, rf2 = sf * sf * invAspect2;
        const float c = std::clamp((sf * sf - sn * sn + rf2 - rn2) / (2.0f * (sf - sn)), sn, sf);
        float radius = std::sqrt(std::max((sf - c) * (sf - c) + rf2, (c - sn) * (c - sn) + rn2));
        radius = std::ceil(radius * 16.0f) / 16.0f;  // estável contra variações minúsculas
        // Folga de 1 texel: o snapping desloca o centro até meio texel em x/y, e sem isto um canto da esfera
        // poderia sair da caixa. radius * size/(size-2) deixa a folga exatamente igual ao texel resultante.
        out.sphereRadiusTight[i] = radius;
        radius *= float(cfg.shadowMapSize) / float(cfg.shadowMapSize - 2);

        const Vec3 centerWorld = eye + fwd * c;
        Vec3 cl = Rl.TransformPoint(centerWorld);
        const float texel = 2.0f * radius / float(cfg.shadowMapSize);
        if (cfg.snapToTexels) {
            cl.x = std::floor(cl.x / texel + 0.5f) * texel;
            cl.y = std::floor(cl.y / texel + 0.5f) * texel;
        }

        // Z também é quantizado (passo radius/16): a matriz inteira só muda quando o centro cruza um passo, o que torna a
        // cascata cacheável (CascadeCache). A margem de meio passo mantém a esfera dentro do volume depois do arredondamento.
        const float zStep = radius / 16.0f;
        if (cfg.snapToTexels) cl.z = std::floor(cl.z / zStep + 0.5f) * zStep;
        const float zMargin = cfg.snapToTexels ? 0.5f * zStep : 0.0f;
        const float dNear = -cl.z - radius - cfg.casterExtension - zMargin;  // distâncias positivas ao longo de -Z da luz
        const float dFar = -cl.z + radius + zMargin;
        const Mat4 ortho = OrthographicRH(cl.x - radius, cl.x + radius, cl.y - radius, cl.y + radius, dNear, dFar);

        CsmCascadeGPU& cc = g.cascades[i];
        cc.viewProj = ortho * Rl;
        cc.splitNear = split[i];
        cc.splitFar = split[i + 1];
        cc.texelWorldSize = texel;
        cc.depthRange = dFar - dNear;
        out.sphereCenter[i] = centerWorld;
        out.sphereRadius[i] = radius;
    }
    return out;
}

struct CsmSystem::Impl {
    CsmConfig cfg;
    CsmResult result{};
    const IGraphicsDevice* boundDevice = nullptr;
    std::unique_ptr<IGpuBuffer> buffer;
};

CsmSystem::CsmSystem(const CsmConfig& config) : m_impl(std::make_unique<Impl>()) { m_impl->cfg = config; }
CsmSystem::~CsmSystem() = default;
CsmSystem::CsmSystem(CsmSystem&&) noexcept = default;
CsmSystem& CsmSystem::operator=(CsmSystem&&) noexcept = default;
void CsmSystem::SetConfig(const CsmConfig& config) { m_impl->cfg = config; }
const CsmConfig& CsmSystem::Config() const { return m_impl->cfg; }
const CsmResult& CsmSystem::Result() const { return m_impl->result; }
IGpuBuffer* CsmSystem::Buffer() const { return m_impl->buffer.get(); }

bool CsmSystem::Update(IGraphicsDevice& device, const FrameLightView& frame, const CsmCameraInput& camera) {
    Impl& s = *m_impl;
    const bool sunCastsShadows = frame.globals.hasSun != 0 && (frame.sun.flags & LightFlag_CastShadows) != 0;
    s.result = sunCastsShadows ? ComputeCsm(s.cfg, camera, frame.sun.direction) : CsmResult{};

    if (s.boundDevice != &device) {
        s.buffer.reset();
        s.boundDevice = &device;
    }
    if (!s.buffer) {
        BufferDesc d;
        d.kind = BufferKind::Uniform;
        d.sizeBytes = sizeof(CsmGPU);
        d.strideBytes = sizeof(CsmGPU);
        d.debugName = "LP_CsmCB";
        s.buffer = device.CreateBuffer(d);
        if (!s.buffer) return false;
    }
    return s.buffer->Update(&s.result.gpu, sizeof(CsmGPU));
}

} // namespace LightPrism
