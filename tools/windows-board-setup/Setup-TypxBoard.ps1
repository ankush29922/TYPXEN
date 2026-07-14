$ErrorActionPreference = 'Stop'
$script = Join-Path $PSScriptRoot 'typx_board_setup.py'
$py = Get-Command py -ErrorAction SilentlyContinue
if ($py) {
  & $py.Source -3 $script
  exit $LASTEXITCODE
}
$python = Get-Command python -ErrorAction SilentlyContinue
if ($python) {
  & $python.Source $script
  exit $LASTEXITCODE
}
Write-Host 'Python 3 is required. Install it from https://www.python.org/downloads/windows/ and run Setup-TypxBoard.cmd again.'
exit 1
