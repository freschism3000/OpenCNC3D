[CmdletBinding()]
param([string]$ScriptPath,[string]$FixtureRoot)
$ErrorActionPreference='Stop'
Set-StrictMode -Version 2
$tokens=$null;$errors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile($ScriptPath,[ref]$tokens,[ref]$errors)
if($errors.Count){throw ('Script parser failed: '+$errors[0].Message)}
foreach($name in @('Test-RuntimeAssetFile','Get-VerifiedRuntimeAssets','Copy-VerifiedRuntimeAssets','Get-AssetFingerprints','Get-ProtectedSnapshot')){
 $fn=$ast.FindAll({param($n)$n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq $name},$true)
 if($fn.Count -ne 1){throw ('Missing unique helper: '+$name)}
 Invoke-Expression $fn[0].Extent.Text
}
$script:passed=0
function Check([bool]$condition,[string]$name){if(-not $condition){throw ('ASSETTEST|FAIL|'+$name)};Write-Output ('ASSETTEST|pass|'+$name);$script:passed++}
function Reject([scriptblock]$action,[string]$name){$failed=$false;try{&$action | Out-Null}catch{$failed=$true};Check $failed $name}
$bundle=Join-Path $FixtureRoot 'bundle'
$original=Join-Path $FixtureRoot 'original'
$private=Join-Path $FixtureRoot 'private'
$manifest=Get-Content -LiteralPath (Join-Path $FixtureRoot 'manifest.json') -Raw|ConvertFrom-Json
$legacy=Get-Content -LiteralPath (Join-Path $FixtureRoot 'legacy.json') -Raw|ConvertFrom-Json
$expected=$manifest.runtime_assets.PSObject.Properties['ebld/FACT.ebld.fclv'].Value
$source=Join-Path $bundle 'assets/ebld/FACT.ebld.fclv'
$originalFile=Join-Path $original 'ebld/FACT.ebld.fclv'
$destination=Join-Path $private 'ebld/FACT.ebld.fclv'
$originalHash=(Get-FileHash -LiteralPath $originalFile).Hash
$oldCopyHash=(Get-FileHash -LiteralPath $destination).Hash
$InstallDir=$original;$normalPref=Join-Path $FixtureRoot 'absent-pref';$legacyPref=Join-Path $FixtureRoot 'absent-legacy'
$snapshot=Get-ProtectedSnapshot
Check ($snapshot['game/ebld/FACT.ebld.fclv'] -eq $originalHash) 'original sidecar included in protected snapshot'
Check (@(Get-VerifiedRuntimeAssets $legacy $bundle).Count -eq 0) 'legacy R2 manifest without asset field accepted under strict mode'
Check (@(Get-VerifiedRuntimeAssets ('{"runtime_assets":{}}'|ConvertFrom-Json) $bundle).Count -eq 0) 'empty optional asset map accepted'
$verified=@(Get-VerifiedRuntimeAssets $manifest $bundle)
Check ($verified.Count -eq 1 -and $verified[0].path -ceq 'ebld/FACT.ebld.fclv') 'valid approved sidecar verified before copy'
Check ((Get-FileHash -LiteralPath $destination).Hash -eq $oldCopyHash) 'validation alone changes no private or original asset'
foreach($key in @('../ebld/FACT.ebld.fclv','ebld/PROC.ebld.fclv','C:/ebld/FACT.ebld.fclv','ebld\FACT.ebld.fclv','ebld/fact.ebld.fclv')){
 $object=[pscustomobject]@{runtime_assets=[pscustomobject]@{}}
 $object.runtime_assets|Add-Member -NotePropertyName $key -NotePropertyValue $expected
 Reject {Get-VerifiedRuntimeAssets $object $bundle} ('reject unsupported/path-traversal key '+$key)
}
foreach($json in @('{"runtime_assets":null}','{"runtime_assets":[]}','{"runtime_assets":{"ebld/FACT.ebld.fclv":{}}}')){
 $bad=$json|ConvertFrom-Json
 Reject {Get-VerifiedRuntimeAssets $bad $bundle} 'reject malformed optional asset schema'
}
$bad=$manifest|ConvertTo-Json -Depth 8|ConvertFrom-Json
$bad.runtime_assets.PSObject.Properties['ebld/FACT.ebld.fclv'].Value.sha256='0'*64
Reject {Get-VerifiedRuntimeAssets $bad $bundle} 'reject asset hash mismatch before copy'
$bad=$manifest|ConvertTo-Json -Depth 8|ConvertFrom-Json
$bad.runtime_assets.PSObject.Properties['ebld/FACT.ebld.fclv'].Value.size=149364
Reject {Get-VerifiedRuntimeAssets $bad $bundle} 'reject unexpected sidecar size'
$backup=[IO.File]::ReadAllBytes($source)
Move-Item -LiteralPath $source -Destination ($source+'.missing')
Reject {Get-VerifiedRuntimeAssets $manifest $bundle} 'reject missing declared sidecar before copy'
Move-Item -LiteralPath ($source+'.missing') -Destination $source
Reject {Copy-VerifiedRuntimeAssets $verified $original $original} 'reject copy into original installation'
Reject {Copy-VerifiedRuntimeAssets $verified (Join-Path $original 'nested') $original} 'reject copy inside original installation'
$changed=$backup.Clone();$changed[20]=$changed[20] -bxor 1;[IO.File]::WriteAllBytes($source,$changed)
Reject {Copy-VerifiedRuntimeAssets $verified $private $original} 'reverify source immediately before copy'
Check ((Get-FileHash -LiteralPath $destination).Hash -eq $oldCopyHash) 'changed bundle asset rejected without private overwrite'
[IO.File]::WriteAllBytes($source,$backup)
Copy-VerifiedRuntimeAssets $verified $private $original
Check ((Get-FileHash -LiteralPath $destination).Hash.ToLowerInvariant() -eq $expected.sha256) 'verified runtime cache copied into private game'
Check ((Get-FileHash -LiteralPath $originalFile).Hash -eq $originalHash) 'original sidecar bytes preserved'
$fingerprints=Get-AssetFingerprints $private
$record=@($fingerprints.files|Where-Object {$_.path -ceq 'ebld/FACT.ebld.fclv'})
Check ($record.Count -eq 1 -and $record[0].sha256 -eq $expected.sha256) 'report fingerprints include sidecar'
$serialized=$fingerprints|ConvertTo-Json -Depth 6
Check (-not $serialized.Contains($FixtureRoot) -and $fingerprints.asset_bytes_included -eq $false) 'report includes relative fingerprint only and no asset bytes'
# Inject corruption after the copy command to prove the production helper's final check.
function Copy-Item {
 [CmdletBinding()]param([string]$LiteralPath,[string]$Destination,[switch]$Force)
 Microsoft.PowerShell.Management\Copy-Item -LiteralPath $LiteralPath -Destination $Destination -Force:$Force
 [IO.File]::AppendAllText($Destination,'tamper')
}
Reject {Copy-VerifiedRuntimeAssets $verified $private $original} 'post-copy hash/size verification rejects tampered destination'
Remove-Item Function:\Copy-Item
Check ((Get-FileHash -LiteralPath $originalFile).Hash -eq $originalHash) 'failed private copy leaves original untouched'
Write-Output ('ASSETTEST|summary|passed='+$script:passed)
