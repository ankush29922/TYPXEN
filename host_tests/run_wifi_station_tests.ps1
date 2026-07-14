$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$compiler = Get-Command clang -ErrorAction SilentlyContinue
if (-not $compiler) {
  $compiler = Get-Command gcc -ErrorAction Stop
}
$output = Join-Path $PSScriptRoot 'typx_wifi_station_tests.exe'
& $compiler.Source `
  -std=c11 -Wall -Wextra -Werror `
  "-I$root/components/typx_wifi_station/include" `
  "$root/components/typx_wifi_station/src/typx_wifi_station.c" `
  "$PSScriptRoot/test_wifi_station.c" `
  -o $output
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $output
exit $LASTEXITCODE
