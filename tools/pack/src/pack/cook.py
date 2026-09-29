"""The asset cooker itself (ADR-0030): walks the authored OpenUSD stages a
scenario's manifest.yaml composes - one map, zero or more characters
(ADR-0041) - and bakes them into one signed client/server pack pair
(ADR-0031/ADR-0032). Walks each stage through pxr directly (the same
pip-installed usd-optimize build optimize.py already uses, rather than
linking a second, independently-built OpenUSD - see ADR-0030 for why those
can't coexist in one process), and calls the small native
_meshoptimizer/_textconv bindings only for the two pieces with no Python
equivalent (mesh optimization, texture compression).
"""

from __future__ import annotations

import math
from collections.abc import Callable, Sequence
from dataclasses import dataclass
from pathlib import Path

from pxr import Gf, Sdf, Usd, UsdGeom, UsdPhysics, UsdShade

from pack import _meshoptimizer, _textconv
from pack.pack import (
    AssetEntry,
    MeshData,
    SceneNode,
    encode_characters_blob,
    encode_eye_blob,
    encode_mesh_blob,
    encode_scene_blob,
    encode_script_blob,
    encode_spawn_point_blob,
    encode_texture_blob,
)
from pack.pack import ASSET_TYPE_CHARACTERS as _TYPE_CHARACTERS
from pack.pack import ASSET_TYPE_CLIENT_PACK as _TYPE_CLIENT_PACK
from pack.pack import ASSET_TYPE_COLLISION as _TYPE_COLLISION
from pack.pack import ASSET_TYPE_EYE as _TYPE_EYE
from pack.pack import ASSET_TYPE_HITBOX as _TYPE_HITBOX
from pack.pack import ASSET_TYPE_MESH as _TYPE_MESH
from pack.pack import ASSET_TYPE_SCENE as _TYPE_SCENE
from pack.pack import ASSET_TYPE_SCRIPT as _TYPE_SCRIPT
from pack.pack import ASSET_TYPE_SPAWN_POINT as _TYPE_SPAWN_POINT
from pack.pack import ASSET_TYPE_TEXTURE as _TYPE_TEXTURE
from pack.pack import CHARACTERS_PATH, CLIENT_PACK_PATH, NO_PARENT, TEXTURE_FORMAT_BC4, TEXTURE_FORMAT_BC5, TEXTURE_FORMAT_BC7, write_pack

# augusta:spawnPoint / augusta:hitbox: custom bool attributes (ADR-0032's
# authoring convention) rather than a native USD prim type. A hitbox is
# authored as a UsdGeomMesh with PhysicsCollisionAPI applied (same as any
# other collider) plus this marker.
_SPAWN_POINT_ATTR = "augusta:spawnPoint"
_HITBOX_ATTR = "augusta:hitbox"
# Node property (ADR-0032) carrying a visual mesh's constant displayColor as
# "r g b" linear floats; the client reads it as the mesh's base color.
BASE_COLOR_PROPERTY = "base_color"
# The default prim every character stage has, named the same regardless of the
# character's folder (ADR-0040), so the client finds its visual mesh at
# <manifest path>/Character/Visual.
CHARACTER_ROOT_PRIM = "Character"
# The child of CHARACTER_ROOT_PRIM every character stage has, whose origin is
# where the local player's camera sits (ADR-0040): the client reads it at
# <manifest path>/Character/Eye.
CHARACTER_EYE_PRIM = "Eye"
# augusta:textureFormat: selects which BC format a UsdUVTexture prim
# compresses to (ADR-0017/issue #49). Defaults to BC7 when absent/
# unrecognized.
_TEXTURE_FORMAT_ATTR = "augusta:textureFormat"
_USD_UV_TEXTURE_SHADER_ID = "UsdUVTexture"

# Z-up -> Y-up is a quarter turn about the X axis.
_Z_UP_TO_Y_UP_DEGREES = -90.0

_TEXTURE_FORMAT_NAMES = {TEXTURE_FORMAT_BC7: "bc7", TEXTURE_FORMAT_BC5: "bc5", TEXTURE_FORMAT_BC4: "bc4"}


class CookError(RuntimeError):
    """Raised when cooking fails: carries a code, the offending prim's
    path (empty if not tied to one), and a human-readable message.
    """

    def __init__(self, code: str, prim_path: str, message: str) -> None:
        super().__init__(f"cook failed{f' at {prim_path}' if prim_path else ''}: {message} ({code})")
        self.code = code
        self.prim_path = prim_path
        self.message = message


