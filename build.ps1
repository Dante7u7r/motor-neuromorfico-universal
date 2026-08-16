param (
    [switch]$NoServer
)

$ErrorActionPreference = "Stop"

Write-Host "======================================================================" -ForegroundColor Cyan
Write-Host "  COMPILACION MOTOR NEUROMORFICO UNIVERSAL (C++17 / MSVC)" -ForegroundColor Cyan
Write-Host "======================================================================" -ForegroundColor Cyan

# 1. Localizar vcvarsall.bat
$common_paths = @(
    "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat",
    "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat",
    "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat",
    "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat",
    "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvarsall.bat"
)

$vcvarsall = $null
foreach ($path in $common_paths) {
    if (Test-Path $path) {
        $vcvarsall = $path
        break
    }
}

if ($null -eq $vcvarsall) {
    Write-Host "[*] Buscando vcvarsall.bat en el disco..." -ForegroundColor Yellow
    $search = Get-ChildItem -Path "C:\Program Files (x86)\Microsoft Visual Studio", "C:\Program Files\Microsoft Visual Studio" -Recurse -Filter "vcvarsall.bat" -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($search) {
        $vcvarsall = $search.FullName
    }
}

if ($null -eq $vcvarsall) {
    Write-Host "[ERROR] No se pudo encontrar vcvarsall.bat de MSVC." -ForegroundColor Red
    exit 1
}

Write-Host "[+] Compilador localizado en: $vcvarsall" -ForegroundColor Green

# 2. Configurar directivas
$defines = ""
if ($NoServer) {
    Write-Host "[!] Compilando en modo embebido sin servidor (-NoServer)..." -ForegroundColor Yellow
    $defines = "/DNO_SERVER"
} else {
    Write-Host "[+] Compilando con servidor HTTP activo..." -ForegroundColor Green
}

# 3. Compilacion
$source_files = "main.cpp cerebro.cpp server.cpp synthetic_signal_adapter.cpp"
$compiler_cmd = "cl /EHsc /O2 /std:c++17 $source_files $defines /link ws2_32.lib /out:motor_neuromorfico.exe"

Write-Host "[*] Compilando con MSVC..." -ForegroundColor Cyan
$cmd_args = "call `"$vcvarsall`" x64 && $compiler_cmd"

cmd.exe /c $cmd_args

if ($LASTEXITCODE -eq 0) {
    Write-Host "======================================================================" -ForegroundColor Green
    Write-Host "  COMPILACION EXITOSA! Ejecutable: motor_neuromorfico.exe" -ForegroundColor Green
    Write-Host "======================================================================" -ForegroundColor Green
} else {
    Write-Host "[ERROR] Fallo la compilacion." -ForegroundColor Red
    exit 1
}
