$ErrorActionPreference = "Continue"
$projectDir = "C:\Users\tian7\Desktop\BossJy\JyTrainer_Win11"
$msbuild = "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"

Set-Location $projectDir
Write-Host "Building JyTrainer..."

& $msbuild JyTrainer.vcxproj /p:Configuration=Release /p:Platform=x64 /v:minimal 2>&1 | ForEach-Object { Write-Host $_ }

if ($LASTEXITCODE -eq 0) {
    Write-Host "Build SUCCESS!"
} else {
    Write-Host "Build FAILED with exit code: $LASTEXITCODE"
}