@dataclass
class CookReport:
    mesh_count: int
    texture_count: int
    node_count: int
    script_count: int


def _sanitize_prim_path(usd_prim_path: str) -> str:
    """Strips the leading '/' from a USD prim path - the pack-relative
    path ADR-0031 addresses its blobs by.
    """
    return usd_prim_path[1:] if usd_prim_path.startswith("/") else usd_prim_path


def _has_collision_enabled(prim: Usd.Prim) -> bool:
    if not prim.HasAPI(UsdPhysics.CollisionAPI):
        return False
    collision_api = UsdPhysics.CollisionAPI(prim)
    enabled = collision_api.GetCollisionEnabledAttr().Get()
    # USD defaults physics:collisionEnabled to true once the API is
    # applied, so an absent attribute (None) still counts as enabled.
    return enabled is None or bool(enabled)


def _read_texture_format(prim: Usd.Prim) -> int:
    attr = prim.GetAttribute(_TEXTURE_FORMAT_ATTR)
    value = attr.Get() if attr else None
    if value == "bc5":
        return TEXTURE_FORMAT_BC5
    if value == "bc4":
        return TEXTURE_FORMAT_BC4
    return TEXTURE_FORMAT_BC7


def _resolve_texture_file_path(asset_path: Sdf.AssetPath, stage_path: Path) -> Path:
    resolved = asset_path.resolvedPath or asset_path.path
    resolved_path = Path(resolved)
    if not resolved_path.is_absolute():
        resolved_path = stage_path.parent / resolved_path
    return resolved_path


def _cook_texture(prim: Usd.Prim, prim_path: str, stage_path: Path) -> tuple[bytes, int]:
    shader = UsdShade.Shader(prim)
    file_input = shader.GetInput("file")
    asset_path = file_input.Get() if file_input else None
    if not isinstance(asset_path, Sdf.AssetPath):
        raise CookError("missing_texture_file", prim_path, "UsdUVTexture prim has no inputs:file")

    texture_path = _resolve_texture_file_path(asset_path, stage_path)
    texture_format = _read_texture_format(prim)
    try:
        dds_bytes = _textconv.compress_texture(texture_path, _TEXTURE_FORMAT_NAMES[texture_format])
    except RuntimeError as error:
        raise CookError("texture_load_failed", prim_path, str(error)) from error
    return dds_bytes, texture_format


def _read_bool_attr(prim: Usd.Prim, attr_name: str) -> bool:
    attr = prim.GetAttribute(attr_name)
    value = attr.Get() if attr else None
    return bool(value)


def _decompose(matrix: Gf.Matrix4d) -> tuple[tuple[float, float, float], tuple[float, float, float, float], tuple[float, float, float]]:
    """Decomposes a USD local-to-parent transform matrix into the
    translation/rotation(x,y,z,w)/scale triple SceneNode stores (ADR-0032
    stores local transforms only, never world).
    """
    transform = Gf.Transform(matrix)
    translation = tuple(transform.GetTranslation())
    rotation = transform.GetRotation().GetQuat()
    imaginary = rotation.GetImaginary()
    rotation_xyzw = (imaginary[0], imaginary[1], imaginary[2], rotation.GetReal())
    scale = tuple(transform.GetScale())
    return translation, rotation_xyzw, scale


# (translation_xyz, rotation_xyzw, scale_xyz) - matches _decompose's own
# return shape above.
_IDENTITY_TRANSFORM = ((0.0, 0.0, 0.0), (0.0, 0.0, 0.0, 1.0), (1.0, 1.0, 1.0))


def _local_transform_of(prim: Usd.Prim):
    xformable = UsdGeom.Xformable(prim)
    if not xformable:
        return _IDENTITY_TRANSFORM
    matrix = xformable.GetLocalTransformation()
    return _decompose(matrix)


