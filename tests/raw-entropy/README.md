# Jitter RNG SP800-90B Entropy Analysis Tool

This archive contains the SP800-90B analysis tool to be used for the Jitter RNG.
The tool set consists of the following individual tools:

- `recording_userspace`: This tools is used to gather the raw entropy of
  the user space Jitter RNG implementation.

See the README files in the different sub directories.

# Interpretation of Results

## Runtime Tests

The result of the data analysis performed with `validation-runtime` contains
in the file `jent-raw-noise-0001.minentropy_FF_8bits.var.txt` at the bottom data
like the following:

```
H_original: 2.387470
H_bitstring: 0.337104

min(H_original, 8 X H_bitstring): 2.387470
```

The last value gives you an upper bound for the min entropy per time delta.
That means for one time delta the given number of entropy in bits is
expected to be less than this value on average.

The Jitter RNG heuristic presumes 1/`osr` bit of entropy per
time delta. This implies that the measurement must show that *at least* 1/`osr` bit
of entropy is present. In the example above, the measurement shows that
as much as 2.3 bits of entropy is present which implies that the available amount of
entropy may be more than what the Jitter RNG heuristic applies.

In more formal validation settings, it is necessary to separately estimate a
heuristic lower bound for the entropy.

# Approach to Justify a Particular Heuristic Entropy Estimate

In general, it is difficult to produce a stochastic model for non-physical noise
sources that apply across different hardware. In this section, we present an
approach for generating a technical (heuristic) argument for why this noise
source can support a stated entropy rate on particular hardware.

One can characterize an IID noise source using only an adequately detailed histogram,
as the min entropy of such a source established by the symbol probabilities viewed in
isolation. For a non-IID noise source, this yields only an upper bound for the min
entropy. This occurs because non-IID sources have statistical memory, that is there is
internal state that induces relationships between the current output and some number of
past outputs.  The statistical memory “depth” is the number of symbols that have a significant
interrelationship.

The approach here has essentially three steps.
1. The distribution of timings is examined for a variety of memory
settings (each setting is selected using the `JENT_MEMORY_BITS` define).
For each memory setting, perform an initial review of the resulting
symbol histograms. It is likely that using a larger memory region will
significantly affect the observed distributions, as a larger memory
region leads to more cache misses. This progression continues until the
distribution becomes fairly fixed at a *terminal distribution*, whence
additionally increasing the memory size has limited observable impact on
the resulting histogram.  On most architectures, the delay associated
with the cache system is likely to be both more predictible and have
significantly lower variation, so it is useful to set the memory size
to at least the smallest value that attains this *terminal distribution*.
2. Select the sub-distribution of interest.  This should be a
sub-distribution that is both common (ideally capturing over 80% of the
observed values) and suitably broad to support a reasonable entropy level.
This sub-distribution is provided using the `JENT_DISTRIBUTION_MIN` and
`JENT_DISTRIBUTION_MAX` settings.
3. Use the `analyze_depth.sh` tool to estimate the statistical memory
depth of the system.  In order to determine what level of statistical
memory depth is associated with this system, the data is probabilistically
decimated at a rate governed by the `JENT_MEMORY_DEPTH_BITS` parameter.
If a suitably large `JENT_MEMORY_DEPTH_BITS` setting results in the
produced data set passing the NIST SP 800-90B Section 5 IID tests at
a suitably high rate, then a histogram-based entropy estimate can be
applied as the heuristic entropy estimate.

A single test result from the NIST SP 800-90B tests is not meaningful
for this testing, but it would be reasonable to instead test many data
sets in this way. The result of such repeated testing can be viewed as
“passing” so long as the proportion of tests passing for each IID
test is larger than some fixed cutoff.

In SP 800-90B Section 5 IID tests, each IID test is designed to have a
false reject rate of 1/1000. One can calculate the (one-sided) p-value
for the number of observed test failures using the binomial distribution:
if we denote the number of observed failures as k and the CDF of the
failure count Binomial Distribution (with parameters p=1/1000, and  n
being the number of 1 million sample non-overlapping data sets) as F(x),
then we can calculate the p-value as 1-F(k-1).

In order for this approach to be meaningful, this testing would have to
show the following properties:
1. The IID testing must fail badly for the non-decimated data set,
indicating that the SP 800-90B tests are sensitive to a style of defect
present in the system. If the original data does not fail this testing,
then we cannot use these tests to estimate the memory depth.
2. The IID test results must generally improve as the decimation rate
increases (i.e., the proportion of observed “passes” should generally
increase).
3. All of the IID tests must eventually “pass” at a rate consistent
with the fixed p-value cutoff for some specific decimation rate.

# Example
A system based on an Intel Xeon 6252 CPU (36MB cache) produces the following data 
histogram:

![Distributions Across Memory Sizes](https://github.com/joshuaehill/jitterentropy-library/blob/MemOnly/tests/raw-entropy/distanim.gif)

We see here that the memory updates resolve to memory when `JENT_MEMORY_DEPTH_BITS`
is set to 27 or larger.

For this evaluation, we proceed with `JENT_MEMORY_DEPTH_BITS` setting of 28.

For step 2, we perform IID testing on 149 sets of 1 million samples each.
The results were as follows:

![IID Testing Results](https://github.com/joshuaehill/jitterentropy-library/blob/MemOnly/tests/raw-entropy/IID-testing.svg)

This shows that...

# Author
Stephan Mueller <smueller@chronox.de>
Joshua E. Hill <josh@keypair.us>
