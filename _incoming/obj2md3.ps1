# OBJ -> Quake3 MD3 converter (single frame, optional tags, N surfaces).
# Triangulates polygons, Y-up(OBJ)->Z-up(Q3) + 180deg yaw, centres + scales,
# encodes per-vertex normals, splits into <=MaxVerts surfaces, one shader.
#
# Part extraction: pass parallel arrays -PartName/-PartGroups/-PartTag/-PartOut to
# pull named OBJ groups out into separate part md3s (centred on their own pivot)
# and stamp a tag at that pivot on the BODY md3, so the engine can attach + animate
# them (e.g. spinning prop via tag_prop1). Body = all groups NOT claimed by a part.
param(
  [string]$ObjPath,
  [string]$OutMd3,
  [string]$ShaderName,
  [single]$TargetSize = 140,
  [int]$MaxVerts = 8000,
  [single]$Yaw = 180,            # yaw (deg about vertical) after Y-up->Z-up; 180 fits the Reaper OBJ, 0 for these glTF-sourced models
  [single]$Pitch = 0,            # pitch (deg about left-right Y) after yaw; nulls a baked-in nose up/down tilt
  [single]$Roll  = 0,            # roll (deg about forward X) after pitch; nulls a baked-in wing tilt (use analyze_obj.py)
  [bool]$FlipV = $true,          # flip texture V (1-v): true for OBJ (bottom-left origin), false for glTF-sourced (top-left)
  [switch]$PerMaterial,          # emit one MD3 surface per OBJ material (usemtl), each shader = <MtlDir>/<material>
  [string]$MtlDir = '',          # shader dir prefix for per-material shaders; default = directory part of ShaderName
  [string[]]$PartName   = @(),   # e.g. prop
  [string[]]$PartGroups = @(),   # e.g. "Propeller,Blades"  (comma list per part)
  [string[]]$PartTag    = @(),   # e.g. tag_prop1
  [string[]]$PartOut    = @(),   # e.g. ...\reaper_prop.md3
  # animated landing gear: legs ';'-separated, groups within a leg ','-separated.
  # Each leg folds about its own hinge (auto-estimated at the strut top) across
  # GearFrames frames: frame 0 = retracted (folded by GearFoldDeg), last = down.
  [string]$GearLegs    = '',     # e.g. "StrutL,WheelL;StrutR,WheelR;WheelN,Strut1N,Strut2N,Scissor1N,Scissor2N"
  [string]$GearOut     = '',     # e.g. ...\reaper_gear.md3
  [string]$GearTag     = 'tag_gear',
  [int]$GearFrames     = 48,     # matches GEAR_DOWN_DEFAULT(47)+1
  [single]$GearFoldDeg = -85,    # default sweep (about Y) if -GearFold not given; +=aft, -=fwd
  [string]$GearFold    = '',     # per-leg sweep deg, ';'-sep (e.g. "90;90;-45"); blank = GearFoldDeg for all
  [string]$GearRoll    = '',      # per-leg roll-about-strut deg, ';'-sep (e.g. "90;-90;0"); blank = 0
  [string]$GearHinge   = '',     # per-leg explicit hinge "x,y,z" in body-local SCALED coords (blank leg = auto)
  # animated VTOL part (doors/nozzle) - SAME frame-rotation system as gear, output as a
  # second md3 attached at its own tag. Each "leg" is a door/nozzle group(s) that rotates.
  [string]$VtolLegs    = '',     # legs ';'-sep, groups within a leg ','-sep
  [string]$VtolOut     = '',     # e.g. ...\f35_special.md3
  [string]$VtolTag     = 'tag_special',
  [string]$VtolFold    = '',     # per-leg sweep deg (about Y)
  [string]$VtolRoll    = '',      # per-leg roll deg (about Z)
  [string]$VtolHinge   = ''      # per-leg explicit hinge override "x,y,z" in body-local SCALED coords (blank leg = auto)
)
$ErrorActionPreference = 'Stop'
$ci = [System.Globalization.CultureInfo]::InvariantCulture
$yawRad = $Yaw * [Math]::PI / 180.0
$yawC = [Math]::Cos($yawRad); $yawS = [Math]::Sin($yawRad)
$pitchRad = $Pitch * [Math]::PI / 180.0
$pC = [Math]::Cos($pitchRad); $pS = [Math]::Sin($pitchRad)
$rollRad = $Roll * [Math]::PI / 180.0
$rC = [Math]::Cos($rollRad); $rS = [Math]::Sin($rollRad)

$px=[System.Collections.Generic.List[double]]::new(); $py=[System.Collections.Generic.List[double]]::new(); $pz=[System.Collections.Generic.List[double]]::new()
$tu=[System.Collections.Generic.List[double]]::new(); $tv=[System.Collections.Generic.List[double]]::new()
$nx=[System.Collections.Generic.List[double]]::new(); $ny=[System.Collections.Generic.List[double]]::new(); $nz=[System.Collections.Generic.List[double]]::new()
# global verts (Q3 space, unscaled)
$gx=[System.Collections.Generic.List[double]]::new(); $gy=[System.Collections.Generic.List[double]]::new(); $gz=[System.Collections.Generic.List[double]]::new()
$gu=[System.Collections.Generic.List[double]]::new(); $gv=[System.Collections.Generic.List[double]]::new()
$gnx=[System.Collections.Generic.List[double]]::new(); $gny=[System.Collections.Generic.List[double]]::new(); $gnz=[System.Collections.Generic.List[double]]::new()
$tri=[System.Collections.Generic.List[int]]::new()       # flat global vert ids (3 per tri)
$triGrp=[System.Collections.Generic.List[string]]::new() # group name per triangle
$triMtl=[System.Collections.Generic.List[string]]::new() # material (usemtl) per triangle
$vmap=[System.Collections.Generic.Dictionary[string,int]]::new()

