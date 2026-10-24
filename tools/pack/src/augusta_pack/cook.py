"""The asset cooker itself (ADR-0030).

Walks the authored OpenUSD stages a scenario's manifest composes - one map, zero
or more characters (ADR-0041), one rifle (ADR-0064) - and bakes them into one
signed client/server pack pair (ADR-0031/ADR-0032). Walks each stage through pxr
directly (the same pip-installed usd-optimize build optimize.py already uses,
rather than linking a second, independently-built OpenUSD - see ADR-0030 for why
those can't coexist in one process), and calls the native module, _native, for
the two pieces with no Python equivalent (mesh optimization, texture
compression) and for augusta_assets' pack encoder, which encodes every blob and
writes, hashes and signs both packs.
"""

from __future__ import annotations

from collections.abc import Callable, Sequence
from dataclasses import dataclass
import itertools
import math
from pathlib import Path

from pxr import Gf
from pxr import Sdf
from pxr import Usd
from pxr import UsdGeom
from pxr import UsdPhysics
from pxr import UsdShade
from pxr import UsdSkel

from augusta_pack import _native
from augusta_pack.sounds import CueSounds

# augusta:spawnPoint / augusta:hitbox: custom bool attributes (ADR-0032's
# authoring convention) rather than a native USD prim type. A hitbox is
# authored as geometry (a UsdGeomMesh, Cube or Capsule) with this marker and
# the body part it stands for; a character has one or more for each body
# part (ADR-0040).
_SPAWN_POINT_ATTR = "augusta:spawnPoint"
_HITBOX_ATTR = "augusta:hitbox"
# The body part a hitbox stands for (US-11): a token or string every hitbox
# carries next to augusta:hitbox, one of _BODY_PARTS' names (ADR-0040).
_BODY_PART_ATTR = "augusta:bodyPart"
_BODY_PARTS = {
    "head": _native.BodyPart.HEAD,
    "torso": _native.BodyPart.TORSO,
    "limb": _native.BodyPart.LIMB,
}
# Node property (ADR-0032) carrying a visual mesh's constant displayColor as
# "r g b" linear floats; the client reads it as the mesh's base color.
BASE_COLOR_PROPERTY = "base_color"
# The default prim every character stage has, named the same regardless of the
# character's name (ADR-0040), so the client finds its visual mesh at
# <character name>/Character/Visual.
CHARACTER_ROOT_PRIM = "Character"
# The child of CHARACTER_ROOT_PRIM every character stage has, whose origin is
# where the local player's camera sits and its Shots leave from (ADR-0040):
# client and server read it at <character name>/Character/Eye.
CHARACTER_EYE_PRIM = "Eye"
# The child of CHARACTER_ROOT_PRIM every character stage has: its UsdSkel
# Skeleton (ADR-0061), which client and server read at <character
# name>/Character/Skeleton.
CHARACTER_SKELETON_PRIM = "Skeleton"
# The children of a rifle stage's default prim that are its markers
# (ADR-0064), each a frame pointing along its -Z: where the holding hand holds
# it, the end of its barrel and the point the Eye looks through in ADS. In the
# order the rifle blob carries them.
RIFLE_MARKER_PRIMS = ("Grip", "Muzzle", "Sight")
# How far a root joint's transform may be from the character's root and still
# be at it: float noise in an authored matrix, not a misplaced skeleton.
_ROOT_TOLERANCE = 1e-6
# augusta:textureFormat: selects which BC format a UsdUVTexture prim's KTX2
# holds (ADR-0017): "bc7", "bc5" or "bc4". The native module cooks any other
# value, or none, as "bc7".
_TEXTURE_FORMAT_ATTR = "augusta:textureFormat"
_USD_UV_TEXTURE_SHADER_ID = "UsdUVTexture"

# Z-up -> Y-up is a quarter turn about the X axis.
_Z_UP_TO_Y_UP_DEGREES = -90.0


class CookError(RuntimeError):
    """Raised when cooking fails.

    Carries a code, the offending prim's path (empty if not tied to one), and a
    human-readable message.
    """

    def __init__(self, code: str, prim_path: str, message: str) -> None:
        super().__init__(
            f"cook failed{f' at {prim_path}' if prim_path else ''}: {message} "
            f"({code})"
        )
        self.code = code
        self.prim_path = prim_path
        self.message = message


@dataclass
class CookReport:
    """How many of each asset a cook wrote into the pack pair."""

    mesh_count: int
    texture_count: int
    node_count: int
    script_count: int
    sound_count: int


