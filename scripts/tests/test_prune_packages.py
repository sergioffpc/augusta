import importlib.util
import pathlib
import sys

import pytest

_SCRIPT = pathlib.Path(__file__).resolve().parents[1] / "prune-packages.py"
_SPEC = importlib.util.spec_from_file_location("prune_packages", _SCRIPT)
prune = importlib.util.module_from_spec(_SPEC)
sys.modules["prune_packages"] = prune
_SPEC.loader.exec_module(prune)


def _nuget(version_id, name, created):
    return prune.Version(id=version_id, name=name, tags=(), created=created)


def test_nuget_keeps_the_newest_versions_and_deletes_the_rest():
    versions = [
        _nuget(1, "1.0-vcpkga", "2026-09-01T00:00:00Z"),
        _nuget(3, "1.0-vcpkgc", "2026-10-01T00:00:00Z"),
        _nuget(2, "1.0-vcpkgb", "2026-09-15T00:00:00Z"),
    ]

    plan = prune.plan_nuget("zlib_x64-windows", versions, keep=2)

    assert [v.name for v in plan.keep] == ["1.0-vcpkgb", "1.0-vcpkgc"]
    assert [v.name for v in plan.delete] == ["1.0-vcpkga"]


def test_nuget_keeps_every_version_of_a_package_with_fewer_than_keep():
    versions = [_nuget(1, "1.0-vcpkga", "2026-09-01T00:00:00Z")]

    plan = prune.plan_nuget("zlib_x64-windows", versions, keep=2)

    assert [v.name for v in plan.keep] == ["1.0-vcpkga"]
    assert plan.delete == ()


def _digest(char):
    return "sha256:" + char * 64


def _image(version_id, char, *tags):
    return prune.Version(
        id=version_id, name=_digest(char), tags=tags, created=""
    )


def _referrers_tag(char):
    return "sha256-" + char * 64


def _index(*children):
    return {
        "mediaType": "application/vnd.oci.image.index.v1+json",
        "manifests": [{"digest": child} for child in children],
    }


_MANIFEST = {"mediaType": "application/vnd.oci.image.manifest.v1+json"}


def _reader(manifests):
    def read_manifest(digest):
        return manifests.get(digest, _MANIFEST)

    return read_manifest


def test_a_kept_index_keeps_its_platform_manifests_and_their_referrers():
    versions = [
        _image(1, "a", "sha-0123456789ab"),
        _image(2, "b"),  # amd64
        _image(3, "c"),  # arm64
        _image(4, "d", _referrers_tag("a")),
        _image(5, "e"),  # the index's signature
        _image(6, "f", _referrers_tag("c") + ".sig"),
        _image(7, "9", "sha-ba9876543210"),
        _image(8, "8"),
    ]
    manifests = {
        _digest("a"): _index(_digest("b"), _digest("c")),
        _digest("d"): _index(_digest("e")),
        _digest("9"): _index(_digest("8")),
    }
    roots = [prune.Root("sha-0123456789ab", "a release", required=True)]

    plan = prune.plan_container("augustad", versions, roots, _reader(manifests))

    assert [v.id for v in plan.keep] == [1, 2, 3, 4, 5, 6]
    assert [v.id for v in plan.delete] == [8, 7]


def test_a_required_root_the_package_lacks_fails_the_plan():
    versions = [_image(1, "a", "buildcache")]
    roots = [
        prune.Root("buildcache", "the layer cache", required=True),
        prune.Root("buildcache-arm64", "the layer cache", required=True),
    ]

    with pytest.raises(prune.PruneError, match="buildcache-arm64"):
        prune.plan_container("augustad", versions, roots, _reader({}))


def test_an_optional_root_the_package_lacks_is_reported_absent():
    versions = [_image(1, "a", "buildcache"), _image(2, "b")]
    fixture = prune.Root(_digest("0"), "a policy fixture", required=False)
    roots = [
        prune.Root("buildcache", "the layer cache", required=True),
        fixture,
    ]

    plan = prune.plan_container("augustad", versions, roots, _reader({}))

    assert plan.absent == (fixture,)
    assert [v.id for v in plan.keep] == [1]


def test_a_manifest_that_cannot_be_read_fails_the_plan():
    versions = [_image(1, "a", "buildcache")]
    roots = [prune.Root("buildcache", "the layer cache", required=True)]

    def unreadable(digest):
        raise prune.PruneError(f"{digest}: 503 Service Unavailable")

    with pytest.raises(prune.PruneError, match="503"):
        prune.plan_container("augustad", versions, roots, unreadable)


def _refs(roots, required):
    return sorted(root.ref for root in roots if root.required == required)


