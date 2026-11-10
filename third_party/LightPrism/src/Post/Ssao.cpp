#include "LightPrism/Post/Ssao.hpp"

#include <algorithm>
#include <cmath>

#include "LightPrism/Sampling.hpp"

namespace LightPrism::Ssao {

namespace {

float SmoothStep01(float t) { t = Saturate(t); return t * t * (3.0f - 2.0f * t); }

constexpr uint8_t kPattern[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};  // Bayer 4x4

} // namespace

void BuildKernel(uint32_t n, Vec3* out) {
    for (uint32_t i = 0; i < n; ++i) {
        const float u1 = (float(i) + 0.5f) / float(n), u2 = RadicalInverse(i);
        const float phi = 2.0f * kPi * u1;
        const float cosT = std::sqrt(1.0f - u2), sinT = std::sqrt(u2);  // ponderado por cosseno
        float t = 0.5f + float(i) * 0.6180339887f;                        // distribuição uniforme em (0,1], decorrelada da direção
        t -= std::floor(t);
        const float scale = Lerp(0.05f, 1.0f, t);
        out[i] = Vec3{sinT * std::cos(phi), sinT * std::sin(phi), cosT} * scale;
    }
}

float KernelRotation(uint32_t x, uint32_t y) { return float(kPattern[(y & 3u) * 4u + (x & 3u)]) * (2.0f * kPi / 16.0f); }

Vec3 ViewPosFromDepth(const SsaoInput& in, uint32_t x, uint32_t y, float d) {
    const float ndcX = (float(x) + 0.5f) / float(in.width) * 2.0f - 1.0f;
    const float ndcY = 1.0f - (float(y) + 0.5f) / float(in.height) * 2.0f;
    return {ndcX * d / in.proj00, ndcY * d / in.proj11, -d};
}

void ComputeSsaoRaw(const SsaoConfig& cfg, const SsaoInput& in, std::vector<float>& out) {
    const uint32_t W = in.width, H = in.height;
    out.assign(size_t(W) * H, 1.0f);
    const uint32_t n = std::min(std::max(cfg.kernelSize, 1u), kMaxKernel);
    Vec3 kernel[kMaxKernel];
    BuildKernel(n, kernel);

    for (uint32_t y = 0; y < H; ++y)
        for (uint32_t x = 0; x < W; ++x) {
            const size_t idx = size_t(y) * W + x;
            const float d = in.viewDepth[idx];
            if (d <= 0.0f) continue;
            const Vec3 p = ViewPosFromDepth(in, x, y, d);
            const Vec3 N = in.viewNormal[idx];

            const Vec3 up = std::fabs(N.y) < 0.99f ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
            const Vec3 t0 = Normalize(Cross(up, N)), b0 = Cross(N, t0);
            const float rot = KernelRotation(x, y), cr = std::cos(rot), sr = std::sin(rot);
            const Vec3 T = t0 * cr + b0 * sr, B = b0 * cr - t0 * sr;

            const float biasEff = cfg.bias + cfg.biasPerMeter * d;
            float occ = 0.0f;
            for (uint32_t i = 0; i < n; ++i) {
                const Vec3 k = kernel[i];
                const Vec3 sp = p + (T * k.x + B * k.y + N * k.z) * cfg.radius;
                if (sp.z >= -1e-4f) continue;  // atrás da câmera
                const float px = (sp.x * in.proj00 / -sp.z * 0.5f + 0.5f) * float(W);
                const float py = (0.5f - sp.y * in.proj11 / -sp.z * 0.5f) * float(H);
                if (px < 0.0f || py < 0.0f || px >= float(W) || py >= float(H)) continue;
                const float sd = in.viewDepth[size_t(uint32_t(py)) * W + uint32_t(px)];
                if (sd <= 0.0f) continue;                  // fundo: nada que ocluir
                const float sceneZ = -sd;
                if (sceneZ >= sp.z + biasEff) {
                    occ += SmoothStep01(cfg.radius / std::max(std::fabs(p.z - sceneZ), 1e-4f));
                }
            }
            const float ao = 1.0f - occ / float(n);
            const float fade = SmoothStep01((d - cfg.fadeStart) / std::max(cfg.fadeEnd - cfg.fadeStart, 1e-3f));
            const float shaped = cfg.power == 1.0f ? ao : std::pow(ao, cfg.power);
            out[idx] = Lerp(shaped, 1.0f, fade);
        }
}

void BilateralBlur(const SsaoConfig& cfg, const SsaoInput& in, const std::vector<float>& raw, std::vector<float>& out) {
    const int W = int(in.width), H = int(in.height);
    out.assign(raw.size(), 1.0f);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            const size_t idx = size_t(y) * W + x;
            const float d = in.viewDepth[idx];
            if (d <= 0.0f) continue;
            float sum = 0.0f, wsum = 0.0f;
            for (int dy = -2; dy <= 1; ++dy)
                for (int dx = -2; dx <= 1; ++dx) {
                    const int xx = x + dx, yy = y + dy;
                    if (xx < 0 || yy < 0 || xx >= W || yy >= H) continue;
                    const size_t j = size_t(yy) * W + xx;
                    const float dj = in.viewDepth[j];
                    if (dj <= 0.0f || std::fabs(dj - d) > cfg.blurDepthRel * d) continue;
                    sum += raw[j];
                    wsum += 1.0f;
                }
            out[idx] = wsum > 0.0f ? sum / wsum : raw[idx];
        }
}

