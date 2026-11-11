// SpriteUI MVP shader. ABI must match SpriteInstanceGPU (80 bytes).
// The backend binds one instance buffer and draws a unit quad instanced per batch.

struct SpriteInstanceGPU
{
    float4 transformRow0; // m00, m01, tx, depth
    float4 transformRow1; // m10, m11, ty, reserved
    float4 uvRect;        // u, v, width, height
    float4 tint;          // linear premultiplied RGBA
    uint4  resourceIds;   // textureSlice, materialIndex, clipIndex, flags
};

cbuffer SpriteUIGlobals : register(b0)
{
    row_major float4x4 ViewProjection;
    uint BaseInstance;
    float MsdfPxRange;   // Amplitude do MSDF em pixels do atlas (FontMetrics::pxRange).
    uint2 _GlobalPadding;
};

StructuredBuffer<SpriteInstanceGPU> SpriteInstances : register(t0);
Texture2DArray SpriteTextures : register(t1);
// minX,minY,maxX,maxY. O backend deve converter de pixels logicos (ClipRectGPU) para pixels
// do framebuffer (multiplicando pelo fator de DPI) ao preencher este buffer.
StructuredBuffer<float4> ClipRects : register(t2);
SamplerState SpriteSampler : register(s0);

struct VSOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    float4 tint : COLOR0;
    nointerpolation uint4 resourceIds : TEXCOORD1;
    float2 local : TEXCOORD2;                 // Unit-quad coordinate (0..1), used by the SDF panel.
    nointerpolation float3 panel : TEXCOORD3; // width, height (logical px), corner radius (px).
};

VSOutput VSMain(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
    static const float2 Quad[6] = {
        float2(0.0, 0.0), float2(1.0, 0.0), float2(0.0, 1.0),
        float2(0.0, 1.0), float2(1.0, 0.0), float2(1.0, 1.0)
    };

    const SpriteInstanceGPU item = SpriteInstances[BaseInstance + instanceId];
    const float2 local = Quad[vertexId];

    const float2 world = float2(
        dot(item.transformRow0.xy, local) + item.transformRow0.z,
        dot(item.transformRow1.xy, local) + item.transformRow1.z
    );

    VSOutput output;
    output.position = mul(float4(world, item.transformRow0.w, 1.0), ViewProjection);
    output.uv = item.uvRect.xy + local * item.uvRect.zw;
    output.tint = item.tint;
    output.resourceIds = item.resourceIds;
    output.local = local;
    // The quad axes are the columns of the 2x2 part of the transform: their lengths are the size.
    output.panel = float3(
        length(float2(item.transformRow0.x, item.transformRow1.x)),
        length(float2(item.transformRow0.y, item.transformRow1.y)),
        item.transformRow1.w); // transformRow1.w (formerly reserved) carries the corner radius.
    return output;
}

float Median(float r, float g, float b)
{
    return max(min(r, g), min(max(r, g), b));
}

// Analytic rounded rectangle. Mirrors Rendering::EvaluatePanelSdf (PanelSdf.hpp), radius clamp included.
float EvaluatePanelSDF(float2 samplePos, float2 halfSize, float cornerRadius)
{
    const float r = clamp(cornerRadius, 0.0, min(halfSize.x, halfSize.y));
    const float2 d = abs(samplePos) - halfSize + r;
    return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0) - r;
}

float4 PSMain(VSOutput input) : SV_TARGET
{
    float4 color = input.tint;

    const uint flags = input.resourceIds.w;
    if ((flags & 0x2u) != 0u) // Flag_MSDFText
    {
        const float3 msd = SpriteTextures.Sample(
            SpriteSampler, float3(input.uv, input.resourceIds.x)).rgb;
        const float sd = Median(msd.r, msd.g, msd.b) - 0.5;
        uint atlasWidth;
        uint atlasHeight;
        uint arraySize;
        uint mipLevels;
        SpriteTextures.GetDimensions(0, atlasWidth, atlasHeight, arraySize, mipLevels);
        const float2 unitRange = float2(MsdfPxRange, MsdfPxRange) / float2(atlasWidth, atlasHeight);
        const float2 screenTexSize = rcp(max(fwidth(input.uv), float2(1e-5, 1e-5)));
        const float screenPxRange = max(0.5 * dot(unitRange, screenTexSize), 1.0);
        const float screenPxDistance = sd * screenPxRange;
        const float textAlpha = clamp(screenPxDistance + 0.5, 0.0, 1.0);
        color *= textAlpha;
    }
    else if ((flags & 0x8u) != 0u) // Flag_BitmapText: grayscale coverage in the red channel.
    {
        const float coverage = SpriteTextures.Sample(
            SpriteSampler, float3(input.uv, input.resourceIds.x)).r;
        color *= coverage;
    }
    else
    {
        const float4 texel = SpriteTextures.Sample(
            SpriteSampler, float3(input.uv, input.resourceIds.x));
        color *= texel;
    }

    if ((flags & 0x4u) != 0u) // Flag_RoundedPanel
    {
        const float2 halfSize = input.panel.xy * 0.5;
        const float2 samplePos = (input.local - 0.5) * input.panel.xy;
        const float sd = EvaluatePanelSDF(samplePos, halfSize, input.panel.z);
        // fwidth(sd) is the size of one device pixel in logical units: DPI independent 1px edge.
        const float coverage = saturate(0.5 - sd / max(fwidth(sd), 1e-5));
        color *= coverage;
    }

    const uint clipIndex = input.resourceIds.z;
    if (clipIndex != 0xFFFFFFFFu)
    {
        const float4 clipRect = ClipRects[clipIndex];
        const float2 pixel = input.position.xy;
        clip(min(pixel.x - clipRect.x, clipRect.z - pixel.x));
        clip(min(pixel.y - clipRect.y, clipRect.w - pixel.y));
    }

    return color;
}
