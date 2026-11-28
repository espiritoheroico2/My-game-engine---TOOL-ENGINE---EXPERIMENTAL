// Trechos comuns da oclusao por GPU (occluder.hlsl e occlusion_cull.hlsl): instancia de 160 B e esfera envolvente no mundo.
// A conta da esfera e a mesma de frustum_cull.hlsl / tool::render::transformSphere (raio escalado pelo maior ganho, conservador).

struct InstanceData {      // igual a InstanceGPU (160 B) / pbr.hlsl
  float4x4 world;
  float4 baseColor;
  float metallic, roughness, receiveShadow, _pad1;
  float4 texInfo;
  float4 emissive;
  float4 fogColor;
  float4 fogParams;
};

// Esfera local (xyz = centro, w = raio) levada ao mundo. false = bounds ou matriz invalidos (quem chama trata como "nao sei").
bool WorldSphere(float4x4 w, float4 sphere, out float3 c, out float radius) {
  c = float3(0, 0, 0); radius = 0.0;
  if (sphere.w < 0.0) return false;
  if (!all(isfinite(sphere))) return false;
  // Matriz nao afim (ultima linha diferente de 0,0,0,1): bounds invalidos, como na CPU.
  if (abs(w[3][0]) > 1e-5 || abs(w[3][1]) > 1e-5 || abs(w[3][2]) > 1e-5 || abs(w[3][3] - 1.0) > 1e-5) return false;
  c = mul(w, float4(sphere.xyz, 1.0)).xyz;
  float3 r0 = abs(float3(w[0][0], w[0][1], w[0][2]));
  float3 r1 = abs(float3(w[1][0], w[1][1], w[1][2]));
  float3 r2 = abs(float3(w[2][0], w[2][1], w[2][2]));
  float maxRow = max(r0.x + r0.y + r0.z, max(r1.x + r1.y + r1.z, r2.x + r2.y + r2.z));
  float3 c0 = abs(float3(w[0][0], w[1][0], w[2][0]));
  float3 c1 = abs(float3(w[0][1], w[1][1], w[2][1]));
  float3 c2 = abs(float3(w[0][2], w[1][2], w[2][2]));
  float maxCol = max(c0.x + c0.y + c0.z, max(c1.x + c1.y + c1.z, c2.x + c2.y + c2.z));
  radius = sphere.w * sqrt(maxRow * maxCol);
  return all(isfinite(c)) && isfinite(radius);
}
