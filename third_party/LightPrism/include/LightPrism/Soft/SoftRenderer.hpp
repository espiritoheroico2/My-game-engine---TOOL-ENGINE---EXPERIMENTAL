#pragma once
// Renderizador por software de referência (CPU, sem API gráfica): raycast analítico (esferas e planos) para um
// G-buffer, depois iluminação com a MESMA matemática da biblioteca (BRDF de referência, LightingSystem,
// culling em clusters). Serve para:
//   - ver o resultado (PNG) em vez de só passar/falhar;
//   - provar, pixel a pixel, que "luzes do cluster" == "todas as luzes" (o culling não perde luz);
//   - medir quantas avaliações de luz o clustering economiza.
// Ainda sem sombras (Fase 3). Raios secundários/GI não existem: é só iluminação direta + ambiente constante.
#include <cstdint>
#include <vector>

#include "LightPrism/Cluster/ClusterCulling.hpp"
#include "LightPrism/Ibl/Ibl.hpp"
#include "LightPrism/Post/Ssao.hpp"
#include "LightPrism/LightingSystem.hpp"
#include "LightPrism/Mat4.hpp"
#include "LightPrism/Reference/Brdf.hpp"
#include "LightPrism/Shadow/Csm.hpp"

namespace LightPrism::Soft {

struct Material {
    Vec3 albedo{0.8f, 0.8f, 0.8f};
    float metallic = 0.0f;
    float roughness = 0.5f;
};

struct Sphere {
    Vec3 center;
    float radius = 1.0f;
    Material material;
};

struct Plane {
    Vec3 point;
    Vec3 normal{0, 1, 0};
    Material material;
    Material materialB;       // usado nas casas alternadas do xadrez
    float checkerSize = 0.0f; // 0 = sem xadrez
};

struct Scene {
    std::vector<Sphere> spheres;
    std::vector<Plane> planes;
    Vec3 ambient{0.03f, 0.03f, 0.03f}; // multiplicado pelo albedo
};

struct CameraSetup {
    Vec3 eye{0, 2, 5};
    Vec3 target{0, 1, 0};
    float fovYRad = 1.0f;
    float zNear = 0.1f;
    float zFar = 200.0f;
    uint32_t width = 1280;
    uint32_t height = 720;
};

enum class ShadeMode : uint8_t {
    AllLights, // laço sobre todas as luzes (verdade de referência)
    Clustered, // só as luzes da lista do cluster do pixel
};

struct FrameBuffer {
    uint32_t width = 0, height = 0;
    std::vector<Vec3> color;                  // HDR linear
    std::vector<uint8_t> hit;                 // 1 = geometria; 0 = fundo
    std::vector<float> viewDepth;             // -viewPos.z
    std::vector<uint32_t> cluster;            // índice do cluster do pixel (só onde hit)
    std::vector<uint32_t> clusterLightCount;  // luzes na lista do cluster (modo Clustered; 0 no outro)
    std::vector<uint8_t> cascade;             // cascata de sombra usada (255 = fora do alcance / sem sombras)
    std::vector<float> sunVisibility;         // visibilidade do sol [0,1] (1 sem sombras)
};

// Shadow maps do sol gerados por raycast analítico a partir das cascatas do CSM (mesma convenção do HLSL:
// profundidade convencional, 1 = vazio).
struct ShadowSetup {
    CsmGPU csm{};
    uint32_t size = 0;
    std::vector<std::vector<float>> maps;  // uma por cascata, size*size
    CsmDepthMaps View() const {
        CsmDepthMaps v;
        v.size = size;
        for (size_t i = 0; i < maps.size() && i < kMaxCsmCascades; ++i) v.data[i] = maps[i].data();
        return v;
    }
};

// Iluminação indireta (Fase 4). Quando passada ao Render, substitui o ambiente constante de `Scene::ambient`:
// difuso por SH + especular por split-sum, modulados por `ao` (por pixel, opcional); o fundo mostra `background`.
struct IndirectSetup {
    const Ibl::IblData* ibl = nullptr;
    Ibl::EnvFn background;                    // radiância do céu para pixels sem geometria (já em unidades finais)
    const std::vector<float>* ao = nullptr;   // width*height, 1 = livre; nullptr = sem AO
};

// G-buffer exportado para efeitos de tela (SSAO) e para comparações.
struct GBufferView {
    uint32_t width = 0, height = 0;
    std::vector<uint8_t> hit;
    std::vector<float> viewDepth;      // 0 = fundo
    std::vector<Vec3> viewNormal;      // view space, voltada para a câmera
    std::vector<Vec3> worldPos, worldNormal;
};

struct RenderStats {
    double gbufferMs = 0, cullMs = 0, shadeMs = 0;
    uint64_t lightEvaluations = 0;     // chamadas Evaluate*Light feitas no shading
    uint64_t pixelsHit = 0;
    uint32_t clusterCount = 0;
    uint32_t nonEmptyClusters = 0;
    uint32_t maxLightsInCluster = 0;
    double avgLightsPerNonEmptyCluster = 0.0;
    double markMs = 0.0;               // custo (CPU) de marcar clusters ativos
    uint32_t activeClusters = 0;       // com a otimização de clusters ativos ligada
    bool overflow = false;             // lista global estourou: alguns clusters perderam luzes
};

class SoftRenderer {
public:
    SoftRenderer(const Scene& scene, const CameraSetup& camera, uint32_t tileSizePx = 16, uint32_t zSlices = 24,
                 uint32_t maxLightsPerCluster = 64);

