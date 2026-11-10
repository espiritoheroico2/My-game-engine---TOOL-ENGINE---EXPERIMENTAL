#include "LightPrism/Soft/SoftRenderer.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

#include "LightPrism/Reference/Tonemap.hpp"

namespace LightPrism::Soft {

namespace {

using Clock = std::chrono::steady_clock;
double MsSince(Clock::time_point t0) { return std::chrono::duration<double, std::milli>(Clock::now() - t0).count(); }

uint8_t ToByte(float v) { return uint8_t(std::lround(Saturate(v) * 255.0f)); }

void PutRgb(std::vector<uint8_t>& img, size_t pixel, const Vec3& c) {
    img[pixel * 3 + 0] = ToByte(c.x);
    img[pixel * 3 + 1] = ToByte(c.y);
    img[pixel * 3 + 2] = ToByte(c.z);
}

// Rampa azul -> ciano -> verde -> amarelo -> vermelho.
Vec3 HeatColor(float t) {
    t = Saturate(t);
    const Vec3 stops[5] = {{0.05f, 0.05f, 0.45f}, {0.0f, 0.7f, 0.9f}, {0.1f, 0.8f, 0.2f}, {0.95f, 0.9f, 0.1f}, {0.9f, 0.1f, 0.05f}};
    const float x = t * 4.0f;
    const int i = std::min(int(x), 3);
    return Lerp(stops[i], stops[i + 1], x - float(i));
}

} // namespace

SoftRenderer::SoftRenderer(const Scene& scene, const CameraSetup& camera, uint32_t tileSizePx, uint32_t zSlices,
                           uint32_t maxLightsPerCluster)
    : m_scene(scene), m_camera(camera) {
    m_view = LookAtRH(camera.eye, camera.target);
    m_proj = PerspectiveRH_ReverseZ(camera.fovYRad, float(camera.width) / float(camera.height), camera.zNear, camera.zFar);

    ClusterGridDesc d;
    d.screenWidth = camera.width; d.screenHeight = camera.height;
    d.tileSizePx = tileSizePx; d.zSlices = zSlices;
    d.zNear = camera.zNear; d.zFar = camera.zFar;
    d.proj00 = m_proj.m[0][0]; d.proj11 = m_proj.m[1][1];
    d.maxLightsPerCluster = maxLightsPerCluster;
    m_grid = MakeClusterGrid(d);
    m_aabbs = BuildAllClusterAABBs(m_grid);

    BuildGBuffer();
}

void SoftRenderer::BuildGBuffer() {
    const auto t0 = Clock::now();
    const uint32_t W = m_camera.width, H = m_camera.height;
    m_gbuffer.assign(size_t(W) * H, GSample{});

    const Vec3 r0{m_view.m[0][0], m_view.m[0][1], m_view.m[0][2]};
    const Vec3 r1{m_view.m[1][0], m_view.m[1][1], m_view.m[1][2]};
    const Vec3 r2{m_view.m[2][0], m_view.m[2][1], m_view.m[2][2]};
    const Vec3 eye = m_camera.eye;

    for (uint32_t y = 0; y < H; ++y) {
        for (uint32_t x = 0; x < W; ++x) {
            const float ndcX = (float(x) + 0.5f) / float(W) * 2.0f - 1.0f;
            const float ndcY = 1.0f - (float(y) + 0.5f) / float(H) * 2.0f;
            const float vx = ndcX / m_proj.m[0][0], vy = ndcY / m_proj.m[1][1];
            // Raio com z de view = -1: o parâmetro t é exatamente a profundidade de view (-viewPos.z).
            const Vec3 dir = r0 * vx + r1 * vy + r2 * -1.0f;

            float bestT = m_camera.zFar;
            GSample best;
            for (const Sphere& s : m_scene.spheres) {
                const Vec3 oc = eye - s.center;
                const float a = Dot(dir, dir), b = Dot(oc, dir), c = Dot(oc, oc) - s.radius * s.radius;
                const float disc = b * b - a * c;
                if (disc < 0.0f) continue;
                const float t = (-b - std::sqrt(disc)) / a;
                if (t > m_camera.zNear && t < bestT) {
                    bestT = t;
                    best.hit = true;
                    best.pos = eye + dir * t;
                    best.normal = Normalize(best.pos - s.center);
                    best.mat = s.material;
                }
            }
            for (const Plane& p : m_scene.planes) {
                const float denom = Dot(dir, p.normal);
                if (std::fabs(denom) < 1e-8f) continue;
                const float t = Dot(p.point - eye, p.normal) / denom;
                if (t > m_camera.zNear && t < bestT) {
                    bestT = t;
                    best.hit = true;
                    best.pos = eye + dir * t;
                    best.normal = p.normal;
                    best.mat = p.material;
                    if (p.checkerSize > 0.0f) {
                        const int cx = int(std::floor(best.pos.x / p.checkerSize));
                        const int cz = int(std::floor(best.pos.z / p.checkerSize));
                        if (((cx + cz) & 1) != 0) best.mat = p.materialB;
                    }
                }
            }
            if (!best.hit) continue;
            best.depth = bestT;
            const uint32_t tx = std::min(x / m_grid.tileSizePx, m_grid.gridX - 1);
            const uint32_t ty = std::min(y / m_grid.tileSizePx, m_grid.gridY - 1);
            best.cluster = ClusterIndexOf(m_grid, tx, ty, SliceFromDepth(m_grid, bestT));
            m_gbuffer[size_t(y) * W + x] = best;
        }
    }
    m_gbufferMs = MsSince(t0);
}

void SoftRenderer::Render(const FrameLightView& lights, ShadeMode mode, FrameBuffer& out, RenderStats& stats,
                          const ShadowSetup* shadows, const IndirectSetup* indirect) const {
    const uint32_t W = m_camera.width, H = m_camera.height;
    out.width = W; out.height = H;
    out.color.assign(size_t(W) * H, Vec3{});
    out.hit.assign(size_t(W) * H, 0);
    out.viewDepth.assign(size_t(W) * H, 0.0f);
    out.cluster.assign(size_t(W) * H, 0);
    out.clusterLightCount.assign(size_t(W) * H, 0);
    out.cascade.assign(size_t(W) * H, 255);
    out.sunVisibility.assign(size_t(W) * H, 1.0f);
    stats = {};
    stats.gbufferMs = m_gbufferMs;
    stats.clusterCount = ClusterCount(m_grid);

    ClusterCullResult cull;
    if (mode == ShadeMode::Clustered) {
        std::vector<uint8_t> mask;
        if (activeClusters) {
            const auto tm = Clock::now();
            std::vector<float> depth(m_gbuffer.size());
            for (size_t i = 0; i < depth.size(); ++i) depth[i] = m_gbuffer[i].hit ? m_gbuffer[i].depth : 0.0f;
            stats.activeClusters = MarkActiveClusters(m_grid, depth.data(), m_camera.width, m_camera.height, mask);
            stats.markMs = MsSince(tm);
        }
        const auto t0 = Clock::now();
        CullLightsCPU(m_grid, m_aabbs, m_view, lights.points, lights.pointCount, lights.spots, lights.spotCount, cull,
                      activeClusters ? mask.data() : nullptr);
        stats.cullMs = MsSince(t0);
        stats.overflow = cull.overflow;
        uint64_t sum = 0;
        for (const ClusterCell& c : cull.cells) {
            if (c.count == 0) continue;
            ++stats.nonEmptyClusters;
            sum += c.count;
            stats.maxLightsInCluster = std::max(stats.maxLightsInCluster, c.count);
        }
        stats.avgLightsPerNonEmptyCluster = stats.nonEmptyClusters ? double(sum) / stats.nonEmptyClusters : 0.0;
    }

    const auto t0 = Clock::now();
    const uint32_t np = lights.pointCount;
    const bool hasSun = lights.globals.hasSun != 0;
    const CsmDepthMaps depthMaps = shadows ? shadows->View() : CsmDepthMaps{};

    for (size_t i = 0; i < m_gbuffer.size(); ++i) {
        const GSample& g = m_gbuffer[i];
        if (!g.hit) {
            if (indirect && indirect->background) {
                const uint32_t px = uint32_t(i % W), py = uint32_t(i / W);
                const float vx = ((float(px) + 0.5f) / float(W) * 2.0f - 1.0f) / m_proj.m[0][0];
                const float vy = (1.0f - (float(py) + 0.5f) / float(H) * 2.0f) / m_proj.m[1][1];
                const Vec3 r0{m_view.m[0][0], m_view.m[0][1], m_view.m[0][2]};
                const Vec3 r1{m_view.m[1][0], m_view.m[1][1], m_view.m[1][2]};
                const Vec3 r2{m_view.m[2][0], m_view.m[2][1], m_view.m[2][2]};
                out.color[i] = indirect->background(Normalize(r0 * vx + r1 * vy - r2));
            } else {
                out.color[i] = Vec3{0.010f, 0.012f, 0.020f};
            }
            continue;
        }
        ++stats.pixelsHit;
        out.hit[i] = 1;
        out.viewDepth[i] = g.depth;
        out.cluster[i] = g.cluster;

        Reference::SurfaceData s;
        s.V = Normalize(m_camera.eye - g.pos);
        s.N = Dot(g.normal, s.V) < 0.0f ? -g.normal : g.normal; // planos de dupla face
        s.albedo = g.mat.albedo;
        s.metallic = g.mat.metallic;
        s.roughness = g.mat.roughness;

        Vec3 c = m_scene.ambient * g.mat.albedo;
        if (indirect && indirect->ibl) {
            const float ao = indirect->ao ? (*indirect->ao)[i] : 1.0f;
            c = Ibl::EvaluateIndirect(*indirect->ibl, s, ao).Total();
        }
        if (hasSun) {
            float vis = 1.0f;
            if (shadows) {
                uint32_t cascade = 255;
                vis = Reference::CsmVisibility(shadows->csm, depthMaps, g.pos, s.N, g.depth, &cascade);
                out.cascade[i] = uint8_t(cascade);
            }
            out.sunVisibility[i] = vis;
            c = c + Reference::EvaluateDirectionalLight(lights.sun, s) * vis;
        }

        if (mode == ShadeMode::AllLights) {
            for (uint32_t k = 0; k < lights.pointCount; ++k) c = c + Reference::EvaluatePointLight(lights.points[k], s, g.pos);
            for (uint32_t k = 0; k < lights.spotCount; ++k) c = c + Reference::EvaluateSpotLight(lights.spots[k], s, g.pos);
            stats.lightEvaluations += uint64_t(lights.pointCount) + lights.spotCount;
        } else {
            const ClusterCell cell = cull.cells[g.cluster];
            out.clusterLightCount[i] = cell.count;
            for (uint32_t k = 0; k < cell.count; ++k) {
                const uint32_t li = cull.indices[cell.offset + k];
                c = c + (li < np ? Reference::EvaluatePointLight(lights.points[li], s, g.pos)
                                 : Reference::EvaluateSpotLight(lights.spots[li - np], s, g.pos));
            }
            stats.lightEvaluations += cell.count;
        }
        out.color[i] = c;
    }
    stats.shadeMs = MsSince(t0);
}

namespace {

// Interseção raio-cena mais próxima com t em (tMin, tMax]; devolve t ou -1.
float NearestHit(const Scene& scene, const Vec3& o, const Vec3& d, float tMin, float tMax) {
    float best = -1.0f;
    for (const Sphere& sp : scene.spheres) {
        const Vec3 oc = o - sp.center;
        const float b = Dot(oc, d), c = Dot(oc, oc) - sp.radius * sp.radius;
        const float disc = b * b - c;  // |d| = 1
        if (disc < 0.0f) continue;
        const float sq = std::sqrt(disc);
        float t = -b - sq;
        if (t <= tMin) t = -b + sq;
        if (t > tMin && t <= tMax && (best < 0.0f || t < best)) best = t;
    }
    for (const Plane& pl : scene.planes) {
        const float denom = Dot(d, pl.normal);
        if (std::fabs(denom) < 1e-8f) continue;
        const float t = Dot(pl.point - o, pl.normal) / denom;
        if (t > tMin && t <= tMax && (best < 0.0f || t < best)) best = t;
    }
    return best;
}

} // namespace

GBufferView SoftRenderer::ExportGBuffer() const {
    GBufferView v;
    const size_t n = m_gbuffer.size();
    v.width = m_camera.width; v.height = m_camera.height;
    v.hit.assign(n, 0); v.viewDepth.assign(n, 0.0f);
    v.viewNormal.assign(n, Vec3{0, 0, 1}); v.worldPos.assign(n, Vec3{}); v.worldNormal.assign(n, Vec3{0, 1, 0});
    for (size_t i = 0; i < n; ++i) {
        const GSample& g = m_gbuffer[i];
        if (!g.hit) continue;
        const Vec3 V = Normalize(m_camera.eye - g.pos);
        const Vec3 N = Dot(g.normal, V) < 0.0f ? -g.normal : g.normal;
        v.hit[i] = 1; v.viewDepth[i] = g.depth; v.worldPos[i] = g.pos; v.worldNormal[i] = N;
        v.viewNormal[i] = Normalize(m_view.TransformDirection(N));
    }
    return v;
}

void SoftRenderer::ComputeRayTracedAO(float radius, uint32_t rays, std::vector<float>& out) const {
    const size_t n = m_gbuffer.size();
    out.assign(n, 1.0f);
    for (size_t i = 0; i < n; ++i) {
        const GSample& g = m_gbuffer[i];
        if (!g.hit) continue;
        const Vec3 V = Normalize(m_camera.eye - g.pos);
        const Vec3 N = Dot(g.normal, V) < 0.0f ? -g.normal : g.normal;
        const Vec3 up = std::fabs(N.y) < 0.99f ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
        const Vec3 T = Normalize(Cross(up, N)), B = Cross(N, T);
        // rotação por pixel evita padrão; a contagem alta mantém o ruído desprezível
        const uint32_t h = uint32_t(i) * 2654435761u;
        const float rot = float(h >> 8) / float(1u << 24);
        float occ = 0.0f;
        for (uint32_t k = 0; k < rays; ++k) {
            const float u1 = std::fmod((float(k) + 0.5f) / float(rays) + rot, 1.0f);
            uint32_t bits = k;
            bits = (bits << 16u) | (bits >> 16u); bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
            bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u); bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
            bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
            const float u2 = float(bits) * 2.3283064365386963e-10f;
            const float phi = 2.0f * kPi * u1, st = std::sqrt(u2), ct = std::sqrt(1.0f - u2);
            const Vec3 d = T * (st * std::cos(phi)) + B * (st * std::sin(phi)) + N * ct;
            const float hitT = NearestHit(m_scene, g.pos + N * 1e-3f, d, 0.0f, radius);
            if (hitT >= 0.0f) occ += 1.0f - hitT / radius;  // atenuação linear com a distância
        }
        out[i] = 1.0f - occ / float(rays);
    }
}

