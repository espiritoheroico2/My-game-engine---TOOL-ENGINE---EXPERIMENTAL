#include "LightPrism/Cluster/ClusterSystem.hpp"

#include <algorithm>
#include <cstring>
#include <utility>

#include "LightPrism/Util.hpp"

namespace LightPrism {

namespace {

struct PassDef {
    const char* debugName;
    const char* file;
    const char* entry;
};

constexpr PassDef kPassPrep      {"LP_ClusterLightPrep", "Cluster/ClusterLightPrep.compute.hlsl", "CSMain"};
constexpr PassDef kPassCount     {"LP_ClusterCullCount", "Cluster/ClusterCullCount.compute.hlsl", "CSMain"};
constexpr PassDef kPassScanLocal {"LP_ClusterScanLocal", "Cluster/ClusterScan.compute.hlsl", "ScanLocal"};
constexpr PassDef kPassScanSums  {"LP_ClusterScanGroupSums", "Cluster/ClusterScan.compute.hlsl", "ScanGroupSums"};
constexpr PassDef kPassFill      {"LP_ClusterCullFill", "Cluster/ClusterCullFill.compute.hlsl", "CSMain"};

bool SameCamera(const ClusterCamera& a, const ClusterCamera& b) {
    return a.screenWidth == b.screenWidth && a.screenHeight == b.screenHeight && a.proj00 == b.proj00 &&
           a.proj11 == b.proj11 && a.zNear == b.zNear && a.zFar == b.zFar;
}

} // namespace

struct ClusterSystem::Impl {
    ClusterSystemDesc desc;

    // Estado do grid
    ClusterGridGPU grid{};
    ClusterCamera lastCamera{};
    bool haveGrid = false;
    std::vector<ClusterAABB> aabbs; // só usado no modo CPU

    // Dispositivo e pipelines
    const IGraphicsDevice* boundDevice = nullptr;
    std::unique_ptr<IComputePipeline> pPrep, pCount, pScanLocal, pScanSums, pFill;
    bool pipelinesTried = false;
    ClusterMode active = ClusterMode::Cpu;
    std::string lastError, fallbackReason;

    // Buffers
    std::unique_ptr<IGpuBuffer> gridCB, viewCB, clusterLights, counts, groupSums, stats, lightIndex, lightGrid, readback;
    uint64_t clusterLightsCap = 0;
    uint32_t allocClusters = 0, allocMaxIndices = 0;

    // Stats do modo CPU (imediatos)
    ClusterStats cpuStats{};
    ClusterCullResult cpuResult; // reaproveitado entre frames

    void ResetDevice(const IGraphicsDevice* dev) {
        pPrep.reset(); pScanLocal.reset(); pCount.reset(); pScanSums.reset(); pFill.reset();
        gridCB.reset(); viewCB.reset(); clusterLights.reset(); counts.reset(); groupSums.reset();
        stats.reset(); lightIndex.reset(); lightGrid.reset(); readback.reset();
        clusterLightsCap = 0; allocClusters = 0; allocMaxIndices = 0;
        pipelinesTried = false;
        boundDevice = dev;
    }

    bool MakePipeline(IGraphicsDevice& dev, const PassDef& p, std::unique_ptr<IComputePipeline>& out) {
        ComputePipelineDesc d;
        d.debugName = p.debugName;
        d.shaderFile = p.file;
        d.entryPoint = p.entry;
        d.shaderDirectory = desc.shaderDirectory.c_str();
        out = dev.CreateComputePipeline(d);
        if (!out) { fallbackReason = std::string("falha ao criar pipeline ") + p.debugName; return false; }
        return true;
    }

    bool CreateAllPipelines(IGraphicsDevice& dev) {
        return MakePipeline(dev, kPassPrep, pPrep) && MakePipeline(dev, kPassCount, pCount) &&
               MakePipeline(dev, kPassScanLocal, pScanLocal) && MakePipeline(dev, kPassScanSums, pScanSums) &&
               MakePipeline(dev, kPassFill, pFill);
    }