def _sanitize_prim_path(usd_prim_path: str) -> str:
    """Strips the leading '/' from a USD prim path.

    The result is the pack-relative path ADR-0031 addresses its blobs by.
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


def _read_texture_format(prim: Usd.Prim) -> str:
    attr = prim.GetAttribute(_TEXTURE_FORMAT_ATTR)
    value = attr.Get() if attr else None
    return str(value) if value is not None else ""


def _resolve_texture_file_path(
    asset_path: Sdf.AssetPath, stage_path: Path
) -> Path:
    resolved = asset_path.resolvedPath or asset_path.path
    resolved_path = Path(resolved)
    if not resolved_path.is_absolute():
        resolved_path = stage_path.parent / resolved_path
    return resolved_path


def _cook_texture(prim: Usd.Prim, prim_path: str, stage_path: Path) -> bytes:
    shader = UsdShade.Shader(prim)
    file_input = shader.GetInput("file")
    asset_path = file_input.Get() if file_input else None
    if not isinstance(asset_path, Sdf.AssetPath):
        raise CookError(
            "missing_texture_file",
            prim_path,
            "UsdUVTexture prim has no inputs:file",
        )

    texture_path = _resolve_texture_file_path(asset_path, stage_path)
    try:
        return _native.compress_texture(
            texture_path, _read_texture_format(prim)
        )
    except RuntimeError as error:
        raise CookError("texture_load_failed", prim_path, str(error)) from error


def _read_bool_attr(prim: Usd.Prim, attr_name: str) -> bool:
    attr = prim.GetAttribute(attr_name)
    value = attr.Get() if attr else None
    return bool(value)


def _read_body_part(prim: Usd.Prim, prim_path: str) -> _native.BodyPart:
    """The body part a hitbox prim's augusta:bodyPart names.

    Every hitbox names one, since a hit resolves to it (US-11).
    """
    attr = prim.GetAttribute(_BODY_PART_ATTR)
    value = attr.Get() if attr else None
    if value is None:
        raise CookError(
            "hitbox_body_part_missing",
            prim_path,
            f"a hitbox must name its body part in {_BODY_PART_ATTR} "
            f"({', '.join(_BODY_PARTS)})",
        )
    body_part = _BODY_PARTS.get(str(value))
    if body_part is None:
        raise CookError(
            "hitbox_body_part_unknown",
            prim_path,
            f"{_BODY_PART_ATTR} is {str(value)!r}, not one of "
            f"{', '.join(_BODY_PARTS)}",
        )
    return body_part


def _decompose(
    matrix: Gf.Matrix4d,
) -> tuple[
    tuple[float, float, float],
    tuple[float, float, float, float],
    tuple[float, float, float],
]:
    """Decomposes a USD local-to-parent matrix into SceneNode's transform.

    The result is the translation/rotation(x,y,z,w)/scale triple SceneNode
    stores (ADR-0032 stores local transforms only, never world).
    """
    transform = Gf.Transform(matrix)
    translation = tuple(transform.GetTranslation())
    rotation = transform.GetRotation().GetQuat()
    imaginary = rotation.GetImaginary()
    rotation_xyzw = (
        imaginary[0],
        imaginary[1],
        imaginary[2],
        rotation.GetReal(),
    )
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
    """Corrects a stage's upAxis/metersPerUnit to the runtime's convention.

    The runtime's is fixed Y-up/right-handed/1-meter (ADR-0032). Applied once,
    prepended to each top-level node's own local transform.
    """
    meters_per_unit = UsdGeom.GetStageMetersPerUnit(stage)
    correction = Gf.Matrix4d(1.0).SetScale(meters_per_unit)
    if UsdGeom.GetStageUpAxis(stage) == UsdGeom.Tokens.z:
        rotate_z_to_y = Gf.Matrix4d(1.0)
        rotate_z_to_y.SetRotate(
            Gf.Rotation(Gf.Vec3d(1.0, 0.0, 0.0), _Z_UP_TO_Y_UP_DEGREES)
        )
        correction = rotate_z_to_y * correction
    return correction


def _validate_triangle_topology(
    face_vertex_counts, face_vertex_index_count: int, prim_path: str
) -> int:
    """Validates the faces are triangles and returns the index count.

    Every face must be a triangle, and faceVertexCounts must sum to
    faceVertexIndices' own length: that sum is returned.
    """
    expected_index_count = 0
    for count in face_vertex_counts:
        if count != 3:
            raise CookError(
                "unsupported_topology",
                prim_path,
                f"face with {count} vertices, only triangles (3) are supported",
            )
        expected_index_count += count
    if expected_index_count != face_vertex_index_count:
        raise CookError(
            "inconsistent_topology",
            prim_path,
            f"faceVertexCounts sums to {expected_index_count} but "
            f"faceVertexIndices has {face_vertex_index_count} entries",
        )
    return expected_index_count


def _read_indices(
    face_vertex_indices, point_count: int, prim_path: str
) -> list[int]:
    indices = []
    for index in face_vertex_indices:
        if index < 0:
            raise CookError(
                "negative_index", prim_path, f"negative index {index}"
            )
        if index >= point_count:
            raise CookError(
                "index_out_of_range",
                prim_path,
                f"index {index} out of range for {point_count} points",
            )
        indices.append(index)
    return indices


@dataclass(frozen=True)
class _Geometry:
    """A prim's triangle geometry as read."""

    # Kept in double precision until the encoder narrows them to the pack's
    # floats.
    points: list[tuple[float, float, float]]
    # Three per triangle.
    indices: list[int]


def _mesh_of(geometry: _Geometry) -> _native.MeshData:
    """The pack's mesh for geometry, as read."""
    return _native.MeshData(points=geometry.points, indices=geometry.indices)


def _optimize_mesh(geometry: _Geometry) -> _native.MeshData:
    """Meshoptimizer pass (ADR-0016).

    Vertex cache optimization, simplification, quantization - turns the
    as-authored read into GPU-ready data rather than a passthrough of the source
    mesh.
    """
    if not geometry.points or not geometry.indices:
        return _mesh_of(geometry)
    flat_points = [
        component for point in geometry.points for component in point
    ]
    optimized_points, optimized_indices = _native.optimize_mesh(
        flat_points, geometry.indices
    )
    return _native.MeshData(
        points=[
            tuple(optimized_points[i : i + 3])
            for i in range(0, len(optimized_points), 3)
        ],
        indices=optimized_indices,
    )


def _read_raw_mesh_geometry(mesh: UsdGeom.Mesh, prim_path: str) -> _Geometry:
    """Reads a UsdGeomMesh's points/triangle-index buffer as authored.

    No meshoptimizer pass is applied: the read is shared by the visual path
    (optimized afterward) and collision/hitbox geometry reading (_build_node),
    where meshoptimizer's lossy simplification could let a physics query miss
    geometry it should have hit.
    """
    face_vertex_counts = mesh.GetFaceVertexCountsAttr().Get()
    usd_points = mesh.GetPointsAttr().Get()
    face_vertex_indices = mesh.GetFaceVertexIndicesAttr().Get()
    if (
        face_vertex_counts is None
        or usd_points is None
        or face_vertex_indices is None
    ):
        raise CookError(
            "missing_mesh_data",
            prim_path,
            "missing points, faceVertexCounts, or faceVertexIndices",
        )

    _validate_triangle_topology(
        face_vertex_counts, len(face_vertex_indices), prim_path
    )

    points = [(point[0], point[1], point[2]) for point in usd_points]
    indices = _read_indices(face_vertex_indices, len(points), prim_path)
    return _Geometry(points, indices)


