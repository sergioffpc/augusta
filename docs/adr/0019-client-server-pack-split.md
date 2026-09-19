# Client/Server Pack Split

Client and server packs are baked from the same source in one cook run (issue #51): the client pack is full (geometry, textures, meshes, audio, plus collision geometry, spawn points, and hitboxes); the server pack is stripped down to collision geometry, spawn points, and hitboxes only, omitting the mesh/texture/audio content the headless Linux server never needs. Collision/spawn/hitbox data lands in both packs rather than server-only, since the client needs it too (client-side prediction, movement, and hit-detection against the same geometry the server uses).
