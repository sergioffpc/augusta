#!/usr/bin/env python3
r"""Prunes the old versions of the owner's GitHub Packages.

GitHub Packages keeps every version CI pushes. This deletes the ones nothing
needs any more, in three families, and prints the plan for each first:

- augustad, the server image: everything outside the closure of its roots
  (image_roots). The closure is each root, the platform manifests of an index,
  and whatever refers to any of them through cosign's tags (the OCI referrers
  tag schema GHCR is read by, since it serves no referrers API): signatures,
  provenance and SBOM attestations.
- charts/augustad, develop's chart: the same, from chart_roots.
- The vcpkg NuGet binary cache: each package's newest versions. vcpkg reads
  only the version its port's ABI hash names, and a miss rebuilds the port.

A plan that cannot be made safely (a required root missing, a manifest that
cannot be read) fails the run before anything is deleted. It deletes only
with --apply. Run from a checkout whose origin/develop, origin/main and v*
tags are fetched, with gh logged in (read:packages; delete:packages to apply):

  scripts/prune-packages.py --owner sergioffpc            # the plan alone
  scripts/prune-packages.py --owner sergioffpc --apply    # and deletes it
"""

import argparse
import dataclasses
import datetime
import json
import re
import subprocess
import sys
import urllib.error
import urllib.parse
import urllib.request

# The commits whose images and charts are kept besides the releases', and
# each NuGet package's versions kept.
_KEEP_DEVELOP_COMMITS = 20
_KEEP_NUGET_VERSIONS = 2

_IMAGE = "augustad"
_CHART = "charts/augustad"
_DEVELOP = "origin/develop"
_MAIN = "origin/main"
# Where the repository names images: its two long-lived branches.
_BRANCHES = (_DEVELOP, _MAIN)
# How long before the newest version a container version is kept for
# whatever it is, as what a CI run overlapping this one may be using.
_KEEP_RECENT = datetime.timedelta(days=1)

# A release, as a Git tag (vX.Y.Z, pre-releases excluded, as staging's
# GitRepository takes it) or as an image or chart tag.
_RELEASE_TAG = re.compile(r"v?(\d+)\.(\d+)\.(\d+)")
# A chart CI published: <chart version>_<12 characters of its commit>.
_CHART_TAG = re.compile(r"\d+\.\d+\.\d+_(?P<commit>[0-9a-f]{12})")
# cosign's tags for what refers to sha256:<hex>: the OCI referrers tag schema
# (sha256-<hex>, an index of the referrers), and its older .sig/.att/.sbom.
_REFERRER_TAG_SUFFIXES = ("", ".sig", ".att", ".sbom")

_MANIFEST_TYPES = ",".join(
    [
        "application/vnd.oci.image.index.v1+json",
        "application/vnd.oci.image.manifest.v1+json",
        "application/vnd.docker.distribution.manifest.list.v2+json",
        "application/vnd.docker.distribution.manifest.v2+json",
    ]
)


@dataclasses.dataclass(frozen=True)
class Version:
    """A package version as the GitHub Packages REST API lists it.

    Attributes:
        id: The version's ID, which deleting it takes.
        name: A container's manifest digest, or a NuGet package's version.
        tags: A container version's tags, sorted.
        created: When it was pushed, as an ISO 8601 UTC timestamp.
    """

    id: int
    name: str
    tags: tuple[str, ...]
    created: str


@dataclasses.dataclass(frozen=True)
class Root:
    """A tag or digest a container package keeps, and why.

    Attributes:
        ref: The tag, or the sha256:<hex> digest.
        reason: Why it is kept, for the log.
        required: Whether its absence fails the plan, rather than being
            reported.
    """

    ref: str
    reason: str
    required: bool


@dataclasses.dataclass(frozen=True)
class Plan:
    """What pruning one package keeps and deletes, each sorted by name.

    Attributes:
        package_type: The package's type, as the REST API names it.
        package: The package's name.
        keep: The versions kept.
        delete: The versions deleted.
        absent: The optional roots the package does not have.
    """

    package_type: str
    package: str
    keep: tuple[Version, ...]
    delete: tuple[Version, ...]
    absent: tuple[Root, ...] = ()