# A UsdGeomCube's 8 corners, ordered (-,-,-) (+,-,-) (+,+,-) (-,+,-)
# (-,-,+) (+,-,+) (+,+,+) (-,+,+), and its 12 outward-CCW triangles.
_CUBE_CORNER_SIGNS = [
    (-1, -1, -1),
    (1, -1, -1),
    (1, 1, -1),
    (-1, 1, -1),
    (-1, -1, 1),
    (1, -1, 1),
    (1, 1, 1),
    (-1, 1, 1),
]
_CUBE_INDICES = [
    4,
    5,
    6,
    4,
    6,
    7,
    1,
    0,
    3,
    1,
    3,
    2,
    5,
    1,
    2,
    5,
    2,
    6,
    0,
    4,
    7,
    0,
    7,
    3,
    3,
    7,
    6,
    3,
    6,
    2,
    0,
    1,
    5,
    0,
    5,
    4,
]


def _read_cube_geometry(cube: UsdGeom.Cube, prim_path: str) -> _Geometry:
    """Expands a UsdGeomCube into the triangle buffer a mesh box would give.

    The cube is an edge-length box centered on its own origin; expanded, it is
    cooked exactly like a mesh from here on.
    """
    size = cube.GetSizeAttr().Get()
    if size is None or size <= 0:
        raise CookError(
            "invalid_cube_size", prim_path, f"cube size {size} must be positive"
        )
    half = size / 2
    points = [
        (sx * half, sy * half, sz * half) for sx, sy, sz in _CUBE_CORNER_SIGNS
    ]
    return _Geometry(points, list(_CUBE_INDICES))


# Longitude segments and latitude rings-per-hemisphere for
# _read_capsule_geometry - a fixed tessellation density, not authorable: a
# capsule is a placeholder collision-derived shape (ADR-0040), not an art asset
# needing LOD control.
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


def _read_capsule_geometry(
    capsule: UsdGeom.Capsule, prim_path: str
) -> _Geometry:
    """Tessellates a UsdGeomCapsule into a triangle buffer.

    The capsule is a cylinder of `height` (excluding caps) and `radius`, capped
    by two hemispheres of the same radius, centered on its own origin and
    extended along `axis`; it gets the same "cook a primitive shape like a mesh
    from here on" treatment _read_cube_geometry gives UsdGeomCube. Built as
    rings of
    _CAPSULE_LONGITUDE_SEGMENTS vertices each, pole to pole: a bottom pole, a
    fan of hemisphere rings, the two equator rings the cylinder spans between,
    a mirrored fan of hemisphere rings, and a top pole.
    """
    height = capsule.GetHeightAttr().Get()
    radius = capsule.GetRadiusAttr().Get()
    axis = capsule.GetAxisAttr().Get()
    if height is None or radius is None or radius <= 0 or height < 0:
        raise CookError(
            "invalid_capsule_size",
            prim_path,
            f"capsule height {height} / radius {radius} must be "
            f"non-negative/positive",
        )
    up, right, forward = _CAPSULE_BASIS.get(
        axis, _CAPSULE_BASIS[UsdGeom.Tokens.z]
    )

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
                    up[i] * axis_offset
                    + right[i] * (ring_radius * math.cos(phi))
                    + forward[i] * (ring_radius * math.sin(phi))
                    for i in range(3)
                )
            )
        return start

    def hemisphere_ring(
        latitude_index: int, pole_sign: float
    ) -> tuple[float, float]:
        theta = (math.pi / 2.0) * (latitude_index / rings)
        return radius * math.cos(theta), pole_sign * (
            half_height + radius * math.sin(theta)
        )

    bottom_pole_index = len(points)
    points.append(
        tuple(component * -(half_height + radius) for component in up)
    )
    bottom_ring_starts = [
        add_ring(*hemisphere_ring(latitude, -1.0))
        for latitude in range(rings - 1, 0, -1)
    ]
    bottom_equator_start = add_ring(radius, -half_height)
    top_equator_start = add_ring(radius, half_height)
    top_ring_starts = [
        add_ring(*hemisphere_ring(latitude, 1.0))
        for latitude in range(1, rings)
    ]
    top_pole_index = len(points)
    points.append(tuple(component * (half_height + radius) for component in up))

    indices: list[int] = []

    def add_fan(pole_index: int, ring_start: int, flip: bool) -> None:
        for segment in range(segments):
            a, b = ring_start + segment, ring_start + (segment + 1) % segments
            indices.extend([pole_index, b, a] if flip else [pole_index, a, b])

    def add_band(lower_start: int, upper_start: int) -> None:
        for segment in range(segments):
            a0, a1 = (
                lower_start + segment,
                lower_start + (segment + 1) % segments,
            )
            b0, b1 = (
                upper_start + segment,
                upper_start + (segment + 1) % segments,
            )
            indices.extend([a0, a1, b1, a0, b1, b0])

    # rings, pole to pole: bottom pole -> hemisphere rings -> equator -> equator
    # -> hemisphere rings -> top pole. flip=True on the top fan mirrors the
    # bottom fan's winding, since the top pole is approached from the
    # opposite direction.
    add_fan(bottom_pole_index, bottom_ring_starts[0], flip=False)
    for lower, upper in itertools.pairwise(bottom_ring_starts):
        add_band(lower, upper)
    add_band(bottom_ring_starts[-1], bottom_equator_start)
    add_band(bottom_equator_start, top_equator_start)
    for lower, upper in itertools.pairwise(top_ring_starts):
        add_band(lower, upper)
    add_fan(top_pole_index, top_ring_starts[-1], flip=True)

    return _Geometry(points, indices)


