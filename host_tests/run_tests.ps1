$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$output = Join-Path $PSScriptRoot 'typx_host_tests.exe'
$includes = @(
  '-Icomponents/typx_protocol/include',
  '-Icomponents/typx_executor/include'
)
$sources = @(
  'components/typx_protocol/src/typx_protocol.c',
  'components/typx_protocol/src/typx_sha256_portable.c',
  'components/typx_executor/src/typx_executor.c',
  'components/typx_executor/src/typx_state_machine.c',
  'host_tests/test_main.c'
)

Push-Location $root
try {
  & gcc -std=c11 -Wall -Wextra -Werror -pedantic @includes @sources -o $output
  if ($LASTEXITCODE -ne 0) {
    throw "Host test compilation failed with exit code $LASTEXITCODE"
  }
  & $output
  if ($LASTEXITCODE -ne 0) {
    throw "Host tests failed with exit code $LASTEXITCODE"
  }
} finally {
  Pop-Location
}