def _stage_correction_matrix(stage: Usd.Stage) -> Gf.Matrix4d:
    """Corrects a stage's own upAxis/metersPerUnit into the runtime's
    fixed Y-up/right-handed/1-meter convention (ADR-0032). Applied once,
    prepended to each top-level node's own local transform.
    """
    meters_per_unit = UsdGeom.GetStageMetersPerUnit(stage)
    correction = Gf.Matrix4d(1.0).SetScale(meters_per_unit)
    if UsdGeom.GetStageUpAxis(stage) == UsdGeom.Tokens.z:
        rotate_z_to_y = Gf.Matrix4d(1.0)
        rotate_z_to_y.SetRotate(Gf.Rotation(Gf.Vec3d(1.0, 0.0, 0.0), _Z_UP_TO_Y_UP_DEGREES))
        correction = rotate_z_to_y * correction
    return correction


def _validate_triangle_topology(face_vertex_counts, face_vertex_index_count: int, prim_path: str) -> int:
    """Validates every face is a triangle and that faceVertexCounts sums
    to faceVertexIndices' own length, returning that sum.
    """
    expected_index_count = 0
    for count in face_vertex_counts:
        if count != 3:
            raise CookError(
                "unsupported_topology", prim_path, f"face with {count} vertices, only triangles (3) are supported"
            )
        expected_index_count += count
    if expected_index_count != face_vertex_index_count:
        raise CookError(
            "inconsistent_topology",
            prim_path,
            f"faceVertexCounts sums to {expected_index_count} but faceVertexIndices has {face_vertex_index_count} entries",
        )
    return expected_index_count


def _read_indices(face_vertex_indices, point_count: int, prim_path: str) -> list[int]:
    indices = []
    for index in face_vertex_indices:
        if index < 0:
            raise CookError("negative_index", prim_path, f"negative index {index}")
        if index >= point_count:
            raise CookError("index_out_of_range", prim_path, f"index {index} out of range for {point_count} points")
        indices.append(index)
    return indices


def _optimize_mesh(mesh: MeshData) -> MeshData:
    """meshoptimizer pass (ADR-0016): vertex cache optimization,
    simplification, quantization - turns the as-authored read into
    GPU-ready data rather than a passthrough of the source mesh.
    """
    if not mesh.points or not mesh.indices:
        return mesh
    flat_points = [component for point in mesh.points for component in point]
    optimized_points, optimized_indices = _meshoptimizer.optimize_mesh(flat_points, mesh.indices)
    points = [tuple(optimized_points[i : i + 3]) for i in range(0, len(optimized_points), 3)]
    return MeshData(points=points, indices=list(optimized_indices))


def _read_raw_mesh_geometry(mesh: UsdGeom.Mesh, prim_path: str) -> MeshData:
    """Reads a UsdGeomMesh's points/triangle-index buffer as authored,
    with no meshoptimizer pass applied - shared by the visual path (optimized afterward) and
    collision/hitbox geometry reading (_build_node), where meshoptimizer's lossy simplification could let a
    physics query miss geometry it should have hit.
    """
    face_vertex_counts = mesh.GetFaceVertexCountsAttr().Get()
    usd_points = mesh.GetPointsAttr().Get()
    face_vertex_indices = mesh.GetFaceVertexIndicesAttr().Get()
    if face_vertex_counts is None or usd_points is None or face_vertex_indices is None:
        raise CookError("missing_mesh_data", prim_path, "missing points, faceVertexCounts, or faceVertexIndices")

    _validate_triangle_topology(face_vertex_counts, len(face_vertex_indices), prim_path)

    points = [(point[0], point[1], point[2]) for point in usd_points]
    indices = _read_indices(face_vertex_indices, len(points), prim_path)
    return MeshData(points=points, indices=indices)


# A UsdGeomCube's 8 corners, ordered (-,-,-) (+,-,-) (+,+,-) (-,+,-)
# (-,-,+) (+,-,+) (+,+,+) (-,+,+), and its 12 outward-CCW triangles.
_CUBE_CORNER_SIGNS = [(-1, -1, -1), (1, -1, -1), (1, 1, -1), (-1, 1, -1), (-1, -1, 1), (1, -1, 1), (1, 1, 1), (-1, 1, 1)]
_CUBE_INDICES = [4, 5, 6, 4, 6, 7, 1, 0, 3, 1, 3, 2, 5, 1, 2, 5, 2, 6, 0, 4, 7, 0, 7, 3, 3, 7, 6, 3, 6, 2, 0, 1, 5, 0, 5, 4]


