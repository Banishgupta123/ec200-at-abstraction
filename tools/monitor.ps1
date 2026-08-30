# Colored live monitor through the serial hub (COM14 stays owned by the hub;
# any number of these can run in parallel).  Ctrl+] to quit.
Remove-Item Env:MSYSTEM -ErrorAction SilentlyContinue
. C:\Users\banis\esp\v6.0.2\esp-idf\export.ps1
Set-Location "$PSScriptRoot\..\examples\esp_idf"
idf.py monitor -p socket://127.0.0.1:2323
