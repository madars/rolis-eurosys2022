#!/bin/bash
[ -z ${SUDO_USER} ] || \
    (echo $0 is best run NOT under sudo && exit 1)

mkdir -p ~/root

[ -d rpclib ] || git clone https://github.com/rpclib/rpclib.git
mkdir -p rpclib/build
cd rpclib/build
cmake ..
make -j
make DESTDIR=$HOME/root install
cd ../..

[ -d jemalloc ] || (git clone https://github.com/jemalloc/jemalloc.git ; cd jemalloc ; git checkout master ; cd ..)
cd jemalloc
./autogen.sh
make -j20
make DESTDIR=$HOME/root install
cd ..

export CPATH=$HOME/root/usr/local/include
export LIBRARY_PATH=$HOME/root/usr/local/lib

make -j20 dbtest MODE=perf SERIALIZE=0 PAXOS_ENABLE_CONFIG=0 STO_BATCH_CONFIG=0 SILO_SERIAL_CONFIG=0 PAXOS_ZERO_CONFIG=0 LOGGING_TO_ONLY_FILE=0 OPTIMIZED_REPLAY=0 REPLAY_FOLLOWER=0 DBTEST_PROFILER=0

echo "[+] Should print jemalloc.so from $HOME/root if jemalloc is used."
LD_LIBRARY_PATH=$HOME/root/usr/local/lib ldd out-perf.masstree/benchmarks/dbtest | grep -i jemalloc
