#Requires -RunAsAdministrator
# Startup-folder / Run-key entries cannot auto-elevate, and retriever.exe's
# manifest requires administrator; a logon scheduled task with RunLevel
# Highest starts it elevated without a UAC prompt.
param(
    [switch]$Unregister
)

$ErrorActionPreference = "Stop"
$TaskName = "retriever"

if ($Unregister) {
    Unregister-ScheduledTask -TaskName $TaskName -Confirm:$false
    Write-Host "Removed startup task '$TaskName'."
    exit 0
}

$Exe = Join-Path $PSScriptRoot "retriever.exe"
if (-not (Test-Path $Exe)) {
    Write-Error "retriever.exe not found next to this script ($Exe)."
}

$Action = New-ScheduledTaskAction -Execute $Exe
$Trigger = New-ScheduledTaskTrigger -AtLogOn -User $env:USERNAME
# ExecutionTimeLimit 0 disables the 72h default that would kill the service.
$Settings = New-ScheduledTaskSettingsSet `
    -AllowStartIfOnBatteries `
    -DontStopIfGoingOnBatteries `
    -ExecutionTimeLimit ([TimeSpan]::Zero)

Register-ScheduledTask -TaskName $TaskName -Action $Action -Trigger $Trigger `
    -RunLevel Highest -Settings $Settings -Force | Out-Null
Write-Host "Registered '$TaskName' to start elevated at logon of $env:USERNAME."

Start-ScheduledTask -TaskName $TaskName
Write-Host "Service started. Remove with: .\register-startup.ps1 -Unregister"