def test_image_roots_are_the_caches_branches_releases_and_develop_history():
    tags = {
        "buildcache",
        "buildcache-arm64",
        "develop",
        "main",
        "sha-aaaaaaaaaaaa",
        "sha-aaaaaaaaaaaa-debuginfo",
        "sha-cccccccccccc",
        "sha-dddddddddddd",
        "sha-eeeeeeeeeeee",
        "sha-eeeeeeeeeeee-arm64",
        "sha-ffffffffffff",
        "2.0.0",
    }

    roots = prune.image_roots(
        tags,
        # Newest first; bbbbbbbbbbbb published no image, and dddddddddddd is
        # beyond the newest two that did.
        develop_commits=[
            "aaaaaaaaaaaa",
            "bbbbbbbbbbbb",
            "cccccccccccc",
            "dddddddddddd",
        ],
        # v0.9.0 published no image.
        releases={
            "v0.9.0": "999999999999",
            "v1.0.0": "ffffffffffff",
            "v1.1.0": "eeeeeeeeeeee",
        },
        develop_chart_commit="cccccccccccc",
        references=["sha-0123456789ab"],
        keep_develop_commits=2,
    )

    assert _refs(roots, required=True) == [
        "buildcache",
        "buildcache-arm64",
        "develop",
        "main",
        "sha-cccccccccccc",
        "sha-eeeeeeeeeeee",
    ]
    assert _refs(roots, required=False) == [
        "2.0.0",
        "sha-0123456789ab",
        "sha-999999999999",
        "sha-aaaaaaaaaaaa",
        "sha-aaaaaaaaaaaa-debuginfo",
        "sha-cccccccccccc",
        "sha-eeeeeeeeeeee",
        "sha-eeeeeeeeeeee-arm64",
        "sha-ffffffffffff",
    ]


def test_the_develop_chart_commit_is_read_from_its_version_tag():
    versions = [
        _image(1, "a", "3.1.0_aaaaaaaaaaaa"),
        _image(2, "b", "3.1.0_bbbbbbbbbbbb", "develop"),
    ]

    assert prune.develop_chart_commit(versions) == "bbbbbbbbbbbb"


def test_a_chart_package_without_a_develop_tag_fails():
    versions = [_image(1, "a", "3.1.0_aaaaaaaaaaaa")]

    with pytest.raises(prune.PruneError, match="develop"):
        prune.develop_chart_commit(versions)


def test_chart_roots_are_develop_the_releases_and_the_kept_commits_charts():
    tags = {
        "develop",
        "3.0.0",
        "3.0.0_aaaaaaaaaaaa",
        "3.1.0_bbbbbbbbbbbb",
        "3.1.0_cccccccccccc",
    }

    roots = prune.chart_roots(tags, ["bbbbbbbbbbbb", "aaaaaaaaaaaa"])

    assert _refs(roots, required=True) == ["develop"]
    assert _refs(roots, required=False) == [
        "3.0.0",
        "3.0.0_aaaaaaaaaaaa",
        "3.1.0_bbbbbbbbbbbb",
    ]


def test_references_take_a_pinned_digest_over_its_tag_and_skip_placeholders():
    pinned = "sha256:" + "d" * 64
    text = "\n".join(
        [
            f"image: ghcr.io/sergioffpc/augustad:sha-f6e03442e921@{pinned}",
            "image: ghcr.io/sergioffpc/augustad:2.0.2",
            "gh attestation verify oci://ghcr.io/sergioffpc/augustad:sha-<12>",
            "image=ghcr.io/sergioffpc/augustad:sha-$(git rev-parse HEAD)",
            "url: oci://ghcr.io/sergioffpc/charts/augustad",
            "ghcr.io/sergioffpc/augustad/other:1.0.0",
        ]
    )

    assert prune.parse_references(text, "sergioffpc") == ["2.0.2", pinned]


def test_a_plan_prints_its_counts_then_each_version_sorted():
    plan = prune.Plan(
        package="charts/augustad",
        keep=(_image(2, "b", "develop", "3.1.0_bbbbbbbbbbbb"), _image(3, "c")),
        delete=(_image(1, "a", "3.1.0_aaaaaaaaaaaa"),),
        absent=(prune.Root("3.0.0", "a release", required=False),),
    )

    assert prune.format_plan(plan) == "\n".join(
        [
            "charts/augustad: keep 2, delete 1",
            "  absent 3.0.0 (a release)",
            f"  keep   {_digest('b')} 3.1.0_bbbbbbbbbbbb develop",
            f"  keep   {_digest('c')}",
            f"  delete {_digest('a')} 3.1.0_aaaaaaaaaaaa",
        ]
    )


def test_a_delete_whose_version_was_tagged_since_the_plan_is_stale():
    plan = prune.Plan(
        package="augustad",
        keep=(),
        delete=(_image(1, "a"), _image(2, "b"), _image(3, "c")),
    )
    # 1 was tagged since; 3 is gone.
    now = [_image(1, "a", "sha-0123456789ab"), _image(2, "b")]

    assert prune.stale_deletes(plan, now) == [plan.delete[0]]