function Get-Vert([string]$key) {
  $idx = 0
  if ($vmap.TryGetValue($key, [ref]$idx)) { return $idx }
  $parts = $key.Split('/')
  $vi = [int]$parts[0]
  $ti = if ($parts.Length -ge 2 -and $parts[1] -ne '') { [int]$parts[1] } else { 0 }
  $ni = if ($parts.Length -ge 3 -and $parts[2] -ne '') { [int]$parts[2] } else { 0 }
  if ($vi -lt 0) { $vi = $px.Count + 1 + $vi }
  $ox=$px[$vi-1]; $oy=$py[$vi-1]; $oz=$pz[$vi-1]
  # Y-up -> Z-up base, then yaw(Z) -> pitch(Y) -> roll(X) (matches analyze_obj.py)
  $bx=$ox; $by=-$oz; $bz=$oy
  $yx=$bx*$yawC - $by*$yawS; $yy=$bx*$yawS + $by*$yawC; $yz=$bz
  $qx=$yx*$pC + $yz*$pS; $qy=$yy; $qz=-$yx*$pS + $yz*$pC
  $gx.Add($qx); $gy.Add($qy*$rC + $qz*$rS); $gz.Add(-$qy*$rS + $qz*$rC)
  if ($ti -gt 0) { $gu.Add($tu[$ti-1]); $gv.Add( $(if($FlipV){ 1.0 - $tv[$ti-1] } else { $tv[$ti-1] }) ) } else { $gu.Add(0.0); $gv.Add(0.0) }
  if ($ni -gt 0) { $bnx=$nx[$ni-1]; $bny=-$nz[$ni-1]; $bnz=$ny[$ni-1]; $ynx=$bnx*$yawC-$bny*$yawS; $yny=$bnx*$yawS+$bny*$yawC; $ynz=$bnz; $qnx=$ynx*$pC+$ynz*$pS; $qny=$yny; $qnz=-$ynx*$pS+$ynz*$pC; $gnx.Add($qnx); $gny.Add($qny*$rC+$qnz*$rS); $gnz.Add(-$qny*$rS+$qnz*$rC) } else { $gnx.Add(0.0); $gny.Add(0.0); $gnz.Add(1.0) }
  $id = $gx.Count - 1
  $vmap[$key] = $id
  return $id
}

$curGroup = ''
$curMtl = 'default'
$sr = [System.IO.StreamReader]::new($ObjPath)
while ($null -ne ($line = $sr.ReadLine())) {
  if ($line.Length -lt 2) { continue }
  $c0 = $line[0]
  if ($c0 -eq 'g' -and ($line[1] -eq ' ' -or $line.Length -eq 1)) {
    $t = $line.Split([char[]]@(' ',"`t"), [System.StringSplitOptions]::RemoveEmptyEntries)
    $curGroup = if ($t.Length -ge 2) { $t[1] } else { '' }
  }
  elseif ($c0 -eq 'o' -and $line[1] -eq ' ') {
    $t = $line.Split([char[]]@(' ',"`t"), [System.StringSplitOptions]::RemoveEmptyEntries)
    $curGroup = if ($t.Length -ge 2) { $t[1] } else { '' }
  }
  elseif ($c0 -eq 'u' -and $line.StartsWith('usemtl')) {
    $t = $line.Split([char[]]@(' ',"`t"), [System.StringSplitOptions]::RemoveEmptyEntries)
    $curMtl = if ($t.Length -ge 2) { $t[1] } else { 'default' }
  }
  elseif ($c0 -eq 'v') {
    $t = $line.Split([char[]]@(' ',"`t"), [System.StringSplitOptions]::RemoveEmptyEntries)
    if ($t[0] -eq 'v')  { $px.Add([double]::Parse($t[1],$ci)); $py.Add([double]::Parse($t[2],$ci)); $pz.Add([double]::Parse($t[3],$ci)) }
    elseif ($t[0] -eq 'vt') { $tu.Add([double]::Parse($t[1],$ci)); $tv.Add([double]::Parse($t[2],$ci)) }
    elseif ($t[0] -eq 'vn') { $nx.Add([double]::Parse($t[1],$ci)); $ny.Add([double]::Parse($t[2],$ci)); $nz.Add([double]::Parse($t[3],$ci)) }
  }
  elseif ($c0 -eq 'f') {
    $t = $line.Split([char[]]@(' ',"`t"), [System.StringSplitOptions]::RemoveEmptyEntries)
    $n = $t.Length - 1
    if ($n -lt 3) { continue }
    $vids = New-Object int[] $n
    for ($i=0; $i -lt $n; $i++) { $vids[$i] = Get-Vert $t[$i+1] }
    for ($i=1; $i -lt ($n-1); $i++) { $tri.Add($vids[0]); $tri.Add($vids[$i+1]); $tri.Add($vids[$i]); $triGrp.Add($curGroup); $triMtl.Add($curMtl) }  # fan, reversed winding
  }
}
$sr.Close()

$nv = $gx.Count; $nt = $triGrp.Count
Write-Output "parsed: $nv verts, $nt tris, groups tracked"

# global bounds + scale (whole model, so all parts share one scale + body frame)
$minx=[double]::MaxValue;$miny=[double]::MaxValue;$minz=[double]::MaxValue;$maxx=[double]::MinValue;$maxy=[double]::MinValue;$maxz=[double]::MinValue
for ($i=0;$i -lt $nv;$i++){ if($gx[$i]-lt$minx){$minx=$gx[$i]};if($gx[$i]-gt$maxx){$maxx=$gx[$i]};if($gy[$i]-lt$miny){$miny=$gy[$i]};if($gy[$i]-gt$maxy){$maxy=$gy[$i]};if($gz[$i]-lt$minz){$minz=$gz[$i]};if($gz[$i]-gt$maxz){$maxz=$gz[$i]} }
$cx=($minx+$maxx)/2;$cy=($miny+$maxy)/2;$cz=($minz+$maxz)/2
$ext=[Math]::Max([Math]::Max($maxx-$minx,$maxy-$miny),$maxz-$minz)
$scale = if ($ext -gt 0) { $TargetSize/$ext } else { 1.0 }
Write-Output ("bbox ext={0:N1} -> scale={1:N4}" -f $ext,$scale)

