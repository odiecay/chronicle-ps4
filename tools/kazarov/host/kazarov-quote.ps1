# Argument quoting for Windows PowerShell 5.1, which passes native arguments unquoted.
# Dot-sourced by kazarov.ps1; loads without running anything else.

# One argument, quoted so CommandLineToArgvW reads it back as the same string.
function ConvertTo-WindowsArgument([string]$Argument) {
    if ($Argument.Length -gt 0 -and $Argument.IndexOfAny([char[]]@(' ', "`t", '"')) -lt 0) {
        return $Argument
    }
    $sb = [System.Text.StringBuilder]::new()
    [void]$sb.Append('"')
    $slashes = 0
    foreach ($c in $Argument.ToCharArray()) {
        if ($c -eq [char]'\') {
            $slashes++
            continue
        }
        if ($c -eq [char]'"') {
            # Backslashes before a quote are doubled, and the quote itself is escaped.
            [void]$sb.Append(('\' * ($slashes * 2 + 1)))
            [void]$sb.Append('"')
        }
        else {
            [void]$sb.Append(('\' * $slashes))
            [void]$sb.Append($c)
        }
        $slashes = 0
    }
    # Backslashes before the closing quote are doubled too.
    [void]$sb.Append(('\' * ($slashes * 2)))
    [void]$sb.Append('"')
    return $sb.ToString()
}
