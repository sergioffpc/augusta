<#
Runs the given command inside the Visual Studio Build Tools x64 developer
environment (cl.exe's INCLUDE/LIB/PATH), so the Ninja generator works from a
plain terminal. Usage: .\scripts\vcenv.ps1 cmake --build --preset windows
The Makefile uses this on Windows; it's also handy on its own.
#>
param(
  [Parameter(Mandatory = $true, ValueFromRemainingArguments = $true)]
  [string[]]$Command
)

$ErrorActionPreference = "Stop"

$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
$vsRoot = if (Test-Path $vswhere) { & $vswhere -latest -products * -property installationPath }
if (-not $vsRoot) {
  throw "Visual Studio Build Tools not found - run scripts\bootstrap-windows.ps1 first."
}

# vcvars64.bat only changes the environment of the cmd.exe it runs in, so run
# it there and copy the resulting variables into this process.
$vcvars = Join-Path $vsRoot "VC\Auxiliary\Build\vcvars64.bat"
cmd /c "`"$vcvars`" >nul && set" | ForEach-Object {
  if ($_ -match '^([^=]+)=(.*)$') {
    Set-Item -Path "env:$($Matches[1])" -Value $Matches[2]
  }
}
if ($LASTEXITCODE -ne 0) {
  exit $LASTEXITCODE
}

$rest = @($Command | Select-Object -Skip 1)
& $Command[0] @rest
exit $LASTEXITCODE