class PruneError(Exception):
    """A plan cannot be made, or carried out, safely."""


# Decisions: what to keep and delete, from what was read.


def plan_nuget(package, versions, keep):
    """Keeps a NuGet package's newest versions, by when they were pushed."""
    newest = sorted(versions, key=lambda v: (v.created, v.id), reverse=True)
    return _plan("nuget", package, newest[:keep], newest[keep:])


def plan_container(package, versions, roots, read_manifest, keep_since=""):
    """Keeps a container package's roots and everything they need.

    Args:
        package: The package's name.
        versions: Its versions.
        roots: What it keeps.
        read_manifest: Reads a manifest, by digest, as parsed JSON.
        keep_since: An ISO 8601 UTC timestamp: the versions pushed since are
            roots too, as what a CI run may be pushing, or making an index
            of, while this one runs.

    Returns:
        The plan.

    Raises:
        PruneError: A required root is missing, or read_manifest raised it.
    """
    by_tag = {tag: v.name for v in versions for tag in v.tags}
    known = {v.name for v in versions}
    pending = [
        v.name for v in versions if keep_since and v.created >= keep_since
    ]
    absent = []
    for root in roots:
        digest = by_tag.get(root.ref, root.ref)
        if digest in known:
            pending.append(digest)
        elif root.required:
            raise PruneError(f"{package}: no {root.ref} ({root.reason})")
        else:
            absent.append(root)
    keep = set()
    while pending:
        digest = pending.pop()
        # What an index names but the package lacks is gone already.
        if digest in keep or digest not in known:
            continue
        keep.add(digest)
        manifest = read_manifest(digest)
        pending.extend(
            child["digest"] for child in manifest.get("manifests", [])
        )
        hex_digest = digest.removeprefix("sha256:")
        for suffix in _REFERRER_TAG_SUFFIXES:
            if referrers := by_tag.get(f"sha256-{hex_digest}{suffix}"):
                pending.append(referrers)
    return _plan(
        "container",
        package,
        [v for v in versions if v.name in keep],
        [v for v in versions if v.name not in keep],
        sorted(set(absent), key=lambda root: root.ref),
    )


def _plan(package_type, package, keep, delete, absent=()):
    def by_name(versions):
        return tuple(sorted(versions, key=lambda v: v.name))

    return Plan(
        package_type, package, by_name(keep), by_name(delete), tuple(absent)
    )


def kept_develop_commits(tags, develop_commits, keep):
    """The newest develop commits, of those whose image CI published.

    Args:
        tags: The augustad image's tags.
        develop_commits: develop's commits, newest first, by the 12
            characters of their sha-<12> tags.
        keep: How many to keep.

    Returns:
        Up to keep commits, newest first.
    """
    return [c for c in develop_commits if f"sha-{c}" in tags][:keep]


def image_roots(
    tags,
    develop_commits,
    releases,
    develop_chart_commit,
    main_commit,
    references,
):
    """What the augustad image package keeps.

    Args:
        tags: The package's tags.
        develop_commits: The develop commits whose images are kept, from
            kept_develop_commits.
        releases: Each vX.Y.Z Git tag's commit, by its 12 characters.
        develop_chart_commit: The commit of the chart tagged develop, which
            the develop environment runs.
        main_commit: main's newest commit, whose index of both architectures
            may not be made yet.
        references: The image's tags and digests the repository names.

    Returns:
        The roots. The required ones are what CI and the cluster run now.
    """
    roots = [
        Root(tag, "CI's layer cache", required=True)
        for tag in ("buildcache", "buildcache-arm64")
    ]
    roots += [
        Root(branch, "the branch's newest image", required=True)
        for branch in ("develop", "main")
    ]
    develop_runs = "what the develop environment runs"
    roots.append(Root(f"sha-{develop_chart_commit}", develop_runs, True))
    roots += _commit_roots(tags, develop_chart_commit, develop_runs)
    if releases:
        newest = max(releases, key=_release_key)
        roots.append(
            Root(
                f"sha-{releases[newest]}",
                f"what the staging environment runs, {newest}",
                required=True,
            )
        )
    for release in sorted(releases, key=_release_key):
        roots += _commit_roots(tags, releases[release], f"release {release}")
    roots += _commit_roots(tags, main_commit, "main's newest commit")
    for commit in develop_commits:
        roots += _commit_roots(tags, commit, "a recent develop image")
    roots += [
        Root(tag, "a release tag", required=False)
        for tag in sorted(tags)
        if _RELEASE_TAG.fullmatch(tag)
    ]
    roots += [
        Root(ref, "named in the repository", required=False)
        for ref in sorted(set(references))
    ]
    return roots


