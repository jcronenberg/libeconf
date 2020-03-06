#!/bin/bash


TOPDIR=$(cd $(dirname $0)/.. && pwd)
[ -n "$abs_top_srcdir" ] || abs_top_srcdir=$TOPDIR

declare -a teststrings=("autofs.conf"
                      "autofs.conf -f"
                      "nonexistant.conf"
                      "randomstring")
declare -a experr=("command: econftool show"
                   "command: econftool show"
                   "Configuration file not found"
                   "-->Currently only works with a dot in the filename!")


teststringslength=${#teststrings[@]}

export ECONFTOOL_ROOT=$abs_top_srcdir/tests/tst-econftool-data

got_error=false

for ((i=0; i<${teststringslength}; i++)); do
    error=$($abs_top_srcdir/util/econftool show ${teststrings[$i]} 2>&1)
    if [[ ! $error =~ ${experr[$i]} ]]; then
        echo error for ${teststrings[$i]}
        echo expected to contain: ${experr[$i]}
        echo got: $error
        got_error=true
    fi
done
if $got_error; then
    exit 1
fi
