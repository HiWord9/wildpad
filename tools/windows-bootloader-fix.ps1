# Wildpad - one-time Windows fix for "code 10" when entering the CH552 bootloader
#
# The CH552's ROM bootloader answers unsupported USB requests with STALL, and because
# of a bug in its USB code the request that follows a STALL often fails too. Windows
# asks every new USB device once for a "Microsoft OS descriptor" (an unsupported
# request for this bootloader) right before starting its driver, so the driver start
# fails about half the time (Device Manager: code 10).
#
# Windows remembers the answer per device model under usbflags\<VID><PID><REV>\osvc.
# Storing "no OS descriptor" (0000) for the bootloader makes Windows skip the
# question, and with it the failure. Needs administrator rights; run it once:
#   powershell -ExecutionPolicy Bypass -File tools\windows-bootloader-fix.ps1

$key = 'HKLM:\SYSTEM\CurrentControlSet\Control\usbflags\434855E00250'   # 4348:55E0 rev 2.50

$admin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
         ).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $admin) {
    Start-Process powershell -Verb RunAs -ArgumentList "-ExecutionPolicy Bypass -File `"$PSCommandPath`""
    exit
}
New-Item -Path $key -Force | Out-Null
New-ItemProperty -Path $key -Name osvc -PropertyType Binary -Value ([byte[]](0, 0)) -Force | Out-Null
Write-Host "Done: Windows will no longer ask the CH552 bootloader for an OS descriptor."
