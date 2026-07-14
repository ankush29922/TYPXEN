$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$output = Join-Path $PSScriptRoot 'typx_hid_tests.exe'
$includes = @(
  '-Icomponents/typx_hid_runtime/include',
  '-Icomponents/typx_protocol/include',
  '-Icomponents/typx_executor/include'
)
$sources = @(
  'components/typx_hid_runtime/src/typx_hid_runtime.c',
  'components/typx_protocol/src/typx_protocol.c',
  'components/typx_protocol/src/typx_sha256_portable.c',
  'components/typx_executor/src/typx_executor.c',
  'host_tests/test_hid_runtime.c'
)

Push-Location $root
try {
  & gcc -std=c11 -Wall -Wextra -Werror -pedantic @includes @sources -o $output
  if ($LASTEXITCODE -ne 0) {
    throw "HID test compilation failed with exit code $LASTEXITCODE"
  }
  & $output
  if ($LASTEXITCODE -ne 0) {
    throw "HID tests failed with exit code $LASTEXITCODE"
  }
} finally {
  Pop-Location
}
