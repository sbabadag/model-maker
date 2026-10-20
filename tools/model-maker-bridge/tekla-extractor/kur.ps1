<#
    Model Maker Tekla Bridge Extractor — derleme betigi (Windows / PowerShell)

    Kullanim:
        .\kur.ps1                                      # Tekla 2025.0 varsayilan, derle + publish
        .\kur.ps1 -TeklaVersion 2024.0                 # baska surum
        .\kur.ps1 -NoPublish                           # sadece derle

    Not: Kisitlama nedeniyle calismazsa:
        powershell -ExecutionPolicy Bypass -File .\kur.ps1
#>

param(
    [string]$TeklaVersion = "2025.0",
    [string]$Configuration = "Release",
    [switch]$NoPublish
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Definition
Set-Location $root

# kurulu Tekla surumunu otomatik bul
if (-not $PSBoundParameters.ContainsKey('TeklaVersion')) {
    $found = @()
    foreach ($base in @("C:\Program Files\Tekla Structures", "C:\Program Files (x86)\Tekla Structures",
                        "C:\TeklaStructures", "D:\TeklaStructures")) {
        if (Test-Path $base) {
            $found += Get-ChildItem -Path $base -Directory -ErrorAction SilentlyContinue |
                      Where-Object { $_.Name -match '^\d{4}\.\d$' } |
                      Select-Object -ExpandProperty Name
        }
    }
    if ($found.Count -gt 0) {
        $TeklaVersion = ($found | Sort-Object -Descending | Select-Object -First 1)
        Write-Host "Kurulu Tekla surumu bulundu: $TeklaVersion" -ForegroundColor Cyan
    }
}

# dotnet bul
$dotnet = "dotnet"
if (-not (Get-Command $dotnet -ErrorAction SilentlyContinue)) {
    $candidate = Join-Path $env:USERPROFILE ".dotnet\dotnet.exe"
    if (Test-Path $candidate) { $dotnet = $candidate }
    else {
        Write-Host "HATA: dotnet bulunamadi. .NET SDK 8 kurulu olmali." -ForegroundColor Red
        exit 1
    }
}

Write-Host "=== Tekla Bridge Extractor derleme ===" -ForegroundColor Cyan
Write-Host "Tekla surumu  : $TeklaVersion"

# temiz
Get-ChildItem -Path $root -Recurse -Directory -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -eq 'bin' -or $_.Name -eq 'obj' } |
    Remove-Item -Recurse -Force -ErrorAction SilentlyContinue
$dist = Join-Path $root "dist"
if (Test-Path $dist) { Remove-Item $dist -Recurse -Force }

# derle
& $dotnet build (Join-Path $root "ModelMaker.TeklaBridge.sln") -c $Configuration -p:TeklaVersion=$TeklaVersion --no-incremental --nologo
if ($LASTEXITCODE -ne 0) { Write-Host "DERLEME BASARISIZ." -ForegroundColor Red; exit 1 }

if ($NoPublish) {
    Write-Host "Publish atlandi (-NoPublish)." -ForegroundColor Yellow
    exit 0
}

# publish
& $dotnet publish (Join-Path $root "src\ModelMaker.TeklaBridge.Extractor\ModelMaker.TeklaBridge.Extractor.csproj") -c $Configuration -o $dist -p:TeklaVersion=$TeklaVersion --nologo
if ($LASTEXITCODE -ne 0) { Write-Host "PUBLISH BASARISIZ." -ForegroundColor Red; exit 1 }

Write-Host ""
Write-Host "TAMAM. Cikti: $dist\TeklaBridge.Extractor.exe" -ForegroundColor Green
Write-Host ""
Write-Host "Kullanim:"
Write-Host "  1. Tekla Structures'i acin ve modeli yukleyin."
Write-Host "  2. Modele bir baglanti/component yerlestirip SECIN."
Write-Host "  3. Tekla acikken calistirin:"
Write-Host "     $dist\TeklaBridge.Extractor.exe --output C:\dizin\paket --attributes e1,e2,t,boltSize"
Write-Host "  4. Olusan paketi Model Maker'da File > Import > Tekla Component Bridge ile acin."