function Enc-Normal([double]$x,[double]$y,[double]$z){
  $len=[Math]::Sqrt($x*$x+$y*$y+$z*$z); if($len -lt 1e-6){return 0}
  $x/=$len;$y/=$len;$z/=$len
  $lng=[Math]::Acos($z) * 255.0/(2*[Math]::PI)
  $lat=[Math]::Atan2($y,$x) * 255.0/(2*[Math]::PI)
  $blat=[int][Math]::Round($lat) -band 255; $blng=[int][Math]::Round($lng) -band 255
  return ([int](($blat -shl 8) -bor $blng))
}

# pivot = centroid of a part's UNIQUE verts (unscaled Q3 space). Centroid (not bbox
# centre) lands on a prop's true hub: blades are rotationally symmetric so they
# cancel, whereas the bbox is lopsided for 2/3-blade props -> spin wobble.
function Get-Pivot($triList){
  $seen=[System.Collections.Generic.HashSet[int]]::new()
  $sx=0.0;$sy=0.0;$sz=0.0;$n=0
  foreach($gi in $triList){
    $g=[int]$gi
    if($seen.Add($g)){ $sx+=[double]$gx[$g];$sy+=[double]$gy[$g];$sz+=[double]$gz[$g];$n++ }
  }
  if($n -eq 0){ return ,([double[]]@(0.0,0.0,0.0)) }
  return ,([double[]]@(($sx/$n),($sy/$n),($sz/$n)))
}

# Write one md3. $triList = flat global vert ids; centre (px,py,pz) subtracted then *scale.
# $tags = array of @{name;ox;oy;oz} already in this md3's local (scaled) coords.
function Write-Md3([string]$outPath,[int[]]$triList,[double]$pcx,[double]$pcy,[double]$pcz,[double]$scl,$tags,$triShaders=$null){
  $ms=[System.IO.MemoryStream]::new(); $bw=[System.IO.BinaryWriter]::new($ms)
  function WStr([string]$s,[int]$len){ $bytes=New-Object byte[] $len; $sb=[System.Text.Encoding]::ASCII.GetBytes($s); [Array]::Copy($sb,$bytes,[Math]::Min($sb.Length,$len)); $bw.Write($bytes) }

  # frame bounds from this part's verts (scaled, centred)
  $mnx=[double]::MaxValue;$mny=[double]::MaxValue;$mnz=[double]::MaxValue;$mxx=[double]::MinValue;$mxy=[double]::MinValue;$mxz=[double]::MinValue
  foreach($g in $triList){ $X=($gx[$g]-$pcx)*$scl;$Y=($gy[$g]-$pcy)*$scl;$Z=($gz[$g]-$pcz)*$scl; if($X-lt$mnx){$mnx=$X};if($X-gt$mxx){$mxx=$X};if($Y-lt$mny){$mny=$Y};if($Y-gt$mxy){$mxy=$Y};if($Z-lt$mnz){$mnz=$Z};if($Z-gt$mxz){$mxz=$Z} }
  $rad=[Math]::Sqrt([Math]::Max($mxx*$mxx,$mnx*$mnx)+[Math]::Max($mxy*$mxy,$mny*$mny)+[Math]::Max($mxz*$mxz,$mnz*$mnz))

  # surfaces (grouped by shader; each shader split so <= MaxVerts unique verts)
  $ntri=[int]($triList.Count/3)
  $shArr=New-Object string[] $ntri
  for($t=0;$t -lt $ntri;$t++){ if($triShaders){ $shArr[$t]=[string]$triShaders[$t] } else { $shArr[$t]=$ShaderName } }
  $shOrder=[System.Collections.Generic.List[string]]::new(); $shSeen=@{}
  for($t=0;$t -lt $ntri;$t++){ if(-not $shSeen.ContainsKey($shArr[$t])){ $shSeen[$shArr[$t]]=$true; $shOrder.Add($shArr[$t]) } }
  $surfs=[System.Collections.Generic.List[object]]::new()
  foreach($sh in $shOrder){
    $curMap=[System.Collections.Generic.Dictionary[int,int]]::new(); $curVerts=[System.Collections.Generic.List[int]]::new(); $curTris=[System.Collections.Generic.List[int]]::new()
    for($t=0;$t -lt $ntri;$t++){
      if($shArr[$t] -ne $sh){ continue }
      $a=$triList[$t*3];$b=$triList[$t*3+1];$c=$triList[$t*3+2]
      $need=0; foreach($g in @($a,$b,$c)){ if(-not $curMap.ContainsKey($g)){$need++} }
      if ($curVerts.Count + $need -gt $MaxVerts -and $curVerts.Count -gt 0){
        $surfs.Add([pscustomobject]@{verts=$curVerts; tris=$curTris; shader=$sh}); $curMap=[System.Collections.Generic.Dictionary[int,int]]::new(); $curVerts=[System.Collections.Generic.List[int]]::new(); $curTris=[System.Collections.Generic.List[int]]::new()
      }
      foreach($g in @($a,$b,$c)){ $li=0; if(-not $curMap.TryGetValue($g,[ref]$li)){ $li=$curVerts.Count; $curMap[$g]=$li; $curVerts.Add($g) }; $curTris.Add($li) }
    }
    if($curVerts.Count -gt 0){ $surfs.Add([pscustomobject]@{verts=$curVerts; tris=$curTris; shader=$sh}) }
  }
  $numSurf=$surfs.Count
  $numTags=$tags.Count

  # header
  WStr 'IDP3' 4; $bw.Write([int]15); WStr 'reaper' 64; $bw.Write([int]0)
  $bw.Write([int]1); $bw.Write([int]$numTags); $bw.Write([int]$numSurf); $bw.Write([int]0)
  $ofsFramesPos=$ms.Position; $bw.Write([int]0); $bw.Write([int]0); $bw.Write([int]0); $bw.Write([int]0)
  # frame
  $ofsFrames=$ms.Position
  $bw.Write([single]$mnx);$bw.Write([single]$mny);$bw.Write([single]$mnz)
  $bw.Write([single]$mxx);$bw.Write([single]$mxy);$bw.Write([single]$mxz)
  $bw.Write([single]0);$bw.Write([single]0);$bw.Write([single]0)
  $bw.Write([single]$rad); WStr 'f0' 16
  # tags
  $ofsTags=$ms.Position
  foreach($tg in $tags){
    WStr $tg.name 64
    $bw.Write([single]$tg.ox);$bw.Write([single]$tg.oy);$bw.Write([single]$tg.oz)
    $bw.Write([single]1);$bw.Write([single]0);$bw.Write([single]0)
    $bw.Write([single]0);$bw.Write([single]1);$bw.Write([single]0)
    $bw.Write([single]0);$bw.Write([single]0);$bw.Write([single]1)
  }
  # surfaces
  $ofsSurfaces=$ms.Position
  foreach($s in $surfs){
    $sv=$s.verts.Count; $st=$s.tris.Count/3
    WStr 'IDP3' 4; WStr 'reaper' 64; $bw.Write([int]0)
    $bw.Write([int]1); $bw.Write([int]1); $bw.Write([int]$sv); $bw.Write([int]$st)
    $hdrSize=108
    $ofsTriangles=$hdrSize
    $ofsShaders=$ofsTriangles + $st*12
    $ofsSt=$ofsShaders + 1*68
    $ofsXyz=$ofsSt + $sv*8
    $ofsEnd=$ofsXyz + $sv*8
    $bw.Write([int]$ofsTriangles);$bw.Write([int]$ofsShaders);$bw.Write([int]$ofsSt);$bw.Write([int]$ofsXyz);$bw.Write([int]$ofsEnd)
    for($i=0;$i -lt $s.tris.Count;$i++){ $bw.Write([int]$s.tris[$i]) }
    WStr $s.shader 64; $bw.Write([int]0)
    foreach($g in $s.verts){ $bw.Write([single]$gu[$g]); $bw.Write([single]$gv[$g]) }
    foreach($g in $s.verts){
      $x=[int][Math]::Round((($gx[$g]-$pcx)*$scl)*64); $y=[int][Math]::Round((($gy[$g]-$pcy)*$scl)*64); $z=[int][Math]::Round((($gz[$g]-$pcz)*$scl)*64)
      foreach($val in @($x,$y,$z)){ $vv=$val; if($vv -gt 32767){$vv=32767}; if($vv -lt -32768){$vv=-32768}; $bw.Write([int16]$vv) }
      $bw.Write([uint16](Enc-Normal $gnx[$g] $gny[$g] $gnz[$g]))
    }
  }
  $ofsEof=$ms.Position
  $bw.Flush(); $arr=$ms.ToArray()
  [Array]::Copy([BitConverter]::GetBytes([int]$ofsFrames),0,$arr,$ofsFramesPos,4)
  [Array]::Copy([BitConverter]::GetBytes([int]$ofsTags),0,$arr,$ofsFramesPos+4,4)
  [Array]::Copy([BitConverter]::GetBytes([int]$ofsSurfaces),0,$arr,$ofsFramesPos+8,4)
  [Array]::Copy([BitConverter]::GetBytes([int]$ofsEof),0,$arr,$ofsFramesPos+12,4)
  [System.IO.File]::WriteAllBytes($outPath,$arr)
  Write-Output ("WROTE $outPath  ({0:N0} bytes, {1} surfaces, {2} tris, {3} tags)" -f $arr.Length,$numSurf,($triList.Count/3),$numTags)
}

