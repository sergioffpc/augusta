param(
  [ValidateRange(1, 10)]
  [int]$MaxAttempts = 3,

  [ValidateRange(1, 300)]
  [int]$InitialDelaySeconds = 15
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$vcpkgRoot = Join-Path $repoRoot "third_party\vcpkg"
$bootstrap = Join-Path $vcpkgRoot "bootstrap-vcpkg.bat"
$vcpkgExecutable = Join-Path $vcpkgRoot "vcpkg.exe"

for ($attempt = 1; $attempt -le $MaxAttempts; $attempt++) {
  try {
    & $bootstrap -disableMetrics
    if ($LASTEXITCODE -eq 0 -and (Test-Path $vcpkgExecutable)) {
      return
    }

    $failure = "bootstrap-vcpkg exited with code $LASTEXITCODE"
  }
  catch {
    $failure = $_.Exception.Message
  }

  if ($attempt -eq $MaxAttempts) {
    throw "vcpkg bootstrap failed after $MaxAttempts attempts: $failure"
  }

  $delaySeconds = $InitialDelaySeconds * $attempt
  Write-Warning "vcpkg bootstrap attempt $attempt of $MaxAttempts failed: $failure Retrying in $delaySeconds seconds."
  Start-Sleep -Seconds $delaySeconds
}
