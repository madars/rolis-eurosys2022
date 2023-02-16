#!/bin/bash
[ -z ${SUDO_USER} ] && \
    (echo $0 is best run under sudo && exit 1)

export DEBIAN_FRONTEND=noninteractive

apt update
apt upgrade
apt dist-upgrade
# this installs libjemalloc2 which is against a rec in BUILD
apt install -y build-essential autoconf libgoogle-perftools-dev libjemalloc-dev cmake libnuma-dev libaio-dev libssl-dev libboost-all-dev libyaml-cpp-dev
sudo apt install -y make automake cmake gcc g++ libboost-all-dev libyaml-cpp-dev libjemalloc-dev libgoogle-perftools-dev libaio-dev build-essential libssl-dev libffi-dev python3-dev silversearcher-ag numactl autoconf cgroup-tools net-tools pkg-config strace
