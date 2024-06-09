## Design docs: [docs/rvdbt.md](docs/rvdbt.md)

### Building rvdbt
```sh
# Pre-install clang++, cmake, ninja-build, libboost-all-dev, llvm-15, llvm-15-dev

cd <rvdbt>
git submodule update --init --recursive
mkdir build && cd build
CC=clang CXX=clang++ cmake -GNinja -DCMAKE_BUILD_TYPE=Debug ..
ninja
```
### Using rvdbt
```sh
# First of all, rvdbt is only a proof of concept, it is quite unstable.
# File IO, memory maps, timers are permitted, rvdbt is able to run
# *Coremark* and *MIBench* benchsuite, as well as few examples in this repo.
# Supported platforms:
# 	guest ISA - *rv32ia*, host ISA - amd64
#	guest/host OS - linux v4+
#	tested with glibc/newlib and riscv32-unknown-linux-gnu-gcc 12.2.0

# Pre-install clang, libboost-all-dev, llvm-15, llvm-15-dev

cd <rvbdt>/build
# Create isolated fs root and cache dir
mkdir troot tcache

# Compile an example, use `target=rv32i` and `static` linking
<riscv32-gcc> -march=rv32i -fpic -fpie -static -O2 ../examples/pi_double.c
mv a.out troot

# Run: [options] -- [guest argv]. Guest argv is relative to `troot`!!
./bin/elfrun --fsroot troot --cache tcache -- a.out 100000
# expected out: prec=100000, res=3.1415926535897936, raw=400921fb54442d19
# increate a.out `prec` for benchmarking 
# It may fail, for example if different libc or ISA is used
# 	add logs: --logs dbt:ukernel
# compatible qemu cmd: 
# 	qemu-riscv32 troot/a.out 100000

# Use collected tcache/<checksum>.prof to create precompiled image
./bin/elfaot --logs dbt:aot --cache tcache --mgdump . --elf troot/a.out

# View compiled binary graph (graphviz dot). xdot suggested.
xdot modulegraph.gv

# Run, --aot on
./bin/elfrun --fsroot troot --cache tcache --aot on -- a.out 100000
```
