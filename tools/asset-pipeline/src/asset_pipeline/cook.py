"""The asset cooker itself (ADR-0030): walks an authored OpenUSD stage and
bakes it into signed client/server packs (ADR-0031/ADR-0032). Walks the
stage through pxr directly (the same pip-installed usd-optimize build
optimize.py already uses, rather than linking a second, independently-
built OpenUSD - see ADR-0030 for why those can't coexist in one process),
and calls the small native _meshoptimizer/_textconv bindings only for the
two pieces with no Python equivalent (mesh optimization, texture
compression).
"""

from __future__ import annotations

from collections.abc import Callable
from dataclasses import dataclass
from pathlib import Path

from pxr import Gf, Sdf, Usd, UsdGeom, UsdPhysics, UsdShade

from asset_pipeline import _meshoptimizer, _textconv
from asset_pipeline.pack import (
    AssetEntry,
    MeshData,
    SceneNode,
    encode_mesh_blob,
    encode_scene_blob,
    encode_spawn_point_blob,
    encode_texture_blob,
)
from asset_pipeline.pack import ASSET_TYPE_COLLISION as _TYPE_COLLISION
from asset_pipeline.pack import ASSET_TYPE_HITBOX as _TYPE_HITBOX
from asset_pipeline.pack import ASSET_TYPE_MESH as _TYPE_MESH
from asset_pipeline.pack import ASSET_TYPE_SCENE as _TYPE_SCENE
from asset_pipeline.pack import ASSET_TYPE_SPAWN_POINT as _TYPE_SPAWN_POINT
from asset_pipeline.pack import ASSET_TYPE_TEXTURE as _TYPE_TEXTURE
from asset_pipeline.pack import NO_PARENT, TEXTURE_FORMAT_BC4, TEXTURE_FORMAT_BC5, TEXTURE_FORMAT_BC7, write_pack

# augusta:spawnPoint / augusta:hitbox: custom bool attributes (ADR-0032's
# authoring convention) rather than a native USD prim type. A hitbox is
# authored as a UsdGeomMesh with PhysicsCollisionAPI applied (same as any
# other collider) plus this marker.
_SPAWN_POINT_ATTR = "augusta:spawnPoint"
_HITBOX_ATTR = "augusta:hitbox"
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
    with no meshoptimizer pass applied - shared by _read_mesh (visual,
    optimized afterward) and collision/hitbox geometry reading
    (_build_node), where meshoptimizer's lossy simplification could let a
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


def _read_mesh(mesh: UsdGeom.Mesh, prim_path: str) -> MeshData:
    return _optimize_mesh(_read_raw_mesh_geometry(mesh, prim_path))


def _build_node(
    prim: Usd.Prim,
    correction: Gf.Matrix4d,
    node_index_of: dict[str, int],
    entries: list[AssetEntry],
) -> SceneNode:
    """Builds prim's own SceneNode, appending a spawn-point AssetEntry if
    augusta:spawnPoint is set, and - if prim is a UsdGeomMesh - exactly
    one geometry AssetEntry: kHitbox if augusta:hitbox is set, else
    kCollision if a PhysX collider is applied, else kMesh. node_index_of
    must already contain every ancestor of prim - guaranteed by pre-order
    traversal (see cook_stage's own comment on its Traverse() call).
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

    if prim.IsA(UsdGeom.Mesh):
        geom_mesh = UsdGeom.Mesh(prim)
        # A mesh prim contributes exactly one geometry blob, addressed by
        # its own path - a hitbox marker or an applied PhysX collider
        # means this prim is a physics-only proxy, read raw (no
        # meshoptimizer simplification) and filed as kHitbox/kCollision
        # instead of kMesh.
        if is_hitbox:
            hitbox_data = _read_raw_mesh_geometry(geom_mesh, prim_path)
            entries.append(AssetEntry(type=_TYPE_HITBOX, path=prim_path, data=encode_mesh_blob(hitbox_data)))
        elif _has_collision_enabled(prim):
            collision_data = _read_raw_mesh_geometry(geom_mesh, prim_path)
            entries.append(AssetEntry(type=_TYPE_COLLISION, path=prim_path, data=encode_mesh_blob(collision_data)))
            node.collider_path = prim_path
        else:
            mesh_data = _read_mesh(geom_mesh, prim_path)
            entries.append(AssetEntry(type=_TYPE_MESH, path=prim_path, data=encode_mesh_blob(mesh_data)))
            node.mesh_path = prim_path

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
# geometry, hitboxes, and spawn points. Mesh/texture/scene are client-only
# (scene is handled separately, since it needs its mesh/material
# references stripped rather than being dropped outright).
_SERVER_PACK_ASSET_TYPES = frozenset({_TYPE_COLLISION, _TYPE_HITBOX, _TYPE_SPAWN_POINT})


def cook_stage(
    stage_path: Path,
    client_output_path: Path,
    server_output_path: Path,
    signing_key: bytes,
    on_prim: Callable[[int, int, str], None] | None = None,
) -> CookReport:
    """Bakes stage_path into signed client/server packs.

    on_prim(done, total, prim_path), if given, is called after each prim is
    cooked - the only part of cooking that scales with the stage's size.
    """
    stage = Usd.Stage.Open(str(stage_path))
    if not stage:
        raise CookError("stage_open_failed", "", str(stage_path))

    correction = _stage_correction_matrix(stage)

    entries: list[AssetEntry] = []
    nodes: list[SceneNode] = []
    node_index_of: dict[str, int] = {}

    # TraverseInstanceProxies: expands instanceable prototypes into
    # per-instance proxy prims instead of skipping them - each instance
    # becomes its own node subtree, de-instanced at cook time. Traverse()
    # visits prims pre-order (a parent always before its children), which
    # _build_node relies on.
    prims = list(stage.Traverse(Usd.TraverseInstanceProxies(Usd.PrimDefaultPredicate)))
    for done, prim in enumerate(prims, start=1):
        node = _build_node(prim, correction, node_index_of, entries)
        prim_path = node.name
        node_index_of[str(prim.GetPath())] = len(nodes)
        nodes.append(node)

        _maybe_cook_texture_prim(prim, prim_path, stage_path, entries)
        if on_prim is not None:
            on_prim(done, len(prims), prim_path)

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
            properties=node.properties,
        )
        for node in nodes
    ]
    server_scene_blob = encode_scene_blob(server_nodes)

    # Server pack: only the collision/hitbox/spawn-point entries, plus the
    # stripped scene.
    server_entries = [entry for entry in entries if entry.type in _SERVER_PACK_ASSET_TYPES]
    server_entries.append(AssetEntry(type=_TYPE_SCENE, path="Scene", data=server_scene_blob))
    try:
        write_pack(server_output_path, server_entries, signing_key)
    except Exception as error:  # noqa: BLE001 - re-raised as CookError below
        raise CookError("pack_write_failed", "", f"server pack: {error}") from error

    # Client pack: everything cooked from this stage, plus the full scene.
    client_entries = [*entries, AssetEntry(type=_TYPE_SCENE, path="Scene", data=client_scene_blob)]
    try:
        write_pack(client_output_path, client_entries, signing_key)
    except Exception as error:  # noqa: BLE001 - re-raised as CookError below
        raise CookError("pack_write_failed", "", f"client pack: {error}") from error

    return CookReport(mesh_count=mesh_count, texture_count=texture_count, node_count=node_count)
