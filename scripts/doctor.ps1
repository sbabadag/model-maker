# Model-Maker: makine teshisi (Windows / yeni makine)
# Kullanim:  powershell -ExecutionPolicy Bypass -File .\scripts\doctor.ps1
# Bu betik HICBIR SEYI DEGISTIRMEZ: yalniz bakar ve ne kurulu oldugunu rapor eder.
# Ciktiyi oldugu gibi kopyalayip gonderin.

$ErrorActionPreference = 'SilentlyContinue'
$script:found = @{}

# Betik repo icindeyse repo kokune gec (teshis komutlari dogru yerde calissin)
if ($PSScriptRoot) {
    $repoRoot = Split-Path $PSScriptRoot -Parent
    if (Test-Path (Join-Path $repoRoot '.git')) { Set-Location $repoRoot }
}

function Line($label, $value) {
    Write-Host ("  {0,-24} {1}" -f ($label + ':'), $value)
}
function Ok($label, $value)   { Line $label $value; }
function Head($text) {
    Write-Host ""
    Write-Host "== $text ==" -ForegroundColor Cyan
}
function CmdPath($name) {
    $c = Get-Command $name -ErrorAction SilentlyContinue
    if ($c) { return $c.Source } else { return $null }
}

Head "1) Makine"
Line 'Bilgisayar'  $env:COMPUTERNAME
Line 'Kullanici'   $env:USERNAME
Line 'Windows'     (Get-CimInstance Win32_OperatingSystem | Select-Object -ExpandProperty Caption)
Line 'Mimari'      $env:PROCESSOR_ARCHITECTURE
Line 'PowerShell'  $PSVersionTable.PSVersion.ToString()
Line 'Bulundugum dizin' (Get-Location).Path

Head "2) Temel araclar (PATH'te)"
foreach ($t in 'git','cmake','ninja','g++','c++','python','py') {
    $p = CmdPath $t
    if ($p) { Ok $t $p } else { Ok $t 'YOK' }
}

Head "3) Qt"
$qtDirs = @()
foreach ($root in 'C:\Qt','C:\Qt6','D:\Qt') {
    if (Test-Path $root) {
        $qtDirs += Get-ChildItem $root -Directory |
                   ForEach-Object { Get-ChildItem $_.FullName -Directory } |
                   Where-Object { Test-Path (Join-Path $_.FullName 'bin\qmake.exe') }
    }
}
if ($qtDirs.Count -gt 0) {
    foreach ($d in $qtDirs) {
        Ok 'Qt' $d.FullName
        Line '  qmake'    (Join-Path $d.FullName 'bin\qmake.exe')
        $cfg = Join-Path $d.FullName 'lib\cmake\Qt6\Qt6Config.cmake'
        Line '  Qt6Config' $(if (Test-Path $cfg) { $cfg } else { 'YOK' })
        $qtRoot = Split-Path (Split-Path $d.FullName -Parent) -Parent   # C:\Qt\<surum>\mingw_64 -> C:\Qt
        $tools  = Join-Path $qtRoot 'Tools'
        $g = Get-ChildItem $tools -Recurse -Filter 'g++.exe' -ErrorAction SilentlyContinue | Select-Object -First 1
        Line '  Qt MinGW' $(if ($g) { $g.FullName } else { "YOK ($tools altinda)" })
    }
} else { Ok 'Qt' 'C:\Qt altinda qmake.exe BULUNAMADI' }

Head "4) MSYS2 / UCRT64 (OpenCASCADE icin)"
$msys = 'C:\msys64'
if (Test-Path $msys) {
    Ok 'MSYS2' $msys
    foreach ($exe in 'ucrt64\bin\g++.exe','ucrt64\bin\cmake.exe','ucrt64\bin\ninja.exe','ucrt64\bin\qmake6.exe') {
        $full = Join-Path $msys $exe
        Line "  $exe" $(if (Test-Path $full) { 'var' } else { 'YOK' })
    }
    $g = Join-Path $msys 'ucrt64\bin\g++.exe'
    if (Test-Path $g) { Line '  g++ surumu' (& $g --version 2>$null | Select-Object -First 1) }

    $occCfg = Join-Path $msys 'ucrt64\lib\cmake\opencascade\OpenCASCADEConfig.cmake'
    Line '  OpenCASCADE' $(if (Test-Path $occCfg) { 'VAR' } else { 'YOK' })
    $tk = Get-ChildItem (Join-Path $msys 'ucrt64\bin') -Filter 'TKernel*.dll' -ErrorAction SilentlyContinue | Select-Object -First 1
    Line '  TKernel DLL' $(if ($tk) { $tk.Name } else { 'YOK' })
    $qt6 = Join-Path $msys 'ucrt64\bin\Qt6Core.dll'
    Line '  MSYS2 Qt6Core' $(if (Test-Path $qt6) { 'VAR' } else { 'YOK' })
} else { Ok 'MSYS2' 'C:\msys64 YOK' }