    const Mat4& View() const { return m_view; }
    const Mat4& Proj() const { return m_proj; }
    const ClusterGridGPU& Grid() const { return m_grid; }
    uint32_t Width() const { return m_camera.width; }
    uint32_t Height() const { return m_camera.height; }

    // `lights` vem de LightingSystem::BuildFrame (ordenado por prioridade, com orçamento aplicado).
    // `shadows` (opcional): aplica a visibilidade do CSM ao sol.
    // `activeClusters`: culla só os clusters usados pelos pixels (otimização 2). Resultado idêntico nos pixels desenhados.
    bool activeClusters = false;
    void Render(const FrameLightView& lights, ShadeMode mode, FrameBuffer& out, RenderStats& stats,
                const ShadowSetup* shadows = nullptr, const IndirectSetup* indirect = nullptr) const;

    GBufferView ExportGBuffer() const;
    // AO de referência: `rays` raios ponderados por cosseno, comprimento `radius`; um raio que bate a distância t
    // ocluí (1 - t/radius). ao = 1 - média.
    void ComputeRayTracedAO(float radius, uint32_t rays, std::vector<float>& out) const;

private:
    struct GSample {
        bool hit = false;
        Vec3 pos, normal;
        float depth = 0.0f;
        Material mat;
        uint32_t cluster = 0;
    };

    void BuildGBuffer();

    Scene m_scene;
    CameraSetup m_camera;
    Mat4 m_view, m_proj;
    ClusterGridGPU m_grid{};
    std::vector<ClusterAABB> m_aabbs;
    std::vector<GSample> m_gbuffer;
    double m_gbufferMs = 0.0;
};

// Gera os shadow maps (raycast de cada texel ao longo da direção do sol) para as cascatas de `csm`.
ShadowSetup BuildShadowSetup(const Scene& scene, const CsmGPU& csm, const Vec3& sunTravelDir);

// Oclusão analítica do sol (raio de `pos` até a luz contra esferas e planos). Referência para validar os shadow maps.
bool SunOccludedAnalytic(const Scene& scene, const Vec3& pos, const Vec3& sunTravelDir);

// --- Conversão para imagem (RGB8, linhas de cima para baixo) ---------------------------------
std::vector<uint8_t> ToneMapToRgb8(const FrameBuffer& fb);                            // ACES + sRGB
std::vector<uint8_t> HeatmapRgb8(const FrameBuffer& fb, uint32_t maxCount);           // luzes por cluster
std::vector<uint8_t> SliceRgb8(const FrameBuffer& fb, const ClusterGridGPU& grid);    // fatias de profundidade
std::vector<uint8_t> CascadeRgb8(const FrameBuffer& fb);                              // cascata por pixel (tingido)
std::vector<uint8_t> AoRgb8(const std::vector<float>& ao, const std::vector<uint8_t>& hit);          // cinza
std::vector<uint8_t> DiffRgb8(const FrameBuffer& a, const FrameBuffer& b, float gain);// |a-b| * gain

// Maior diferença absoluta por canal (HDR linear) entre dois frames de mesmo tamanho.
double MaxAbsDiff(const FrameBuffer& a, const FrameBuffer& b);

} // namespace LightPrism::Soft
