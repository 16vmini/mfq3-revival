# Rebuild the MFQ3 Carrier Sea level end-to-end.
#   - regenerates the .map (tools/gen_carrier_map.py)
#   - compiles BSP (bakes the CVN-65 misc_model via -fs_pakpath) + lightmaps
#   - skips VIS (open sea: pointless + pathologically slow)
#   - repacks play/mfdata/zz_mfq3_carrier.pk3  (only the .bsp ships; model is baked in)
# Load in-game with:  \devmap mfq3_carrier
$ErrorActionPreference = 'Stop'
$tools = 'C:\source\mfq3\tools'
$q3    = "$tools\netradiant\q3map2.exe"
$map   = "$tools\mapsrc\mfq3_carrier.map"
$base  = 'C:\source\mfq3\play'
$pak   = "$tools\carrier_pak"        # holds models/mapobjects/cvn65/cvn65.obj

Set-Location $tools
python gen_carrier_map.py | Out-Host

Write-Host "`n===== BSP (bake model) =====" -ForegroundColor Cyan
& $q3 -fs_basepath $base -fs_game mfdata -fs_pakpath $pak -meta $map 2>&1 |
  Select-String -Pattern "leaked|Couldn't find image|degener|Wrote .* MB" | Select-Object -Last 8

Write-Host "`n===== LIGHT (no vis, coarse grid) =====" -ForegroundColor Cyan
& $q3 -fs_basepath $base -fs_game mfdata -fs_pakpath $pak -light -fast -patchshadows $map 2>&1 |
  Select-String -Pattern "lightmaps|Wrote .* MB" | Select-Object -Last 4

# repack pk3 (bsp + arena + credits) with forward-slash entries via python
python "$tools\pack_carrier.py" | Out-Host
Write-Host "`nDone. Load in-game with:  \devmap mfq3_carrier" -ForegroundColor Green
