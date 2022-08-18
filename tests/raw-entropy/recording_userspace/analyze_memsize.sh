#!/bin/bash
#
# Tool to generate test results for various Jitter RNG memory settings
#
# This tool is only needed if you have insufficient entropy. See ../README.md
# for details
#

OUTDIR="../results-measurements"
export sampleSize=1000000
#export sampleRounds=10
export sampleRounds=2
FILETYPE="u64.bin"

for bits in {10..30}
do
	export CFLAGS="-DJENT_MEMORY_BITS=$bits"

	./invoke_testing.sh

        for round in $(seq -f "%04g" 1 $sampleRounds); do
                cat $OUTDIR/jent-raw-noise-${round}-${FILETYPE} >> $OUTDIR/jent-raw-noise-${FILETYPE}
                rm -f $OUTDIR/jent-raw-noise-${round}-${FILETYPE}
        done

	mv $OUTDIR $OUTDIR-random_memaccess-${bits}bits

done