def _read_cube_geometry(cube: UsdGeom.Cube, prim_path: str) -> MeshData:
    """Expands a UsdGeomCube (an edge-length box centered on its own
    origin) into the same triangle buffer a UsdGeomMesh box would give,
    so a cube is cooked exactly like a mesh from here on.
    """
    size = cube.GetSizeAttr().Get()
    if size is None or size <= 0:
        raise CookError("invalid_cube_size", prim_path, f"cube size {size} must be positive")
    half = size / 2
    points = [(sx * half, sy * half, sz * half) for sx, sy, sz in _CUBE_CORNER_SIGNS]
    return MeshData(points=points, indices=list(_CUBE_INDICES))


# Longitude segments and latitude rings-per-hemisphere for _read_capsule_geometry
# - a fixed tessellation density, not authorable: a capsule is a placeholder
# collision-derived shape (ADR-0040), not an art asset needing LOD control.
_CAPSULE_LONGITUDE_SEGMENTS = 12
_CAPSULE_LATITUDE_RINGS = 4

# axis token -> (up, right, forward) unit-vector basis: up is the axis the
# cylinder/hemispheres extend along, right/forward span each latitude ring's
# own circle.
_CAPSULE_BASIS = {
    UsdGeom.Tokens.x: ((1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, 1.0)),
    UsdGeom.Tokens.y: ((0.0, 1.0, 0.0), (1.0, 0.0, 0.0), (0.0, 0.0, 1.0)),
    UsdGeom.Tokens.z: ((0.0, 0.0, 1.0), (1.0, 0.0, 0.0), (0.0, 1.0, 0.0)),
}


def _read_capsule_geometry(capsule: UsdGeom.Capsule, prim_path: str) -> MeshData:
    """Tessellates a UsdGeomCapsule - a cylinder of `height` (excluding caps)
    and `radius`, capped by two hemispheres of the same radius, centered on
    its own origin and extended along `axis` - into a triangle buffer, the
    same "cook a primitive shape like a mesh from here on" treatment
    _read_cube_geometry gives UsdGeomCube. Built as rings of
    _CAPSULE_LONGITUDE_SEGMENTS vertices each, pole to pole: a bottom pole, a
    fan of hemisphere rings, the two equator rings the cylinder spans between,
    a mirrored fan of hemisphere rings, and a top pole.
    """
    height = capsule.GetHeightAttr().Get()
    radius = capsule.GetRadiusAttr().Get()
    axis = capsule.GetAxisAttr().Get()
    if height is None or radius is None or radius <= 0 or height < 0:
        raise CookError(
            "invalid_capsule_size", prim_path, f"capsule height {height} / radius {radius} must be non-negative/positive"
        )
    up, right, forward = _CAPSULE_BASIS.get(axis, _CAPSULE_BASIS[UsdGeom.Tokens.z])

    half_height = height / 2.0
    segments = _CAPSULE_LONGITUDE_SEGMENTS
    rings = _CAPSULE_LATITUDE_RINGS
    points: list[tuple[float, float, float]] = []

    def add_ring(ring_radius: float, axis_offset: float) -> int:
        start = len(points)
        for segment in range(segments):
            phi = 2.0 * math.pi * segment / segments
            points.append(
                tuple(
                    up[i] * axis_offset + right[i] * (ring_radius * math.cos(phi)) + forward[i] * (ring_radius * math.sin(phi))
                    for i in range(3)
                )
            )
        return start

    def hemisphere_ring(latitude_index: int, pole_sign: float) -> tuple[float, float]:
        theta = (math.pi / 2.0) * (latitude_index / rings)
        return radius * math.cos(theta), pole_sign * (half_height + radius * math.sin(theta))

    bottom_pole_index = len(points)
    points.append(tuple(component * -(half_height + radius) for component in up))
    bottom_ring_starts = [add_ring(*hemisphere_ring(latitude, -1.0)) for latitude in range(rings - 1, 0, -1)]
    bottom_equator_start = add_ring(radius, -half_height)
    top_equator_start = add_ring(radius, half_height)
    top_ring_starts = [add_ring(*hemisphere_ring(latitude, 1.0)) for latitude in range(1, rings)]
    top_pole_index = len(points)
    points.append(tuple(component * (half_height + radius) for component in up))

    indices: list[int] = []

    def add_fan(pole_index: int, ring_start: int, flip: bool) -> None:
        for segment in range(segments):
            a, b = ring_start + segment, ring_start + (segment + 1) % segments
            indices.extend([pole_index, b, a] if flip else [pole_index, a, b])

    def add_band(lower_start: int, upper_start: int) -> None:
        for segment in range(segments):
            a0, a1 = lower_start + segment, lower_start + (segment + 1) % segments
            b0, b1 = upper_start + segment, upper_start + (segment + 1) % segments
            indices.extend([a0, a1, b1, a0, b1, b0])

    # rings, pole to pole: bottom pole -> hemisphere rings -> equator -> equator
    # -> hemisphere rings -> top pole. flip=True on the top fan mirrors the
    # bottom fan's winding, since the top pole is approached from the
    # opposite direction.
    add_fan(bottom_pole_index, bottom_ring_starts[0], flip=False)
    for lower, upper in zip(bottom_ring_starts, bottom_ring_starts[1:]):
        add_band(lower, upper)
    add_band(bottom_ring_starts[-1], bottom_equator_start)
    add_band(bottom_equator_start, top_equator_start)
    for lower, upper in zip(top_ring_starts, top_ring_starts[1:]):
        add_band(lower, upper)
    add_fan(top_pole_index, top_ring_starts[-1], flip=True)

    return MeshData(points=points, indices=indices)


