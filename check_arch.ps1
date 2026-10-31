$files = 'third_party/libsodium/lib/libsodium.a','third_party/libuv/lib/libuv.a','third_party/uWebSockets/lib/libuSockets.a','third_party/uWebSockets/lib/libuWebSockets.a','third_party/oneTBB/lib/libtbb.a','third_party/pcre2/lib/libpcre2-8.a'
foreach ($f in $files) {
  $d = Get-Content $f -Encoding Byte
  $pe = $false; for ($i = 0; $i -lt $d.Length - 3; $i++) {
    if ($d[$i] -eq 0x55 -and $d[$i+1] -eq 0xAA) { $pe = $true; break }
  }
  Write-Host $f ': PE-in-archive = ' $pe
}
