#!/bin/sh
# Cross-check MicroCS test expectations against the real .NET runtime.
# Usage: tools/verify_dotnet.sh [test.cs ...]   (needs `dotnet` 8+ on PATH)
# Only pure-language tests are meaningful here; tests that use MicroCS modules
# (FS sandbox, HAL simulator, Scheduler) or MicroCS-specific error text are skipped.
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
W=${TMPDIR:-/tmp}/mcs_dotnet_verify
mkdir -p "$W"
cat > "$W/v.csproj" <<'P'
<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net8.0</TargetFramework>
  <Nullable>disable</Nullable><ImplicitUsings>enable</ImplicitUsings><InvariantGlobalization>true</InvariantGlobalization>
  <NoWarn>CS8321;CS0168;CS0219;CS0162;CS1998;CS0414;CS0649</NoWarn></PropertyGroup>
</Project>
P
# Default set = tests whose expected output is byte-identical to .NET 8.
# t01/t03/t04/t06 intentionally exercise MicroCS-specific behaviour (int64 constant
# wrap, GroupBy -> Dictionary, catchable StackOverflow, extra index detail, class
# declarations between statements) and are NOT expected to match.
[ $# -gt 0 ] || set -- "$ROOT"/tests/t02_*.cs "$ROOT"/tests/t05_*.cs "$ROOT"/tests/t10_*.cs "$ROOT"/tests/t11_*.cs "$ROOT"/tests/t13_*.cs "$ROOT"/examples/tour.cs
pass=0; fail=0
for t in "$@"; do
    cp "$t" "$W/Program.cs"
    if (cd "$W" && dotnet build -nologo -v q -o bin > build.txt 2>&1 && dotnet bin/v.dll > out.txt 2> err.txt) && diff -q "$W/out.txt" "${t%.cs}.out" > /dev/null; then
        echo "SAME  $(basename "$t")"; pass=$((pass + 1))
    else
        echo "DIFF  $(basename "$t")"; grep -h "error" "$W/build.txt" | head -3; diff "$W/out.txt" "${t%.cs}.out" | head -10; fail=$((fail + 1))
    fi
done
echo "$pass identical to .NET, $fail different"
[ $fail -eq 0 ]
