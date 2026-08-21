#Requires -Version 5.1
<#
Scaffolds a plug-and-play module folder (plan Part 3, P3-02): module.json +
screen.json + a self-registering C++ stub. Adding a module never touches a
central file — the EXE glob compiles the stub, the static registrar
registers it, and the build-time merge embeds its screen.

The scaffold round-trips both JSON files through ConvertFrom-Json before
writing, mirroring New-UiScreen.ps1.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true, Position = 0)]
    [ValidatePattern('^[a-z0-9]+(-[a-z0-9]+)*$')]
    [string] $Name,

    [string] $Title,

    [string] $RepositoryRoot
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not $PSBoundParameters.ContainsKey('RepositoryRoot')) {
    $RepositoryRoot = Split-Path -Parent $PSScriptRoot
}
$RepositoryRoot = [System.IO.Path]::GetFullPath($RepositoryRoot)

$moduleRoot = Join-Path $RepositoryRoot "src\modules\$Name"
if (Test-Path -LiteralPath $moduleRoot) {
    throw "Module '$Name' already exists: $moduleRoot"
}

if (-not $PSBoundParameters.ContainsKey('Title') -or [string]::IsNullOrWhiteSpace($Title)) {
    $words = foreach ($word in $Name.Split('-')) {
        if ($word.Length -eq 1) {
            $word.ToUpperInvariant()
        } else {
            $word.Substring(0, 1).ToUpperInvariant() + $word.Substring(1)
        }
    }
    $Title = $words -join ' '
}

$manifest = [ordered]@{
    moduleId   = $Name
    tabLabel   = $Title
    order      = 100
    showInTabs = $true
    actions    = @()
    bindings   = @()
    capabilities = @()
}
$screen = [ordered]@{
    components = @(
        [ordered]@{
            type      = 'screen'
            route_id  = $Name
            module_id = $Name
            tab_label = $Title
            children  = @(
                [ordered]@{ type = 'text'; text = $Title; variant = 'title' }
            )
        }
    )
}

# Round-trip before writing: what we emit must parse.
$null = ($manifest | ConvertTo-Json -Depth 16) | ConvertFrom-Json
$null = ($screen | ConvertTo-Json -Depth 16) | ConvertFrom-Json

$null = New-Item -ItemType Directory -Path $moduleRoot
$encoding = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllText((Join-Path $moduleRoot 'module.json'),
    (($manifest | ConvertTo-Json -Depth 16) + "`n"), $encoding)
[System.IO.File]::WriteAllText((Join-Path $moduleRoot 'screen.json'),
    (($screen | ConvertTo-Json -Depth 16) + "`n"), $encoding)

$class = -join ($Name.Split('-') | ForEach-Object { $_.Substring(0, 1).ToUpperInvariant() + $_.Substring(1) }) + 'Module'
$wide = -join ($Name.ToCharArray() | ForEach-Object { $_ })
$cpp = @"
// Self-registering logic half of the $Title module. The UI half lives in
// screen.json; the gate pairs them by moduleId.
#include "modules/contract/module_contract.h"
#include "modules/registry/module_registry.h"

#include <memory>

namespace {

class $class final : public modules::ModuleDescriptor {
 public:
  std::wstring_view ModuleId() const override { return L"$wide"; }
  std::wstring_view TabLabel() const override { return L"$Title"; }
  int Order() const override { return 100; }
  bool ShowInTabs() const override { return true; }
  std::wstring_view SettingsRoute() const override { return {}; }
  std::vector<std::wstring> DeclaredActions() const override { return {}; }
  std::vector<std::wstring> DeclaredBindings() const override { return {}; }
  std::vector<std::wstring> DeclaredCapabilities() const override { return {}; }

  core::Status Bind(modules::ModuleHost&) override { return core::Ok(); }
  core::Status Handle(std::wstring_view, const json::Value&, json::Value*) override {
    return core::Err(core::ErrorCode::NotFound, L"$Name declares no actions");
  }
  void Release() override {}
};

std::unique_ptr<modules::ModuleDescriptor> Make() {
  return std::make_unique<$class>();
}

const modules::ModuleRegistrar registrar{L"$wide", &Make};

}  // namespace
"@
[System.IO.File]::WriteAllText((Join-Path $moduleRoot "${Name}_module.cpp"), $cpp.Replace("`r`n", "`n"), $encoding)

Write-Host "Module scaffolded: $moduleRoot"
Write-Host "Rebuild — the EXE glob compiles it, the registrar registers it, the merge embeds its screen."
