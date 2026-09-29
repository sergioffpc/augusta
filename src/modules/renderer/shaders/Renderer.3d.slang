// Draws the flat-shaded scene geometry (world-space positions plus a face
// normal and a base color per vertex). Not meant to survive PresentationWorld's real
// material/lighting model, which doesn't exist yet (see renderer.h's header
// comment): one directional light, a world-space checker so scale and
// position read on an otherwise untextured surface, darkening the base color
// on alternate cells.

cbuffer PerFrameCB
{
    float4x4 gViewProj;
};

static const float3 kLightDirection = normalize(float3(0.4f, 1.0f, 0.6f));
static const float kAmbient = 0.25f;
static const float kCheckerLight = 1.0f;
static const float kCheckerDark = 0.75f;

struct VsIn
{
    float3 pos : POSITION;
    float3 normal : NORMAL;
    float3 color : COLOR;
};

struct VsOut
{
    float4 posH : SV_Position;
    float3 posW : POSITION;
    float3 normal : NORMAL;
    float3 color : COLOR;
};

VsOut vsMain(VsIn vIn)
{
    VsOut vOut;
    vOut.posH = mul(gViewProj, float4(vIn.pos, 1.0f));
    vOut.posW = vIn.pos;
    vOut.normal = vIn.normal;
    vOut.color = vIn.color;
    return vOut;
}

float4 psMain(VsOut vOut) : SV_Target
{
    // Nudged off the cell boundaries so faces lying exactly on an integer
    // plane (a floor at y = 0) don't flicker between cells.
    const float3 cell = floor(vOut.posW + 0.001f);
    const bool light = fmod(abs(cell.x + cell.y + cell.z), 2.0f) < 1.0f;
    const float3 albedo = vOut.color * (light ? kCheckerLight : kCheckerDark);

    // abs(): winding isn't verified end to end (culling is off), so light
    // both sides of a face the same rather than trust the normal's sign.
    const float diffuse = abs(dot(normalize(vOut.normal), kLightDirection));
    return float4(albedo * (kAmbient + (1.0f - kAmbient) * diffuse), 1.0f);
}
