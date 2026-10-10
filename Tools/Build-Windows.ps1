<#
[INPUT]: UE 5.6.1 官方安装、VS2022 C++ 工具链、完整获授权工程资产；显式新输出目录。
[OUTPUT]: Development Win64 Cook/Stage/Archive 与真实 UAT 日志、参数清单。
[POS]: 可重复的本地构建入口。只读配置，不自动删除资产、不改写音乐许可选择。
[PROTOCOL]: 变更时更新此头部，然后检查 AGENTS.md；发布验证见 Docs/RELEASE.md。
#>
[CmdletBinding()]
param(
    [string]$ProjectRoot = (Split-Path -Parent $PSScriptRoot),
    [Parameter(Mandatory = $true)][string]$EngineRoot,
    [Parameter(Mandatory = $true)][string]$ArchiveDirectory,
    [switch]$PlanOnly
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath $ProjectRoot).Path.TrimEnd('\', '/')
$engine = (Resolve-Path -LiteralPath $EngineRoot).Path.TrimEnd('\', '/')
$target = [IO.Path]::GetFullPath($ArchiveDirectory).TrimEnd('\', '/')
$project = Join-Path $root 'AICompanionDemo.uproject'
$uat = Join-Path $engine 'Engine\Build\BatchFiles\RunUAT.bat'
if (-not (Test-Path -LiteralPath $project -PathType Leaf)) { throw '未找到工程文件。' }
if (-not (Test-Path -LiteralPath $uat -PathType Leaf)) { throw '未找到官方引擎 RunUAT.bat。' }
$versionInfo = Get-Content -LiteralPath (Join-Path $engine 'Engine\Build\Build.version') -Raw | ConvertFrom-Json
$engineVersion = "$($versionInfo.MajorVersion).$($versionInfo.MinorVersion).$($versionInfo.PatchVersion)"
if ($engineVersion -ne '5.6.1') { throw "本工程构建约定 UE 5.6.1，实际为 $engineVersion。" }
if (-not (Test-Path -LiteralPath (Join-Path $root 'Content\ThirdPerson\Lvl_ThirdPerson.umap') -PathType Leaf)) {
    throw '缺入口地图：公开源码快照不含完整资产，请使用获授权的完整工程。'
}
$separator = [IO.Path]::DirectorySeparatorChar
$driveRoot = [IO.Path]::GetPathRoot($target).TrimEnd('\', '/')
if ($target.Equals($driveRoot, [StringComparison]::OrdinalIgnoreCase) -or
    $target.Equals($root, [StringComparison]::OrdinalIgnoreCase) -or
    ($engine + $separator).StartsWith($target + $separator, [StringComparison]::OrdinalIgnoreCase) -or
    ($target + $separator).StartsWith($engine + $separator, [StringComparison]::OrdinalIgnoreCase)) {
    throw '输出必须是狭窄的新目录，不能是盘根、工程根或引擎范围。'
}
foreach ($dirName in @('Source', 'Content', 'Config', '.git', 'Tools', 'Docs')) {
    $prefix = (Join-Path $root $dirName) + $separator
    if (($target + $separator).StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'ArchiveDirectory 不能放在源码、资产、配置、Git、工具或文档目录内。'
    }
}
if ((Test-Path -LiteralPath $target) -or (Test-Path -LiteralPath "$target.uat.log") -or (Test-Path -LiteralPath "$target.build-plan.json")) {
    throw '目标或审计文件已经存在；选择新版本目录。脚本不会删除或覆盖旧包。'
}
if (Get-Process -Name UnrealEditor -ErrorAction SilentlyContinue) {
    throw '必须先关闭 Unreal Editor，再完整构建；禁止热重载。'
}
$gameConfig = [IO.File]::ReadAllText((Join-Path $root 'Config\DefaultGame.ini'))
if ($gameConfig -match '(?m)^[ \t]*BGM_Track[ \t]*=[ \t]*\S+') {
    throw 'BGM_Track 非空：当前音乐许可尚未有完备证据。请使用明确处理音乐许可的候选副本；此脚本不改原配置。'
}
$engineConfig = [IO.File]::ReadAllText((Join-Path $root 'Config\DefaultEngine.ini'))
if ($engineConfig -match '(?m)^[ \t]*SecurityToken[ \t]*=[ \t]*\S+') {
    throw 'DefaultEngine.ini 有非空 SecurityToken，清理后再构建公开候选。'
}
if ($gameConfig -notmatch '(?m)^\s*\+DirectoriesToNeverCook\s*=\s*\(Path="/Game/BGM"\)\s*$') {
    throw '候选需要明确排除 /Game/BGM 的 Cook，避免未获授权的备选音乐被意外收入。'
}
$uatArguments = @(
    'BuildCookRun', "-project=$project", '-noP4', '-platform=Win64',
    '-clientconfig=Development', '-build', '-cook', '-map=/Game/ThirdPerson/Lvl_ThirdPerson',
    '-stage', '-pak', '-compressed', '-archive', "-archivedirectory=$target",
    '-prereqs', '-nodebuginfo', '-utf8output', '-unattended'
)
$plan = [ordered]@{
    EngineVersion = $engineVersion
    Configuration = 'Development'
    Platform = 'Win64'
    Map = '/Game/ThirdPerson/Lvl_ThirdPerson'
    MusicPolicy = 'BGM slot empty and /Game/BGM explicitly excluded from Cook.'
    Arguments = $uatArguments
    ConfigSha256 = @('DefaultEngine.ini', 'DefaultGame.ini') | ForEach-Object {
        [pscustomobject]@{File = $_; Sha256 = (Get-FileHash -LiteralPath (Join-Path (Join-Path $root 'Config') $_) -Algorithm SHA256).Hash.ToLowerInvariant()}
    }
}
if ($PlanOnly) { $plan | ConvertTo-Json -Depth 5; return }
New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
[IO.File]::WriteAllText("$target.build-plan.json", ($plan | ConvertTo-Json -Depth 5), (New-Object Text.UTF8Encoding $false))
$savedErrorPreference = $ErrorActionPreference
try {
    # 原生 stderr 也必须完整落盘；结果以 UAT exit code 判定，不因日志通道中断管线。
    $ErrorActionPreference = 'Continue'
    & $uat @uatArguments 2>&1 | Tee-Object -FilePath "$target.uat.log"
    $uatExitCode = $LASTEXITCODE
} finally { $ErrorActionPreference = $savedErrorPreference }
if ($uatExitCode -ne 0) { throw "BuildCookRun 失败，exit=$uatExitCode；读取 $target.uat.log 定位首个根因。" }
if (-not (Test-Path -LiteralPath (Join-Path $target 'Windows\AICompanionDemo.exe') -PathType Leaf)) {
    throw 'UAT exit=0，但归档入口缺失，不能宣称成功。'
}
[pscustomobject]@{Status = 'UATCompleted'; ArchiveDirectory = $target; Log = "$target.uat.log"; RuntimeValidation = 'Pending; successful packaging is not gameplay validation.'}
