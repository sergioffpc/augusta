<#
Lints PowerShell scripts with PSScriptAnalyzer and checks, or with -Fix applies,
the formatting Invoke-Formatter gives them, both by PSScriptAnalyzerSettings.psd1
at the repository root. With no paths it takes every tracked *.ps1. The Makefile,
the git hooks and CI run it, so they agree. PSScriptAnalyzer is the module
scripts\bootstrap-windows.ps1 installs, or any already installed.
Usage: .\scripts\psscriptanalyzer.ps1 [-Fix] [path ...]
#>
param(
  [switch]$Fix,
  [Parameter(ValueFromRemainingArguments = $true)]
  [string[]]$Path
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$settings = Join-Path $root "PSScriptAnalyzerSettings.psd1"
# PSUseCorrectCasing reads CommandInfo.Parameters from the analyzer's parallel
# rule threads, which now and then throws a NullReferenceException (seen with
# PSScriptAnalyzer 1.25.0 on pwsh). Invoke-Formatter applies its rules one at a
# time and still fixes the casing, so the lint pass alone leaves it out.
$lintSettings = Import-PowerShellDataFile $settings
$lintSettings.ExcludeRules += "PSUseCorrectCasing"
if (-not $Path) {
  $Path = git -C $root ls-files -- "*.ps1" | ForEach-Object { Join-Path $root $_ }
}

if (-not (Get-Module -ListAvailable PSScriptAnalyzer)) {
  $bootstrapped = Join-Path $env:LOCALAPPDATA "Programs\PSScriptAnalyzer\PSScriptAnalyzer.psd1"
  if (-not ($env:LOCALAPPDATA -and (Test-Path $bootstrapped))) {
    throw "PSScriptAnalyzer not found - run scripts\bootstrap-windows.ps1 first."
  }
  Import-Module $bootstrapped
}

$failed = $false
$utf8 = New-Object System.Text.UTF8Encoding $false
foreach ($file in $Path) {
  $text = [System.IO.File]::ReadAllText($file)
  $formatted = Invoke-Formatter -ScriptDefinition $text -Settings $settings
  # -cne: a fix that only changes casing is a difference too.
  if ($formatted -cne $text) {
    if ($Fix) {
      [System.IO.File]::WriteAllText($file, $formatted, $utf8)
    } else {
      Write-Output "${file}: not formatted (run with -Fix)"
      $failed = $true
    }
  }
  $findings = Invoke-ScriptAnalyzer -Path $file -Settings $lintSettings
  foreach ($finding in $findings) {
    Write-Output "$($finding.ScriptName):$($finding.Line): $($finding.RuleName): $($finding.Message)"
    $failed = $true
  }
}
if ($failed) {
  exit 1
}