def _commit_roots(tags, commit, reason):
    # A commit's image, with its debug info and arm64 image beside it: the
    # image itself even if absent, so the plan says a release's is missing.
    prefix = f"sha-{commit}"
    return [Root(prefix, reason, required=False)] + [
        Root(tag, reason, required=False)
        for tag in sorted(tags)
        if tag.startswith(prefix + "-")
    ]


def _release_key(tag):
    return tuple(int(part) for part in _RELEASE_TAG.fullmatch(tag).groups())


def develop_chart_commit(versions):
    """The commit of the chart tagged develop, which develop runs.

    Args:
        versions: The chart package's versions.

    Returns:
        The commit, by the 12 characters its image's sha-<12> tag has.

    Raises:
        PruneError: No chart is tagged develop, or none with a version.
    """
    for version in versions:
        if "develop" in version.tags:
            for tag in version.tags:
                if match := _CHART_TAG.fullmatch(tag):
                    return match["commit"]
    raise PruneError(f"{_CHART}: no chart tagged develop and a version")


def chart_roots(tags, commits):
    """What the chart package keeps.

    Args:
        tags: The package's tags.
        commits: The commits whose images are kept, the releases' and the
            recent develop ones: their charts are what develop can be
            rolled back to.

    Returns:
        The roots.
    """
    roots = [
        Root("develop", "what the develop environment runs", required=True)
    ]
    commits = set(commits)
    for tag in sorted(tags):
        if _RELEASE_TAG.fullmatch(tag):
            roots.append(Root(tag, "a release", required=False))
        elif (match := _CHART_TAG.fullmatch(tag)) and match[
            "commit"
        ] in commits:
            roots.append(Root(tag, "a kept commit's chart", required=False))
    return roots


def parse_references(text, owner):
    """The augustad image's tags and digests a text names.

    A reference pinned by digest is the digest alone, and a placeholder
    (sha-<12>, sha-$(...)) is no reference.

    Args:
        text: The text.
        owner: The image's GHCR owner.

    Returns:
        The tags and digests, sorted.
    """
    reference = re.compile(
        rf"ghcr\.io/{re.escape(owner)}/{_IMAGE}"
        r"(?::(?P<tag>\w[\w.-]{0,127}))?"
        r"(?:@(?P<digest>sha256:[0-9a-f]{64}))?"
        r"(?![\w.<${/-])"
    )
    refs = set()
    for match in reference.finditer(text):
        if ref := match["digest"] or match["tag"]:
            refs.add(ref)
    return sorted(refs)


def stale_deletes(plan, versions):
    """The deletes a package's versions now contradict.

    A version tagged since the plan was made may be what CI just pushed (a
    rebuild of the same image has the same digest), so the plan is stale.

    Args:
        plan: The plan.
        versions: The package's versions, listed again.

    Returns:
        The plan's deletes whose version's tags have changed.
    """
    now = {v.id: v for v in versions}
    return [v for v in plan.delete if v.id in now and now[v.id].tags != v.tags]


def format_plan(plan):
    """A plan as the workflow log shows it: the same plan, the same text."""
    lines = [_counts(plan)]
    lines += [f"  absent {root.ref} ({root.reason})" for root in plan.absent]
    for action, versions in (("keep  ", plan.keep), ("delete", plan.delete)):
        for version in versions:
            lines.append(
                " ".join([f"  {action}", version.name, *sorted(version.tags)])
            )
    return "\n".join(lines)


def _counts(plan):
    return f"{plan.package}: keep {len(plan.keep)}, delete {len(plan.delete)}"