ShadowSetup BuildShadowSetup(const Scene& scene, const CsmGPU& csm, const Vec3& sunTravelDir) {
    ShadowSetup out;
    out.csm = csm;
    out.size = uint32_t(csm.shadowMapSize);
    const Vec3 dir = Normalize(sunTravelDir);
    const size_t N = out.size;
    out.maps.assign(csm.cascadeCount, std::vector<float>(N * N, 1.0f));
    for (uint32_t k = 0; k < csm.cascadeCount; ++k) {
        const CsmCascadeGPU& c = csm.cascades[k];
        const Mat4 inv = InverseAffine(c.viewProj);
        for (size_t j = 0; j < N; ++j)
            for (size_t i = 0; i < N; ++i) {
                const float cx = (float(i) + 0.5f) / float(N) * 2.0f - 1.0f;
                const float cy = 1.0f - (float(j) + 0.5f) / float(N) * 2.0f;
                const Vec3 origin = inv.TransformPoint({cx, cy, 0.0f});  // plano near da ortográfica
                const float t = NearestHit(scene, origin, dir, 0.0f, c.depthRange);
                if (t >= 0.0f) out.maps[k][j * N + i] = t / c.depthRange;
            }
    }
    return out;
}

bool SunOccludedAnalytic(const Scene& scene, const Vec3& pos, const Vec3& sunTravelDir) {
    const Vec3 toSun = -Normalize(sunTravelDir);
    return NearestHit(scene, pos, toSun, 1e-3f, 1e6f) >= 0.0f;
}

