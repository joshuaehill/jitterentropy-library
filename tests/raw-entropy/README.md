# Jitter RNG SP800-90B Entropy Analysis Tool

This archive contains the SP800-90B analysis tool to be used for the
Jitter RNG.

See the README files in the different sub directories.

# Runtime Tests

Statistical entropy estimates are performed using NIST's `ea_noniid` tool.
The final results of this testing (when using two verbose flags) look like this:
```
H_bitstring = 0.61449352149098757
H_bitstring Per Symbol = 4.3014546504369129
H_original = 4.3830259853234228
Assessed min entropy: 4.3014546504369129
```

The last value gives you an upper bound for the min entropy per time
delta, namely 
$$H_{\text{original}} \approx 4.3 \text{ and } n \times H_{\text{bitstring}} \approx 4.3 \text{.}$$

That means for one time delta the given number of entropy in
bits is expected to be less than this value on average.

The Jitter RNG uses a parameter called `osr` as the basis for its
internal entropy estimate for each raw noise sample. This parameter
is set when allocating the jitter entropy context using the
`jent_entropy_collector_alloc` call.

The Jitter RNG internally presumes that the raw noise contains 

$$H_{\text{min}} \geq \frac{1}{\texttt{osr}}$$

per raw noise sample ("time delta"). This implies that the statistical
entropy estimate must show that at least 

$$\frac{1}{\texttt{osr}}$$

bits of min entropy is present. In the
example above, the measurement shows that as much as 4.3 bits of entropy
is present which implies that the available amount of entropy may be
more than what the Jitter RNG heuristic applies even when setting `osr` to
the smallest possible value (`osr = 1`).

In more formal validation settings, it is necessary to separately estimate
a heuristic lower bound for the entropy.

# Approach to Justify a Particular Heuristic Entropy Estimate

In general, it is difficult to produce a stochastic model for non-physical
noise sources that applies across different hardware. In this section,
we present an approach for generating a technical (heuristic) argument
for why this noise source can support a stated entropy rate on particular
hardware.

One can characterize an IID noise source using only an adequately detailed
histogram, as the min entropy of such a source is established by the
symbol probabilities viewed in isolation. For a non-IID noise source,
this yields only an upper bound for the min entropy. This occurs because
non-IID sources have statistical memory: that is, there is an internal
state that induces relationships between the current output and some
number of past outputs.  The statistical memory “depth” is the number
of symbols that have a significant interrelationship.

The approach here has essentially four steps.
1. Use the `analyze_memsize.sh` script to generate (non-decimated)
data samples for a wide variety of memory sizes.
2. Create and examine raw data histograms for noise source outputs across
the tested candidate memory settings in order to select the size of the
memory area used by the primary noise source (`jent_memaccess`). Each
setting is selected using the `JENT_MEMORY_BITS` define).  For each memory
setting, the submitter should perform an initial review of the resulting
symbol histograms. It is likely that using a larger memory region will
significantly affect the observed distributions, as a larger memory
region leads to more cache misses. This progression continues until the
distribution becomes essentially fixed at a *terminal distribution*,
whence additionally increasing the memory size has limited observable
impact on the resulting histogram.  On most architectures, the delays
associated with updates that resolve purely within the cache system are
likely both to be more predictable and have significantly lower variation
as compared to the same updates that resolve in RAM reads and writes,
so it is useful to set the memory size to at least the smallest value
that attains this *terminal distribution*. Testing thus far suggests
that this terminal distribution is attained when approximately 10 times
the local cache size is allocated for the `jent_memaccess` buffer.
3. Select a single sub-distribution of interest.  This should be a
sub-distribution that is both common (ideally the selected sub-distribution would include over 80% of the
observed values) and suitably broad to support a reasonable entropy level.
This sub-distribution is provided setting the `JENT_DISTRIBUTION_MIN` and
`JENT_DISTRIBUTION_MAX` defines.
4. Use the `analyze_depth.sh` tool (using the selected `JENT_MEMORY_BITS`,
`JENT_DISTRIBUTION_MIN` and `JENT_DISTRIBUTION_MAX` settings)
to estimate the statistical memory depth of the system.  In order
to determine what level of statistical memory depth is associated
with this system, the data is probabilistically decimated at a rate
governed by the `JENT_MEMORY_DEPTH_BITS` parameter.  If a tested
`JENT_MEMORY_DEPTH_BITS` setting results in the produced data set
passing the NIST SP 800-90B Section 5 IID tests at a suitably high rate,
then a histogram-based entropy estimate can be applied as the heuristic
entropy estimate.

