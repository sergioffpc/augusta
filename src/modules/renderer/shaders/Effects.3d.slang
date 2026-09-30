// Draws the fight's effects (ADR-0014, on the D3D12 path): tracers, impacts
// on the Map and muzzle flashes, in world space, and the crosshair and hit
// marker over the frame, already in clip space (an identity gViewProj). Unlit:
// each vertex carries the color it glows with, and renderer.cpp's blend state
// adds it to what is behind (effects) or writes it over it (the overlay). The
// vertex layout is the scene's (Renderer.3d.slang); the normal goes unused.

cbuffer PerFrameCB
{
    float4x4 gViewProj;
};

struct VsIn
{
    float3 pos : POSITION;
    float3 normal : NORMAL;
    float3 color : COLOR;
};

struct VsOut
{
    float4 posH : SV_Position;
    float3 color : COLOR;
};

VsOut vsMain(VsIn vIn)
{
    VsOut vOut;
    vOut.posH = mul(gViewProj, float4(vIn.pos, 1.0f));
    vOut.color = vIn.color;
    return vOut;
}

float4 psMain(VsOut vOut) : SV_Target
{
    return float4(vOut.color, 1.0f);
}
