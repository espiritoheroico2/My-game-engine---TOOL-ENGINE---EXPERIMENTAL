// ToolEngine - FSR 1.0 (EASU + RCAS) como passes de pixel shader em tela cheia.
//
// Portado para HLSL a partir da referencia publica da AMD:
//   https://github.com/GPUOpen-Effects/FidelityFX-FSR  (ffx-fsr/ffx_fsr1.h e ffx_a.h, v1.20210629)
// A logica do EASU (direcao/comprimento de borda, kernel Lanczos2 aproximado de 12 taps, anti-ringing) e do RCAS
// (lobo negativo limitado pela vizinhanca em cruz) e a da AMD. Mudancas feitas aqui, so de plataforma:
//   - Os 12 taps do EASU sao lidos com Load() de inteiros (esta interface do RHI nao expoe sampler/gather);
//     os vizinhos b c / e f g h / i j k l / n o sao exatamente os mesmos texels que o gather4 original lia.
//   - Opcional: conversao linear <-> perceptual (gama 2.0) em volta dos filtros, porque o alvo da engine e sRGB
//     (o hardware entrega linear ao Load) e o FSR espera cor perceptual.
//   - Aproximacoes de rcp/rsq do ffx_a.h em float de 32 bits (APrxLoRcpF1, APrxMedRcpF1, APrxLoRsqF1).
//
// ------------------------------------------------------------------------------------------------------------------------------
// FidelityFX Super Resolution Sample
//
// Copyright (c) 2021 Advanced Micro Devices, Inc. All rights reserved.
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files(the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions :
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
// ------------------------------------------------------------------------------------------------------------------------------

cbuffer FsrCB : register(b0) {
  float4 Con0;      // EASU: xy = escala saida->entrada (entrada/saida), zw = 0.5*escala - 0.5   (FsrEasuCon, con0)
  float2 SrcSize;   // tamanho da textura de entrada em pixels
  float  Gamma;     // > 0.5: converte linear->perceptual (sqrt) na entrada e de volta (quadrado) na saida
  float  RcasScale; // RCAS: 2^-stops (FsrRcasCon); 0 = sem nitidez
};
Texture2D<float4> Src : register(t0);

struct VSOut { float4 clip : SV_Position; };
VSOut VSMain(float3 pos : ATTRIB0) { VSOut o; o.clip = float4(pos, 1.0); return o; }

// Aproximacoes do ffx_a.h (32 bits).
float APrxLoRcpF1(float a) { return asfloat(0x7ef07ebb - asuint(a)); }
float APrxMedRcpF1(float a) { float b = asfloat(0x7ef19fff - asuint(a)); return b * (-b * a + 2.0); }
float APrxLoRsqF1(float a) { return asfloat(0x5f347d74 - (asuint(a) >> 1)); }
float ASatF1(float a) { return saturate(a); }

// Le um texel com clamp de borda e converte para o espaco perceptual quando pedido.
float4 Fetch(int2 p) {
  int2 q = clamp(p, int2(0, 0), int2(SrcSize) - 1);
  float4 c = Src.Load(int3(q, 0));
  if (Gamma > 0.5) c.rgb = sqrt(max(c.rgb, 0.0));
  return c;
}
float3 Out3(float3 c) { return Gamma > 0.5 ? c * c : c; }

// ---------------------------------------------------------------- EASU (FsrEasuTapF / FsrEasuSetF / FsrEasuF)
void FsrEasuTapF(inout float3 aC, inout float aW, float2 off, float2 dir, float2 len, float lob, float clp, float3 c) {
  float2 v;
  v.x = (off.x * ( dir.x)) + (off.y * dir.y);
  v.y = (off.x * (-dir.y)) + (off.y * dir.x);
  v *= len;
  float d2 = v.x * v.x + v.y * v.y;
  d2 = min(d2, clp);
  // Aproximacao de lanczos2 sem sin(), rcp() ou sqrt() (comentario original da AMD):
  //  (25/16 * (2/5 * x^2 - 1)^2 - (25/16 - 1)) * (1/4 * x^2 - 1)^2
  float wB = (2.0 / 5.0) * d2 + (-1.0);
  float wA = lob * d2 + (-1.0);
  wB *= wB;
  wA *= wA;
  wB = (25.0 / 16.0) * wB + (-(25.0 / 16.0 - 1.0));
  float w = wB * wA;
  aC += c * w; aW += w;
}

void FsrEasuSetF(inout float2 dir, inout float len, float2 pp, bool biS, bool biT, bool biU, bool biV,
                 float lA, float lB, float lC, float lD, float lE) {
  float w = 0.0;
  if (biS) w = (1.0 - pp.x) * (1.0 - pp.y);
  if (biT) w =        pp.x  * (1.0 - pp.y);
  if (biU) w = (1.0 - pp.x) *        pp.y;
  if (biV) w =        pp.x  *        pp.y;
  float dc = lD - lC;
  float cb = lC - lB;
  float lenX = max(abs(dc), abs(cb));
  lenX = APrxLoRcpF1(lenX);
  float dirX = lD - lB;
  dir.x += dirX * w;
  lenX = ASatF1(abs(dirX) * lenX);
  lenX *= lenX;
  len += lenX * w;
  float ec = lE - lC;
  float ca = lC - lA;
  float lenY = max(abs(ec), abs(ca));
  lenY = APrxLoRcpF1(lenY);
  float dirY = lE - lA;
  dir.y += dirY * w;
  lenY = ASatF1(abs(dirY) * lenY);
  lenY *= lenY;
  len += lenY * w;
}

float4 EasuMain(VSOut i) : SV_Target {
  int2 ip = int2(i.clip.xy);
  // Posicao de 'f' no espaco da entrada.
  float2 pp = float2(ip) * Con0.xy + Con0.zw;
  float2 fp = floor(pp);
  pp -= fp;
  int2 f0 = int2(fp);
  // Kernel de 12 taps:   b c / e f g h / i j k l / n o   (mesmos texels do gather4 da AMD).
  float4 tb = Fetch(f0 + int2( 0, -1)), tc = Fetch(f0 + int2( 1, -1));
  float4 te = Fetch(f0 + int2(-1,  0)), tf = Fetch(f0 + int2( 0,  0)), tg = Fetch(f0 + int2( 1,  0)), th = Fetch(f0 + int2( 2,  0));
  float4 ti = Fetch(f0 + int2(-1,  1)), tj = Fetch(f0 + int2( 0,  1)), tk = Fetch(f0 + int2( 1,  1)), tl = Fetch(f0 + int2( 2,  1));
  float4 tn = Fetch(f0 + int2( 0,  2)), to = Fetch(f0 + int2( 1,  2));
  // Luma aproximada (x2) em 2 FMA.
  float bL = tb.b * 0.5 + (tb.r * 0.5 + tb.g), cL = tc.b * 0.5 + (tc.r * 0.5 + tc.g);
  float iL = ti.b * 0.5 + (ti.r * 0.5 + ti.g), jL = tj.b * 0.5 + (tj.r * 0.5 + tj.g);
  float fL = tf.b * 0.5 + (tf.r * 0.5 + tf.g), eL = te.b * 0.5 + (te.r * 0.5 + te.g);
  float kL = tk.b * 0.5 + (tk.r * 0.5 + tk.g), lL = tl.b * 0.5 + (tl.r * 0.5 + tl.g);
  float hL = th.b * 0.5 + (th.r * 0.5 + th.g), gL = tg.b * 0.5 + (tg.r * 0.5 + tg.g);
  float oL = to.b * 0.5 + (to.r * 0.5 + to.g), nL = tn.b * 0.5 + (tn.r * 0.5 + tn.g);
  // Acumula direcao e comprimento para a interpolacao bilinear.
  float2 dir = float2(0.0, 0.0);
  float len = 0.0;
  FsrEasuSetF(dir, len, pp, true,  false, false, false, bL, eL, fL, gL, jL);
  FsrEasuSetF(dir, len, pp, false, true,  false, false, cL, fL, gL, hL, kL);
  FsrEasuSetF(dir, len, pp, false, false, true,  false, fL, iL, jL, kL, nL);
  FsrEasuSetF(dir, len, pp, false, false, false, true,  gL, jL, kL, lL, oL);
  // Normaliza com aproximacao e trata o caso perto de zero.
  float2 dir2 = dir * dir;
  float dirR = dir2.x + dir2.y;
  bool zro = dirR < (1.0 / 32768.0);
  dirR = APrxLoRsqF1(dirR);
  dirR = zro ? 1.0 : dirR;
  dir.x = zro ? 1.0 : dir.x;
  dir *= float2(dirR, dirR);
  // {0..2} -> {0..1} e formato quadratico.
  len = len * 0.5;
  len *= len;
  // Alonga o kernel (1.0 na vertical/horizontal, sqrt(2) na diagonal).
  float stretch = (dir.x * dir.x + dir.y * dir.y) * APrxLoRcpF1(max(abs(dir.x), abs(dir.y)));
  float2 len2 = float2(1.0 + (stretch - 1.0) * len, 1.0 + (-0.5) * len);
  // A janela se desloca conforme a quantidade de borda.
  float lob = 0.5 + ((1.0 / 4.0 - 0.04) - 0.5) * len;
  float clp = APrxLoRcpF1(lob);
  // Min/max dos 4 vizinhos mais proximos (f, g, j, k) para o anti-ringing.
  float3 min4 = min(min(min(tf.rgb, tg.rgb), tj.rgb), tk.rgb);
  float3 max4 = max(max(max(tf.rgb, tg.rgb), tj.rgb), tk.rgb);
  float3 aC = float3(0.0, 0.0, 0.0);
  float aW = 0.0;
  FsrEasuTapF(aC, aW, float2( 0.0, -1.0) - pp, dir, len2, lob, clp, tb.rgb); // b
  FsrEasuTapF(aC, aW, float2( 1.0, -1.0) - pp, dir, len2, lob, clp, tc.rgb); // c
  FsrEasuTapF(aC, aW, float2(-1.0,  1.0) - pp, dir, len2, lob, clp, ti.rgb); // i
  FsrEasuTapF(aC, aW, float2( 0.0,  1.0) - pp, dir, len2, lob, clp, tj.rgb); // j
  FsrEasuTapF(aC, aW, float2( 0.0,  0.0) - pp, dir, len2, lob, clp, tf.rgb); // f
  FsrEasuTapF(aC, aW, float2(-1.0,  0.0) - pp, dir, len2, lob, clp, te.rgb); // e
  FsrEasuTapF(aC, aW, float2( 1.0,  1.0) - pp, dir, len2, lob, clp, tk.rgb); // k
  FsrEasuTapF(aC, aW, float2( 2.0,  1.0) - pp, dir, len2, lob, clp, tl.rgb); // l
  FsrEasuTapF(aC, aW, float2( 2.0,  0.0) - pp, dir, len2, lob, clp, th.rgb); // h
  FsrEasuTapF(aC, aW, float2( 1.0,  0.0) - pp, dir, len2, lob, clp, tg.rgb); // g
  FsrEasuTapF(aC, aW, float2( 1.0,  2.0) - pp, dir, len2, lob, clp, to.rgb); // o
  FsrEasuTapF(aC, aW, float2( 0.0,  2.0) - pp, dir, len2, lob, clp, tn.rgb); // n
  // Normaliza e remove ringing.
  float3 pix = min(max4, max(min4, aC * (1.0 / max(aW, 1e-6))));
  // Alfa: bilinear dos 4 vizinhos mais proximos (a AMD nao trata alfa no EASU).
  float a = lerp(lerp(tf.a, tg.a, pp.x), lerp(tj.a, tk.a, pp.x), pp.y);
  return float4(Out3(pix), a);
}

// ---------------------------------------------------------------- RCAS (FsrRcasF)
#define FSR_RCAS_LIMIT (0.25 - (1.0 / 16.0))

float4 RcasMain(VSOut i) : SV_Target {
  int2 sp = int2(i.clip.xy);
  // Vizinhanca minima em cruz:   b / d e f / h
  float3 b = Fetch(sp + int2( 0, -1)).rgb;
  float3 d = Fetch(sp + int2(-1,  0)).rgb;
  float4 ee = Fetch(sp);
  float3 e = ee.rgb;
  float3 f = Fetch(sp + int2( 1,  0)).rgb;
  float3 h = Fetch(sp + int2( 0,  1)).rgb;
  float bR = b.r, bG = b.g, bB = b.b;
  float dR = d.r, dG = d.g, dB = d.b;
  float eR = e.r, eG = e.g, eB = e.b;
  float fR = f.r, fG = f.g, fB = f.b;
  float hR = h.r, hG = h.g, hB = h.b;
  // Luma x2.
  float bL = bB * 0.5 + (bR * 0.5 + bG);
  float dL = dB * 0.5 + (dR * 0.5 + dG);
  float eL = eB * 0.5 + (eR * 0.5 + eG);
  float fL = fB * 0.5 + (fR * 0.5 + fG);
  float hL = hB * 0.5 + (hR * 0.5 + hG);
  // Min e max do anel.
  float mn4R = min(min(min(bR, dR), fR), hR);
  float mn4G = min(min(min(bG, dG), fG), hG);
  float mn4B = min(min(min(bB, dB), fB), hB);
  float mx4R = max(max(max(bR, dR), fR), hR);
  float mx4G = max(max(max(bG, dG), fG), hG);
  float mx4B = max(max(max(bB, dB), fB), hB);
  // Limitadores (rcp de alta precisao; a AMD marca isto como obrigatorio). Guarda contra divisao por zero em preto.
  float2 peakC = float2(1.0, -1.0 * 4.0);
  float hitMinR = min(mn4R, eR) * rcp(max(4.0 * mx4R, 1e-6));
  float hitMinG = min(mn4G, eG) * rcp(max(4.0 * mx4G, 1e-6));
  float hitMinB = min(mn4B, eB) * rcp(max(4.0 * mx4B, 1e-6));
  float hitMaxR = (peakC.x - max(mx4R, eR)) * rcp(min(4.0 * mn4R + peakC.y, -1e-6));
  float hitMaxG = (peakC.x - max(mx4G, eG)) * rcp(min(4.0 * mn4G + peakC.y, -1e-6));
  float hitMaxB = (peakC.x - max(mx4B, eB)) * rcp(min(4.0 * mn4B + peakC.y, -1e-6));
  float lobeR = max(-hitMinR, hitMaxR);
  float lobeG = max(-hitMinG, hitMaxG);
  float lobeB = max(-hitMinB, hitMaxB);
  float lobe = max(-FSR_RCAS_LIMIT, min(max(max(lobeR, lobeG), lobeB), 0.0)) * RcasScale;
  // Resolve (rcp de precisao media, para evitar mudanca visivel de tonalidade).
  float rcpL = APrxMedRcpF1(4.0 * lobe + 1.0);
  float3 pix = float3((lobe * bR + lobe * dR + lobe * hR + lobe * fR + eR) * rcpL,
                      (lobe * bG + lobe * dG + lobe * hG + lobe * fG + eG) * rcpL,
                      (lobe * bB + lobe * dB + lobe * hB + lobe * fB + eB) * rcpL);
  return float4(Out3(saturate(pix)), ee.a);
}
