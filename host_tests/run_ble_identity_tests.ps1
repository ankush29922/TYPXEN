$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$compiler = Get-Command clang -ErrorAction SilentlyContinue
if (-not $compiler) { $compiler = Get-Command gcc -ErrorAction Stop }
$output = Join-Path $PSScriptRoot 'typx_ble_identity_tests.exe'
& $compiler.Source `
  -std=c11 -Wall -Wextra -Werror -pedantic `
  "-I$root/components/typx_ble_identity/include" `
  "$root/components/typx_ble_identity/src/typx_ble_identity.c" `
  "$PSScriptRoot/test_ble_identity.c" `
  -o $output
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $output
exit $LASTEXITCODE