# Estimate a leg's hinge: XY = centroid of the top 25% (by Z) of its verts, Z = top.
# The strut top (where it meets the body) is the real fold pivot, not the centroid.
function Get-Hinge($legTris){
  $seen=[System.Collections.Generic.HashSet[int]]::new()
  $mnz=[double]::MaxValue;$mxz=[double]::MinValue
  foreach($gi in $legTris){ $g=[int]$gi; if($seen.Add($g)){ $z=[double]$gz[$g]; if($z-lt$mnz){$mnz=$z}; if($z-gt$mxz){$mxz=$z} } }
  $thr=$mxz - 0.25*($mxz-$mnz)
  $sx=0.0;$sy=0.0;$n=0; $seen2=[System.Collections.Generic.HashSet[int]]::new()
  foreach($gi in $legTris){ $g=[int]$gi; if($seen2.Add($g)){ if([double]$gz[$g] -ge $thr){ $sx+=[double]$gx[$g]; $sy+=[double]$gy[$g]; $n++ } } }
  if($n -eq 0){ $n=1 }
  return ,([double[]]@(($sx/$n),($sy/$n),$mxz))
}

# Write a multi-frame gear md3. Each leg folds about its own hinge with TWO rotations:
# a roll about the strut axis (Z) THEN a sweep about the left-right axis (Y), both
# running full at frame 0 (up) -> 0 at the last frame (down). legFold/legRoll = per-leg deg.
function Write-GearMd3([string]$outPath,[int[]]$gearTris,$vLeg,$legHinge,$legFold,$legRoll,[int]$numFrames,[double]$pcx,[double]$pcy,[double]$pcz,[double]$scl,$triShaders=$null){
  $ms=[System.IO.MemoryStream]::new(); $bw=[System.IO.BinaryWriter]::new($ms)
  function WStr([string]$s,[int]$len){ $bytes=New-Object byte[] $len; $sb=[System.Text.Encoding]::ASCII.GetBytes($s); [Array]::Copy($sb,$bytes,[Math]::Min($sb.Length,$len)); $bw.Write($bytes) }
  $maxF=[Math]::Max(1,$numFrames-1); $d2r=[Math]::PI/180.0
  $nLeg=$legFold.Count
  # per-leg per-frame cos/sin: sweep (about Y) + roll (about Z)
  $swc=@();$sws=@();$rlc=@();$rls=@()
  for($l=0;$l -lt $nLeg;$l++){
    $cf=New-Object double[] $numFrames;$sf=New-Object double[] $numFrames;$cr=New-Object double[] $numFrames;$srr=New-Object double[] $numFrames
    for($f=0;$f -lt $numFrames;$f++){
      $tt=($maxF-$f)/$maxF; $sa=[double]$legFold[$l]*$tt*$d2r; $ra=[double]$legRoll[$l]*$tt*$d2r
      $cf[$f]=[Math]::Cos($sa);$sf[$f]=[Math]::Sin($sa);$cr[$f]=[Math]::Cos($ra);$srr[$f]=[Math]::Sin($ra)
    }
    $swc+=,$cf;$sws+=,$sf;$rlc+=,$cr;$rls+=,$srr
  }

  # surfaces (grouped by shader; each shader split so <= MaxVerts unique verts)
  $ntri=[int]($gearTris.Count/3)
  $shArr=New-Object string[] $ntri
  for($t=0;$t -lt $ntri;$t++){ if($triShaders){ $shArr[$t]=[string]$triShaders[$t] } else { $shArr[$t]=$ShaderName } }
  $shOrder=[System.Collections.Generic.List[string]]::new(); $shSeen=@{}
  for($t=0;$t -lt $ntri;$t++){ if(-not $shSeen.ContainsKey($shArr[$t])){ $shSeen[$shArr[$t]]=$true; $shOrder.Add($shArr[$t]) } }
  $surfs=[System.Collections.Generic.List[object]]::new()
  foreach($sh in $shOrder){
    $curMap=[System.Collections.Generic.Dictionary[int,int]]::new(); $curVerts=[System.Collections.Generic.List[int]]::new(); $curTris=[System.Collections.Generic.List[int]]::new()
    for($t=0;$t -lt $ntri;$t++){
      if($shArr[$t] -ne $sh){ continue }
      $a=$gearTris[$t*3];$b=$gearTris[$t*3+1];$c=$gearTris[$t*3+2]
      $need=0; foreach($g in @($a,$b,$c)){ if(-not $curMap.ContainsKey($g)){$need++} }
      if ($curVerts.Count + $need -gt $MaxVerts -and $curVerts.Count -gt 0){ $surfs.Add([pscustomobject]@{verts=$curVerts; tris=$curTris; shader=$sh}); $curMap=[System.Collections.Generic.Dictionary[int,int]]::new(); $curVerts=[System.Collections.Generic.List[int]]::new(); $curTris=[System.Collections.Generic.List[int]]::new() }
      foreach($g in @($a,$b,$c)){ $li=0; if(-not $curMap.TryGetValue($g,[ref]$li)){ $li=$curVerts.Count; $curMap[$g]=$li; $curVerts.Add($g) }; $curTris.Add($li) }
    }
    if($curVerts.Count -gt 0){ $surfs.Add([pscustomobject]@{verts=$curVerts; tris=$curTris; shader=$sh}) }
  }
  $numSurf=$surfs.Count

  # per-frame bounds (over all unique verts)
  $fbMinX=New-Object double[] $numFrames;$fbMinY=New-Object double[] $numFrames;$fbMinZ=New-Object double[] $numFrames
  $fbMaxX=New-Object double[] $numFrames;$fbMaxY=New-Object double[] $numFrames;$fbMaxZ=New-Object double[] $numFrames
  for($f=0;$f -lt $numFrames;$f++){ $fbMinX[$f]=[double]::MaxValue;$fbMinY[$f]=[double]::MaxValue;$fbMinZ[$f]=[double]::MaxValue;$fbMaxX[$f]=[double]::MinValue;$fbMaxY[$f]=[double]::MinValue;$fbMaxZ[$f]=[double]::MinValue }
  $allVerts=[System.Collections.Generic.HashSet[int]]::new(); foreach($s in $surfs){ foreach($g in $s.verts){ [void]$allVerts.Add($g) } }
  foreach($g in $allVerts){
    $leg=0; if($vLeg.ContainsKey($g)){ $leg=$vLeg[$g] }; $h=$legHinge[$leg]
    $px=[double]$gx[$g]-$h[0]; $py=[double]$gy[$g]-$h[1]; $pz=[double]$gz[$g]-$h[2]
    for($f=0;$f -lt $numFrames;$f++){
      $cr=$rlc[$leg][$f];$sr=$rls[$leg][$f];$cs=$swc[$leg][$f];$ss=$sws[$leg][$f]
      $x1=$px*$cr-$py*$sr; $y1=$px*$sr+$py*$cr; $z1=$pz
      $X=(($h[0]+$x1*$cs+$z1*$ss)-$pcx)*$scl; $Z=(($h[2]-$x1*$ss+$z1*$cs)-$pcz)*$scl; $Y=(($h[1]+$y1)-$pcy)*$scl
      if($X-lt$fbMinX[$f]){$fbMinX[$f]=$X}; if($X-gt$fbMaxX[$f]){$fbMaxX[$f]=$X}
      if($Y-lt$fbMinY[$f]){$fbMinY[$f]=$Y}; if($Y-gt$fbMaxY[$f]){$fbMaxY[$f]=$Y}
      if($Z-lt$fbMinZ[$f]){$fbMinZ[$f]=$Z}; if($Z-gt$fbMaxZ[$f]){$fbMaxZ[$f]=$Z}
    }
  }

  WStr 'IDP3' 4; $bw.Write([int]15); WStr 'reaper' 64; $bw.Write([int]0)
  $bw.Write([int]$numFrames); $bw.Write([int]0); $bw.Write([int]$numSurf); $bw.Write([int]0)
  $ofsFramesPos=$ms.Position; $bw.Write([int]0); $bw.Write([int]0); $bw.Write([int]0); $bw.Write([int]0)
  $ofsFrames=$ms.Position
  for($f=0;$f -lt $numFrames;$f++){
    $bw.Write([single]$fbMinX[$f]);$bw.Write([single]$fbMinY[$f]);$bw.Write([single]$fbMinZ[$f])
    $bw.Write([single]$fbMaxX[$f]);$bw.Write([single]$fbMaxY[$f]);$bw.Write([single]$fbMaxZ[$f])
    $bw.Write([single]0);$bw.Write([single]0);$bw.Write([single]0)
    $rad=[Math]::Sqrt([Math]::Max($fbMaxX[$f]*$fbMaxX[$f],$fbMinX[$f]*$fbMinX[$f])+[Math]::Max($fbMaxY[$f]*$fbMaxY[$f],$fbMinY[$f]*$fbMinY[$f])+[Math]::Max($fbMaxZ[$f]*$fbMaxZ[$f],$fbMinZ[$f]*$fbMinZ[$f]))
    $bw.Write([single]$rad); WStr ("f"+$f) 16
  }
  $ofsTags=$ms.Position
  $ofsSurfaces=$ms.Position
  foreach($s in $surfs){
    $sv=$s.verts.Count; $st=$s.tris.Count/3
    WStr 'IDP3' 4; WStr 'reaper' 64; $bw.Write([int]0)
    $bw.Write([int]$numFrames); $bw.Write([int]1); $bw.Write([int]$sv); $bw.Write([int]$st)
    $ofsTriangles=108; $ofsShaders=$ofsTriangles+$st*12; $ofsSt=$ofsShaders+68; $ofsXyz=$ofsSt+$sv*8; $ofsEnd=$ofsXyz+$numFrames*$sv*8
    $bw.Write([int]$ofsTriangles);$bw.Write([int]$ofsShaders);$bw.Write([int]$ofsSt);$bw.Write([int]$ofsXyz);$bw.Write([int]$ofsEnd)
    for($i=0;$i -lt $s.tris.Count;$i++){ $bw.Write([int]$s.tris[$i]) }
    WStr $s.shader 64; $bw.Write([int]0)
    foreach($g in $s.verts){ $bw.Write([single]$gu[$g]); $bw.Write([single]$gv[$g]) }
    for($f=0;$f -lt $numFrames;$f++){
      foreach($g in $s.verts){
        $leg=0; if($vLeg.ContainsKey($g)){ $leg=$vLeg[$g] }; $h=$legHinge[$leg]
        $cr=$rlc[$leg][$f];$sr=$rls[$leg][$f];$cs=$swc[$leg][$f];$ss=$sws[$leg][$f]
        $px=[double]$gx[$g]-$h[0]; $py=[double]$gy[$g]-$h[1]; $pz=[double]$gz[$g]-$h[2]
        $x1=$px*$cr-$py*$sr; $y1=$px*$sr+$py*$cr; $z1=$pz
        $X=(($h[0]+$x1*$cs+$z1*$ss)-$pcx)*$scl; $Z=(($h[2]-$x1*$ss+$z1*$cs)-$pcz)*$scl; $Y=(($h[1]+$y1)-$pcy)*$scl
        $ix=[int][Math]::Round($X*64);$iy=[int][Math]::Round($Y*64);$iz=[int][Math]::Round($Z*64)
        foreach($val in @($ix,$iy,$iz)){ $vv=$val; if($vv-gt32767){$vv=32767}; if($vv-lt-32768){$vv=-32768}; $bw.Write([int16]$vv) }
        $nx=[double]$gnx[$g];$ny=[double]$gny[$g];$nz=[double]$gnz[$g]
        $nx1=$nx*$cr-$ny*$sr;$ny1=$nx*$sr+$ny*$cr;$nz1=$nz
        $bw.Write([uint16](Enc-Normal ($nx1*$cs+$nz1*$ss) $ny1 (-$nx1*$ss+$nz1*$cs)))
      }
    }
  }
  $ofsEof=$ms.Position
  $bw.Flush(); $arr=$ms.ToArray()
  [Array]::Copy([BitConverter]::GetBytes([int]$ofsFrames),0,$arr,$ofsFramesPos,4)
  [Array]::Copy([BitConverter]::GetBytes([int]$ofsTags),0,$arr,$ofsFramesPos+4,4)
  [Array]::Copy([BitConverter]::GetBytes([int]$ofsSurfaces),0,$arr,$ofsFramesPos+8,4)
  [Array]::Copy([BitConverter]::GetBytes([int]$ofsEof),0,$arr,$ofsFramesPos+12,4)
  [System.IO.File]::WriteAllBytes($outPath,$arr)
  Write-Output ("WROTE $outPath  ({0:N0} bytes, {1} surfaces, {2} frames, {3} tris)" -f $arr.Length,$numSurf,$numFrames,($gearTris.Count/3))
}

