#!/bin/bash
set -x -e
source $HOME/.cargo/env

pushd ch_integration_tests
cargo test
popd