$ErrorActionPreference = 'Stop'
py -3 -m unittest host_tests.test_public_package host_tests.test_windows_setup host_tests.test_firmware_verifier
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
