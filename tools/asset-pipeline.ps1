<#
Runs the full asset-cooking pipeline end to end (ADR-0030): usd-optimize
(stage cleanup, ADR-0015) -> usd-validation-nvidia (validation, ADR-0015) ->
augusta_asset_cooking (bake to a runtime pack, ADR-0031/ADR-0032). Stage
cleanup/validation deliberately stay out of augusta_asset_cooking itself -
usdOptimize/nvidia_usd_validate are authoring-only, pip-installed CLI tools
(scripts/bootstrap-asset-pipeline.ps1, opt-in) with no place in the cooker's
own vcpkg-vendored C++ dependency graph (meshoptimizer/DirectXTex/OpenUSD)
or in CI, which never installs them.

A validation failure aborts before the cooker ever runs, so no pack is
written for a stage that didn't pass cleanup/validation.
#>

param(
  # The raw authored OpenUSD stage (e.g. exported from USD Composer).
  [Parameter(Mandatory = $true)]
  [string]$Stage,

  # Where to write the signed runtime pack.
  [Parameter(Mandatory = $true)]
  [string]$OutputPack,

  # Ed25519 private key (augusta_asset_cooking --gen-keypair output) used to
  # sign the pack.
  [Parameter(Mandatory = $true)]
  [string]$SigningKey,

  # Defaults to the windows-tools preset's own build output (CMakePresets.json)
  # - override only if building/invoking from a different configuration.
  [string]$CookerPath = (Join-Path $PSScriptRoot "..\build\x64-windows-tools\tools\asset-cooking\augusta_asset_cooking.exe")
)

$ErrorActionPreference = "Stop"

function Assert-CommandExists {
  param([string]$Name)
  if (-not (Get-Command $Name -ErrorAction SilentlyContinue)) {
    throw "$Name not found on PATH - run scripts/bootstrap-asset-pipeline.ps1 first."
  }
}

Assert-CommandExists "usdOptimize"
Assert-CommandExists "nvidia_usd_validate"
if (-not (Test-Path $CookerPath)) {
  throw "augusta_asset_cooking not found at $CookerPath - build it first (cmake --build --preset " +
    "windows-tools --target augusta_asset_cooking)."
}
if (-not (Test-Path $Stage)) {
  throw "Stage not found: $Stage"
}
if (-not (Test-Path $SigningKey)) {
  throw "Signing key not found: $SigningKey"
}

$cleanedStage = Join-Path ([System.IO.Path]::GetTempPath()) `
  "$([System.IO.Path]::GetFileNameWithoutExtension($Stage))-cleaned-$([guid]::NewGuid()).usda"

try {
  # --- 1. usd-optimize: stage cleanup (ADR-0015) ---
  # triangulateMeshes first: augusta_asset_cooking never triangulates itself
  # (tools/asset-cooking/include/augusta/asset_cooking.h - "that's
  # usd-optimize's job, ADR-0030") and rejects any non-triangular face with
  # CookError::kUnsupportedTopology, so a quad/n-gon export must be
  # triangulated before any op below touches its topology.
  # deduplicateHierarchies: finds duplicate placed-object hierarchies and
  # replaces them with instances ("dedups instanced geometry").
  # flattenHierarchy: finds and removes redundant Xforms ("flattens
  # redundant hierarchy"). removeSmallGeometry: identifies and removes
  # small/degenerate geometry ("removes degenerate geometry"). These three
  # are ADR-0015's own description of what usd-optimize does here, run in
  # this order since later operations (e.g. removeSmallGeometry) are
  # cheaper against an already-deduplicated/flattened stage.
  Write-Host "Running usd-optimize on $Stage..."
  & usdOptimize -i $Stage -o triangulateMeshes -o deduplicateHierarchies -o flattenHierarchy -o removeSmallGeometry -w $cleanedStage
  if ($LASTEXITCODE -ne 0) {
    throw "usd-optimize failed (exit $LASTEXITCODE) - stage not cleaned, cook aborted."
  }

  # --- 2. usd-validation-nvidia: validate the cleaned stage (ADR-0015) ---
  # nvidia_usd_validate exits non-zero on any issue (including warnings),
  # not just failures/errors - a clean stage means no output at all.
  Write-Host "Running usd-validation-nvidia on $cleanedStage..."
  & nvidia_usd_validate $cleanedStage
  if ($LASTEXITCODE -ne 0) {
    throw "usd-validation-nvidia found issues (exit $LASTEXITCODE) - cook aborted, no pack written."
  }

  # --- 3. augusta_asset_cooking: bake the cleaned/validated stage ---
  Write-Host "Cooking $cleanedStage into $OutputPack..."
  & $CookerPath $cleanedStage $OutputPack $SigningKey
  if ($LASTEXITCODE -ne 0) {
    throw "augusta_asset_cooking failed (exit $LASTEXITCODE)."
  }
} finally {
  Remove-Item -Path $cleanedStage -Force -ErrorAction SilentlyContinue
}

Write-Host "Pipeline complete: $OutputPack"
