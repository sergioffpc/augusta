# Client/Server Pack Split

Client and server packs are baked from the same source in one cook run (built in
issue #51): the client pack is full (geometry, textures, meshes, audio, plus
collision geometry, spawn points, hitboxes, character eyes and character
skeletons); the server pack is stripped down to collision geometry, spawn
points, hitboxes, character eyes and character skeletons, omitting the
mesh/texture/audio content the headless Linux server never needs, plus the
scenario's Lua scripts (ADR-0031, ADR-0039), which only the server ever reads: a
client is sent the values a script decides, never the script. The server pack
also carries the hash of the client pack cooked with it (ADR-0031, ADR-0038),
since a signature alone proves only that a pack came from the signing key, not
that the two packs came from the same cook. Collision/spawn/hitbox data lands in
both packs rather than server-only, since the client needs it too (client-side
prediction of movement against the same geometry the server uses, and drawing
where a Shot lands, ADR-0044; hit detection itself runs only on the server,
ADR-0024). A character's eye lands in both for the opposite reason: it is the
client's camera, and the server fires each Shot from it (ADR-0040). A
character's skeleton lands in both, the same list of joints in each, including
joints that carry no hitbox, so a joint is the same thing on either side
(ADR-0061): the server poses hitboxes by it, and a skeleton is small. Its skin
bindings, when something reads them, are an optional section of the mesh blob
and so client-only.