A single test result from the NIST SP 800-90B tests is not meaningful
for this testing, but it would be reasonable to instead test many data
sets in this way. For each IID test (e.g., Excursion Test Statistic,
Chi-Square Independence test, etc.), the result of such repeated testing
can be viewed as “passing” so long as the proportion of that test
passing is larger than some pre-determined cutoff.

In SP 800-90B Section 5 IID tests, each IID test is designed to have a
false reject rate of 

$$p_{\text{false reject}} = \frac{1}{1000} \text{.}$$

One can calculate the (one-sided) p-value
for the number of observed test failures using the binomial distribution:
if we denote the number of observed failures as `k` and the CDF of the
failure count Binomial Distribution (with parameters 

$$p=\frac{1}{1000}$$

and `n` being the number of 1 million sample non-overlapping data sets)
as `F(x)` , then we can calculate the p-value as

$$p_{\text{value}} = 1-F \left( k-1 \right) \text{.}$$

In order for this approach to be meaningful, this testing would have to
show the following properties:

* Property A: The IID testing must fail badly for the non-decimated data set,
indicating that the SP 800-90B tests are sensitive to a style of defect
present in the system. If the original data does not fail this testing,
then we cannot use these tests to estimate the memory depth.
* Property B: The IID test results must generally improve as the decimation rate
increases (i.e., the proportion of observed “passes” should generally
increase).
* Property C: All of the IID tests must eventually “pass” at a rate consistent
with the fixed p-value cutoff for some specific decimation rate.

## Example
### Test System
The following tests were conducted using a system with a Intel Xeon 
6252 CPU (36MB cache) and 384 GB memory.

### Counter Source
On x86-64 platforms, the `rdtscp` (“Read Time-Stamp Counter”) instruction
provides a clock that runs at some CPU-defined clock rate. From ["Intel
64 and IA-32 Architectures Software Developer’s Manual, Volume 3",
Section 17.17]:

> The time-stamp counter ... is a 64-bit counter that is set to 0
> following a RESET of the processor. Following a RESET, the counter
> increments even when the processor is halted...
>
> Processor families increment the time-stamp counter differently:
> [(Older) Option 1:] the time-stamp counter increments with every internal processor clock cycle.
>
> [(Newer) Option 2:] the time-stamp counter increments at a constant
> rate... Constant TSC behavior ensures that the duration of each clock
> tick is uniform and supports the use of the TSC as a wall clock timer
> even if the processor core changes frequency. This is the architectural
> behavior moving forward…
>
> The time stamp counter in newer processors may support an enhancement,
> referred to as invariant TSC... This is the architectural behavior moving
> forward. On processors with invariant TSC support, the OS may use the
> TSC for wall clock timer services...  The features supported can be
> identified by using the CPUID instruction.

With Linux systems, the “invariant TSC” CPU feature is available when
both the `constant_tsc` and `nonstop_tsc` CPU feature flags are
present in /proc/cpuinfo.

On this platform, the used counter is the TSC, so the counter is
sufficiently fine-grained to support `JENT_MEMACCESSLOOP_BITS = 0`. On a
different platform, had the distribution been very narrow, we may have
had to increase this parameter.

### Test Results
For step #1, The `analyze_memsize.sh` script was used to generate (non-decimated) data for
setting between 10 and 30. 

For Step #2, the following histograms were generated:

