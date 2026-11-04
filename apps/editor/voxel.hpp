// voxel.hpp - Chunks de blocos (estilo Minecraft): geracao da malha, terreno de teste e edicao de blocos.
//
// Um chunk e uma grade 16x16x16 (VoxelChunk). Em vez de uma entidade por bloco, a malha do chunk tem so as faces
// que dao para o ar, separadas por tipo de bloco: cada tipo vira um lote desenhado uma vez por chunk. Um chunk de
// terreno com 500 chunks custa ~500 x 7 desenhos, nao milhoes de cubos. Veja docs/PHYSICS.md para a fisica (nao usada aqui).
#pragma once

#include "components.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <memory>

namespace editor {

// Malhas de um chunk, uma por tipo de bloco (cada tipo tem o seu material/cor).
struct ChunkMeshes {
  tool::Mesh byType[kBlockTypes];
  unsigned revision = 0;   // revisao do VoxelChunk usada na construcao
  float cell = 0.f;        // tamanho do bloco usado na construcao
  bool built = false;
  unsigned faces = 0;      // faces emitidas (estatistica e testes)
  uint64_t stamp = 0;      // revisoes do proprio chunk e dos vizinhos na construcao (muda = refazer a malha)
};

// Vizinhos nas 6 faces: +x, -x, +y, -y, +z, -z. nullptr = sem vizinho (a borda vira face visivel).
using ChunkNeighbors = std::array<const VoxelChunk*, 6>;

// Gera as faces visiveis de um chunk. Uma face so entra se o bloco do outro lado for ar. Esse bloco pode estar
// dentro do chunk ou, na borda, no chunk vizinho (`nb`): sem vizinho, a borda conta como ar e a face aparece.
inline void buildChunkMeshes(const VoxelChunk& c, ChunkMeshes& out, const ChunkNeighbors& nb = ChunkNeighbors{}) {
  for (auto& m : out.byType) { m.pos.clear(); m.normal.clear(); m.idx.clear(); m.uv.clear(); m.revision = c.revision; }
  out.faces = 0;
  const float s = c.cell;
  auto solidAt = [&](int x, int y, int z) -> bool {
    if (x >= 0 && y >= 0 && z >= 0 && x < kChunkSize && y < kChunkSize && z < kChunkSize) return c.at(x, y, z) != 0;
    const int face = x >= kChunkSize ? 0 : x < 0 ? 1 : y >= kChunkSize ? 2 : y < 0 ? 3 : z >= kChunkSize ? 4 : 5;
    const VoxelChunk* o = nb[static_cast<size_t>(face)];
    if (!o || o->cell != c.cell) return false;   // vizinho de outro tamanho nao conta (evita faces erradas)
    auto wrap = [](int v) { return v >= kChunkSize ? v - kChunkSize : v < 0 ? v + kChunkSize : v; };
    return o->at(wrap(x), wrap(y), wrap(z)) != 0;
  };
  for (int y = 0; y < kChunkSize; ++y)
    for (int z = 0; z < kChunkSize; ++z)
      for (int x = 0; x < kChunkSize; ++x) {
        const unsigned char t = c.at(x, y, z);
        if (!t) continue;
        tool::Mesh& m = out.byType[t];
        for (int axis = 0; axis < 3; ++axis)
          for (int side = -1; side <= 1; side += 2) {
            int n[3] = {x, y, z}; n[axis] += side;
            if (solidAt(n[0], n[1], n[2])) continue;   // vizinho solido (dentro ou no chunk ao lado): a face fica escondida
            // Quad da face: eixo normal `axis` na borda (0 ou 1 do bloco), e os eixos u, v no plano da face.
            const int u = (axis + 1) % 3, v = (axis + 2) % 3;
            float nrm[3] = {0, 0, 0}; nrm[axis] = static_cast<float>(side);
            const tool::Vec3 normal{nrm[0], nrm[1], nrm[2]};
            const float base = static_cast<float>(side > 0 ? 1 : 0);
            const float corners[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
            tool::u32 vi[4];
            for (int k = 0; k < 4; ++k) {
              float p[3] = {static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)};
              p[axis] = (p[axis] + base) * s;
              p[u] = (p[u] + corners[k][0]) * s;
              p[v] = (p[v] + corners[k][1]) * s;
              vi[k] = m.addVtx({p[0], p[1], p[2]}, normal);
            }
            m.addTri(vi[0], vi[1], vi[2]);
            m.addTri(vi[0], vi[2], vi[3]);
            ++out.faces;
          }
      }
  out.revision = c.revision; out.cell = c.cell; out.built = true;
}

// Terreno de teste: colinas suaves, camada de grama em cima, terra abaixo, pedra no fundo. Cada chunk (cx, cz) cobre
// as coordenadas globais de blocos [cx*16, cx*16+16) x [cz*16, cz*16+16); a altura usa so as coordenadas globais,
// entao chunks vizinhos encaixam sem costura.
inline float terrainHeight(int gx, int gz) {
  return 6.f + 3.f * std::sin(static_cast<float>(gx) * 0.35f) * std::cos(static_cast<float>(gz) * 0.27f) + 2.f * std::sin(static_cast<float>(gx + gz) * 0.11f);
}
inline void fillTerrain(VoxelChunk& c, int cx, int cz) {
  for (int x = 0; x < kChunkSize; ++x)
    for (int z = 0; z < kChunkSize; ++z) {
      const int h = static_cast<int>(std::floor(terrainHeight(cx * kChunkSize + x, cz * kChunkSize + z)));
      for (int y = 0; y < kChunkSize; ++y) {
        unsigned char t = 0;
        if (y < h - 3) t = 3;            // pedra
        else if (y < h) t = 2;           // terra
        else if (y == h) t = 1;          // grama (topo)
        c.blocks[VoxelChunk::index(x, y, z)] = t;
      }
    }
  ++c.revision;
}

// Edita um bloco. Sobe a revisao: a malha e refeita na proxima vez que o chunk for desenhado.
inline bool setBlock(VoxelChunk& c, int x, int y, int z, unsigned char type) {
  if (x < 0 || y < 0 || z < 0 || x >= kChunkSize || y >= kChunkSize || z >= kChunkSize || type >= kBlockTypes) return false;
  const int i = VoxelChunk::index(x, y, z);
  if (c.blocks[i] == type) return false;
  c.blocks[i] = type; ++c.revision;
  return true;
}

inline void clearChunk(VoxelChunk& c) {
  for (unsigned char& b : c.blocks) b = 0;
  ++c.revision;
}

inline unsigned solidCount(const VoxelChunk& c) {
  unsigned n = 0;
  for (unsigned char b : c.blocks) n += b != 0;
  return n;
}

} // namespace editor
