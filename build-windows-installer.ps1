Param (
    [switch]$installer
)

$BuildSpec = Get-Content -Path ./buildspec.json -Raw | ConvertFrom-Json
$ProductName = $BuildSpec.name
$ProductVersion = $BuildSpec.version

$OutputName = "${ProductName}-${ProductVersion}-windows-x64"

$Env:API_SERVER = "https://replaceYourApiServer"
$Env:API_WS_SERVER = "wss://replaceYourApiServer"
$Env:CLIENT_ID = "replaceYourClientId"
$env:CLIENT_SECRET = "replaceYourClientSecret"

cmake --fresh -S . -B build_x64 -Wdev -Wdeprecated -DCMAKE_SYSTEM_VERSION="10.0.18363.657" -G "Visual Studio 17 2022" -A x64
cmake --build build_x64 --config RelWithDebInfo --target ALL_BUILD --
cmake --install build_x64 --prefix release/Package --config RelWithDebInfo

if ($installer) {
    iscc build_x64/installer-Windows.generated.iss /O"release" /F"${OutputName}-Installer-signed"
}
