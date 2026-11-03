# Lua / nanoFramework host comparison

This compares equivalent fib(30), 10-million-iteration integer loop and one-million-object
workloads. It is a host comparison, not a microcontroller measurement. Lua uses tables
and metatables for objects; MicroCS uses `List<P>`; nanoFramework uses `ArrayList`.
All programs print the same checksums (832040, 29999994, 980302).

Reproduce on Linux x86-64 with GCC, Python, .NET 8 and a Lua 5.4.8 source build:

```sh
make -j2 mcs
make bench                          # builds build/bench_host
sh bench/compare/nanoframework/run.sh   # downloads and builds nanoCLR benchmark
export LUA_DIR=/path/to/lua-5.4.8      # build with: make generic MYCFLAGS=-O2
mkdir -p build/compare-lua
cc -O2 -I"$LUA_DIR/src" bench/compare/lua/lua_runner.c "$LUA_DIR/src/liblua.a" -lm -ldl -o build/compare-lua/lua_runner
for name in fib loop objects; do "$LUA_DIR/src/luac" -o "build/compare-lua/$name.luac" "bench/compare/lua/$name.lua"; done
python3 bench/compare/lua/run_compare.py
```

The script pins to one available CPU, rotates runtime order over three rounds,
and prints medians of each round's three runs, then medians across rounds. It
checks Lua and nanoFramework checksums and stores per-round medians in
`build/compare-lua/results.json`. MicroCS uses `bench_host`'s image timings
(startup subtracted); Lua times bytecode load, execution and print after state
creation and library setup; nanoFramework times execution and print inside its
program, without process startup or assembly load. Hence timing boundaries
and object representations differ; do not interpret the values as perfectly
identical runtime operations or board performance. MicroCS image output can be
checked separately with `./mcs -c bench/fib.cs -o /tmp/fib.mcsb && ./mcs /tmp/fib.mcsb`.
