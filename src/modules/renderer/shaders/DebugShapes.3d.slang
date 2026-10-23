// Draws the Debug overlays' lines and triangles (ADR-0075), tessellated on the
// CPU by augusta::debug, in world space from the camera's gViewProj. Unlit:
// each vertex carries its shape's colour and alpha, which the Renderer's blend
// state lays over what is behind it.

cbuffer PerFrameCB : register(b0)
{
    float4x4 gViewProj;
};

struct VsIn
{
    float3 pos : POSITION;
    float3 color : COLOR;
    float alpha : ALPHA;
};

struct VsOut
{
    float4 posH : SV_Position;
    float4 color : COLOR;
};

VsOut vsMain(VsIn vIn)
{
    VsOut vOut;
    vOut.posH = mul(gViewProj, float4(vIn.pos, 1.0f));
    vOut.color = float4(vIn.color, vIn.alpha);
    return vOut;
}

float4 psMain(VsOut vOut) : SV_Target
{
    return vOut.color;
}
