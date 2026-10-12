# Texture Compression

Textures are baked to BC7/BC5/BC4 in DDS containers via DirectXTex/texconv.

Source images (PNG/JPEG) are decoded by stb_image into 8-bit RGBA, not by
DirectXTex's WIC loader: WIC and the COM it needs are Windows-only, and the
cooker runs on Linux too. stb_image is the one decoder on both platforms, so a
cook gives the same bytes on either. It drops what WIC would have kept: 16-bit
channels are narrowed to 8 bits, and a PNG's gamma, sRGB and colour-profile
chunks are ignored - the pixels are taken as stored.

## Considered Options

KTX2 was evaluated and rejected — it offers no benefit without Basis Universal
transcoding on a D3D12-only client.