def _read_raw_geometry(prim: Usd.Prim, prim_path: str) -> MeshData | None:
    """Reads prim's triangle geometry as authored if it is a UsdGeomMesh,
    UsdGeomCube or UsdGeomCapsule, or None for any other prim type.
    """
    if prim.IsA(UsdGeom.Mesh):
        return _read_raw_mesh_geometry(UsdGeom.Mesh(prim), prim_path)
    if prim.IsA(UsdGeom.Cube):
        return _read_cube_geometry(UsdGeom.Cube(prim), prim_path)
    if prim.IsA(UsdGeom.Capsule):
        return _read_capsule_geometry(UsdGeom.Capsule(prim), prim_path)
    return None


def _read_base_color(prim: Usd.Prim) -> str | None:
    """prim's constant primvars:displayColor as an "r g b" string, or None
    if it has none. Only the first element is used: per-face/per-vertex
    color isn't supported.
    """
    gprim = UsdGeom.Gprim(prim)
    colors = gprim.GetDisplayColorAttr().Get() if gprim else None
    if not colors:
        return None
    return " ".join(f"{channel:g}" for channel in colors[0])


def _is_guide(prim: Usd.Prim) -> bool:
    """True if prim is authored with purpose "guide": a DCC-only helper
    (e.g. a spawn-point marker) that is never rendered.
    """
    imageable = UsdGeom.Imageable(prim)
    return bool(imageable) and imageable.GetPurposeAttr().Get() == UsdGeom.Tokens.guide


