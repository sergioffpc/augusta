# PowerShell (scripts/, tools/*/scripts/): PSScriptAnalyzer's default rules,
# and the formatting Invoke-Formatter applies: the same 2-space indent as the
# rest of the tree (.editorconfig), braces on the line that opens them.
@{
  # The bootstraps are run by a person at a console, and Write-Host is how they
  # talk to that person, not output a caller would pipe on.
  ExcludeRules = @('PSAvoidUsingWriteHost')
  Rules = @{
    PSPlaceOpenBrace = @{ Enable = $true; OnSameLine = $true; NewLineAfter = $true; IgnoreOneLineBlock = $true }
    PSPlaceCloseBrace = @{ Enable = $true; NewLineAfter = $false; IgnoreOneLineBlock = $true; NoEmptyLineBefore = $false }
    # Off: it pulls a continued line back to the statement's own indent.
    PSUseConsistentIndentation = @{ Enable = $false }
    PSUseConsistentWhitespace = @{ Enable = $true; CheckOpenBrace = $true; CheckOpenParen = $true; CheckOperator = $true; CheckSeparator = $true; CheckInnerBrace = $true; CheckPipe = $true; CheckParameter = $false }
    PSAlignAssignmentStatement = @{ Enable = $false }
    PSUseCorrectCasing = @{ Enable = $true }
  }
}
