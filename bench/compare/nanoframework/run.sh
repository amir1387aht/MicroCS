#!/bin/sh
# .NET nanoFramework side of the MicroCS comparison (see ../README.md).
# Compiles Program.cs with Roslyn against the nanoFramework mscorlib, converts
# it to a nanoFramework PE with MetadataProcessor and runs it on the nanoCLR
# virtual device (the `nanoclr` dotnet tool, native x64 nanoCLR) - on a PC.
# Needs the .NET 8 SDK and network access to nuget.org. On Windows, the same
# can be done from Visual Studio with the nanoFramework extension.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=$(cd "$HERE/../../.." && pwd)/build/compare-nf
mkdir -p "$OUT" && cd "$OUT"
get() { # id version dir
    [ -d "$3" ] || { curl -sfL -o "$3.nupkg" "https://www.nuget.org/api/v2/package/$1/$2"; mkdir -p "$3"; (cd "$3" && unzip -qo "../$3.nupkg"); }
}
get nanoFramework.CoreLibrary 1.17.12 corlib
get nanoFramework.System.Text 1.3.42 text
get nanoFramework.Tools.MetadataProcessor.CLI 3.0.104 mdp
get System.Drawing.Common 8.0.10 sdc
get nanoclr 1.1.311 nanoclr
# MetadataProcessor ships as a .NET Framework exe; run it on .NET 8
MP=mdp/content/MetadataProcessor
cp $MP/nanoFramework.Tools.MetadataProcessor.exe $MP/mp.dll
cp sdc/lib/net8.0/System.Drawing.Common.dll $MP/
echo '{"runtimeOptions":{"tfm":"net8.0","framework":{"name":"Microsoft.NETCore.App","version":"8.0.0"},"rollForward":"LatestMajor"}}' > $MP/mp.runtimeconfig.json
CSC=$(ls -d "$(dirname "$(command -v dotnet)")"/sdk/8.*/Roslyn/bincore/csc.dll 2>/dev/null | head -1)
[ -n "$CSC" ] || CSC=$(ls -d "$DOTNET_ROOT"/sdk/8.*/Roslyn/bincore/csc.dll | head -1)
dotnet "$CSC" -nologo -nostdlib -noconfig -optimize+ -target:exe -out:Bench.exe \
    -r:corlib/lib/mscorlib.dll -r:text/lib/nanoFramework.System.Text.dll "$HERE/Program.cs"
dotnet $MP/mp.dll -loadHints mscorlib "$PWD/corlib/lib/mscorlib.dll" \
    -loadHints nanoFramework.System.Text "$PWD/text/lib/nanoFramework.System.Text.dll" \
    -parse Bench.exe -compile Bench.pe false > mdp.log
dotnet nanoclr/tools/net8.0/any/nanoFramework.nanoCLR.CLI.dll run \
    -a Bench.pe corlib/lib/mscorlib.pe text/lib/nanoFramework.System.Text.pe | grep -v '^ \|^Total\|^$'