def _build_node(
    prim: Usd.Prim,
    correction: Gf.Matrix4d,
    node_index_of: dict[str, int],
    entries: list[AssetEntry],
) -> SceneNode:
    """Builds prim's own SceneNode, appending a spawn-point AssetEntry if
    augusta:spawnPoint is set, and - if prim is a UsdGeomMesh or UsdGeomCube - at
    most one geometry AssetEntry: kHitbox if augusta:hitbox is set, else
    kCollision if a PhysX collider is applied, else kMesh. node_index_of
    must already contain every ancestor of prim - guaranteed by pre-order
    traversal (see cook_scenario's own comment on its Traverse() call).
    """
    prim_path = _sanitize_prim_path(str(prim.GetPath()))
    node = SceneNode(name=prim_path)

    parent = prim.GetParent()
    if not parent.IsValid() or parent.IsPseudoRoot():
        node.parent_index = NO_PARENT
    else:
        # Always succeeds under pre-order traversal - see this function's
        # own comment.
        node.parent_index = node_index_of[str(parent.GetPath())]

    translation, rotation, scale = _local_transform_of(prim)
    if node.parent_index == NO_PARENT:
        # Only the stage's own top-level (parentless) nodes get the
        # up-axis/unit correction prepended.
        xformable = UsdGeom.Xformable(prim)
        local_matrix = xformable.GetLocalTransformation() if xformable else Gf.Matrix4d(1.0)
        # GfMatrix4d is row-vector convention (v' = v * M; "apply A then
        # B" composes as A * B) - local_matrix must be applied first
        # (the prim's own authored transform), correction second.
        translation, rotation, scale = _decompose(local_matrix * correction)
    node.translation = translation
    node.rotation = rotation
    node.scale = scale

    node.is_spawn_point = _read_bool_attr(prim, _SPAWN_POINT_ATTR)
    if node.is_spawn_point:
        entries.append(
            AssetEntry(
                type=_TYPE_SPAWN_POINT,
                path=prim_path,
                data=encode_spawn_point_blob(node.translation, node.rotation),
            )
        )

    is_hitbox = _read_bool_attr(prim, _HITBOX_ATTR)
    if is_hitbox:
        node.hitbox_path = prim_path

    geometry = _read_raw_geometry(prim, prim_path)
    if geometry is not None:
        # A geometry prim contributes at most one blob, addressed by its
        # own path - a hitbox marker or an applied PhysX collider means
        # this prim is a physics-only proxy, kept raw (no meshoptimizer
        # simplification) and filed as kHitbox/kCollision instead of
        # kMesh. A guide-purpose prim that is neither is a DCC-only helper
        # and contributes nothing.
        if is_hitbox:
            entries.append(AssetEntry(type=_TYPE_HITBOX, path=prim_path, data=encode_mesh_blob(geometry)))
        elif _has_collision_enabled(prim):
            entries.append(AssetEntry(type=_TYPE_COLLISION, path=prim_path, data=encode_mesh_blob(geometry)))
            node.collider_path = prim_path
        elif not _is_guide(prim):
            mesh_data = _optimize_mesh(geometry)
            entries.append(AssetEntry(type=_TYPE_MESH, path=prim_path, data=encode_mesh_blob(mesh_data)))
            node.mesh_path = prim_path
            base_color = _read_base_color(prim)
            if base_color is not None:
                node.properties.append((BASE_COLOR_PROPERTY, base_color))

    return node


def _maybe_cook_texture_prim(prim: Usd.Prim, prim_path: str, stage_path: Path, entries: list[AssetEntry]) -> None:
    """A UsdShadeShader prim with shader id UsdUVTexture is additionally
    cooked into its own texture blob (ADR-0017/issue #49) and appended to
    entries. A no-op for any other prim.
    """
    if not prim.IsA(UsdShade.Shader):
        return
    shader = UsdShade.Shader(prim)
    if shader.GetShaderId() != _USD_UV_TEXTURE_SHADER_ID:
        return

    dds_bytes, texture_format = _cook_texture(prim, prim_path, stage_path)
    entries.append(
        AssetEntry(type=_TYPE_TEXTURE, path=prim_path, data=encode_texture_blob(dds_bytes, texture_format))
    )


# True for the AssetType values ADR-0019 puts in the server pack: collision
# geometry, hitboxes, and spawn points. Mesh/texture/eye/scene are client-only
# (scene is handled separately, since it needs its mesh/material
# references stripped rather than being dropped outright).
_SERVER_PACK_ASSET_TYPES = frozenset({_TYPE_COLLISION, _TYPE_HITBOX, _TYPE_SPAWN_POINT})


def _transform_mesh(mesh: MeshData, matrix: Gf.Matrix4d) -> MeshData:
    """Applies matrix to mesh's points, leaving indices untouched - used for
    character geometry (_cook_character_prim), which carries no SceneNode
    transform of its own to place it at runtime (cook_scenario's Scene blob
    is map-only): the prim's own local-to-character-root placement and its
    stage's up-axis/metersPerUnit correction are baked into the points
    directly instead, the same two corrections a map's top-level nodes carry
    in their own SceneNode transform (_build_node) rather than in their
    points.
    """
    return MeshData(points=[tuple(matrix.Transform(Gf.Vec3d(*point))) for point in mesh.points], indices=mesh.indices)