# ---- claim groups: parts (prop) + gear legs are pulled out of the body ----
$claimed=[System.Collections.Generic.Dictionary[string,int]]::new([System.StringComparer]::OrdinalIgnoreCase)
for($p=0;$p -lt $PartName.Count;$p++){
  foreach($g in $PartGroups[$p].Split(',')){ $gg=$g.Trim(); if($gg -ne ''){ $claimed[$gg]=$p } }
}
$gearLegGroups=@()
$gearClaimed=[System.Collections.Generic.Dictionary[string,int]]::new([System.StringComparer]::OrdinalIgnoreCase)
if($GearLegs.Trim() -ne ''){
  $legStrs=$GearLegs.Split(';')
  for($l=0;$l -lt $legStrs.Count;$l++){
    $grps=@(); foreach($g in $legStrs[$l].Split(',')){ $gg=$g.Trim(); if($gg -ne ''){ $grps+=$gg; $gearClaimed[$gg]=$l } }
    $gearLegGroups += ,$grps
  }
}
$numLegs=$gearLegGroups.Count

# vtol animated part (doors/nozzle) - same machinery as gear
$vtolLegGroups=@()
$vtolClaimed=[System.Collections.Generic.Dictionary[string,int]]::new([System.StringComparer]::OrdinalIgnoreCase)
if($VtolLegs.Trim() -ne ''){
  $vlegStrs=$VtolLegs.Split(';')
  for($l=0;$l -lt $vlegStrs.Count;$l++){
    $grps=@(); foreach($g in $vlegStrs[$l].Split(',')){ $gg=$g.Trim(); if($gg -ne ''){ $grps+=$gg; $vtolClaimed[$gg]=$l } }
    $vtolLegGroups += ,$grps
  }
}
$numVtol=$vtolLegGroups.Count

