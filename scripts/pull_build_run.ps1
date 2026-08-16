# Model-Maker Windows: pull + derle + test + calistir (tek script)
# Calistirma: PowerShell'de -> .\scripts\pull_build_run.ps1
Set-Location $PSScriptRoot\..

# --- MinGW toolchain + Qt DLL'lerini PATH'e ekle (varsa) ---
$mingw = 'C:\Qt\Tools\mingw1310_64\bin'
if (Test-Path $mingw) { $env:PATH = "$mingw;$env:PATH" }
# Qt DLL'leri: exe'nin CALISMASI icin sart (Qt6Widgets.dll, qwindows.dll vb.)
$qtbin = 'C:\Qt\6.9.3\mingw_64\bin'
if (Test-Path $qtbin) { $env:PATH = "$qtbin;$env:PATH" }

Write-Host '== 1) Fetch + hermes-nightly ==' -ForegroundColor Cyan
git fetch origin
if ($LASTEXITCODE -ne 0) { Write-Host 'HATA: git fetch basarisiz' -ForegroundColor Red; exit 1 }
git switch hermes-nightly 2>$null
git pull --ff-only origin hermes-nightly
if ($LASTEXITCODE -ne 0) {
    Write-Warning 'ff-only pull basarisiz: yerelde push edilmemis commit olabilir.
    Once: git push origin hermes-nightly (veya main) -> sonra bu scripti tekrar calistir.'
    exit 1
}

Write-Host '== 2) Derleme (Ninja, Release) ==' -ForegroundColor Cyan
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE -ne 0) {
    Write-Host 'HATA: cmake configure basarisiz. Qt yolu farkliysa: -DQT_ROOT=C:\yol\qt' -ForegroundColor Red
    exit 1
}
cmake --build build-release
if ($LASTEXITCODE -ne 0) { Write-Host 'HATA: derleme basarisiz' -ForegroundColor Red; exit 1 }

Write-Host '== 3) Testler (ctest) ==' -ForegroundColor Cyan
ctest --test-dir build-release --output-on-failure
if ($LASTEXITCODE -ne 0) { Write-Warning 'Bazi testler basarisiz (yukarida gorulur)' }

Write-Host '== 4) Benchmarklar ==' -ForegroundColor Cyan
& .\build-release\model_maker_render_prep_benchmark.exe
& .\build-release\model_maker_spatial_query_benchmark.exe

Write-Host '== 5) Uygulama baslatiliyor ==' -ForegroundColor Cyan
# Eski acik kopyalari kapat — aksi halde birden fazla pencere ayni anda
# calisir ve test eski (duzeltmeleri icermeyen) pencerede yapilir.
taskkill /IM model-maker.exe /F 2>$null | Out-Null
Start-Sleep -Milliseconds 300
Start-Process .\build-release\model-maker.exe
Write-Host 'TAMAM: model-maker.exe baslatildi.' -ForegroundColor Green