def _read_raw_geometry(prim: Usd.Prim, prim_path: str) -> _Geometry | None:
    """Reads prim's triangle geometry as authored, if it has any.

    Only a UsdGeomMesh, UsdGeomCube or UsdGeomCapsule does; any other prim type
    gives None.
    """
    if prim.IsA(UsdGeom.Mesh):
        return _read_raw_mesh_geometry(UsdGeom.Mesh(prim), prim_path)
    if prim.IsA(UsdGeom.Cube):
        return _read_cube_geometry(UsdGeom.Cube(prim), prim_path)
    if prim.IsA(UsdGeom.Capsule):
        return _read_capsule_geometry(UsdGeom.Capsule(prim), prim_path)
    return None


def _read_base_color(prim: Usd.Prim) -> str | None:
    """Prim's constant primvars:displayColor as an "r g b" string, or None.

    Only the first element is used: per-face/per-vertex color isn't supported.
    """
    gprim = UsdGeom.Gprim(prim)
    colors = gprim.GetDisplayColorAttr().Get() if gprim else None
    if not colors:
        return None
    return " ".join(f"{channel:g}" for channel in colors[0])


def _is_guide(prim: Usd.Prim) -> bool:
    """True if prim is authored with purpose "guide".

    A DCC-only helper (e.g. a spawn-point marker) that is never rendered.
    """
    imageable = UsdGeom.Imageable(prim)
    return (
        bool(imageable)
        and imageable.GetPurposeAttr().Get() == UsdGeom.Tokens.guide
    )


