@echo off
setlocal

echo changing files

powershell -NoProfile -ExecutionPolicy Bypass -Command ^
    "$files = Get-ChildItem -Path 'dnn' -Recurse -Include '*_data.c', '*_data.h' | Where-Object { $_.Name -notmatch 'dump_data' }; " ^
    "foreach ($file in $files) { " ^
    "    (Get-Content $file.FullName) | ForEach-Object { " ^
    "        $_ -creplace 'OPUS', 'OAC' " ^
    "           -creplace 'Opus', 'Oac' " ^
    "           -creplace 'opus_', 'oac_' " ^
    "           -creplace 'opus', 'oac' " ^
    "           -creplace 'linear_init', 'oaci_linear_init' " ^
    "           -creplace '^const WeightArray ', 'const WeightArray oaci_' " ^
    "           -creplace '^int init_', 'int oaci_init_' " ^
    "           -creplace 'oac_uint8 dred_', 'oac_uint8 oaci_dred_' " ^
    "           -creplace 'conv2d_init', 'oaci_conv2d_init' " ^
    "    } | Set-Content -Path $file.FullName " ^
    "}"

echo Done.
pause