![Distributions Across Memory Sizes](https://github.com/joshuaehill/jitterentropy-library/blob/MemOnly/tests/raw-entropy/distanim.gif)

Note: This (and all following diagrams and specified values) are number
of distinct increments of the counter. In the tested architecture, the
counter is incremented in multiples of 2, so all values used by JEnt
are divided by this common factor.

We see here that the memory read/update event mostly results in actual
RAM reads when `JENT_MEMORY_BITS` is set to 27 or larger; the last three
histograms show essentially the same behavior.

For this evaluation, we proceed with `JENT_MEMORY_BITS` setting of 28
(resulting in a memory region of 256 MB). 

For Step 3, The distribution that we are interested in is in the interval [100, 200].

For Step 4, we used the `analyze_depth.sh` script to generate 149 million
samples for each of the tested `JENT_MEMORY_DEPTH_BITS` settings, using
the parameters `JENT_MEMORY_BITS = 28`, `JENT_DISTRIBUTION_MIN = 100`,
and `JENT_DISTRIBUTION_MAX = 200`.  We then performed IID testing on
the resulting data sets of 149 subsets of 1 million samples for each
`JENT_MEMORY_DEPTH_BITS` setting. The IID testing results were as follows:

![IID Testing Results](https://github.com/joshuaehill/jitterentropy-library/blob/MemOnly/tests/raw-entropy/IID-testing.svg)

Here, a "round" is a full SP 800-90B IID test round, which is considered
to be "passing" if and only if using all 22 of the IID tests pass. The
"Tests Passing" refers to the total proportion of the IID tests that
pass across all 149 tested subsets for each tested memory depth setting.

This shows that the data seems to become increasingly close to
IID behavior as the `JENT_MEMORY_DEPTH_BITS` value is increased.
Note that this satisfies all of the properties described above:

* Property A: for `JENT_MEMORY_DEPTH_BITS = 0`, 100% of the 149 distinct rounds fail
the SP 800-90B Section 5 IID testing.
* Property B: It is clear from the "Tests Passing" proportion that more tests
tend to pass as `JENT_MEMORY_DEPTH_BITS` is increased.
* Property C: Most of the rounds of testing pass when `JENT_MEMORY_DEPTH_BITS = 7`
or higher.

We also note that the rate of improvement of the "Rounds Passing" proportion falls off 
after the value `JENT_MEMORY_DEPTH_BITS = 7`. With this setting, the results mostly pass
the full IID testing, so we use this as the setting.

Using the parameters `JENT_MEMORY_DEPTH_BITS = 7`, `JENT_MEMORY_BITS = 28`, `JENT_DISTRIBUTION_MIN = 100`,
and `JENT_DISTRIBUTION_MAX = 200`, the source produces the following histogram.
![IID Testing Results](https://github.com/joshuaehill/jitterentropy-library/blob/MemOnly/tests/raw-entropy/final-hist.svg)

Now that the source is behaving as an essentially IID source, we can
directly produce an estimate for the entropy, namely approximately 

$$H_{\text{submitter}} = - \log_2 ( 0.047 ) \approx 4.3 $$

bits of min entropy per symbol. Assessment
via the non-IID track of SP 800-90B yields a similar estimate of 

$$ H_I = \text{min} \left( H_{\text{original}},  n \times H_{\text{bitstring}}, H_{\text{submitter}} \right) \approx 4.3 $$

bits of min entropy per symbol.

## Commentary

The decimation has a significant impact on the data output rate for
the JEnt library.  On the test system with `JENT_MEMORY_DEPTH_BITS = 0`
(no decimation), this library produces approximately 265 outputs every
second (where each output is 256 bits). On average, the probabilistic
decimation outputs a value approximately every

$$\left (3 \times 2^{\text{JENT\\_MEMORY\\_DEPTH\\_BITS} - 1} - 1 \right)$$

candidates, so the setting `JENT_MEMORY_DEPTH_BITS = 7` reduces the output
rate by a factor of approximately 191.5. As expected, this results in a rate
of approximately 1.4 outputs per second.

With this degree of slowdown, is use of this option "worth it"?  First,
it is important to point out that even though `JENT_MEMORY_DEPTH_BITS`
may look like it is "throwing away" significant amounts of (possibly
entropy-containing) data, the decimated values are fed into the
conditioner as "supplemental data". As such, though no entropy can be
formally claimed associated with this data, in practice any entropy
would be retained within the conditioning function.  As such, use of
this value is expected to only make the security of the system better,
as considerably more data is sent into the conditioning function. 

Second, it is useful to note that, while the decimated data and
non-decimated data seem to perform essentially the same way in the SP
800-90B non-IID entropy estimators, IID testing indicates that there is
a significant difference between the decimated and non-decimated data
streams. This suggests that the non-decimated data suffers from some
statistical flaws that are not detected (and thus not accounted for) by
the non-IID entropy estimators. This suggests that decimation helps
prevent over crediting entropy in the system (thus there is a technical
reason for including the decimation beyond production of a defensible
heuristic entropy estimate.)

In the case where one is unconcerned with a formal validation or the
abstract risk of over-estimating the produced entropy, it is probably
reasonable to set `JENT_MEMORY_DEPTH_BITS = 0`. Otherwise, it is best to
select an architecture-specific value supported by the described testing.

# Authors
Stephan Mueller <smueller@chronox.de> 
Joshua E. Hill <josh@keypair.us>
