// M1 spike shader (ADR-0009): textures and shades one rotating primitive.
// Not meant to survive past the spike - PresentationWorld's real material/
// lighting model doesn't exist yet (see renderer.h's header comment).

cbuffer PerFrameCB
{
    float4x4 gMvp;
};

Texture2D gTexture;
SamplerState gSampler;

struct VsIn
{
    float3 pos : POSITION;
    float2 uv : TEXCOORD;
};

struct VsOut
{
    float4 posH : SV_Position;
    float2 uv : TEXCOORD;
};

VsOut vsMain(VsIn vIn)
{
    VsOut vOut;
    vOut.posH = mul(gMvp, float4(vIn.pos, 1.0f));
    vOut.uv = vIn.uv;
    return vOut;
}

float4 psMain(VsOut vOut) : SV_Target
{
    return gTexture.Sample(gSampler, vOut.uv);
}
