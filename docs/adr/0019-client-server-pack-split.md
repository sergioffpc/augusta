# Client/Server Pack Split

Client and server packs are baked separately from the same source: the client pack is full (geometry, textures, meshes, audio), the server pack is stripped (collision geometry, spawn points, hitboxes only), keeping the headless Linux server lean.
