#!/bin/bash
#
# Tool to generate test results for various Jitter RNG memory settings
#
# This tool is only needed if you have insufficient entropy. See ../README.md
# for details
#

OUTDIR="../results-measurements"

for bits in {10..30}
do
	export CFLAGS="-DJENT_MEMORY_BITS=$bits"

	./invoke_testing.sh

	mv $OUTDIR $OUTDIR-random_memaccess-${bits}bits

done
