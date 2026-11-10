// Testes do ClusterSystem + RHI (NullDevice): ordem dos dispatches, barreiras, bindings (conferidos contra os
// .hlsl reais), readback, fallback CPU, validações de erro.
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

#include "LightPrism/Cluster/ClusterSystem.hpp"
#include "LightPrism/Soft/DemoScene.hpp"

using namespace LightPrism;

static int g_failures = 0;
#define CHECK(cond)                                                                                   \
    do {                                                                                              \
        if (!(cond)) { std::printf("FALHOU %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_failures; } \
    } while (0)

#ifndef LP_SHADER_DIR
#error LP_SHADER_DIR deve apontar para a pasta shaders/
#endif

namespace {

std::string ReadFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

size_t CountOccurrences(const std::string& text, const std::string& needle) {
    size_t n = 0;
    for (size_t p = text.find(needle); p != std::string::npos; p = text.find(needle, p + needle.size())) ++n;
    return n;
}

bool ContainsIdentifier(const std::string& text, const std::string& id) {
    for (size_t p = text.find(id); p != std::string::npos; p = text.find(id, p + 1)) {
        const bool l = p == 0 || !(std::isalnum((unsigned char)text[p - 1]) || text[p - 1] == '_');
        const size_t e = p + id.size();
        const bool r = e >= text.size() || !(std::isalnum((unsigned char)text[e]) || text[e] == '_');
        if (l && r) return true;
    }
    return false;
}

struct Rig {
    std::unique_ptr<IGraphicsDevice> dev;
    LightingSystem lights;
    const FrameLightView* view = nullptr;
    ClusterCamera cam;

    explicit Rig(DeviceCaps caps = {}, uint32_t np = 40, uint32_t ns = 6, uint32_t w = 640, uint32_t h = 360) {
        dev = CreateNullDevice(caps);
        Soft::PopulateDemoLights(lights, np, ns);
        const Soft::CameraSetup c = Soft::MakeDemoCamera(w, h);
        const Mat4 proj = PerspectiveRH_ReverseZ(c.fovYRad, float(w) / float(h), c.zNear, c.zFar);
        cam = MakeClusterCamera(LookAtRH(c.eye, c.target), proj, w, h, c.zNear, c.zFar);
        FrameParams fp;
        fp.cameraPosition = c.eye;
        view = &lights.BuildFrame(fp);
        lights.UploadToGPU(*dev);
    }
};

ClusterSystemDesc Desc() {
    ClusterSystemDesc d;
    d.shaderDirectory = LP_SHADER_DIR;
    return d;
}

const char* ShaderFileOf(const std::string& pipeline) {
    static const std::map<std::string, const char*> m = {
        {"LP_ClusterLightPrep", "Cluster/ClusterLightPrep.compute.hlsl"},
        {"LP_ClusterCullCount", "Cluster/ClusterCullCount.compute.hlsl"},
        {"LP_ClusterScanLocal", "Cluster/ClusterScan.compute.hlsl"},
        {"LP_ClusterScanGroupSums", "Cluster/ClusterScan.compute.hlsl"},
        {"LP_ClusterCullFill", "Cluster/ClusterCullFill.compute.hlsl"},
    };
    auto it = m.find(pipeline);
    return it == m.end() ? nullptr : it->second;
}

} // namespace

static void TestGpuCommandSequence() {
    Rig rig;
    ClusterSystem cs(Desc());
    CHECK(cs.Execute(*rig.dev, rig.lights, *rig.view, rig.cam));
    CHECK(cs.LastError().empty());
    CHECK(cs.ActiveMode() == ClusterMode::Gpu);
    CHECK(cs.FallbackReason().empty());

    const NullCommandLog* log = GetNullCommandLog(*rig.dev);
    CHECK(log != nullptr);
    if (!log) return;

    const uint32_t clusters = ClusterCount(cs.Grid());
    const uint32_t total = rig.view->pointCount + rig.view->spotCount;
    struct Expect { const char* name; uint32_t groups; };
    const Expect dispatches[] = {
        {"LP_ClusterLightPrep", (total + 63) / 64},
        {"LP_ClusterCullCount", (clusters + 63) / 64},
        {"LP_ClusterScanLocal", (clusters + 255) / 256},
        {"LP_ClusterScanGroupSums", 1},
        {"LP_ClusterCullFill", (clusters + 63) / 64},
    };

    size_t di = 0;
    size_t barriers = 0, copies = 0;
    for (const NullCommand& c : log->commands) {
        if (c.type == NullCommand::Type::Dispatch) {
            CHECK(di < 5);
            if (di >= 5) break;
            CHECK(c.pipeline == dispatches[di].name);
            CHECK(c.groups[0] == dispatches[di].groups && c.groups[1] == 1 && c.groups[2] == 1);

            // Bindings batem com o HLSL real: todo nome existe no arquivo e todo recurso declarado está ligado.
            const char* file = ShaderFileOf(c.pipeline);
            CHECK(file != nullptr);
            if (file) {
                const std::string src = ReadFile(std::string(LP_SHADER_DIR) + "/" + file);
                CHECK(!src.empty());
                for (const auto& b : c.bindings) {
                    CHECK(b.second != nullptr);
                    CHECK(ContainsIdentifier(src, b.first));
                }
                CHECK(c.bindings.size() == CountOccurrences(src, ": register("));
            }
            ++di;
        } else if (c.type == NullCommand::Type::UavBarrier) {
            ++barriers;
            CHECK(di > 0); // nunca antes do primeiro dispatch
        } else {
            ++copies;
            CHECK(di == 5); // cópia do Stats só depois do último dispatch
        }
    }
    CHECK(di == 5);
    CHECK(barriers == 8); // prep 1, count 1, scanLocal 2, scanSums 2, fill 2
    CHECK(copies == 1);
    CHECK(log->commands.back().type == NullCommand::Type::Copy);

    CHECK(cs.GridBuffer() && cs.LightGridBuffer() && cs.LightIndexBuffer());
    CHECK(cs.LightGridBuffer()->Desc().sizeBytes == uint64_t(clusters) * sizeof(ClusterCell));
    CHECK(cs.LightIndexBuffer()->Desc().sizeBytes == uint64_t(cs.Grid().maxIndices) * 4);
}

static void TestZeroLightsSkipsPrep() {
    Rig rig({}, 0, 0);
    ClusterSystem cs(Desc());
    CHECK(cs.Execute(*rig.dev, rig.lights, *rig.view, rig.cam));
    size_t dispatches = 0;
    for (const NullCommand& c : GetNullCommandLog(*rig.dev)->commands)
        if (c.type == NullCommand::Type::Dispatch) {
            ++dispatches;
            CHECK(c.pipeline != "LP_ClusterLightPrep");
        }
    CHECK(dispatches == 4);
}

static void TestStatsReadback() {
    Rig rig;
    ClusterSystem cs(Desc());
    // Antes do primeiro Execute não há dado.
    ClusterStats st;
    CHECK(!cs.ReadbackStats(st));

    CHECK(cs.Execute(*rig.dev, rig.lights, *rig.view, rig.cam)); // cria buffers
    CHECK(cs.DebugStatsBuffer() != nullptr);
    // Simula a GPU: o NullDevice não roda shaders, então escrevemos o resultado do scan à mão.
    const uint32_t fake = cs.Grid().maxIndices + 5;
    CHECK(cs.DebugStatsBuffer()->Update(&fake, 4));
    CHECK(cs.Execute(*rig.dev, rig.lights, *rig.view, rig.cam)); // o Execute copia Stats -> readback
    CHECK(cs.ReadbackStats(st));
    CHECK(st.valid && st.requestedIndices == fake && st.maxIndices == cs.Grid().maxIndices && st.overflow);

    const uint32_t fine = 1000;
    CHECK(cs.DebugStatsBuffer()->Update(&fine, 4));
    CHECK(cs.Execute(*rig.dev, rig.lights, *rig.view, rig.cam));
    CHECK(cs.ReadbackStats(st));
    CHECK(st.requestedIndices == 1000 && !st.overflow);
}

static void TestCpuFallbackMatchesReference() {
    DeviceCaps caps;
    caps.computeShaders = false;
    Rig rig(caps);
    ClusterSystem cs(Desc());
    CHECK(cs.Execute(*rig.dev, rig.lights, *rig.view, rig.cam));
    CHECK(cs.ActiveMode() == ClusterMode::Cpu);
    CHECK(!cs.FallbackReason().empty());
    CHECK(GetNullCommandLog(*rig.dev)->commands.empty()); // nenhum dispatch no fallback

    // Os buffers contêm exatamente o resultado de CullLightsCPU.
    const ClusterGridGPU& g = cs.Grid();
    ClusterCullResult ref;
    const auto aabbs = BuildAllClusterAABBs(g);
    CullLightsCPU(g, aabbs, rig.cam.view, rig.view->points, rig.view->pointCount, rig.view->spots, rig.view->spotCount, ref);
    CHECK(std::memcmp(cs.LightGridBuffer()->NativeHandle(), ref.cells.data(), ref.cells.size() * sizeof(ClusterCell)) == 0);
    CHECK(std::memcmp(cs.LightIndexBuffer()->NativeHandle(), ref.indices.data(), ref.indices.size() * 4) == 0);

    ClusterStats st;
    CHECK(cs.ReadbackStats(st) && st.valid);
    CHECK(st.requestedIndices == ref.requestedIndices && st.overflow == ref.overflow);
    CHECK(st.requestedIndices > 0);
}

static void TestModesAndErrors() {
    // Gpu forçado sem compute: erro explícito.
    {
        DeviceCaps caps;
        caps.computeShaders = false;
        Rig rig(caps);
        ClusterSystemDesc d = Desc();
        d.mode = ClusterMode::Gpu;
        ClusterSystem cs(d);
        CHECK(!cs.Execute(*rig.dev, rig.lights, *rig.view, rig.cam));
        CHECK(!cs.LastError().empty());
    }
    // Cpu forçado mesmo com compute disponível.
    {
        Rig rig;
        ClusterSystemDesc d = Desc();
        d.mode = ClusterMode::Cpu;
        ClusterSystem cs(d);
        CHECK(cs.Execute(*rig.dev, rig.lights, *rig.view, rig.cam));
        CHECK(cs.ActiveMode() == ClusterMode::Cpu);
        CHECK(GetNullCommandLog(*rig.dev)->commands.empty());
    }
    // Câmera inválida.
    {
        Rig rig;
        ClusterSystem cs(Desc());
        ClusterCamera bad = rig.cam;
        bad.zFar = bad.zNear;
        CHECK(!cs.Execute(*rig.dev, rig.lights, *rig.view, bad));
        bad = rig.cam;
        bad.screenWidth = 0;
        CHECK(!cs.Execute(*rig.dev, rig.lights, *rig.view, bad));
        bad = rig.cam;
        bad.proj00 = 0.0f;
        CHECK(!cs.Execute(*rig.dev, rig.lights, *rig.view, bad));
    }
    // Grid grande demais para um dispatch 1D com este limite de grupos.
    {
        DeviceCaps caps;
        caps.maxComputeGroupsPerDim = 8;
        Rig rig(caps);
        ClusterSystem cs(Desc());
        CHECK(!cs.Execute(*rig.dev, rig.lights, *rig.view, rig.cam));
        CHECK(!cs.LastError().empty());
    }
    // UploadToGPU esquecido.
    {
        auto dev = CreateNullDevice();
        LightingSystem lights;
        Soft::PopulateDemoLights(lights, 5, 1);
        FrameParams fp;
        const FrameLightView& v = lights.BuildFrame(fp);
        Rig other;
        ClusterSystem cs(Desc());
        CHECK(!cs.Execute(*dev, lights, v, other.cam));
        CHECK(cs.LastError().find("UploadToGPU") != std::string::npos);
    }
}

static void TestResizeAndDeviceSwitch() {
    Rig rig(DeviceCaps{}, 40, 6, 640, 360);
    ClusterSystem cs(Desc());
    CHECK(cs.Execute(*rig.dev, rig.lights, *rig.view, rig.cam));
    const uint32_t before = ClusterCount(cs.Grid());
    CHECK(before == 40u * 23u * 24u); // 640/16 = 40; 360/16 = 22,5 -> 23 (tile parcial)

    ClusterCamera big = rig.cam;
    big.screenWidth = 1280; big.screenHeight = 720;
    CHECK(cs.Execute(*rig.dev, rig.lights, *rig.view, big));
    const uint32_t after = 80u * 45u * 24u;
    CHECK(ClusterCount(cs.Grid()) == after);
    CHECK(cs.LightGridBuffer()->Desc().sizeBytes == uint64_t(after) * sizeof(ClusterCell));
    CHECK(cs.LightIndexBuffer()->Desc().sizeBytes == uint64_t(cs.Grid().maxIndices) * 4);

    // Trocar de dispositivo recria tudo sem crash.
    auto dev2 = CreateNullDevice();
    CHECK(rig.lights.UploadToGPU(*dev2));
    CHECK(cs.Execute(*dev2, rig.lights, *rig.view, big));
    CHECK(GetNullCommandLog(*dev2)->commands.size() > 5);
}

static void TestRhiBasics() {
    auto dev = CreateNullDevice();
    BufferDesc b;
    b.kind = BufferKind::StructuredRW; b.strideBytes = 8; b.sizeBytes = 64; b.debugName = "rw";
    auto rw = dev->CreateBuffer(b);
    BufferDesc r;
    r.kind = BufferKind::Readback; r.sizeBytes = 64; r.debugName = "rb";
    auto rb = dev->CreateBuffer(r);
    CHECK(rw && rb);
    if (!rw || !rb) return;

    uint32_t v[4] = {1, 2, 3, 4};
    CHECK(rw->Update(v, 16, 8));
    CHECK(!rw->Update(v, 16, 56));      // fora dos limites
    CHECK(!rb->Update(v, 16, 0));       // Readback não aceita Update
    uint32_t out[4] = {};
    CHECK(!rw->ReadBack(out, 16, 0));   // só Readback lê
    CHECK(dev->ImmediateContext().CopyBuffer(*rw, *rb, 64));
    CHECK(rb->ReadBack(out, 16, 8));
    CHECK(out[0] == 1 && out[3] == 4);
    CHECK(!rb->ReadBack(out, 16, 56));

    // Validações de criação
    BufferDesc bad;
    bad.kind = BufferKind::Structured; bad.sizeBytes = 100; bad.strideBytes = 8; // não múltiplo
    CHECK(dev->CreateBuffer(bad) == nullptr);
    bad.kind = BufferKind::Uniform; bad.sizeBytes = 20; bad.strideBytes = 20;     // não múltiplo de 16
    CHECK(dev->CreateBuffer(bad) == nullptr);
    bad.sizeBytes = 0;
    CHECK(dev->CreateBuffer(bad) == nullptr);

    // Bindings: uso incompatível com o tipo é recusado.
    ComputePipelineDesc pd;
    pd.debugName = "p"; pd.shaderFile = "x.hlsl";
    auto p = dev->CreateComputePipeline(pd);
    CHECK(p != nullptr);
    BufferDesc ub;
    ub.kind = BufferKind::Uniform; ub.sizeBytes = 16; ub.strideBytes = 16;
    auto uni = dev->CreateBuffer(ub);
    BufferDesc ro;
    ro.kind = BufferKind::Structured; ro.sizeBytes = 16; ro.strideBytes = 4;
    auto sro = dev->CreateBuffer(ro);
    CHECK(p->SetBuffer("cb", uni.get(), BindUsage::Constant));
    CHECK(!p->SetBuffer("cb", sro.get(), BindUsage::Constant));
    CHECK(p->SetBuffer("t", sro.get(), BindUsage::ReadOnly));
    CHECK(!p->SetBuffer("u", sro.get(), BindUsage::ReadWrite));   // Structured não tem UAV
    CHECK(p->SetBuffer("u", rw.get(), BindUsage::ReadWrite));
    CHECK(p->SetBuffer("t2", rw.get(), BindUsage::ReadOnly));     // StructuredRW também vira SRV
    CHECK(!p->SetBuffer(nullptr, rw.get(), BindUsage::ReadOnly));
    CHECK(!p->SetBuffer("n", nullptr, BindUsage::ReadOnly));

    // Dispatch: grupos inválidos
    ICommandContext& ctx = dev->ImmediateContext();
    CHECK(!ctx.Dispatch(*p, 0));
    CHECK(!ctx.Dispatch(*p, 1, 0, 1));
    CHECK(!ctx.Dispatch(*p, 70000));
    CHECK(ctx.Dispatch(*p, 3, 2, 1));

    // Sem compute: nenhum pipeline.
    DeviceCaps nc;
    nc.computeShaders = false;
    CHECK(CreateNullDevice(nc)->CreateComputePipeline(pd) == nullptr);
}

int main() {
    TestGpuCommandSequence();
    TestZeroLightsSkipsPrep();
    TestStatsReadback();
    TestCpuFallbackMatchesReference();
    TestModesAndErrors();
    TestResizeAndDeviceSwitch();
    TestRhiBasics();
    if (g_failures == 0) std::printf("Testes ClusterSystem/RHI: todos passaram.\n");
    else std::printf("Testes ClusterSystem/RHI: %d falha(s).\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
