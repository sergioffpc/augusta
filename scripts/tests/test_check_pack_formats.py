import importlib.util
import pathlib
import sys

import pytest

_SCRIPT = pathlib.Path(__file__).resolve().parents[1] / "check-pack-formats.py"
_SPEC = importlib.util.spec_from_file_location("check_pack_formats", _SCRIPT)
formats = importlib.util.module_from_spec(_SPEC)
sys.modules["check_pack_formats"] = formats
_SPEC.loader.exec_module(formats)

_RELEASE = """\
apiVersion: helm.toolkit.fluxcd.io/v2
kind: HelmRelease
spec:
  values:
    servers:
      firebase:
        packVersion: "c136bad8059d"
{pack_format}        nodePort: 30700
"""


def _repo(root, kformat_version, pack_format_line):
    """A repository at root with one environment and the engine's format."""
    assets = root / "src" / "modules" / "assets" / "assets.cpp"
    assets.parent.mkdir(parents=True)
    assets.write_text(
        "constexpr std::array<char, 4> kMagic = {'A', 'U', 'G', 'P'};\n"
        f"constexpr std::uint32_t kFormatVersion = {kformat_version};\n",
        encoding="utf-8",
    )
    apps = root / "clusters" / "onprem" / "apps"
    apps.mkdir(parents=True)
    (apps / "develop.yaml").write_text(
        _RELEASE.format(pack_format=pack_format_line), encoding="utf-8"
    )
    return root


def test_packs_of_the_engines_format_pass(tmp_path):
    root = _repo(tmp_path, 3, "        packFormat: 3\n")

    assert formats.check(root) == []


def test_a_format_bump_without_moving_the_packs_fails(tmp_path):
    root = _repo(tmp_path, 4, "        packFormat: 3\n")

    (error,) = formats.check(root)

    assert error.startswith(
        "clusters/onprem/apps/develop.yaml: "
        "spec.values.servers.firebase.packFormat is 3, but the engine reads "
        "pack format 4"
    )


def test_a_pack_version_with_no_pack_format_fails(tmp_path):
    root = _repo(tmp_path, 3, "")

    (error,) = formats.check(root)

    assert "servers.firebase names a packVersion but no packFormat" in error


@pytest.mark.parametrize("value", ['"3"', "3.0", "true"])
def test_a_pack_format_that_is_not_an_integer_fails(tmp_path, value):
    root = _repo(tmp_path, 3, f"        packFormat: {value}\n")

    (error,) = formats.check(root)

    assert "servers.firebase.packFormat must be an integer" in error


def test_every_document_of_a_file_is_checked():
    documents = [
        {"kind": "Namespace"},
        {"servers": {"a": {"packVersion": "x", "packFormat": 3}}},
        {"servers": {"b": {"packVersion": "y", "packFormat": 2}}},
    ]

    (error,) = formats.mismatches(documents, 3)

    assert error.startswith("servers.b.packFormat is 2")


def _engine(root, source):
    assets = root / "src" / "modules" / "assets" / "assets.cpp"
    assets.parent.mkdir(parents=True)
    assets.write_text(source, encoding="utf-8")
    return root


@pytest.mark.parametrize(
    "source",
    [
        "",
        "constexpr std::uint32_t kFormatVersion = 3;\n"
        "constexpr std::uint32_t kFormatVersion = 4;\n",
    ],
)
def test_an_engine_not_defining_one_format_version_is_an_error(
    tmp_path, source
):
    root = _engine(tmp_path, source)

    with pytest.raises(ValueError, match="kFormatVersion"):
        formats.engine_format(root)


def test_the_format_version_is_read_whatever_its_type_and_spacing(tmp_path):
    root = _engine(tmp_path, "constexpr  uint16_t kFormatVersion=7 ;\n")

    assert formats.engine_format(root) == 7


def test_one_environment_moved_to_another_format_fails(tmp_path):
    root = _repo(tmp_path, 3, "        packFormat: 3\n")
    staging = root / "clusters" / "onprem" / "apps" / "staging.yaml"
    staging.write_text(
        _RELEASE.format(pack_format="        packFormat: 2\n"),
        encoding="utf-8",
    )

    (error,) = formats.check(root)

    assert error.startswith(
        "clusters/onprem/apps/staging.yaml: "
        "spec.values.servers.firebase.packFormat is 2, but the engine reads "
        "pack format 3"
    )