# ---- partition triangles into body / parts / gear-legs / vtol-legs ----
$bodyTris=[System.Collections.Generic.List[int]]::new()
$bodyTriMtl=[System.Collections.Generic.List[string]]::new()
$partTris=@(); for($p=0;$p -lt $PartName.Count;$p++){ $partTris += ,([System.Collections.Generic.List[int]]::new()) }
$gearTris=[System.Collections.Generic.List[int]]::new()
$legTris=@(); for($l=0;$l -lt $numLegs;$l++){ $legTris += ,([System.Collections.Generic.List[int]]::new()) }
$vLeg=[System.Collections.Generic.Dictionary[int,int]]::new()
$vtolTris=[System.Collections.Generic.List[int]]::new()
$vtolLegTris=@(); for($l=0;$l -lt $numVtol;$l++){ $vtolLegTris += ,([System.Collections.Generic.List[int]]::new()) }
$vVtol=[System.Collections.Generic.Dictionary[int,int]]::new()
$gearTriMtl=[System.Collections.Generic.List[string]]::new()   # material per gear triangle
$vtolTriMtl=[System.Collections.Generic.List[string]]::new()   # material per vtol triangle
$partTriMtl=@(); for($p=0;$p -lt $PartName.Count;$p++){ $partTriMtl += ,([System.Collections.Generic.List[string]]::new()) }
for($f=0;$f -lt $triGrp.Count;$f++){
  $g=$triGrp[$f]
  $a=$tri[$f*3];$b=$tri[$f*3+1];$c=$tri[$f*3+2]
  if($g -ne '' -and $vtolClaimed.ContainsKey($g)){
    $l=$vtolClaimed[$g]
    $vtolTris.Add($a);$vtolTris.Add($b);$vtolTris.Add($c)
    $vtolLegTris[$l].Add($a);$vtolLegTris[$l].Add($b);$vtolLegTris[$l].Add($c)
    $vVtol[$a]=$l;$vVtol[$b]=$l;$vVtol[$c]=$l
    $vtolTriMtl.Add($triMtl[$f])
  } elseif($g -ne '' -and $gearClaimed.ContainsKey($g)){
    $l=$gearClaimed[$g]
    $gearTris.Add($a);$gearTris.Add($b);$gearTris.Add($c)
    $legTris[$l].Add($a);$legTris[$l].Add($b);$legTris[$l].Add($c)
    $vLeg[$a]=$l;$vLeg[$b]=$l;$vLeg[$c]=$l
    $gearTriMtl.Add($triMtl[$f])
  } elseif($g -ne '' -and $claimed.ContainsKey($g)){
    $pi=$claimed[$g]; $partTris[$pi].Add($a);$partTris[$pi].Add($b);$partTris[$pi].Add($c)
    $partTriMtl[$pi].Add($triMtl[$f])
  } else { $bodyTris.Add($a);$bodyTris.Add($b);$bodyTris.Add($c); $bodyTriMtl.Add($triMtl[$f]) }
}