    std::unique_ptr<IGpuBuffer> Make(IGraphicsDevice& dev, BufferKind kind, uint64_t elems, uint32_t stride,
                                     const char* name) {
        BufferDesc b;
        b.kind = kind;
        b.strideBytes = stride;
        b.sizeBytes = elems * stride;
        b.debugName = name;
        return dev.CreateBuffer(b);
    }

    // (Re)cria o que depende do tamanho do grid. Buffers de tamanho fixo (cbuffers, stats) criados uma vez.
    bool EnsureBuffers(IGraphicsDevice& dev, uint32_t lightCount) {
        const uint32_t clusters = ClusterCount(grid);
        if (!gridCB) {
            gridCB = Make(dev, BufferKind::Uniform, 1, sizeof(ClusterGridGPU), "LP_ClusterGridCB");
            viewCB = Make(dev, BufferKind::Uniform, 1, sizeof(Mat4), "LP_ClusterViewCB");
            stats = Make(dev, BufferKind::StructuredRW, 1, 4, "LP_ClusterStats");
            readback = Make(dev, BufferKind::Readback, 1, 4, "LP_ClusterStatsReadback");
            if (!gridCB || !viewCB || !stats || !readback) return false;
        }
        if (allocClusters != clusters || allocMaxIndices != grid.maxIndices || !counts) {
            const uint32_t groups = DivUp(clusters, kClusterScanGroupSize);
            counts = Make(dev, BufferKind::StructuredRW, clusters, 4, "LP_ClusterCounts");
            groupSums = Make(dev, BufferKind::StructuredRW, groups, 4, "LP_ClusterGroupSums");
            lightGrid = Make(dev, BufferKind::StructuredRW, clusters, sizeof(ClusterCell), "LP_LightGrid");
            lightIndex = Make(dev, BufferKind::StructuredRW, grid.maxIndices, 4, "LP_LightIndexList");
            if (!counts || !groupSums || !lightGrid || !lightIndex) return false;
            allocClusters = clusters;
            allocMaxIndices = grid.maxIndices;
        }
        const uint64_t need = std::max<uint64_t>(lightCount, 1);
        if (!clusterLights || clusterLightsCap < need) {
            const uint64_t cap = NextPow2(need);
            clusterLights = Make(dev, BufferKind::StructuredRW, cap, sizeof(ClusterLightGPU), "LP_ClusterLights");
            if (!clusterLights) return false;
            clusterLightsCap = cap;
        }
        return true;
    }

    void UpdateGrid(const ClusterCamera& cam, uint32_t np, uint32_t ns) {
        if (!haveGrid || !SameCamera(cam, lastCamera)) {
            ClusterGridDesc d;
            d.screenWidth = cam.screenWidth; d.screenHeight = cam.screenHeight;
            d.tileSizePx = desc.tileSizePx; d.zSlices = desc.zSlices;
            d.zNear = cam.zNear; d.zFar = cam.zFar;
            d.proj00 = cam.proj00; d.proj11 = cam.proj11;
            d.maxLightsPerCluster = desc.maxLightsPerCluster;
            d.maxIndices = 0;
            grid = MakeClusterGrid(d);
            grid.maxIndices = budgetOverride ? budgetOverride : ClusterCount(grid) * std::max(desc.indicesPerCluster, 1u);
            aabbs.clear(); // reconstruído sob demanda no modo CPU
            lastCamera = cam;
            haveGrid = true;
        }
        grid.numPointLights = np;
        grid.numSpotLights = ns;
    }

    uint32_t budgetOverride = 0;   // maxIndices escolhido pelo ajuste adaptativo (0 = padrão)
    uint32_t calmFrames = 0;