def _cook_character_prim(prim: Usd.Prim, prim_path: str, matrix: Gf.Matrix4d, entries: list[AssetEntry]) -> None:
    """The geometry half of _build_node's classification (mesh vs. collision
    vs. hitbox - ADR-0040/ADR-0041), for one prim of a character stage:
    appends at most one AssetEntry, its points already transformed into the
    character's own root space by matrix. Builds no SceneNode - see
    _transform_mesh.
    """
    geometry = _read_raw_geometry(prim, prim_path)
    if geometry is None:
        return
    transformed = _transform_mesh(geometry, matrix)
    if _read_bool_attr(prim, _HITBOX_ATTR):
        entries.append(AssetEntry(type=_TYPE_HITBOX, path=prim_path, data=encode_mesh_blob(transformed)))
    elif _has_collision_enabled(prim):
        entries.append(AssetEntry(type=_TYPE_COLLISION, path=prim_path, data=encode_mesh_blob(transformed)))
    elif not _is_guide(prim):
        entries.append(AssetEntry(type=_TYPE_MESH, path=prim_path, data=encode_mesh_blob(_optimize_mesh(transformed))))


def cook_scenario(
    map_stage_path: Path,
    character_stages: Sequence[tuple[str, Path]],
    client_output_path: Path,
    server_output_path: Path,
    signing_key: bytes,
    scripts: Sequence[tuple[str, bytes]] = (),
    on_prim: Callable[[int, int, str], None] | None = None,
) -> CookReport:
    """Bakes map_stage_path and every (manifest_path, stage_path) in
    character_stages into one signed client/server pack pair (ADR-0041) -
    manifest_path is a character's own path as the scenario's manifest.yaml
    named it (e.g. "characters/player"), also the prefix its blobs are
    addressed under (ADR-0040).

    scripts are a scenario's Lua files as (path relative to its folder, bytes):
    they go into the server pack only, as script assets (ADR-0031, ADR-0039). A
    client is sent the values a script decides, never the script.

    The manifest paths, in character_stages' order, are also recorded as the
    character list both packs carry (ADR-0042).

    The client pack is written first, so the server pack can carry its hash: the
    server admits only clients that loaded that pack.

    on_prim(done, total, prim_path), if given, is called after each prim (the
    map's and every character's) is cooked.
    """
    # Encoded first so a list the wire can't carry fails before any stage is
    # walked.
    try:
        characters_entry = AssetEntry(
            type=_TYPE_CHARACTERS,
            path=CHARACTERS_PATH,
            data=encode_characters_blob([manifest_path for manifest_path, _ in character_stages]),
        )
    except ValueError as error:
        raise CookError("characters_encode_failed", "", str(error)) from error

    map_stage = Usd.Stage.Open(str(map_stage_path))
    if not map_stage:
        raise CookError("stage_open_failed", "", str(map_stage_path))
    # TraverseInstanceProxies: expands instanceable prototypes into
    # per-instance proxy prims instead of skipping them - each instance
    # becomes its own node subtree, de-instanced at cook time. Traverse()
    # visits prims pre-order (a parent always before its children), which
    # _build_node relies on.
    map_prims = list(map_stage.Traverse(Usd.TraverseInstanceProxies(Usd.PrimDefaultPredicate)))

    opened_characters = []
    for manifest_path, stage_path in character_stages:
        character_stage = Usd.Stage.Open(str(stage_path))
        if not character_stage:
            raise CookError("stage_open_failed", "", str(stage_path))
        # The client finds a character's visual mesh at <manifest path>/Character/Visual (ADR-0040).
        default_prim = character_stage.GetDefaultPrim()
        if not default_prim or default_prim.GetName() != CHARACTER_ROOT_PRIM:
            raise CookError(
                "character_root_misnamed",
                "",
                f"{stage_path}: a character's default prim must be named {CHARACTER_ROOT_PRIM!r}",
            )
        # The client puts its camera at <manifest path>/Character/Eye (ADR-0040).
        eye_prim = default_prim.GetChild(CHARACTER_EYE_PRIM)
        if not eye_prim:
            raise CookError(
                "character_eye_missing",
                "",
                f"{stage_path}: a character's {CHARACTER_ROOT_PRIM!r} prim must have an {CHARACTER_EYE_PRIM!r} child",
            )
        character_prims = list(character_stage.Traverse(Usd.TraverseInstanceProxies(Usd.PrimDefaultPredicate)))
        opened_characters.append((manifest_path, stage_path, character_stage, character_prims, eye_prim.GetPath()))

    total_prims = len(map_prims) + sum(len(prims) for _, _, _, prims, _ in opened_characters)
    done = 0

    correction = _stage_correction_matrix(map_stage)
    entries: list[AssetEntry] = []
    nodes: list[SceneNode] = []
    node_index_of: dict[str, int] = {}

    for prim in map_prims:
        node = _build_node(prim, correction, node_index_of, entries)
        prim_path = node.name
        node_index_of[str(prim.GetPath())] = len(nodes)
        nodes.append(node)

        _maybe_cook_texture_prim(prim, prim_path, map_stage_path, entries)
        done += 1
        if on_prim is not None:
            on_prim(done, total_prims, prim_path)

    for manifest_path, stage_path, character_stage, character_prims, eye_path in opened_characters:
        character_correction = _stage_correction_matrix(character_stage)
        for prim in character_prims:
            prim_path = f"{manifest_path}/{_sanitize_prim_path(str(prim.GetPath()))}"
            xformable = UsdGeom.Xformable(prim)
            local_to_root = (
                xformable.ComputeLocalToWorldTransform(Usd.TimeCode.Default()) if xformable else Gf.Matrix4d(1.0)
            )
            if prim.GetPath() == eye_path:
                eye = (local_to_root * character_correction).Transform(Gf.Vec3d(0.0, 0.0, 0.0))
                entries.append(AssetEntry(type=_TYPE_EYE, path=prim_path, data=encode_eye_blob(tuple(eye))))
            _cook_character_prim(prim, prim_path, local_to_root * character_correction, entries)
            _maybe_cook_texture_prim(prim, prim_path, stage_path, entries)
            done += 1
            if on_prim is not None:
                on_prim(done, total_prims, prim_path)

    mesh_count = sum(1 for entry in entries if entry.type == _TYPE_MESH)
    texture_count = sum(1 for entry in entries if entry.type == _TYPE_TEXTURE)
    node_count = len(nodes)

    # Client scene: full node data, mesh/material references intact.
    client_scene_blob = encode_scene_blob(nodes)

    # Server scene: same nodes/hierarchy/collision-spawn-hitbox
    # references, but mesh/material path stripped (ADR-0019) - the server
    # never receives visual content, even as a dangling path it can't
    # resolve.
    server_nodes = [
        SceneNode(
            name=node.name,
            parent_index=node.parent_index,
            translation=node.translation,
            rotation=node.rotation,
            scale=node.scale,
            mesh_path=None,
            material_path=None,
            collider_path=node.collider_path,
            hitbox_path=node.hitbox_path,
            is_spawn_point=node.is_spawn_point,
            # The server never receives visual content (ADR-0019), color included.
            properties=[(key, value) for key, value in node.properties if key != BASE_COLOR_PROPERTY],
        )
        for node in nodes
    ]
    server_scene_blob = encode_scene_blob(server_nodes)

    # Encoded before either pack is written, so a script the wire can't carry
    # leaves neither behind.
    try:
        script_entries = [
            AssetEntry(type=_TYPE_SCRIPT, path=script_path, data=encode_script_blob(script))
            for script_path, script in scripts
        ]
    except Exception as error:  # noqa: BLE001 - re-raised as CookError below
        raise CookError("script_encode_failed", "", str(error)) from error

    # Client pack: everything cooked from this stage, plus the full scene.
    client_entries = [*entries, AssetEntry(type=_TYPE_SCENE, path="Scene", data=client_scene_blob), characters_entry]
    try:
        client_pack_hash = write_pack(client_output_path, client_entries, signing_key)
    except Exception as error:  # noqa: BLE001 - re-raised as CookError below
        raise CookError("pack_write_failed", "", f"client pack: {error}") from error

    # Server pack: only the collision/hitbox/spawn-point entries, plus the
    # stripped scene, the client pack's hash and the scripts.
    server_entries = [entry for entry in entries if entry.type in _SERVER_PACK_ASSET_TYPES]
    server_entries.append(AssetEntry(type=_TYPE_SCENE, path="Scene", data=server_scene_blob))
    server_entries.append(characters_entry)
    server_entries.append(AssetEntry(type=_TYPE_CLIENT_PACK, path=CLIENT_PACK_PATH, data=client_pack_hash))
    server_entries.extend(script_entries)
    try:
        write_pack(server_output_path, server_entries, signing_key)
    except Exception as error:  # noqa: BLE001 - re-raised as CookError below
        raise CookError("pack_write_failed", "", f"server pack: {error}") from error

    return CookReport(
        mesh_count=mesh_count, texture_count=texture_count, node_count=node_count, script_count=len(scripts)
    )
