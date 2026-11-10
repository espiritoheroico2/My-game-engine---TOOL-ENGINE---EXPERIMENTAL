#pragma once
// Cache de cascatas do CSM (otimização 4). O texel snapping faz a matriz de uma cascata mudar só quando o centro da
// esfera anda um texel inteiro; cascatas distantes quase nunca mudam. Se a matriz é idêntica e o conteúdo é estático,
// o shadow map do frame anterior ainda vale: não redesenha.
//   - geometria estática: re-renderiza só quando a matriz muda (ou depois de Invalidate());
//   - com casters dinâmicos: cascata i é atualizada pelo menos a cada (1 << i) frames (0: todo frame, 1: a cada 2, ...),
//     limitado por `maxStride`.
#include <cstdint>
#include <cstring>

#include "LightPrism/Shadow/Csm.hpp"

namespace LightPrism {

class CascadeCache {
public:
    // Retorna a máscara de bits das cascatas que precisam ser re-renderizadas neste frame.
    uint32_t Update(const CsmGPU& csm, bool dynamicCasters, uint32_t maxStride = 4) {
        if (csm.cascadeCount != m_count || csm.shadowMapSize != m_size) { Invalidate(); m_count = csm.cascadeCount; m_size = csm.shadowMapSize; }
        uint32_t mask = 0;
        for (uint32_t i = 0; i < csm.cascadeCount && i < kMaxCsmCascades; ++i) {
            const bool changed = !m_valid[i] || std::memcmp(&m_last[i], &csm.cascades[i].viewProj, sizeof(Mat4)) != 0;
            uint32_t stride = 1u << i;
            if (stride > maxStride) stride = maxStride;
            const bool stale = dynamicCasters && (m_age[i] + 1 >= stride);
            if (changed || stale) {
                mask |= 1u << i;
                m_last[i] = csm.cascades[i].viewProj;
                m_valid[i] = true;
                m_age[i] = 0;
            } else {
                ++m_age[i];
            }
        }
        return mask;
    }

    // Geometria estática mudou (ou o mapa foi perdido): tudo precisa ser refeito.
    void Invalidate() { for (bool& v : m_valid) v = false; }

private:
    Mat4 m_last[kMaxCsmCascades]{};
    bool m_valid[kMaxCsmCascades]{};
    uint32_t m_age[kMaxCsmCascades]{};
    uint32_t m_count = 0;
    float m_size = 0.0f;
};

} // namespace LightPrism
