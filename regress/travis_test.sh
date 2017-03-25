#!/bin/bash

set -x

export PG_REGRESS_DIFF_OPTS=-u

if ! make installcheck; then
    cat regress/regression.diffs
    exit 1
fi
