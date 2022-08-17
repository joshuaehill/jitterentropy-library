#!/bin/bash
#
# Tool to generate test results for various Jitter RNG memory settings
#
# This tool is only needed if you have insufficient entropy. See ../README.md
# for details
#

OUTDIR="../results-measurements"
export sampleSize=1200000

for bits in {0..8}
do
	export CFLAGS="-DJENT_MEMORY_ACCESSLOOPS_BITS=$bits"

	./invoke_testing.sh

	mv $OUTDIR $OUTDIR-random_memaccess-${bits}-rounds

done