std::vector<uint8_t> ToneMapToRgb8(const FrameBuffer& fb) {
    std::vector<uint8_t> img(fb.color.size() * 3);
    for (size_t i = 0; i < fb.color.size(); ++i) PutRgb(img, i, Reference::TonemapToSRGB(fb.color[i]));
    return img;
}

std::vector<uint8_t> HeatmapRgb8(const FrameBuffer& fb, uint32_t maxCount) {
    std::vector<uint8_t> img(fb.color.size() * 3, 0);
    const float inv = 1.0f / float(std::max(maxCount, 1u));
    for (size_t i = 0; i < fb.color.size(); ++i)
        if (fb.hit[i]) PutRgb(img, i, HeatColor(float(fb.clusterLightCount[i]) * inv));
    return img;
}

std::vector<uint8_t> SliceRgb8(const FrameBuffer& fb, const ClusterGridGPU& grid) {
    std::vector<uint8_t> img(fb.color.size() * 3, 0);
    for (size_t i = 0; i < fb.color.size(); ++i) {
        if (!fb.hit[i]) continue;
        const uint32_t slice = SliceFromDepth(grid, fb.viewDepth[i]);
        const float t = float(slice) / float(std::max(grid.gridZ - 1, 1u));
        const Vec3 c = HeatColor(t);
        PutRgb(img, i, (slice & 1u) ? c : c * 0.7f); // fatias alternadas ficam distinguíveis
    }
    return img;
}

