$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$output = Join-Path $PSScriptRoot 'typx_execution_safety_tests.exe'
$includes = @('-Icomponents/typx_execution_safety/include')
$sources = @(
  'components/typx_execution_safety/src/typx_execution_safety.c',
  'host_tests/test_execution_safety.c'
)

Push-Location $root
try {
  & gcc -std=c11 -Wall -Wextra -Werror -pedantic @includes @sources -o $output
  if ($LASTEXITCODE -ne 0) {
    throw "Execution safety test compilation failed with exit code $LASTEXITCODE"
  }
  & $output
  if ($LASTEXITCODE -ne 0) {
    throw "Execution safety tests failed with exit code $LASTEXITCODE"
  }
} finally {
  Pop-Location
}