# Mechanisms: the GitHub Packages REST API, GHCR and Git.


def _run(*command, success=(0,)):
    result = subprocess.run(
        command, check=False, capture_output=True, text=True
    )
    if result.returncode not in success:
        raise PruneError(f"{' '.join(command)}: {result.stderr.strip()}")
    return result.stdout


def _versions_path(owner, package_type, package):
    name = urllib.parse.quote(package, safe="")
    return f"users/{owner}/packages/{package_type}/{name}/versions"


def list_versions(owner, package_type, package):
    """Lists a package's versions, through gh."""
    query = (
        ".[] | {id, name, created_at, tags: (.metadata.container.tags // [])}"
    )
    out = _run(
        "gh",
        "api",
        "--paginate",
        "--jq",
        query,
        _versions_path(owner, package_type, package) + "?per_page=100",
    )
    versions = []
    for line in out.splitlines():
        version = json.loads(line)
        versions.append(
            Version(
                id=version["id"],
                name=version["name"],
                tags=tuple(sorted(version["tags"])),
                created=version["created_at"],
            )
        )
    return versions


def list_packages(owner, package_type):
    """Lists the owner's packages of a type, by name."""
    out = _run(
        "gh",
        "api",
        "--paginate",
        "--jq",
        ".[].name",
        f"users/{owner}/packages?package_type={package_type}&per_page=100",
    )
    return sorted(out.split())


def delete_version(owner, package_type, package, version):
    """Deletes a package version, through gh."""
    _run(
        "gh",
        "api",
        "--method",
        "DELETE",
        f"{_versions_path(owner, package_type, package)}/{version.id}",
    )


class Registry:
    """Reads one GHCR repository's manifests, anonymously: it is public."""

    def __init__(self, owner, package):
        self._repository = f"{owner}/{package}"
        self._token = None

    def read_manifest(self, digest):
        """Reads a manifest, by digest, as parsed JSON.

        Raises:
            PruneError: It cannot be read.
        """
        url = f"https://ghcr.io/v2/{self._repository}/manifests/{digest}"
        request = urllib.request.Request(
            url,
            headers={
                "Authorization": f"Bearer {self._pull_token()}",
                "Accept": _MANIFEST_TYPES,
            },
        )
        return self._read_json(request)

    def _pull_token(self):
        if self._token is None:
            scope = f"repository:{self._repository}:pull"
            url = "https://ghcr.io/token?" + urllib.parse.urlencode(
                {"scope": scope}
            )
            self._token = self._read_json(url)["token"]
        return self._token

    @staticmethod
    def _read_json(request):
        try:
            with urllib.request.urlopen(request, timeout=60) as response:
                return json.load(response)
        except (urllib.error.URLError, TimeoutError, ValueError) as error:
            url = getattr(request, "full_url", request)
            raise PruneError(f"{url}: {error}") from error


def develop_commits():
    """The develop commits, newest first, as their sha-<12> tags name them.

    First parents alone: what CI ran on develop, each merge pushed.
    """
    return [
        c[:12]
        for c in _run("git", "rev-list", "--first-parent", _DEVELOP).split()
    ]


def releases():
    """Each vX.Y.Z Git tag's commit, by its 12 characters."""
    out = _run(
        "git",
        "for-each-ref",
        "--format=%(refname:strip=2) %(objectname) %(*objectname)",
        "refs/tags/v*",
    )
    commits = {}
    for line in out.splitlines():
        # An annotated tag's commit is its peeled object, a lightweight's its
        # own.
        tag, *objects = line.split()
        if _RELEASE_TAG.fullmatch(tag):
            commits[tag] = objects[-1][:12]
    return commits


def repository_references(owner):
    """The augustad image's tags and digests the long-lived branches name."""
    # git grep exits 1 when nothing matches.
    out = _run(
        "git",
        "grep",
        "-hIF",
        f"ghcr.io/{owner}/{_IMAGE}",
        *_BRANCHES,
        success=(0, 1),
    )
    return parse_references(out, owner)


def main_commit():
    """The newest commit on main, by its 12 characters."""
    return _run("git", "rev-parse", _MAIN).strip()[:12]