# resolve the per-material shader dir once (used by body + parts when -PerMaterial)
$resolvedMtlDir=$MtlDir
if($resolvedMtlDir -eq ''){ $li=$ShaderName.LastIndexOf('/'); if($li -ge 0){ $resolvedMtlDir=$ShaderName.Substring(0,$li) } }
function MtlShaders($mtlList){
  if(-not $PerMaterial){ return $null }
  $arr=New-Object string[] $mtlList.Count
  for($i=0;$i -lt $mtlList.Count;$i++){ $arr[$i]="$resolvedMtlDir/$($mtlList[$i])" }
  return ,$arr
}

# ---- parts (e.g. prop): own pivot + tag on body ----
$bodyTags=@()
for($p=0;$p -lt $PartName.Count;$p++){
  if($partTris[$p].Count -eq 0){ Write-Output "WARN: part '$($PartName[$p])' matched 0 tris (groups: $($PartGroups[$p]))"; continue }
  $piv=Get-Pivot $partTris[$p]
  $bodyTags += ,([pscustomobject]@{ name=$PartTag[$p]; ox=(($piv[0]-$cx)*$scale); oy=(($piv[1]-$cy)*$scale); oz=(($piv[2]-$cz)*$scale) })
  Write-Output ("part '{0}': {1} tris, pivot tag '{2}' at ({3:N1},{4:N1},{5:N1})" -f $PartName[$p],($partTris[$p].Count/3),$PartTag[$p],(($piv[0]-$cx)*$scale),(($piv[1]-$cy)*$scale),(($piv[2]-$cz)*$scale))
  Write-Md3 $PartOut[$p] $partTris[$p] $piv[0] $piv[1] $piv[2] $scale @() (MtlShaders $partTriMtl[$p])
}

# ---- gear: multi-frame fold animation, attached at tag_gear (body origin) ----
if($numLegs -gt 0 -and $gearTris.Count -gt 0 -and $GearOut -ne ''){
  $legHinge=@()
  $ghingeArr=$GearHinge.Split(';')
  for($l=0;$l -lt $numLegs;$l++){
    if($legTris[$l].Count -eq 0){ Write-Output "WARN: gear leg $l matched 0 tris"; $legHinge += ,([double[]]@($cx,$cy,$cz)); continue }
    if($l -lt $ghingeArr.Count -and $ghingeArr[$l].Trim() -ne ''){
      $hc=$ghingeArr[$l].Split(','); $hx=[double]$hc[0]/$scale+$cx; $hy=[double]$hc[1]/$scale+$cy; $hz=[double]$hc[2]/$scale+$cz
      $h=([double[]]@($hx,$hy,$hz)); $legHinge += ,$h
      Write-Output ("gear leg {0}: {1} tris, EXPLICIT hinge body-local ({2})" -f $l,($legTris[$l].Count/3),$ghingeArr[$l].Trim())
    } else {
      $h=Get-Hinge $legTris[$l]; $legHinge += ,$h
      Write-Output ("gear leg {0}: {1} tris, auto hinge body-local ({2:N1},{3:N1},{4:N1})" -f $l,($legTris[$l].Count/3),(($h[0]-$cx)*$scale),(($h[1]-$cy)*$scale),(($h[2]-$cz)*$scale))
    }
  }
  # per-leg sweep + roll (fall back to GearFoldDeg / 0)
  $foldArr=$GearFold.Split(';'); $rollArr=$GearRoll.Split(';')
  $legFold=@(); $legRoll=@()
  for($l=0;$l -lt $numLegs;$l++){
    $fv=$GearFoldDeg; if($l -lt $foldArr.Count -and $foldArr[$l].Trim() -ne ''){ $fv=[double]$foldArr[$l].Trim() }
    $rv=0.0;          if($l -lt $rollArr.Count -and $rollArr[$l].Trim() -ne ''){ $rv=[double]$rollArr[$l].Trim() }
    $legFold+=[double]$fv; $legRoll+=[double]$rv
    Write-Output ("   leg {0}: sweep {1} deg, roll {2} deg" -f $l,$fv,$rv)
  }
  $bodyTags += ,([pscustomobject]@{ name=$GearTag; ox=0.0; oy=0.0; oz=0.0 })
  Write-GearMd3 $GearOut $gearTris.ToArray() $vLeg $legHinge $legFold $legRoll $GearFrames $cx $cy $cz $scale (MtlShaders $gearTriMtl)
}

