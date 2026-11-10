// lp_softrender: renderiza a cena de demonstração por software e grava PNGs.
//   lp_softrender [pasta_saida] [largura altura] [luzes_pontuais] [spots] [tile] [intensidade_sol] [sombras 0|1] [ibl 0|1]
// Gera: lit.png (clusters), lit_all.png (todas as luzes), heatmap.png, slices.png, diff_x100.png,
//       cascades.png (cascata de sombra por pixel), lit_noshadow.png (sem sombras, para comparar)
//       com ibl=1: ibl_full.png (SH + split-sum + SSAO, com céu), ibl_noao.png, ibl_ambient_only.png, ao_ssao.png, ao_truth.png
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "LightPrism/Soft/DemoScene.hpp"
#include "LightPrism/Soft/Png.hpp"

using namespace LightPrism;
using namespace LightPrism::Soft;

int main(int argc, char** argv) {
    const std::string dir = argc > 1 ? argv[1] : ".";
    const uint32_t w = argc > 3 ? uint32_t(std::atoi(argv[2])) : 1280u;
    const uint32_t h = argc > 3 ? uint32_t(std::atoi(argv[3])) : 720u;
    const uint32_t numPoint = argc > 4 ? uint32_t(std::atoi(argv[4])) : 120u;
    const uint32_t numSpot = argc > 5 ? uint32_t(std::atoi(argv[5])) : 12u;
    const uint32_t tile = argc > 6 ? uint32_t(std::atoi(argv[6])) : 16u;
    const float sunIntensity = argc > 7 ? float(std::atof(argv[7])) : 1.5f;
    const bool useShadows = argc > 8 ? std::atoi(argv[8]) != 0 : true;
    const bool useIbl = argc > 9 ? std::atoi(argv[9]) != 0 : false;
    if (w == 0 || h == 0 || tile == 0) { std::fprintf(stderr, "argumentos invalidos\n"); return 2; }

    LightingConfig cfg;
    cfg.maxPointLights = 4096;
    LightingSystem lights(cfg);
    PopulateDemoLights(lights, numPoint, numSpot, sunIntensity);

    const Scene scene = MakeDemoScene();
    const CameraSetup cam = MakeDemoCamera(w, h);
    SoftRenderer renderer(scene, cam, tile, 24, 128);

    FrameParams fp;
    fp.cameraPosition = cam.eye;
    fp.preExposure = 1.0f;
    const FrameLightView& frame = lights.BuildFrame(fp);

    // Sombras do sol: cascatas do CSM + shadow maps por raycast (a engine real desenharia os mapas na GPU).
    CsmConfig csmCfg;
    csmCfg.cascadeCount = 3;
    csmCfg.shadowMapSize = 1024;
    csmCfg.shadowDistance = 60.0f;
    csmCfg.pcfTaps = 4;
    CsmCameraInput ci;
    ci.view = renderer.View();
    ci.proj00 = renderer.Proj().m[0][0];
    ci.proj11 = renderer.Proj().m[1][1];
    ci.zNear = cam.zNear;
    ci.zFar = cam.zFar;
    ShadowSetup shadowSetup;
    double shadowMs = 0.0;
    CsmResult csm;
    if (useShadows) {
        const auto t0 = std::chrono::steady_clock::now();
        csm = ComputeCsm(csmCfg, ci, frame.sun.direction);
        shadowSetup = BuildShadowSetup(scene, csm.gpu, frame.sun.direction);
        shadowMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    }
    const ShadowSetup* shadows = useShadows ? &shadowSetup : nullptr;

    FrameBuffer fbAll, fbCl, fbNo;
    RenderStats stAll, stCl, stNo;
    renderer.Render(frame, ShadeMode::AllLights, fbAll, stAll, shadows);
    renderer.Render(frame, ShadeMode::Clustered, fbCl, stCl, shadows);
    if (useShadows) renderer.Render(frame, ShadeMode::Clustered, fbNo, stNo, nullptr);

    const double maxDiff = MaxAbsDiff(fbAll, fbCl);
    const auto path = [&](const char* n) { return dir + "/" + n; };
    bool ok = true;
    ok &= WritePng(path("lit.png").c_str(), ToneMapToRgb8(fbCl), w, h);
    ok &= WritePng(path("lit_all.png").c_str(), ToneMapToRgb8(fbAll), w, h);
    ok &= WritePng(path("heatmap.png").c_str(), HeatmapRgb8(fbCl, stCl.maxLightsInCluster), w, h);
    ok &= WritePng(path("slices.png").c_str(), SliceRgb8(fbCl, renderer.Grid()), w, h);
    ok &= WritePng(path("diff_x100.png").c_str(), DiffRgb8(fbAll, fbCl, 100.0f), w, h);
    if (useShadows) {
        ok &= WritePng(path("cascades.png").c_str(), CascadeRgb8(fbCl), w, h);
        ok &= WritePng(path("lit_noshadow.png").c_str(), ToneMapToRgb8(fbNo), w, h);
    }
    if (!ok) { std::fprintf(stderr, "falha ao gravar PNGs em '%s'\n", dir.c_str()); return 1; }

    if (useIbl) {
        Ibl::ProceduralSky sky;
        sky.sunDir = -frame.sun.direction;  // aponta PARA o sol
        const Ibl::EnvFn env = [&sky](const Vec3& d) { return sky.Radiance(d); };
        const auto ti = std::chrono::steady_clock::now();
        Ibl::IblData ibl = Ibl::BuildIblData(env, 96, 64, 6, 32);
        ibl.intensity = 0.6f;
        const double iblMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - ti).count();

        const GBufferView gb = renderer.ExportGBuffer();
        Ssao::SsaoConfig sc;
        sc.radius = 0.7f;
        Ssao::SsaoInput in;
        in.width = w; in.height = h; in.viewDepth = gb.viewDepth.data(); in.viewNormal = gb.viewNormal.data();
        in.proj00 = renderer.Proj().m[0][0]; in.proj11 = renderer.Proj().m[1][1];
        std::vector<float> ao, aoTruth;
        const auto ts = std::chrono::steady_clock::now();
        Ssao::ComputeSsao(sc, in, ao);
        const double ssaoMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - ts).count();
        renderer.ComputeRayTracedAO(sc.radius, 128, aoTruth);

        IndirectSetup ind;
        ind.ibl = &ibl;
        ind.background = [&](const Vec3& d) { return sky.Radiance(d) * ibl.intensity; };
        FrameBuffer fbFull, fbNoAo, fbAmb;
        RenderStats st;
        ind.ao = &ao;
        renderer.Render(frame, ShadeMode::Clustered, fbFull, st, shadows, &ind);
        ind.ao = nullptr;
        renderer.Render(frame, ShadeMode::Clustered, fbNoAo, st, shadows, &ind);
        LightingSystem noLights;  // só o ambiente: sem luzes diretas
        FrameParams fp0; fp0.cameraPosition = cam.eye;
        const FrameLightView& frame0 = noLights.BuildFrame(fp0);
        ind.ao = &ao;
        renderer.Render(frame0, ShadeMode::Clustered, fbAmb, st, nullptr, &ind);
        bool ok2 = true;
        ok2 &= WritePng(path("ibl_full.png").c_str(), ToneMapToRgb8(fbFull), w, h);
        ok2 &= WritePng(path("ibl_noao.png").c_str(), ToneMapToRgb8(fbNoAo), w, h);
        ok2 &= WritePng(path("ibl_ambient_only.png").c_str(), ToneMapToRgb8(fbAmb), w, h);
        ok2 &= WritePng(path("ao_ssao.png").c_str(), AoRgb8(ao, gb.hit), w, h);
        ok2 &= WritePng(path("ao_truth.png").c_str(), AoRgb8(aoTruth, gb.hit), w, h);
        if (!ok2) { std::fprintf(stderr, "falha ao gravar PNGs do IBL\n"); return 1; }
        std::printf("IBL: SH9 + cubemap 64 (6 mips) + LUT 32x32 em %.0f ms; SSAO (16 amostras, raio %.1f m) %.0f ms em CPU\n", iblMs, sc.radius,
                    ssaoMs);
    }

    const ClusterGridGPU& g = renderer.Grid();
    std::printf("Light Prism soft renderer  %ux%u  tile %u  grid %ux%ux%u = %u clusters\n", w, h, tile, g.gridX, g.gridY, g.gridZ,
                stCl.clusterCount);
    std::printf("luzes: %u pontuais + %u spots (descartadas pelo orcamento: %u)\n", frame.pointCount, frame.spotCount, frame.droppedLights);
    std::printf("pixels com geometria: %llu\n", (unsigned long long)stCl.pixelsHit);
    std::printf("avaliacoes de luz  todas: %llu   clusters: %llu   (%.1f%% do custo)\n", (unsigned long long)stAll.lightEvaluations,
                (unsigned long long)stCl.lightEvaluations, 100.0 * double(stCl.lightEvaluations) / double(stAll.lightEvaluations));
    std::printf("clusters nao vazios: %u de %u   luzes/cluster: media %.1f, max %u   overflow: %s\n", stCl.nonEmptyClusters,
                stCl.clusterCount, stCl.avgLightsPerNonEmptyCluster, stCl.maxLightsInCluster, stCl.overflow ? "SIM" : "nao");
    std::printf("tempo (ms)  gbuffer %.1f  cull %.1f  shade todas %.1f  shade clusters %.1f\n", stCl.gbufferMs, stCl.cullMs, stAll.shadeMs,
                stCl.shadeMs);
    std::printf("diferenca maxima clusters vs todas (HDR linear): %.3g\n", maxDiff);
    if (useShadows) {
        uint64_t inShadow = 0;
        for (size_t i = 0; i < fbCl.sunVisibility.size(); ++i)
            if (fbCl.hit[i] && fbCl.sunVisibility[i] < 0.5f) ++inShadow;
        std::printf("sombras: %u cascatas de %ux%u, %.0f ms (raycast em CPU); pixels em sombra do sol: %.1f%%\n", csm.gpu.cascadeCount,
                    csmCfg.shadowMapSize, csmCfg.shadowMapSize, shadowMs, 100.0 * double(inShadow) / double(stCl.pixelsHit));
        for (uint32_t k = 0; k < csm.gpu.cascadeCount; ++k)
            std::printf("  cascata %u: profundidade %.1f-%.1f m, texel %.1f cm\n", k, csm.gpu.cascades[k].splitNear,
                        csm.gpu.cascades[k].splitFar, 100.0 * csm.gpu.cascades[k].texelWorldSize);
    }
    return 0;
}
