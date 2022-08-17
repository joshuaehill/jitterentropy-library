#!/bin/bash
#
# In some sources that display statistical dependence, the observed dependence can 
# only be prominent when samples are taken to be sufficiently close to each other.
# By increasing JENT_MEMORY_ACCESSLOOPS_BITS, the number of discarded measurements
# between used measurements for the memaccess source can be increased.
# This can be used to decrease the dependence between used samples, so that when
# they are summed, the result conforms with the normal distribution (by the
# central limit theorem).
#
# This should only be run once the size has been fixed.
# This fixes the number of memaccess loops to 1, so we are essentially characterizing
# a single read/update operation.
#

set -eu

OUTDIR="../results-measurements"
export sampleSize=1000000
export sampleRounds=149
#export sampleSize=2000000

for bits in {0..16}
do
	export CFLAGS="-DJENT_MEMORY_DEPTH_BITS=$bits -DJENT_MEMORY_BITS=28"

	./invoke_testing.sh

	for round in $(seq -f "%04g" 1 $sampleRounds); do
		cat $OUTDIR/jent-raw-noise-${round}.data >> $OUTDIR/jent-raw-noise-sd.bin
		rm -f $OUTDIR/jent-raw-noise-${round}.data
	done

	mv $OUTDIR $OUTDIR-random_memaccess-depth-${bits}

done