def _build_node(
    prim: Usd.Prim,
    correction: Gf.Matrix4d,
    node_index_of: dict[str, int],
    entries: list[_native.AssetEntry],
) -> _native.SceneNode:
    """Builds prim's own SceneNode and the asset entries it carries.

    Appends a spawn-point AssetEntry if augusta:spawnPoint is set, and - if prim
    is a UsdGeomMesh or UsdGeomCube - at most one geometry AssetEntry: kHitbox
    if augusta:hitbox is set, else kCollision if a PhysX collider is applied,
    else kMesh. node_index_of must already contain every ancestor of prim -
    guaranteed by pre-order traversal (see cook_scenario's own comment on its
    Traverse() call).
    """
    prim_path = _sanitize_prim_path(str(prim.GetPath()))
    node = _native.SceneNode(name=prim_path)

    parent = prim.GetParent()
    if not parent.IsValid() or parent.IsPseudoRoot():
        node.parent_index = _native.SCENE_NODE_NO_PARENT
    else:
        # Always succeeds under pre-order traversal - see this function's
        # own comment.
        node.parent_index = node_index_of[str(parent.GetPath())]

    translation, rotation, scale = _local_transform_of(prim)
    if node.parent_index == _native.SCENE_NODE_NO_PARENT:
        # Only the stage's own top-level (parentless) nodes get the
        # up-axis/unit correction prepended.
        xformable = UsdGeom.Xformable(prim)
        local_matrix = (
            xformable.GetLocalTransformation()
            if xformable
            else Gf.Matrix4d(1.0)
        )
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
            _native.AssetEntry(
                type=_native.AssetType.SPAWN_POINT,
                path=prim_path,
                data=_native.encode_spawn_point_blob(
                    _native.SpawnPointData(
                        translation=node.translation, rotation=node.rotation
                    )
                ),
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
            # A map's hitbox moves with no Skeleton, so it names no joint.
            blob = _native.encode_hitbox_blob(
                _native.HitboxData(
                    part=_read_body_part(prim, prim_path),
                    joint="",
                    mesh=_mesh_of(geometry),
                )
            )
            entries.append(
                _native.AssetEntry(
                    type=_native.AssetType.HITBOX, path=prim_path, data=blob
                )
            )
        elif _has_collision_enabled(prim):
            entries.append(
                _native.AssetEntry(
                    type=_native.AssetType.COLLISION,
                    path=prim_path,
                    data=_native.encode_mesh_blob(_mesh_of(geometry)),
                )
            )
            node.collider_path = prim_path
        elif not _is_guide(prim):
            entries.append(
                _native.AssetEntry(
                    type=_native.AssetType.MESH,
                    path=prim_path,
                    data=_native.encode_mesh_blob(_optimize_mesh(geometry)),
                )
            )
            node.mesh_path = prim_path
            base_color = _read_base_color(prim)
            if base_color is not None:
                # Assigned, not appended to: see SceneNode's docstring.
                node.properties = [
                    *node.properties,
                    (BASE_COLOR_PROPERTY, base_color),
                ]

    return node


def _maybe_cook_texture_prim(
    prim: Usd.Prim,
    prim_path: str,
    stage_path: Path,
    entries: list[_native.AssetEntry],
) -> None:
    """Cooks a UsdUVTexture shader prim into its own texture blob.

    The blob (ADR-0017/issue #49) is appended to entries. A no-op for any other
    prim.
    """
    if not prim.IsA(UsdShade.Shader):
        return
    shader = UsdShade.Shader(prim)
    if shader.GetShaderId() != _USD_UV_TEXTURE_SHADER_ID:
        return

    ktx2_bytes = _cook_texture(prim, prim_path, stage_path)
    entries.append(
        _native.AssetEntry(
            type=_native.AssetType.TEXTURE,
            path=prim_path,
            data=_native.encode_texture_blob(ktx2_bytes),
        )
    )


# True for the AssetType values ADR-0019 puts in the server pack: collision
# geometry, hitboxes, spawn points, character eyes (the server fires each
# Shot from its shooter's eye, ADR-0040), character skeletons (the same
# joints on either side, ADR-0061) and the rifle's markers (the server places
# each Muzzle, ADR-0064). Mesh/texture/scene are client-only
# (scene is handled separately, since it needs its mesh/material
# references stripped rather than being dropped outright).
_SERVER_PACK_ASSET_TYPES = frozenset(
    {
        _native.AssetType.COLLISION,
        _native.AssetType.HITBOX,
        _native.AssetType.SPAWN_POINT,
        _native.AssetType.EYE,
        _native.AssetType.SKELETON,
        _native.AssetType.RIFLE,
    }
)


def _transform_geometry(geometry: _Geometry, matrix: Gf.Matrix4d) -> _Geometry:
    """Applies matrix to geometry's points, leaving indices untouched.

    Used for character geometry (_cook_character_prim), which carries no
    SceneNode transform of its own to place it at runtime (cook_scenario's Scene
    blob is map-only): the prim's own local-to-character-root placement and its
    stage's up-axis/metersPerUnit correction are baked into the points directly
    instead, the same two corrections a map's top-level nodes carry in their own
    SceneNode transform (_build_node) rather than in their points.
    """
    return _Geometry(
        [
            tuple(matrix.Transform(Gf.Vec3d(*point)))
            for point in geometry.points
        ],
        geometry.indices,
    )


@dataclass(frozen=True)
class _Skeleton:
    """A character's Skeleton as cooked (ADR-0061), and its rest pose."""

    # The prim's path in the pack.
    path: str
    # Parents before children, as the pack carries them.
    joints: list[_native.JointData]
    # Each joint's rest transform in the character's root space, by name, as
    # authored: before the stage's up-axis/metersPerUnit correction.
    rest_to_root: dict[str, Gf.Matrix4d]


@dataclass(frozen=True)
class _OpenedCharacter:
    """A character stage opened and checked, before its prims are cooked."""

    # Its name in the manifest, which its blobs are addressed under.
    name: str
    stage_path: Path
    # Its stage's up-axis/metersPerUnit correction.
    correction: Gf.Matrix4d
    # Pre-order, as cook_scenario walks a map's.
    prims: list[Usd.Prim]
    eye_path: Sdf.Path
    skeleton: _Skeleton


def _is_at_root(matrix: Gf.Matrix4d) -> bool:
    identity = Gf.Matrix4d(1.0)
    return all(
        abs(matrix[row][column] - identity[row][column]) <= _ROOT_TOLERANCE
        for row in range(4)
        for column in range(4)
    )


def _skeleton_prim(
    character_name: str, default_prim: Usd.Prim, stage_path: Path
) -> UsdSkel.Skeleton:
    """The UsdSkel Skeleton at the Character prim's Skeleton child.

    Raises CookError for a character with none: a Skeleton is mandatory
    (ADR-0061).
    """
    character_path = f"{character_name}/{CHARACTER_ROOT_PRIM}"
    prim = default_prim.GetChild(CHARACTER_SKELETON_PRIM)
    if not prim or not prim.IsA(UsdSkel.Skeleton):
        raise CookError(
            "character_skeleton_missing",
            character_path,
            f"{stage_path}: character {character_name!r} needs a UsdSkel "
            f"Skeleton prim, {CHARACTER_SKELETON_PRIM!r}, under its "
            f"{CHARACTER_ROOT_PRIM!r} prim",
        )
    return UsdSkel.Skeleton(prim)


def _check_joint_count(names: list[str], path: str) -> None:
    """Raises CookError if names are more joints than a pack carries."""
    if len(names) > _native.MAX_JOINTS:
        raise CookError(
            "skeleton_too_many_joints",
            path,
            f"has {len(names)} joints, and a pack carries at most "
            f"{_native.MAX_JOINTS}",
        )


def _joint_parents(
    names: list[str], rests: Sequence[Gf.Matrix4d], path: str
) -> list[int]:
    """Each joint's parent index, negative for a root, in names' order.

    Raises CookError unless names are a topology UsdSkel can pose, each joint
    named once and with a rest transform.
    """
    topology = UsdSkel.Topology(names)
    valid, reason = topology.Validate()
    if not valid or len(set(names)) != len(names) or len(rests) != len(names):
        raise CookError(
            "skeleton_invalid",
            path,
            reason
            or "needs a rest transform for each of its joints, each joint "
            "named once",
        )
    return list(topology.GetParentIndices())


def _check_root_placement(name: str, local: Gf.Matrix4d, path: str) -> None:
    """Raises CookError unless root joint name, at local, is at the root.

    The character's root is its feet, facing -Z, with no turn or scale.
    """
    if not _is_at_root(local):
        raise CookError(
            "skeleton_root_off_origin",
            path,
            f"root joint {name!r} must sit at the character's root, "
            "its feet, facing -Z, with no turn or scale",
        )


def _decompose_joints(
    names: list[str],
    rests: Sequence[Gf.Matrix4d],
    parents: list[int],
    skeleton_to_root: Gf.Matrix4d,
    correction: Gf.Matrix4d,
    path: str,
) -> tuple[list[_native.JointData], dict[str, Gf.Matrix4d]]:
    """Each joint as the pack carries it, and its rest transform by name.

    A joint's transform is relative to its parent, or to the character's root
    for a root joint, corrected like the rest of the character; its rest
    transform in the character's root space is left uncorrected, as authored.
    """
    inverse_correction = correction.GetInverse()
    joints = []
    rest_to_root: dict[str, Gf.Matrix4d] = {}
    for name, rest, parent in zip(names, rests, parents, strict=True):
        local = Gf.Matrix4d(rest)
        if parent < 0:
            local = local * skeleton_to_root
            _check_root_placement(name, local, path)
            rest_to_root[name] = local
        else:
            rest_to_root[name] = local * rest_to_root[names[parent]]
        translation, rotation, scale = _decompose(
            inverse_correction * local * correction
        )
        joints.append(
            _native.JointData(
                name=name,
                parent=_native.JOINT_NO_PARENT if parent < 0 else parent,
                translation=translation,
                rotation=rotation,
                scale=scale,
            )
        )
    return joints, rest_to_root


def _read_skeleton(
    character_name: str,
    default_prim: Usd.Prim,
    stage_path: Path,
    correction: Gf.Matrix4d,
) -> _Skeleton:
    """The UsdSkel Skeleton at the Character prim's Skeleton child.

    Each joint is named by its path within the Skeleton and carries its rest
    transform relative to its parent, corrected like the rest of the
    character, so client and server compose its rest pose from its joints
    alone. Raises CookError for a character with no Skeleton, one with more
    joints than the pack allows, one UsdSkel could not pose, and one whose
    root joints are not at the character's root: its feet, facing -Z.
    """
    path = f"{character_name}/{CHARACTER_ROOT_PRIM}/{CHARACTER_SKELETON_PRIM}"
    skeleton = _skeleton_prim(character_name, default_prim, stage_path)
    names = [str(name) for name in skeleton.GetJointsAttr().Get() or []]
    _check_joint_count(names, path)
    rests = skeleton.GetRestTransformsAttr().Get() or []
    parents = _joint_parents(names, rests, path)
    skeleton_to_root = UsdGeom.Xformable(
        skeleton.GetPrim()
    ).ComputeLocalToWorldTransform(Usd.TimeCode.Default())
    joints, rest_to_root = _decompose_joints(
        names, rests, parents, skeleton_to_root, correction, path
    )
    return _Skeleton(path=path, joints=joints, rest_to_root=rest_to_root)


def _read_hitbox_joint(
    prim: Usd.Prim, prim_path: str, skeleton: _Skeleton
) -> str:
    """The name of the joint a character's hitbox prim is authored under.

    A hitbox is bound rigidly to one joint as UsdSkel binds any prim: a
    constant primvars:skel:jointIndices of one index, into the prim's
    skel:joints if it has them, else into its Skeleton's joints.
    """
    binding = UsdSkel.BindingAPI(prim)
    primvar = binding.GetJointIndicesPrimvar()
    indices = primvar.Get() if primvar and primvar.HasValue() else None
    order = binding.GetJointsAttr().Get() if binding.GetJointsAttr() else None
    if order is None:
        order = [joint.name for joint in skeleton.joints]
    if (
        indices is None
        or len(indices) != 1
        or primvar.GetInterpolation() != UsdGeom.Tokens.constant
        or not 0 <= indices[0] < len(order)
    ):
        raise CookError(
            "hitbox_joint_missing",
            prim_path,
            "a hitbox must be authored under the joint it moves with: bound "
            "rigidly to one, by a constant primvars:skel:jointIndices of one "
            "index into skel:joints",
        )
    joint = str(order[indices[0]])
    if joint not in skeleton.rest_to_root:
        raise CookError(
            "hitbox_joint_unknown",
            prim_path,
            f"names joint {joint!r}, which {skeleton.path} does not have",
        )
    return joint


def _cook_character_prim(
    prim: Usd.Prim,
    prim_path: str,
    local_to_root: Gf.Matrix4d,
    correction: Gf.Matrix4d,
    skeleton: _Skeleton,
    entries: list[_native.AssetEntry],
) -> _native.BodyPart | None:
    """The geometry half of _build_node's classification (mesh vs.

    Collision vs. hitbox - ADR-0040/ADR-0041), for one prim of a character
    stage: appends at most one AssetEntry, its points transformed by
    local_to_root then the stage's correction into the character's own root
    space, or for a hitbox into its joint's (ADR-0061). Builds no SceneNode -
    see _transform_geometry. Returns the body part if the prim is a hitbox.
    """
    geometry = _read_raw_geometry(prim, prim_path)
    if geometry is None:
        return None
    if _read_bool_attr(prim, _HITBOX_ATTR):
        body_part = _read_body_part(prim, prim_path)
        joint = _read_hitbox_joint(prim, prim_path, skeleton)
        # Into the joint's rest space, then corrected as its joint is, so
        # the joint's corrected transform places it back where authored.
        in_joint = _transform_geometry(
            geometry,
            local_to_root
            * skeleton.rest_to_root[joint].GetInverse()
            * correction,
        )
        entries.append(
            _native.AssetEntry(
                type=_native.AssetType.HITBOX,
                path=prim_path,
                data=_native.encode_hitbox_blob(
                    _native.HitboxData(
                        part=body_part, joint=joint, mesh=_mesh_of(in_joint)
                    )
                ),
            )
        )
        return body_part
    transformed = _transform_geometry(geometry, local_to_root * correction)
    if _has_collision_enabled(prim):
        entries.append(
            _native.AssetEntry(
                type=_native.AssetType.COLLISION,
                path=prim_path,
                data=_native.encode_mesh_blob(_mesh_of(transformed)),
            )
        )
    elif not _is_guide(prim):
        entries.append(
            _native.AssetEntry(
                type=_native.AssetType.MESH,
                path=prim_path,
                data=_native.encode_mesh_blob(_optimize_mesh(transformed)),
            )
        )
    return None


def _sound_entries(sounds: CueSounds) -> list[_native.AssetEntry]:
    """The client pack's entries for sounds: each cue's, then its variants'.

    Raises CookError naming the cue whose sound does not encode.
    """
    entries = []
    for cue, sound in sounds.cues:
        variant_paths = [
            _native.cue_variant_path(cue, variant)
            for variant in range(len(sound.variants))
        ]
        try:
            entries.append(
                _native.AssetEntry(
                    type=_native.AssetType.CUE_SOUND,
                    path=_native.cue_sound_path(cue),
                    data=_native.encode_cue_sound_blob(
                        _native.CueSoundData(
                            gain_db=sound.gain_db, variants=variant_paths
                        )
                    ),
                )
            )
            entries.extend(
                _native.AssetEntry(
                    type=_native.AssetType.AUDIO,
                    path=path,
                    data=_native.encode_audio_blob(
                        _native.AudioData(
                            sample_rate=variant.sample_rate,
                            bits_per_sample=variant.bits_per_sample,
                            samples=variant.samples,
                        )
                    ),
                )
                for path, variant in zip(
                    variant_paths, sound.variants, strict=True
                )
            )
        except Exception as error:  # noqa: BLE001 - re-raised as CookError
            raise CookError(
                "sound_encode_failed", "", f"cue {cue!r}: {error}"
            ) from error
    return entries


def _cook_rifle(stage_path: Path) -> _native.AssetEntry:
    """The rifle's Grip, Muzzle and Sight out of its stage (ADR-0064).

    Each marker is a child of the stage's default prim, read as its frame in
    the stage's root space, corrected to the runtime's Y-up meters; its scale
    is dropped, since a marker is a frame. Nothing else of the stage is cooked:
    no pack reader draws or tests the rifle itself.
    """
    stage = Usd.Stage.Open(str(stage_path))
    if not stage:
        raise CookError("stage_open_failed", "", str(stage_path))
    default_prim = stage.GetDefaultPrim()
    if not default_prim:
        raise CookError(
            "rifle_root_missing",
            "",
            f"{stage_path}: a rifle stage needs a default prim holding its "
            f"{', '.join(RIFLE_MARKER_PRIMS)}",
        )
    correction = _stage_correction_matrix(stage)
    markers = []
    for name in RIFLE_MARKER_PRIMS:
        marker_prim = default_prim.GetChild(name)
        if not marker_prim:
            raise CookError(
                "rifle_marker_missing",
                "",
                f"{stage_path}: a rifle's {default_prim.GetName()!r} prim "
                f"must have a {name!r} child",
            )
        xformable = UsdGeom.Xformable(marker_prim)
        local_to_root = (
            xformable.ComputeLocalToWorldTransform(Usd.TimeCode.Default())
            if xformable
            else Gf.Matrix4d(1.0)
        )
        translation, rotation, _ = _decompose(local_to_root * correction)
        markers.append(
            _native.RifleMarker(translation=translation, rotation=rotation)
        )
    grip, muzzle, sight = markers
    return _native.AssetEntry(
        type=_native.AssetType.RIFLE,
        path=_native.RIFLE_PATH,
        data=_native.encode_rifle_blob(
            _native.RifleData(grip=grip, muzzle=muzzle, sight=sight)
        ),
    )


def cook_scenario(
    map_stage_path: Path,
    character_stages: Sequence[tuple[str, Path]],
    rifle_stage_path: Path,
    client_output_path: Path,
    server_output_path: Path,
    signing_key: bytes,
    scripts: Sequence[tuple[str, bytes]] = (),
    sounds: CueSounds | None = None,
    on_prim: Callable[[int, int, str], None] | None = None,
) -> CookReport:
    """Bakes a map and its characters into one signed client/server pack pair.

    The map is map_stage_path and the characters every (character_name,
    stage_path) in character_stages (ADR-0041) - character_name is a
    character's name in the manifest (e.g. "soldier"), also the prefix its
    blobs are addressed under (ADR-0040). The rifle's markers, out of
    rifle_stage_path, go into both packs at RIFLE_PATH (ADR-0064).

    scripts are a scenario's Lua files as (path in the pack, bytes):
    they go into the server pack only, as script assets (ADR-0031, ADR-0039). A
    client is sent the values a script decides, never the script.

    sounds, if given, are the client's cue sounds: they go into the client pack
    only, each cue's gain and variants' paths at sounds/<cue> and each variant
    as an audio asset at sounds/<cue>/<n> (_native.cue_sound_path and
    cue_variant_path, ADR-0020, ADR-0031). The headless server plays nothing
    (ADR-0019).

    The character names, in character_stages' order, are also recorded as the
    character list both packs carry (ADR-0042).

    The client pack is written first, so the server pack can carry its hash: the
    server admits only clients that loaded that pack.

    on_prim(done, total, prim_path), if given, is called after each prim (the
    map's and every character's) is cooked.
    """
    # Encoded first so a list the wire can't carry fails before any stage is
    # walked.
    try:
        characters_entry = _native.AssetEntry(
            type=_native.AssetType.CHARACTERS,
            path=_native.CHARACTERS_PATH,
            data=_native.encode_characters_blob(
                [character_name for character_name, _ in character_stages]
            ),
        )
    except _native.EncodeError as error:
        raise CookError("characters_encode_failed", "", str(error)) from error

    map_stage = Usd.Stage.Open(str(map_stage_path))
    if not map_stage:
        raise CookError("stage_open_failed", "", str(map_stage_path))
    # TraverseInstanceProxies: expands instanceable prototypes into
    # per-instance proxy prims instead of skipping them - each instance
    # becomes its own node subtree, de-instanced at cook time. Traverse()
    # visits prims pre-order (a parent always before its children), which
    # _build_node relies on.
    map_prims = list(
        map_stage.Traverse(
            Usd.TraverseInstanceProxies(Usd.PrimDefaultPredicate)
        )
    )

    # A character's blobs are addressed under its name and a map's under its
    # prim paths, so a name that is one of the map's root prims would mix the
    # two (ADR-0040).
    map_roots = {
        prim.GetName() for prim in map_prims if prim.GetParent().IsPseudoRoot()
    }
    for character_name, _ in character_stages:
        if character_name in map_roots:
            raise CookError(
                "character_name_collides",
                character_name,
                f"character {character_name!r} has the name of a root prim of "
                f"the map {map_stage_path}",
            )

    opened_characters = []
    for character_name, stage_path in character_stages:
        character_stage = Usd.Stage.Open(str(stage_path))
        if not character_stage:
            raise CookError("stage_open_failed", "", str(stage_path))
        # The client finds a character's visual mesh at <character
        # path>/Character/Visual (ADR-0040).
        default_prim = character_stage.GetDefaultPrim()
        if not default_prim or default_prim.GetName() != CHARACTER_ROOT_PRIM:
            raise CookError(
                "character_root_misnamed",
                "",
                f"{stage_path}: a character's default prim must be named "
                f"{CHARACTER_ROOT_PRIM!r}",
            )
        # The camera sits, and Shots leave from, <character name>/Character/Eye
        # (ADR-0040).
        eye_prim = default_prim.GetChild(CHARACTER_EYE_PRIM)
        if not eye_prim:
            raise CookError(
                "character_eye_missing",
                "",
                f"{stage_path}: a character's {CHARACTER_ROOT_PRIM!r} prim "
                f"must have an {CHARACTER_EYE_PRIM!r} child",
            )
        # Every Hitbox moves with a joint of it (ADR-0061).
        character_correction = _stage_correction_matrix(character_stage)
        skeleton = _read_skeleton(
            character_name, default_prim, stage_path, character_correction
        )
        character_prims = list(
            character_stage.Traverse(
                Usd.TraverseInstanceProxies(Usd.PrimDefaultPredicate)
            )
        )
        opened_characters.append(
            _OpenedCharacter(
                name=character_name,
                stage_path=stage_path,
                correction=character_correction,
                prims=character_prims,
                eye_path=eye_prim.GetPath(),
                skeleton=skeleton,
            )
        )

    rifle_entry = _cook_rifle(rifle_stage_path)

    total_prims = len(map_prims) + sum(
        len(character.prims) for character in opened_characters
    )
    done = 0

    correction = _stage_correction_matrix(map_stage)
    entries: list[_native.AssetEntry] = [rifle_entry]
    nodes: list[_native.SceneNode] = []
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

    for character in opened_characters:
        body_parts: set[_native.BodyPart] = set()
        for prim in character.prims:
            prim_path = (
                f"{character.name}/{_sanitize_prim_path(str(prim.GetPath()))}"
            )
            xformable = UsdGeom.Xformable(prim)
            local_to_root = (
                xformable.ComputeLocalToWorldTransform(Usd.TimeCode.Default())
                if xformable
                else Gf.Matrix4d(1.0)
            )
            if prim.GetPath() == character.eye_path:
                eye = (local_to_root * character.correction).Transform(
                    Gf.Vec3d(0.0, 0.0, 0.0)
                )
                entries.append(
                    _native.AssetEntry(
                        type=_native.AssetType.EYE,
                        path=prim_path,
                        data=_native.encode_eye_blob(
                            _native.EyeData(position=tuple(eye))
                        ),
                    )
                )
            if prim_path == character.skeleton.path:
                entries.append(
                    _native.AssetEntry(
                        type=_native.AssetType.SKELETON,
                        path=prim_path,
                        data=_native.encode_skeleton_blob(
                            character.skeleton.joints
                        ),
                    )
                )
            body_part = _cook_character_prim(
                prim,
                prim_path,
                local_to_root,
                character.correction,
                character.skeleton,
                entries,
            )
            if body_part is not None:
                body_parts.add(body_part)
            _maybe_cook_texture_prim(
                prim, prim_path, character.stage_path, entries
            )
            done += 1
            if on_prim is not None:
                on_prim(done, total_prims, prim_path)
        # Every hit on a player resolves to a body part (US-11), so a character
        # must be hittable in each.
        missing = [
            name
            for name, body_part in _BODY_PARTS.items()
            if body_part not in body_parts
        ]
        if missing:
            raise CookError(
                "character_hitbox_missing",
                "",
                f"{character.stage_path}: a character needs a hitbox for each "
                f"body part, and has none for {', '.join(missing)}",
            )

    mesh_count = sum(
        1 for entry in entries if entry.type == _native.AssetType.MESH
    )
    texture_count = sum(
        1 for entry in entries if entry.type == _native.AssetType.TEXTURE
    )
    node_count = len(nodes)

    # Client scene: full node data, mesh/material references intact.
    client_scene_blob = _native.encode_scene_blob(nodes)

    # Server scene: same nodes/hierarchy/collision-spawn-hitbox
    # references, but mesh/material path stripped (ADR-0019) - the server
    # never receives visual content, even as a dangling path it can't
    # resolve.
    server_nodes = [
        _native.SceneNode(
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
            # The server never receives visual content (ADR-0019), color
            # included.
            properties=[
                (key, value)
                for key, value in node.properties
                if key != BASE_COLOR_PROPERTY
            ],
        )
        for node in nodes
    ]
    server_scene_blob = _native.encode_scene_blob(server_nodes)

    # Encoded before either pack is written, so a script the wire can't carry
    # leaves neither behind.
    try:
        script_entries = [
            _native.AssetEntry(
                type=_native.AssetType.SCRIPT,
                path=script_path,
                data=_native.encode_script_blob(script),
            )
            for script_path, script in scripts
        ]
    except Exception as error:  # noqa: BLE001 - re-raised as CookError below
        raise CookError("script_encode_failed", "", str(error)) from error
    sound_entries = _sound_entries(sounds) if sounds is not None else []

    # Client pack: everything cooked from this stage, plus the full scene and
    # the cue sounds.
    client_entries = [
        *entries,
        _native.AssetEntry(
            type=_native.AssetType.SCENE,
            path=_native.SCENE_PATH,
            data=client_scene_blob,
        ),
        characters_entry,
        *sound_entries,
    ]
    try:
        client_pack_hash = _native.write_pack(
            client_output_path, client_entries, signing_key
        )
    except Exception as error:  # noqa: BLE001 - re-raised as CookError below
        raise CookError(
            "pack_write_failed", "", f"client pack: {error}"
        ) from error

    # Server pack: only the collision/hitbox/spawn-point/eye entries, plus the
    # stripped scene and the scripts. Its header names the client pack's hash.
    server_entries = [
        entry for entry in entries if entry.type in _SERVER_PACK_ASSET_TYPES
    ]
    server_entries.append(
        _native.AssetEntry(
            type=_native.AssetType.SCENE,
            path=_native.SCENE_PATH,
            data=server_scene_blob,
        )
    )
    server_entries.append(characters_entry)
    server_entries.extend(script_entries)
    try:
        _native.write_pack(
            server_output_path, server_entries, signing_key, client_pack_hash
        )
    except Exception as error:  # noqa: BLE001 - re-raised as CookError below
        raise CookError(
            "pack_write_failed", "", f"server pack: {error}"
        ) from error

    return CookReport(
        mesh_count=mesh_count,
        texture_count=texture_count,
        node_count=node_count,
        script_count=len(scripts),
        sound_count=len(sounds.cues) if sounds is not None else 0,
    )