std::vector<uint8_t> CascadeRgb8(const FrameBuffer& fb) {
    static const Vec3 tint[5] = {{1.0f, 0.35f, 0.35f}, {0.35f, 1.0f, 0.35f}, {0.4f, 0.55f, 1.0f}, {1.0f, 0.95f, 0.3f}, {0.5f, 0.5f, 0.5f}};
    std::vector<uint8_t> img = ToneMapToRgb8(fb);
    for (size_t i = 0; i < fb.color.size(); ++i) {
        if (!fb.hit[i]) continue;
        const Vec3 t = tint[fb.cascade[i] < 4 ? fb.cascade[i] : 4];
        for (int k = 0; k < 3; ++k) {
            const float base = float(img[i * 3 + size_t(k)]) / 255.0f;
            const float tc = k == 0 ? t.x : (k == 1 ? t.y : t.z);
            img[i * 3 + size_t(k)] = ToByte(base * 0.55f + tc * 0.45f * (0.35f + 0.65f * base));
        }
    }
    return img;
}

std::vector<uint8_t> AoRgb8(const std::vector<float>& ao, const std::vector<uint8_t>& hit) {
    std::vector<uint8_t> img(ao.size() * 3);
    for (size_t i = 0; i < ao.size(); ++i) PutRgb(img, i, hit[i] ? Vec3{ao[i], ao[i], ao[i]} : Vec3{0.1f, 0.1f, 0.2f});
    return img;
}

std::vector<uint8_t> DiffRgb8(const FrameBuffer& a, const FrameBuffer& b, float gain) {
    std::vector<uint8_t> img(a.color.size() * 3, 0);
    if (a.color.size() != b.color.size()) return img;
    for (size_t i = 0; i < a.color.size(); ++i) {
        const Vec3 d = a.color[i] - b.color[i];
        PutRgb(img, i, Vec3{std::fabs(d.x), std::fabs(d.y), std::fabs(d.z)} * gain);
    }
    return img;
}

double MaxAbsDiff(const FrameBuffer& a, const FrameBuffer& b) {
    if (a.color.size() != b.color.size()) return 1e30;
    double m = 0.0;
    for (size_t i = 0; i < a.color.size(); ++i) {
        const Vec3 d = a.color[i] - b.color[i];
        m = std::max({m, double(std::fabs(d.x)), double(std::fabs(d.y)), double(std::fabs(d.z))});
    }
    return m;
}

} // namespace LightPrism::Soft
