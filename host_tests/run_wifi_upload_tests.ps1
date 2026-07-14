$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$output = Join-Path $PSScriptRoot 'typx_wifi_upload_tests.exe'
$includes = @(
  '-Icomponents/typx_wifi_upload/include',
  '-Icomponents/typx_protocol/include',
  '-Icomponents/typx_executor/include'
)
$sources = @(
  'components/typx_wifi_upload/src/typx_wifi_upload.c',
  'components/typx_protocol/src/typx_protocol.c',
  'components/typx_protocol/src/typx_sha256_portable.c',
  'components/typx_executor/src/typx_executor.c',
  'host_tests/test_wifi_upload.c'
)

Push-Location $root
try {
  & gcc -std=c11 -Wall -Wextra -Werror -pedantic @includes @sources -o $output
  if ($LASTEXITCODE -ne 0) {
    throw "Wi-Fi upload test compilation failed with exit code $LASTEXITCODE"
  }
  & $output
  if ($LASTEXITCODE -ne 0) {
    throw "Wi-Fi upload tests failed with exit code $LASTEXITCODE"
  }
} finally {
  Pop-Location
}
