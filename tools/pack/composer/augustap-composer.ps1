<#
Launches the Augusta USD Composer app (ADR-0015), built by
tools\pack\scripts\bootstrap-windows.ps1 (skipped with -SkipAuthoring). A thin
wrapper around kit-app-template's own repo.bat launch, run with its working
directory set to <assets-root>\tools\kit-app-template (repo.bat's own scripts
assume that), located relative to this script's own path ($PSScriptRoot) so it
works wherever the assets root lives - copied alongside augustap.exe et al.
into <assets-root>\bin by the bootstrap. Arguments are forwarded as-is, e.g.
augustap-composer.ps1 --name augusta.kit if repo.bat launch asks which app
when more than one is registered.

repo.bat is called by its full path, not a bare name after changing into its
directory: a machine with NoDefaultCurrentDirectoryInExePath set (Win32 then
never searches the working directory for an unqualified command) would
otherwise report "'repo.bat' is not recognized" despite having just changed
into the directory that holds it.
#>

param(
  [Parameter(ValueFromRemainingArguments = $true)]
  [string[]]$Arguments
)

$ErrorActionPreference = "Stop"

$kitAppTemplateDir = Join-Path $PSScriptRoot "..\tools\kit-app-template"
$repoBat = Join-Path $kitAppTemplateDir "repo.bat"
if (-not (Test-Path $repoBat)) {
  throw "$repoBat not found - run bootstrap-windows.ps1 without -SkipAuthoring first."
}

Push-Location $kitAppTemplateDir
try {
  & $repoBat launch @Arguments
  $code = $LASTEXITCODE
} finally {
  Pop-Location
}
exit $code