def _parse_args(argv):
    parser = argparse.ArgumentParser(
        description=__doc__.split("\n", 1)[0],
    )
    parser.add_argument("--owner", required=True, help="the packages' owner")
    parser.add_argument(
        "--keep-develop-commits",
        type=int,
        default=_KEEP_DEVELOP_COMMITS,
        help="develop's newest images, and their charts, to keep "
        "(default %(default)s)",
    )
    parser.add_argument(
        "--keep-nuget-versions",
        type=int,
        default=_KEEP_NUGET_VERSIONS,
        help="each NuGet package's newest versions to keep "
        "(default %(default)s)",
    )
    parser.add_argument(
        "--keep-image",
        action="append",
        default=[],
        metavar="REF",
        help="another augustad tag or digest to keep, which must exist",
    )
    parser.add_argument(
        "--apply",
        action="store_true",
        help="delete what the plan deletes, rather than only printing it",
    )
    return parser.parse_args(argv)


def _tags(versions):
    return {tag for v in versions for tag in v.tags}


def _keep_since(versions):
    # A day before the newest version: whatever a CI run that overlaps this
    # one pushed. Not the clock's, so an unchanged package plans the same.
    newest = max(datetime.datetime.fromisoformat(v.created) for v in versions)
    return (newest - _KEEP_RECENT).strftime("%Y-%m-%dT%H:%M:%SZ")


def _plans(args):
    owner = args.owner
    chart_versions = list_versions(owner, "container", _CHART)
    image_versions = list_versions(owner, "container", _IMAGE)
    image_tags = _tags(image_versions)
    commits = kept_develop_commits(
        image_tags, develop_commits(), args.keep_develop_commits
    )
    release_commits = releases()
    roots = image_roots(
        image_tags,
        commits,
        release_commits,
        develop_chart_commit(chart_versions),
        main_commit(),
        repository_references(owner),
    )
    roots += [Root(ref, "asked for", required=True) for ref in args.keep_image]
    plans = [
        plan_container(
            _IMAGE,
            image_versions,
            roots,
            Registry(owner, _IMAGE).read_manifest,
            _keep_since(image_versions),
        ),
        plan_container(
            _CHART,
            chart_versions,
            chart_roots(
                _tags(chart_versions), commits + list(release_commits.values())
            ),
            Registry(owner, _CHART).read_manifest,
            _keep_since(chart_versions),
        ),
    ]
    packages = list_packages(owner, "nuget")
    # vcpkg's cache is never empty: a token that cannot see it lists nothing.
    if not packages:
        raise PruneError(f"{owner}: no NuGet packages listed")
    for package in packages:
        plans.append(
            plan_nuget(
                package,
                list_versions(owner, "nuget", package),
                args.keep_nuget_versions,
            )
        )
    return plans


def _summary(plans):
    lines = ["Summary:"]
    lines += [f"  {_counts(p)}" for p in plans if p.package_type == "container"]
    nuget = [plan for plan in plans if plan.package_type == "nuget"]
    lines.append(
        f"  NuGet, {len(nuget)} packages:"
        f" keep {sum(len(plan.keep) for plan in nuget)},"
        f" delete {sum(len(plan.delete) for plan in nuget)}"
    )
    return "\n".join(lines)


def _apply(owner, plan):
    if not plan.delete:
        return
    now = list_versions(owner, plan.package_type, plan.package)
    if stale := stale_deletes(plan, now):
        names = ", ".join(v.name for v in stale)
        raise PruneError(f"{plan.package}: tagged since the plan: {names}")
    for version in plan.delete:
        delete_version(owner, plan.package_type, plan.package, version)
    print(f"{plan.package}: deleted {len(plan.delete)}")


def main(argv):
    """Prints the plans, and carries them out with --apply."""
    args = _parse_args(argv)
    try:
        # Every plan is made before anything is deleted.
        plans = _plans(args)
        for plan in plans:
            print(format_plan(plan))
        print(_summary(plans))
        if not args.apply:
            print("Dry run: nothing deleted. --apply deletes it.")
            return 0
        for plan in plans:
            _apply(args.owner, plan)
    except PruneError as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