    // Chamado no começo de Execute, depois de UpdateGrid: ajusta grid.maxIndices com base nas estatísticas do frame anterior.
    void AdaptBudget(const ClusterSystem& self) {
        if (!desc.adaptiveIndexBudget || !haveGrid) return;
        ClusterStats st;
        if (!self.ReadbackStats(st) || !st.valid) return;
        const uint32_t clusters = ClusterCount(grid);
        const uint64_t lo = uint64_t(clusters) * std::max(desc.minIndicesPerCluster, 1u);
        const uint64_t hi = uint64_t(clusters) * std::max(desc.maxIndicesPerCluster, 1u);
        uint64_t target = grid.maxIndices;
        if (st.requestedIndices > grid.maxIndices) {
            target = std::min<uint64_t>(hi, st.requestedIndices + st.requestedIndices / 4 + 64);
            calmFrames = 0;
        } else if (st.requestedIndices * 2 < grid.maxIndices) {
            if (++calmFrames >= desc.shrinkAfterFrames) {
                target = std::max<uint64_t>(lo, st.requestedIndices + st.requestedIndices / 2 + 64);
                calmFrames = 0;
            }
        } else {
            calmFrames = 0;
        }
        if (target != grid.maxIndices) { grid.maxIndices = uint32_t(target); budgetOverride = grid.maxIndices; }
    }

    bool Fail(const std::string& msg) { lastError = msg; return false; }

    bool RunCpu(IGraphicsDevice& dev, const FrameLightView& view, const ClusterCamera& cam) {
        if (aabbs.empty()) aabbs = BuildAllClusterAABBs(grid);
        CullLightsCPU(grid, aabbs, cam.view, view.points, view.pointCount, view.spots, view.spotCount, cpuResult);
        (void)dev;
        bool ok = lightGrid->Update(cpuResult.cells.data(), uint64_t(cpuResult.cells.size()) * sizeof(ClusterCell));
        ok &= lightIndex->Update(cpuResult.indices.data(), uint64_t(cpuResult.indices.size()) * 4);
        ok &= gridCB->Update(&grid, sizeof(ClusterGridGPU));
        cpuStats.valid = true;
        cpuStats.requestedIndices = cpuResult.requestedIndices;
        cpuStats.maxIndices = grid.maxIndices;
        cpuStats.overflow = cpuResult.overflow;
        return ok ? true : Fail("falha ao enviar LightGrid/LightIndexList do CPU para a GPU");
    }

    bool Bind(IComputePipeline& p, const char* name, IGpuBuffer* b, BindUsage u) {
        if (!p.SetBuffer(name, b, u)) return Fail(std::string("binding '") + name + "' recusado em " + p.Desc().debugName);
        return true;
    }