void ComputeSsaoHalf(const SsaoConfig& cfg, const SsaoInput& full, std::vector<float>& out) {
    const uint32_t W = full.width, H = full.height;
    const uint32_t hw = (W + 1) / 2, hh = (H + 1) / 2;
    std::vector<float> hd(size_t(hw) * hh, 0.0f);
    std::vector<Vec3> hn(size_t(hw) * hh, Vec3{0, 0, 1});
    for (uint32_t by = 0; by < hh; ++by)
        for (uint32_t bx = 0; bx < hw; ++bx) {
            float sum = 0.0f; int n = 0;
            for (uint32_t dy = 0; dy < 2; ++dy)
                for (uint32_t dx = 0; dx < 2; ++dx) {
                    const uint32_t x = bx * 2 + dx, y = by * 2 + dy;
                    if (x >= W || y >= H) continue;
                    const float d = full.viewDepth[size_t(y) * W + x];
                    if (d > 0.0f) { sum += d; ++n; }
                }
            if (!n) continue;
            const float mean = sum / float(n);
            float best = 1e30f; size_t bi = 0;
            for (uint32_t dy = 0; dy < 2; ++dy)
                for (uint32_t dx = 0; dx < 2; ++dx) {
                    const uint32_t x = bx * 2 + dx, y = by * 2 + dy;
                    if (x >= W || y >= H) continue;
                    const size_t i = size_t(y) * W + x;
                    const float d = full.viewDepth[i];
                    if (d > 0.0f && std::fabs(d - mean) < best) { best = std::fabs(d - mean); bi = i; }
                }
            hd[size_t(by) * hw + bx] = full.viewDepth[bi];
            hn[size_t(by) * hw + bx] = full.viewNormal[bi];
        }
    SsaoInput hin;
    hin.width = hw; hin.height = hh; hin.viewDepth = hd.data(); hin.viewNormal = hn.data();
    hin.proj00 = full.proj00; hin.proj11 = full.proj11;
    std::vector<float> hao;
    ComputeSsao(cfg, hin, hao);

    out.assign(size_t(W) * H, 1.0f);
    for (uint32_t y = 0; y < H; ++y)
        for (uint32_t x = 0; x < W; ++x) {
            const float d = full.viewDepth[size_t(y) * W + x];
            if (d <= 0.0f) continue;
            const float fx = (float(x) + 0.5f) * 0.5f - 0.5f, fy = (float(y) + 0.5f) * 0.5f - 0.5f;
            const int x0 = int(std::floor(fx)), y0 = int(std::floor(fy));
            const float tx = fx - float(x0), ty = fy - float(y0);
            float sum = 0.0f, wsum = 0.0f, nearestAo = 1.0f, nearestDz = 1e30f;
            for (int j = 0; j < 2; ++j)
                for (int i = 0; i < 2; ++i) {
                    const int xx = std::min(std::max(x0 + i, 0), int(hw) - 1), yy = std::min(std::max(y0 + j, 0), int(hh) - 1);
                    const size_t k = size_t(yy) * hw + xx;
                    const float dh = hd[k];
                    if (dh <= 0.0f) continue;
                    const float dz = std::fabs(dh - d);
                    if (dz < nearestDz) { nearestDz = dz; nearestAo = hao[k]; }
                    if (dz > cfg.blurDepthRel * d) continue;               // atravessaria uma borda de profundidade
                    const float w = (i ? tx : 1.0f - tx) * (j ? ty : 1.0f - ty) + 1e-4f;
                    sum += hao[k] * w; wsum += w;
                }
            out[size_t(y) * W + x] = wsum > 0.0f ? sum / wsum : nearestAo;
        }
}

SsaoGPU MakeSsaoGPU(const SsaoConfig& cfg, uint32_t width, uint32_t height, const float proj[4][4]) {
    SsaoGPU g{};
    const uint32_t n = std::min(std::max(cfg.kernelSize, 1u), kMaxKernel);
    Vec3 k[kMaxKernel];
    BuildKernel(n, k);
    for (uint32_t i = 0; i < n; ++i) { g.kernel[i][0] = k[i].x; g.kernel[i][1] = k[i].y; g.kernel[i][2] = k[i].z; }
    g.proj00 = proj[0][0]; g.proj11 = proj[1][1]; g.proj22 = proj[2][2]; g.proj23 = proj[2][3];
    g.invWidth = 1.0f / float(width); g.invHeight = 1.0f / float(height);
    g.width = width; g.height = height;
    g.radius = cfg.radius; g.bias = cfg.bias; g.power = cfg.power; g.blurDepthRel = cfg.blurDepthRel;
    g.kernelSize = n;
    g.biasPerMeter = cfg.biasPerMeter; g.fadeStart = cfg.fadeStart; g.fadeEnd = cfg.fadeEnd;
    return g;
}

} // namespace LightPrism::Ssao
