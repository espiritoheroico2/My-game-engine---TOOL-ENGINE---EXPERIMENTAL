// ToolEngine - Particulas: migracao do emissor do sistema antigo (componente ParticleComp do editor, removido) para um
// Effect da biblioteca. O dado antigo e lido de cenas antigas (chave "particles") e convertido ao abrir a cena.
// Quadro do que cada campo antigo virou: docs/planos/36_MIGRACAO_PARTICULAS.md.
#pragma once
#include <algorithm>

#include "tool/particles/effect.hpp"

namespace tool::particles {

// Campos do emissor antigo (mesmos nomes e padroes do ParticleComp). Dado puro, so serve para converter.
struct LegacyEmitter {
  bool emitting = true, loop = true;
  int maxParticles = 200;
  f32 rate = 40.f, lifetime = 1.5f, speed = 3.f, spread = 25.f;
  f32 size = 0.12f, sizeEnd = 0.02f, gravityScale = 1.f, bounce = 0.f, floorY = -1000.f;
  f32 color[3] = {1.f, 0.6f, 0.2f}, color2[3] = {1.f, 0.42f, 0.12f}, color3[3] = {0.9f, 0.22f, 0.07f}, colorEnd[3] = {0.8f, 0.1f, 0.05f};
  f32 alpha = 0.9f;
  bool worldSpace = true;
  int form = 0;                         // 0 bolinha, 1 quadrado, 2 triangulo, 3 estrela, 4 anel, 5 risco, 6 fumaca
  int area = 0;                         // 0 ponto, 1 caixa aleatoria, 2 esfera aleatoria
  f32 areaSize[3] = {1.f, 0.2f, 1.f};   // medidas COMPLETAS (a caixa vira meias-medidas; a esfera usa [0] como diametro)
  bool fadeOpacity = true;
  f32 fadeDelay = 0.f;                  // segundos com a opacidade cheia antes de comecar a sumir
  bool route = false;
  int routeCount = 0;
  f32 routePts[8][3] = {{0, 0, 0}, {0, 1, 0}, {1, 2, 0}, {1, 3, 1}, {0, 4, 1}, {0, 5, 0}, {0, 0, 0}, {0, 0, 0}};
  bool tornado = false;
  f32 tornadoSpeed = 1.5f, tornadoRadius = 0.6f, tornadoHeight = 2.f;
};

// Resultado da conversao: o efeito e os dois valores que ficam no componente do editor (nao no efeito).
struct LegacyConversion {
  Effect fx;
  bool playing = true;
  int maxParticles = 200;
};

// Converte o emissor antigo num efeito equivalente. `gravity` e a gravidade do mundo (o antigo usava a da cena vezes
// gravityScale). Com trajetoria ligada o antigo ignorava gravidade e velocidade (posicao vinha da curva); o tornado
// idem: nesses casos a velocidade inicial e zero e nao ha gravidade nem chao.
inline LegacyConversion effectFromLegacy(const LegacyEmitter& o, const f32 gravity[3]) {
  LegacyConversion r;
  Effect& fx = r.fx;
  fx.name = "migrated";
  fx.shader = ShaderKind::Alpha;
  fx.form = static_cast<u32>(std::clamp(o.form, 0, 6));
  r.playing = o.emitting;
  r.maxParticles = std::clamp(o.maxParticles, 1, 5000);
  // Emissor: taxa e area de nascimento (caixa e esfera do antigo).
  fx.emitter.rate = o.rate;
  fx.emitter.loop = o.loop;
  fx.emitter.seed = 12345;
  if (o.area == 1) {
    fx.emitter.shape = EmitShape::Box;
    for (int k = 0; k < 3; ++k) fx.emitter.boxHalf[k] = std::max(0.f, o.areaSize[k]) * 0.5f;
  } else if (o.area == 2) {
    fx.emitter.shape = EmitShape::Sphere;
    fx.emitter.radius = std::max(0.f, o.areaSize[0]) * 0.5f;
  } else {
    fx.emitter.shape = EmitShape::Point;
  }
  const bool path = o.route && o.routeCount >= 1, spiral = !path && o.tornado;
  // Inicializacao: vida +-20%, velocidade 0,75x a 1,25x, cone de `spread` graus em torno de +Y.
  fx.init.lifeMin = o.lifetime * 0.8f; fx.init.lifeMax = o.lifetime * 1.2f;
  fx.init.speedMin = path || spiral ? 0.f : o.speed * 0.75f; fx.init.speedMax = path || spiral ? 0.f : o.speed * 1.25f;
  fx.init.dir[0] = 0; fx.init.dir[1] = 1; fx.init.dir[2] = 0;
  fx.init.coneDeg = o.spread;
  fx.init.sizeMin = fx.init.sizeMax = o.size;
  fx.init.temp0 = 0;
  fx.init.color = packColor(o.color[0], o.color[1], o.color[2], std::clamp(o.alpha, 0.f, 1.f));
  // Movimento.
  if (path) {
    Module m; m.kind = ModuleKind::Route; m.follow = true;
    m.routeCount = static_cast<u32>(std::clamp(o.routeCount, 0, 8));
    for (u32 i = 0; i < kMaxRoutePoints; ++i) for (int k = 0; k < 3; ++k) m.pts[i][k] = o.routePts[i][k];
    fx.modules.push_back(m);
  } else if (spiral) {
    Module m; m.kind = ModuleKind::Swirl; m.follow = true;
    m.swirlTurns = o.tornadoSpeed; m.swirlRadius = o.tornadoRadius; m.swirlHeight = o.tornadoHeight;
    fx.modules.push_back(m);
  } else {
    if (o.gravityScale != 0.f) {
      Module g; g.kind = ModuleKind::Gravity;
      for (int k = 0; k < 3; ++k) g.g[k] = gravity[k] * o.gravityScale;
      fx.modules.push_back(g);
    }
    if (o.bounce > 0.f) {
      Module f; f.kind = ModuleKind::Floor; f.floorY = o.floorY; f.bounce = std::clamp(o.bounce, 0.f, 1.f); f.friction = 0.9f;
      fx.modules.push_back(f);
    }
  }
  // Vida: tamanho, cor em 4 paradas (alfa 1) e opacidade (cheia ate o atraso, depois some).
  { Module s; s.kind = ModuleKind::SizeOverLife; s.s0 = o.size; s.s1 = o.sizeEnd; fx.modules.push_back(s); }
  {
    Module c; c.kind = ModuleKind::ColorOverLife; c.stops = 4;
    c.c0 = packColor(o.color[0], o.color[1], o.color[2], 1.f);
    c.c2 = packColor(o.color2[0], o.color2[1], o.color2[2], 1.f);
    c.c3 = packColor(o.color3[0], o.color3[1], o.color3[2], 1.f);
    c.c1 = packColor(o.colorEnd[0], o.colorEnd[1], o.colorEnd[2], 1.f);
    fx.modules.push_back(c);
  }
  if (o.fadeOpacity) {
    Module a; a.kind = ModuleKind::AlphaOverLife; a.a0 = 1.f; a.a1 = 0.f;
    a.delay = std::clamp(o.fadeDelay / std::max(o.lifetime, 1e-3f), 0.f, 0.99f);
    fx.modules.push_back(a);
  }
  return r;
}

} // namespace tool::particles