    bool RunGpu(IGraphicsDevice& dev, const LightingSystem& lights, const FrameLightView& view,
                const ClusterCamera& cam) {
        ICommandContext& ctx = dev.ImmediateContext();
        const uint32_t total = view.pointCount + view.spotCount;
        const uint32_t clusters = ClusterCount(grid);
        const uint32_t cullGroups = DivUp(clusters, 64);
        const uint32_t scanGroups = DivUp(clusters, kClusterScanGroupSize);
        const uint32_t maxG = dev.Caps().maxComputeGroupsPerDim;
        if (cullGroups > maxG || scanGroups > maxG)
            return Fail("grid grande demais para um dispatch 1D: aumente tileSizePx ou reduza zSlices");

        IGpuBuffer* pointBuf = lights.PointLightBuffer();
        IGpuBuffer* spotBuf = lights.SpotLightBuffer();
        if (!pointBuf || !spotBuf) return Fail("LightingSystem::UploadToGPU(device) precisa rodar antes do Execute");

        if (!gridCB->Update(&grid, sizeof(ClusterGridGPU)) || !viewCB->Update(&cam.view, sizeof(Mat4)))
            return Fail("falha ao atualizar cbuffers de cluster");

        // 1) Prep: luzes -> view space + faixa de clusters
        if (total > 0) {
            if (!Bind(*pPrep, "ClusterGridCB", gridCB.get(), BindUsage::Constant) ||
                !Bind(*pPrep, "ViewCB", viewCB.get(), BindUsage::Constant) ||
                !Bind(*pPrep, "InPointLights", pointBuf, BindUsage::ReadOnly) ||
                !Bind(*pPrep, "InSpotLights", spotBuf, BindUsage::ReadOnly) ||
                !Bind(*pPrep, "OutClusterLights", clusterLights.get(), BindUsage::ReadWrite))
                return false;
            if (!ctx.Dispatch(*pPrep, DivUp(total, 64))) return Fail("dispatch Prep falhou");
            ctx.UavBarrier(*clusterLights);
        }

        // 2) Contagem por cluster
        if (!Bind(*pCount, "ClusterGridCB", gridCB.get(), BindUsage::Constant) ||
            !Bind(*pCount, "Lights", clusterLights.get(), BindUsage::ReadOnly) ||
            !Bind(*pCount, "ClusterCounts", counts.get(), BindUsage::ReadWrite))
            return false;
        if (!ctx.Dispatch(*pCount, cullGroups)) return Fail("dispatch Count falhou");
        ctx.UavBarrier(*counts);

        // 3a) Scan local por grupo de 256
        if (!Bind(*pScanLocal, "ClusterGridCB", gridCB.get(), BindUsage::Constant) ||
            !Bind(*pScanLocal, "ClusterCounts", counts.get(), BindUsage::ReadWrite) ||
            !Bind(*pScanLocal, "GroupSums", groupSums.get(), BindUsage::ReadWrite) ||
            !Bind(*pScanLocal, "Stats", stats.get(), BindUsage::ReadWrite))
            return false;
        if (!ctx.Dispatch(*pScanLocal, scanGroups)) return Fail("dispatch ScanLocal falhou");
        ctx.UavBarrier(*counts);
        ctx.UavBarrier(*groupSums);

        // 3b) Scan das somas dos grupos (1 grupo de 1024 threads) + total em Stats[0]
        if (!Bind(*pScanSums, "ClusterGridCB", gridCB.get(), BindUsage::Constant) ||
            !Bind(*pScanSums, "ClusterCounts", counts.get(), BindUsage::ReadWrite) ||
            !Bind(*pScanSums, "GroupSums", groupSums.get(), BindUsage::ReadWrite) ||
            !Bind(*pScanSums, "Stats", stats.get(), BindUsage::ReadWrite))
            return false;
        if (!ctx.Dispatch(*pScanSums, 1)) return Fail("dispatch ScanGroupSums falhou");
        ctx.UavBarrier(*groupSums);
        ctx.UavBarrier(*stats);

        // 4) Preenche LightIndexList + LightGrid
        if (!Bind(*pFill, "ClusterGridCB", gridCB.get(), BindUsage::Constant) ||
            !Bind(*pFill, "Lights", clusterLights.get(), BindUsage::ReadOnly) ||
            !Bind(*pFill, "LightIndexList", lightIndex.get(), BindUsage::ReadWrite) ||
            !Bind(*pFill, "LightGrid", lightGrid.get(), BindUsage::ReadWrite) ||
            !Bind(*pFill, "ClusterCounts", counts.get(), BindUsage::ReadWrite) ||
            !Bind(*pFill, "GroupSums", groupSums.get(), BindUsage::ReadWrite))
            return false;
        if (!ctx.Dispatch(*pFill, cullGroups)) return Fail("dispatch Fill falhou");
        ctx.UavBarrier(*lightIndex);
        ctx.UavBarrier(*lightGrid);

        // 5) Stats -> readback (lido alguns frames depois, sem travar a GPU)
        if (!ctx.CopyBuffer(*stats, *readback, 4)) return Fail("CopyBuffer de Stats falhou");
        return true;
    }
};

ClusterSystem::ClusterSystem(const ClusterSystemDesc& desc) : m_impl(std::make_unique<Impl>()) { m_impl->desc = desc; }
ClusterSystem::~ClusterSystem() = default;
ClusterSystem::ClusterSystem(ClusterSystem&&) noexcept = default;
ClusterSystem& ClusterSystem::operator=(ClusterSystem&&) noexcept = default;

bool ClusterSystem::Execute(IGraphicsDevice& dev, const LightingSystem& lights, const FrameLightView& view,
                            const ClusterCamera& cam) {
    Impl& s = *m_impl;
    s.lastError.clear();

    if (cam.screenWidth == 0 || cam.screenHeight == 0 || !(cam.zNear > 0.0f) || !(cam.zFar > cam.zNear) ||
        !(cam.proj00 > 0.0f) || !(cam.proj11 > 0.0f))
        return s.Fail("ClusterCamera inválida (tela, near/far ou projeção)");

    if (s.boundDevice != &dev) s.ResetDevice(&dev);

    // Decide o modo (uma vez por dispositivo; Auto tenta GPU e cai no CPU se algo faltar).
    if (!s.pipelinesTried) {
        s.pipelinesTried = true;
        s.fallbackReason.clear();
        const ClusterMode want = s.desc.mode;
        if (want == ClusterMode::Cpu) {
            s.active = ClusterMode::Cpu;
        } else if (!dev.Caps().computeShaders) {
            s.active = ClusterMode::Cpu;
            s.fallbackReason = "dispositivo sem compute shaders";
        } else if (!s.CreateAllPipelines(dev)) {
            s.active = ClusterMode::Cpu;
            s.pPrep.reset(); s.pCount.reset(); s.pScanLocal.reset(); s.pScanSums.reset(); s.pFill.reset();
        } else {
            s.active = ClusterMode::Gpu;
        }
        if (want == ClusterMode::Gpu && s.active != ClusterMode::Gpu)
            return s.Fail("modo Gpu pedido, mas indisponível: " + s.fallbackReason);
    }

    s.UpdateGrid(cam, view.pointCount, view.spotCount);
    s.AdaptBudget(*this);
    if (!s.EnsureBuffers(dev, view.pointCount + view.spotCount)) return s.Fail("falha ao criar buffers de cluster");

    return s.active == ClusterMode::Gpu ? s.RunGpu(dev, lights, view, cam) : s.RunCpu(dev, view, cam);
}

uint64_t ClusterSystem::AllocatedBytes() const {
    const Impl& s = *m_impl;
    if (!s.counts) return 0;
    const uint64_t clusters = s.allocClusters;
    return clusters * 4 + DivUp(uint32_t(clusters), kClusterScanGroupSize) * 4ull + clusters * sizeof(ClusterCell) +
           uint64_t(s.allocMaxIndices) * 4;
}

ClusterMode ClusterSystem::ActiveMode() const { return m_impl->active; }
const std::string& ClusterSystem::LastError() const { return m_impl->lastError; }
const std::string& ClusterSystem::FallbackReason() const { return m_impl->fallbackReason; }
const ClusterGridGPU& ClusterSystem::Grid() const { return m_impl->grid; }
IGpuBuffer* ClusterSystem::GridBuffer() const { return m_impl->gridCB.get(); }
IGpuBuffer* ClusterSystem::LightGridBuffer() const { return m_impl->lightGrid.get(); }
IGpuBuffer* ClusterSystem::LightIndexBuffer() const { return m_impl->lightIndex.get(); }
IGpuBuffer* ClusterSystem::DebugStatsBuffer() const { return m_impl->stats.get(); }
IGpuBuffer* ClusterSystem::DebugCountsBuffer() const { return m_impl->counts.get(); }

bool ClusterSystem::ReadbackStats(ClusterStats& out) const {
    const Impl& s = *m_impl;
    out = {};
    if (s.active == ClusterMode::Cpu) {
        out = s.cpuStats;
        return out.valid;
    }
    if (!s.readback) return false;
    uint32_t v = 0;
    if (!s.readback->ReadBack(&v, 4)) return false;
    out.valid = true;
    out.requestedIndices = v;
    out.maxIndices = s.grid.maxIndices;
    out.overflow = uint64_t(v) > s.grid.maxIndices;
    return true;
}

} // namespace LightPrism
