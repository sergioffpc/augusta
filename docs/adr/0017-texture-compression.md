# Texture Compression

Textures are baked to BC7/BC5/BC4 in DDS containers via DirectXTex/texconv.

## Considered Options

KTX2 was evaluated and rejected — it offers no benefit without Basis Universal transcoding on a D3D12-only client.
