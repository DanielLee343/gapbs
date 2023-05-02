#!/bin/bash

TWITTER_SMALL=/home/cc/functions/run_bench/normal_run/graph_dir/twitter.el
TWITTER_BIG=/home/cc/functions/run_bench/normal_run/graph_dir/twitter_rv.el

echo 1 | sudo tee /proc/sys/vm/drop_caches

./bfs -g 20 -n 200
# ./bfs -f $TWITTER_BIG -n 100