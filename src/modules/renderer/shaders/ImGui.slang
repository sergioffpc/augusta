// Draws Dear ImGui's triangles (the debug HUD): each vertex in pixels, with a
// texture coordinate into the font atlas and a colour. Adapted from Donut's
// imgui_vertex.hlsl and imgui_pixel.hlsl (MIT, see imgui_renderer.cpp).

struct Constants
{
    float2 invDisplaySize;
};

// NVRHI's push constants: root constants at b0 on D3D12.
[[vk::push_constant]] ConstantBuffer<Constants> gConstants : register(b0);
Texture2D gTexture : register(t0);
SamplerState gSampler : register(s0);

struct VsIn
{
    float2 pos : POSITION;
    float2 uv : TEXCOORD;
    float4 color : COLOR;
};

struct VsOut
{
    float4 posH : SV_Position;
    float4 color : COLOR;
    float2 uv : TEXCOORD;
};

VsOut vsMain(VsIn vIn)
{
    VsOut vOut;
    // Pixels, from the top left, to clip space.
    vOut.posH = float4(vIn.pos * gConstants.invDisplaySize * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    vOut.color = vIn.color;
    vOut.uv = vIn.uv;
    return vOut;
}

float4 psMain(VsOut vOut) : SV_Target
{
    return vOut.color * gTexture.Sample(gSampler, vOut.uv);
}
