function Pause
{
    Write-Host
    Write-Host "Press any key to exit..." -ForegroundColor Cyan
    $null = $Host.UI.RawUI.ReadKey("NoEcho,IncludeKeyDown")
}

# Elevation Check
if (-not ([Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]"Administrator"))
{
    Write-Host "Script not running as administrator. Requesting elevation..." -ForegroundColor Yellow
    Start-Process powershell "-ExecutionPolicy Bypass -File `"$PSCommandPath`"" -Verb RunAs
    exit
}

# Get current script directory
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition

# Required files in script directory
$requiredFiles = @("vmtheme32.dll", "vmtheme64.dll", "setdll32.exe", "setdll64.exe")
foreach ($file in $requiredFiles)
{
    $filePath = Join-Path $scriptDir $file
    if (-not (Test-Path $filePath))
    {
        Write-Host "Required file '$file' not found in script directory. Exiting..." -ForegroundColor Red
        Pause
        exit 1
    }
}

# Ask for Voicemeeter version
Write-Host "Which Voicemeeter version do you want to patch?" -ForegroundColor Cyan
Write-Host
Write-Host "1. Default" -ForegroundColor White
Write-Host "2. Banana" -ForegroundColor White
Write-Host "3. Potato" -ForegroundColor White
Write-Host
$selection = Read-Host "Enter a number (1-3)"

switch ($selection)
{
    '1' {
        $exeNames = @("voicemeeter.exe", "voicemeeter_x64.exe")
    }
    '2' {
        $exeNames = @("voicemeeterpro.exe", "voicemeeterpro_x64.exe")
    }
    '3' {
        $exeNames = @("voicemeeter8.exe", "voicemeeter8x64.exe")
    }
    default {
        Write-Host "Invalid selection. Please enter 1, 2, or 3." -ForegroundColor Red
        Pause
        exit 1
    }
}

# Check if both Voicemeeter executable files exist
$voicemeeterPath = "C:\Program Files (x86)\VB\Voicemeeter"
foreach ($exe in $exeNames)
{
    $exePath = Join-Path $voicemeeterPath $exe
    if (-not (Test-Path $exePath))
    {
        Write-Host "Executable '$exe' not found in $voicemeeterPath, Exiting" -ForegroundColor Red
        Pause
        exit 1
    }
}

# Copy executables with _vmtheme suffix (i.e voicemeeter8x64_vmtheme.exe)
foreach ($exe in $exeNames)
{
    $exePath = Join-Path $voicemeeterPath $exe
    $modExePath = Join-Path $voicemeeterPath ($exe -replace '\.exe$', '_vmtheme.exe')

    Write-Host
    Write-Host "Duplicate executable for patching: $destPath" -ForegroundColor Cyan
    Copy-Item -Path $exePath -Destination $modExePath -Force

    if (-not (Test-Path $modExePath))
    {
        Write-Host "Failed to create copy of $exe. Exiting..." -ForegroundColor Red
        Pause
        exit 1
    }

    Write-Host "Created duplicate: $modExePath" -ForegroundColor Green
}

# Run setdll.exe for both 64-bit and 32-bit
$cmd32 = "& `"$scriptDir\setdll32.exe`" vmtheme32.dll `"$voicemeeterPath\$($exeNames[0])`" `"$voicemeeterPath\$( $exeNames[0] -replace '\.exe$', '_vmtheme.exe' )`""
$cmd64 = "& `"$scriptDir\setdll64.exe`" vmtheme64.dll `"$voicemeeterPath\$($exeNames[1])`" `"$voicemeeterPath\$( $exeNames[1] -replace '\.exe$', '_vmtheme.exe' )`""

Write-Host
Write-Host "Patching 32bit target: $cmd32" -ForegroundColor Yellow
Invoke-Expression $cmd32
if ($LASTEXITCODE -ne 0)
{
    Write-Host "setdll32.exe failed for $dll with exit code $LASTEXITCODE. Exiting..." -ForegroundColor Red
    Pause
    exit $LASTEXITCODE
}
Write-Host "Successfully patched: $( $exeNames[0] -replace '\.exe$', '_vmtheme.exe' )" -ForegroundColor Green

Write-Host
Write-Host "Patching 64bit target: $cmd64" -ForegroundColor Yellow
Invoke-Expression $cmd64
if ($LASTEXITCODE -ne 0)
{
    Write-Host "setdll64.exe failed for $dll with exit code $LASTEXITCODE. Exiting..." -ForegroundColor Red
    Pause
    exit $LASTEXITCODE
}
Write-Host "Successfully patched: $( $exeNames[1] -replace '\.exe$', '_vmtheme.exe' )" -ForegroundColor Green

# Copy DLLs to Voicemeeter folder
foreach ($dll in @("vmtheme32.dll", "vmtheme64.dll"))
{
    Write-Host
    Write-Host "Copy $dll to Voicemeeter folder..." -ForegroundColor Cyan
    $source = Join-Path $scriptDir $dll
    $dest = Join-Path $voicemeeterPath $dll
    try
    {
        Copy-Item -Path $source -Destination $dest -Force
    }
    catch
    {
        Write-Host "Failed to copy $dll to $voicemeeterPath. Error: $($_.Exception.Message)" -ForegroundColor Red
        Pause
        exit 1
    }

    if (-not (Test-Path $dest))
    {
        Write-Host "Failed to copy $dll to Voicemeeter folder. Exiting..." -ForegroundColor Red
        Pause
        exit 1
    }
    Write-Host "Copied $dll to $voicemeeterPath" -ForegroundColor Green
}

Write-Host
Write-Host "Voicemeeter patching complete!" -ForegroundColor Green
Pause
exit 0
