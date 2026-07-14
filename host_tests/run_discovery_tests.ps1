$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$compiler = Get-Command clang -ErrorAction SilentlyContinue
if (-not $compiler) { $compiler = Get-Command gcc -ErrorAction Stop }
$output = Join-Path $PSScriptRoot 'typx_discovery_tests.exe'
& $compiler.Source `
  -std=c11 -Wall -Wextra -Werror `
  "-I$root/components/typx_discovery/include" `
  "$root/components/typx_discovery/src/typx_discovery.c" `
  "$PSScriptRoot/test_discovery.c" `
  -o $output
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $output
exit $LASTEXITCODE
