#pragma once
// Cena determinística usada pela ferramenta lp_softrender e pelos testes: piso xadrez, parede, esferas com
// materiais variados, muitas luzes pontuais coloridas, alguns spots e um sol fraco.
#include <cmath>
#include <cstdint>

#include "LightPrism/LightingSystem.hpp"
#include "LightPrism/Soft/SoftRenderer.hpp"

namespace LightPrism::Soft {

struct DemoRng {
    uint32_t state = 0x1234567u;
    float Next01() { // LCG simples e portável: mesma cena em qualquer plataforma
        state = state * 1664525u + 1013904223u;
        return float(state >> 8) / float(1u << 24);
    }
    float Range(float a, float b) { return a + (b - a) * Next01(); }
};

inline Vec3 HueToRgb(float h) {
    const float r = std::fabs(h * 6.0f - 3.0f) - 1.0f, g = 2.0f - std::fabs(h * 6.0f - 2.0f), b = 2.0f - std::fabs(h * 6.0f - 4.0f);
    return {Saturate(r), Saturate(g), Saturate(b)};
}

inline Scene MakeDemoScene() {
    Scene s;
    Plane floor;
    floor.point = {0, 0, 0}; floor.normal = {0, 1, 0};
    floor.material = {{0.75f, 0.75f, 0.75f}, 0.0f, 0.55f};
    floor.materialB = {{0.12f, 0.12f, 0.14f}, 0.0f, 0.35f};
    floor.checkerSize = 2.0f;
    s.planes.push_back(floor);

    Plane wall;
    wall.point = {0, 0, -34}; wall.normal = {0, 0, 1};
    wall.material = {{0.6f, 0.6f, 0.65f}, 0.0f, 0.8f};
    s.planes.push_back(wall);

    // 4 fileiras x 7 esferas; metal/rugosidade variam por posição.
    for (int row = 0; row < 4; ++row)
        for (int col = 0; col < 7; ++col) {
            Sphere sp;
            sp.center = {-9.0f + 3.0f * float(col), 0.9f, -4.0f - 6.0f * float(row)};
            sp.radius = 0.9f;
            sp.material.albedo = HueToRgb(float((row * 7 + col) % 11) / 11.0f) * 0.7f + Vec3{0.2f, 0.2f, 0.2f};
            sp.material.metallic = (col % 2) ? 1.0f : 0.0f;
            sp.material.roughness = 0.15f + 0.14f * float(col % 6);
            s.spheres.push_back(sp);
        }
    return s;
}

inline CameraSetup MakeDemoCamera(uint32_t width, uint32_t height) {
    CameraSetup c;
    c.eye = {0.0f, 6.5f, 7.0f};
    c.target = {0.0f, 0.2f, -12.0f};
    c.fovYRad = 1.05f;
    c.zNear = 0.1f;
    c.zFar = 120.0f;
    c.width = width;
    c.height = height;
    return c;
}

// Preenche `lights` com `numPoint` luzes pontuais e `numSpot` spots, mais um sol fraco.
inline void PopulateDemoLights(LightingSystem& lights, uint32_t numPoint, uint32_t numSpot, float sunIntensity = 0.25f) {
    DemoRng rng;
    for (uint32_t i = 0; i < numPoint; ++i) {
        PointLightDesc d;
        d.position = {rng.Range(-13.0f, 13.0f), rng.Range(0.4f, 3.2f), rng.Range(-30.0f, 2.0f)};
        d.radius = rng.Range(3.5f, 7.0f);
        d.color = HueToRgb(rng.Next01()) * 0.8f + Vec3{0.2f, 0.2f, 0.2f};
        d.intensity = rng.Range(14.0f, 40.0f);
        lights.AddPointLight(d);
    }
    for (uint32_t i = 0; i < numSpot; ++i) {
        SpotLightDesc d;
        d.position = {rng.Range(-10.0f, 10.0f), rng.Range(4.0f, 6.0f), rng.Range(-24.0f, -2.0f)};
        d.direction = Normalize(Vec3{rng.Range(-0.3f, 0.3f), -1.0f, rng.Range(-0.3f, 0.3f)});
        d.radius = rng.Range(9.0f, 14.0f);
        d.outerAngleRad = rng.Range(0.35f, 0.6f);
        d.innerAngleRad = d.outerAngleRad * 0.55f;
        d.color = HueToRgb(rng.Next01()) * 0.5f + Vec3{0.5f, 0.5f, 0.5f};
        d.intensity = rng.Range(120.0f, 260.0f);
        lights.AddSpotLight(d);
    }
    DirectionalLightDesc sun;
    sun.direction = Normalize(Vec3{0.4f, -1.0f, -0.3f});
    sun.color = {1.0f, 0.95f, 0.85f};
    sun.intensity = sunIntensity;
    lights.SetDirectionalLight(sun);
}

} // namespace LightPrism::Soft
