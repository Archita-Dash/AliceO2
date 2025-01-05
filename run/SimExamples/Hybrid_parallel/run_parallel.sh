#!/usr/bin/env bash
#
# Hybrid generator simulation example in which multiple clones of the same generator
# are listed in a JSON file (hybridconfig_parallel.json in this folder). These multiple
# clones are running in parallel to produce events faster.

NEV=100
WORKERS=8

# Starting simulation with Hybrid generator in parallel mode
${O2_ROOT}/bin/o2-sim --noGeant -j 1 --field ccdb --vertexMode kCCDB --run 300000 --configKeyValues "GeneratorHybrid.configFile=$PWD/hybridconfig_parallel.json;GeneratorHybrid.randomize=false;GeneratorHybrid.num_workers=${WORKERS}" -g hybrid -o genevents_parallel --timestamp 1546300800000 --seed 836302859 -n $NEV