# ---- vtol part (doors/nozzle): multi-frame animation, attached at tag_special ----
if($numVtol -gt 0 -and $vtolTris.Count -gt 0 -and $VtolOut -ne ''){
  $vhingeArr=$VtolHinge.Split(';')
  $vHinge=@()
  for($l=0;$l -lt $numVtol;$l++){
    if($vtolLegTris[$l].Count -eq 0){ Write-Output "WARN: vtol leg $l matched 0 tris"; $vHinge += ,([double[]]@($cx,$cy,$cz)); continue }
    if($l -lt $vhingeArr.Count -and ($vhingeArr[$l].Trim() -eq 'front' -or $vhingeArr[$l].Trim() -eq 'back')){
      # edge hinge: pivot at the forward (front=+X) or aft (back=-X) edge of the part,
      # Y/Z = centroid of the verts near that edge (so doors swing about their hinge line)
      $hv=$vhingeArr[$l].Trim()
      $mnx=[double]::MaxValue;$mxx=[double]::MinValue; $seen=[System.Collections.Generic.HashSet[int]]::new()
      foreach($gi in $vtolLegTris[$l]){ $gg=[int]$gi; if($seen.Add($gg)){ $x=[double]$gx[$gg]; if($x-lt$mnx){$mnx=$x}; if($x-gt$mxx){$mxx=$x} } }
      $edge = if($hv -eq 'front'){$mxx}else{$mnx}
      $thr = if($hv -eq 'front'){ $mxx - 0.2*($mxx-$mnx) } else { $mnx + 0.2*($mxx-$mnx) }
      $sy=0.0;$sz=0.0;$n=0; $seen2=[System.Collections.Generic.HashSet[int]]::new()
      foreach($gi in $vtolLegTris[$l]){ $gg=[int]$gi; if($seen2.Add($gg)){ $x=[double]$gx[$gg]; if(($hv -eq 'front' -and $x -ge $thr) -or ($hv -eq 'back' -and $x -le $thr)){ $sy+=[double]$gy[$gg]; $sz+=[double]$gz[$gg]; $n++ } } }
      if($n -eq 0){$n=1}
      $vHinge += ,([double[]]@($edge,($sy/$n),($sz/$n)))
      Write-Output ("vtol leg {0}: {1} tris, {2}-EDGE hinge body-local ({3:N1},{4:N1},{5:N1})" -f $l,($vtolLegTris[$l].Count/3),$hv,(($edge-$cx)*$scale),((($sy/$n)-$cy)*$scale),((($sz/$n)-$cz)*$scale))
    } elseif($l -lt $vhingeArr.Count -and $vhingeArr[$l].Trim() -ne ''){
      $hc=$vhingeArr[$l].Split(','); $hx=[double]$hc[0]/$scale+$cx; $hy=[double]$hc[1]/$scale+$cy; $hz=[double]$hc[2]/$scale+$cz
      $vHinge += ,([double[]]@($hx,$hy,$hz))
      Write-Output ("vtol leg {0}: {1} tris, EXPLICIT hinge body-local ({2})" -f $l,($vtolLegTris[$l].Count/3),$vhingeArr[$l].Trim())
    } else {
      $h=Get-Hinge $vtolLegTris[$l]; $vHinge += ,$h
      Write-Output ("vtol leg {0}: {1} tris, auto hinge body-local ({2:N1},{3:N1},{4:N1})" -f $l,($vtolLegTris[$l].Count/3),(($h[0]-$cx)*$scale),(($h[1]-$cy)*$scale),(($h[2]-$cz)*$scale))
    }
  }
  $vfoldArr=$VtolFold.Split(';'); $vrollArr=$VtolRoll.Split(';')
  $vFold=@(); $vRoll=@()
  for($l=0;$l -lt $numVtol;$l++){
    $fv=0.0; if($l -lt $vfoldArr.Count -and $vfoldArr[$l].Trim() -ne ''){ $fv=[double]$vfoldArr[$l].Trim() }
    $rv=0.0; if($l -lt $vrollArr.Count -and $vrollArr[$l].Trim() -ne ''){ $rv=[double]$vrollArr[$l].Trim() }
    $vFold+=[double]$fv; $vRoll+=[double]$rv
    Write-Output ("   vtol leg {0}: sweep {1} deg, roll {2} deg" -f $l,$fv,$rv)
  }
  $bodyTags += ,([pscustomobject]@{ name=$VtolTag; ox=0.0; oy=0.0; oz=0.0 })
  Write-GearMd3 $VtolOut $vtolTris.ToArray() $vVtol $vHinge $vFold $vRoll $GearFrames $cx $cy $cz $scale (MtlShaders $vtolTriMtl)
}

# ---- body (whole-model centre) with the part + gear tags ----
if($PerMaterial){
  $resolvedMtlDir=$MtlDir
  if($resolvedMtlDir -eq ''){ $li=$ShaderName.LastIndexOf('/'); if($li -ge 0){ $resolvedMtlDir=$ShaderName.Substring(0,$li) } }
  $bodyShaders=New-Object string[] $bodyTriMtl.Count
  for($i=0;$i -lt $bodyTriMtl.Count;$i++){ $bodyShaders[$i]="$resolvedMtlDir/$($bodyTriMtl[$i])" }
  Write-Output ("per-material: {0} body tris, {1} materials" -f $bodyTriMtl.Count,(($bodyTriMtl | Select-Object -Unique).Count))
  Write-Md3 $OutMd3 $bodyTris $cx $cy $cz $scale $bodyTags $bodyShaders
} else {
  Write-Md3 $OutMd3 $bodyTris $cx $cy $cz $scale $bodyTags
}