Head "5) WinGet MinGW (Qt'siz sade derleme icin)"
$wg = Join-Path $env:LOCALAPPDATA 'Microsoft\WinGet\Packages'
$gpp = Get-ChildItem $wg -Recurse -Filter 'g++.exe' -ErrorAction SilentlyContinue | Select-Object -First 1
if ($gpp) {
    Ok 'WinGet MinGW' $gpp.FullName
    Line '  surum' (& $gpp.FullName --version 2>$null | Select-Object -First 1)
} else { Ok 'WinGet MinGW' 'YOK' }

Head "6) Repo"
$here = (Get-Location).Path
if (Test-Path (Join-Path $here '.git')) {
    Ok 'Repo' $here
    Line '  dal'        (git rev-parse --abbrev-ref HEAD 2>$null)
    Line '  HEAD'       (git rev-parse --short HEAD 2>$null)
    Line '  origin'     (git remote get-url origin 2>$null)
    $dirty = git status --porcelain 2>$null
    Line '  degisiklik' $(if ($dirty) { "VAR ($(@($dirty).Count) dosya) - once temizle" } else { 'temiz' })
    git fetch origin 2>$null | Out-Null
    if ($LASTEXITCODE -eq 0) {
        Line '  origin/OCC' (git rev-parse --short origin/OCC 2>$null)
        Line '  origin/main' (git rev-parse --short origin/main 2>$null)
    }
} else { Ok 'Repo' "bu dizinde .git YOK ($here)" }

Head "7) Oneri"
$hasMsysOcc = (Test-Path (Join-Path $msys 'ucrt64\lib\cmake\opencascade\OpenCASCADEConfig.cmake')) -and (Test-Path (Join-Path $msys 'ucrt64\bin\g++.exe'))
$hasMsysQt  = Test-Path (Join-Path $msys 'ucrt64\bin\Qt6Core.dll')
$hasQt      = $qtDirs.Count -gt 0
$hasGpp     = ($null -ne (CmdPath 'g++')) -or ($null -ne $gpp)

if ($hasMsysOcc -and ($hasMsysQt -or $hasQt)) {
    Write-Host "  TAM OZELLIK (OpenCASCADE): MSYS2 UCRT64 hazir. UCRT64 kabugunda:" -ForegroundColor Green
    Write-Host "    cmake -S <repo> -B <repo>/build-occ -G Ninja -DCMAKE_BUILD_TYPE=Release ``"
    Write-Host "      -DQT_ROOT=C:/msys64/ucrt64 -DOCC_ROOT=C:/msys64/ucrt64 -DMM_OCC_LINK_DIRECT=ON"
    Write-Host "    cmake --build <repo>/build-occ --target model-maker"
} elseif ($hasQt -and $hasGpp) {
    Write-Host "  OCC YOK -> SADE DERLEME yapilabilir (wireframe CAD + DXF calisir; kati modelleme kapali):" -ForegroundColor Yellow
    Write-Host "    `$env:PATH='<Qt bin>;'+`$env:PATH"
    Write-Host "    cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release"
    Write-Host "    cmake --build build-release --target model-maker"
    Write-Host "  Tam ozellik icin MSYS2 UCRT64: pacman -S --needed mingw-w64-ucrt-x86_64-{gcc,cmake,ninja,qt6-base,qt6-tools,opencascade}"
} else {
    Write-Host "  EKSIK PARCALAR:" -ForegroundColor Red
    if (-not $hasGpp)     { Write-Host "    - C++ derleyici YOK  -> MSYS2 UCRT64 kurup pacman -S mingw-w64-ucrt-x86_64-gcc" }
    if (-not $hasQt -and -not $hasMsysQt) { Write-Host "    - Qt6 YOK            -> Qt Online Installer (Qt 6.x, MinGW) veya MSYS2 mingw-w64-ucrt-x86_64-qt6-base" }
    if ($hasQt -and -not $hasGpp) { Write-Host "    - Qt var ama derleyici bulunamadi (Qt'in kendi MinGW'i kurulu degil olabilir)" }
    if (-not $hasMsysOcc) { Write-Host "    - OpenCASCADE YOK    -> (istege bagli) MSYS2 mingw-w64-ucrt-x86_64-opencascade" }
    Write-Host "    Derleyici + Qt tamamsa sade derleme yeterli: cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release"
}

Write-Host ""
Write-Host "TESHIS TAMAM. Yukaridaki ciktiyi oldugu gibi kopyalayip gonderin." -ForegroundColor Cyan
