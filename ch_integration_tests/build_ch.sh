#!/bin/bash
set -x -e
source $HOME/.cargo/env

CH_VERSION="master"
git init cloud-hypervisor
pushd cloud-hypervisor
git remote add origin https://github.com/cloud-hypervisor/cloud-hypervisor
git fetch origin --depth 1 "$CH_VERSION"
git checkout "$CH_VERSION"
cargo build
sudo cp target/debug/cloud-hypervisor /usr/local/bin
sudo setcap cap_net_admin+ep /usr/local/bin/cloud-hypervisor
popd
