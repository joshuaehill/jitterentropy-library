# Pull Request Description

## Pull Request Summary
I have a proposed set of changes to the JEnt library; they essentially can be broken into a few types of changes:
1. Addition of some new functionality to make producing a heuristic entropy argument more straight-forward and technically justifiable.
2. Separation of the memory timing and overall timing noise sources. These have always been presented as conceptually distinct but were combined within any analysis because the raw data reflected both.
3. General clean up and "cruft" removal. Changes here include the removal of optional functionality that seems to be present for historical reasons, but either can't be used any longer or are non-default functionality that probably ought not be used.

## Pull Request Changes
1. Noise source:
	- Make the timing of the `jent_memaccess()` function the primary noise source.
		- All health testing is on this data, and the raw data output for testing is now this timing.
		- This source's distribution is easier to analyze than the overall timing.
		- Use the "volatile" keyword in a few places to prevent reordering of the timing and memory updates within this source.
		- Allow a raw noise source value range to be selected so that a specified sub-distribution is uniformly used.
			- Only data within this range is counted as being output from the primary noise source.
			- All sampled data is sent into the conditioning function.
				- Decimated data and data from different sub-distributions is treated as supplemental data.
		- Probabilistically decimate the data to account for statistical memory depth (including a pseudo-random component of delay to increase independence).
	- Use compiler intrinsic for IA64 TSC access:
		- Change to rdtscp and _mm_lfence()
	- Overall timing (including the additional hashing operation) is treated as an additional noise source.
	- Pseudo-random values are taken from a PRNG, not an ad hoc method.
		- Move to xoshiro256** so that this mechanism is usable by jent_loop_shuffle.
	- Simplify `mem` allocation:
		- Track the size of `mem` through its exponent (`2^memsize_exp` is allocated).
		- Make it possible to compile in a specific memory size request using the `JENT_MEMORY_SIZE_EXP` macro.
			- This compiled in default can be overridden by a flag.
		- Treat `JENT_MAX_MEMSIZE_*` macros as maximums and `JENT_MEMSIZE_*` as memory size requests.
			- The `JENT_MAX_MEMSIZE_*` flags are now handled as a fixed maximum size.
				- Previously, this value could be increased automatically in `jent_update_memsize_exp()`.
					- This increase wouldn't consistently cause an increased allocation.
				- Such increases now change the requested memory through use of a different `JENT_MEMSIZE_*` flag.
		- Remove 32kB flag (it seems unlikely that any modern CPU isn't going to cache such a region!) and add a 1GB flag.
		- Cause `jent_cache_size()` to return the cache size, rather than `jent_cache_size_roundup`, which returned the nearest power of 2 above the cache size.
		- By default (in the absence of guidance in flags or a compiled-in macro) allocate approximately 8*cache_size for the jent_memaccess() noise source.
			- This size forces most memory updates to resolve to RAM I/O.
			- This is consistent with the testing example presented.
2. Conditioning:
	- Make it more clear what data is primary noise source output vs additional noise source output vs supplemental data.
	- Split `jent_hash_time()` into two functions
		- `jent_hash_time()`: Processes the raw data from the primary noise source.
			- Data that is decimated and data from other sub-distributions is treated as supplemental data.
		- `jent_hash_additional()`: Processes the data from an additional noise source and supplemental data.
	- Remove ability to fix PRNG output.
		- The primary raw noise source data is not dominated by the result of PRNG output, so fixing this output is not necessary for analysis.
	- Create data using the PRNG rather than internal state for hashing in `jent_hash_additional()`.
		- The timing of this hash contributes to the additional noise source.
		- The result of the hash (the `intermediary` buffer) is fed into the conditioning function (as before) but now essentially provides a nonce.
3. Health tests:
	- Make a new health test to check if the specified sub-distribution has become too infrequent.
4. Testing scripts and tools:
	- jitterentropy-hashtime:
		- Remove extra tests (ec_min).
		- Store and output smaller binary data types
			- This reduces the memory impact in testing.
		- Print configuration and performance information.
	- jitterentropy-rng:
		- Print configuration and performance information.
	- Added a few scripts for testing.
5. Documentation:
	- Update testing/raw-entropy/README.md to provide a detailed rationale and assessment worked example.

## Pull Request Rationale
This set of changes increases both the level of security and assurance
over the base JEnt behavior.

1. The overall timing of the entropy source behavior no longer influences the raw output of the primary noise source (Note: this timing is still included as additional noise source output). This means that arbitrarily complex OS and other system behavior no longer leads to sets of overlapping of sub-distributions, any of which could possibly be made more common by an active attacker (and thus, each of which must be separately assessed when producing an entropy assessment).  As such, with this new behavior there is no longer any need to fully characterize the (likely very complicated) emergent system behavior.

2. With this new JEnt behavior the underlying variation that is being assessed is essentially governed by a small amount of hardware. As such, unrelated software changes are unlikely to significantly alter this behavior, so an assessment on a system using this new behavior is expected to be more durable than an assessment of the base JEnt behavior. In comparison, with the base JEnt behavior the observed emergent behavior could be significantly impacted by apparently unrelated software or loading changes.

3. Even the relatively simple memory I/O timing used by the primary noise source in the new JEnt behavior can resolve in a number of ways (predominantly L1 cache, L2 cache, L3 cache, and RAM I/O), each with their own sub-distribution.  In the updated JEnt behavior we are able to identify the specific sub-distribution that we are interested in and configure the noise source so that it only outputs values from this sub-distribution.  Once we have this relatively simpler timing distribution, health testing becomes dramatically more powerful and statistical assessment becomes dramatically more meaningful.  This approach reduces the need for data translation: in the worked example, no translation was required to reduce the symbol size. With the base JEnt behavior the tester is directed to analyze the lower byte of the data, essentially superimposing possibly dozens of distinct sub-distributions (each of which resulted from the timing differences experienced during different sets of events, each with their own associated timing distributions!) Such translation approaches are unlikely to yield meaningful assessments when using the SP 800-90B estimators, as consecutive raw data symbols may have been drawn from completely different sub-distributions, each of which may have quite different behavior.

4. The new JEnt behavior is much more conservative than can be configured in the base JEnt package. In the worked example, the setting `JENT_MEMORY_DEPTH_EXP = 11` leads to outputting one raw noise sample from the primary noise source per 3071.5 raw symbols sampled (on average), and for the worked example the entropy analysis supports the notion that there are 3.5 bits of min entropy per decimated output. This suggests that an undecimated data stream could instead be used so long as `osr >= 878` (as the worked example analysis supports an average claim of 1 bit of min entropy per 878 undecimated raw symbols).  The base JEnt library does not support `osr` settings greater than 20.

5. The new JEnt behavior provides a library that is at **at least as good** as the base JEnt behavior. The overall timing is still integrated into the conditioning function, it just isn't credited as providing entropy (it is considered output from an *additional noise source*). All data (including decimated data and data that is not from the identified sub-distribution) is similarly integrated as *supplemental data* provided to the conditioning function, but not credited as providing any entropy.

6. The underlying assumption of attacker non-predictability required to formally justify a particular security level with the new JEnt behavior applies in more circumstances than the corresponding assumption required for the base JEnt behavior.  The assumption underlying any specific entropy claim for a software noise source of similar design is this: the variation that was characterized does not have any unexpected patterns and is unpredictable to an attacker. More specifically: 
	1. We have identified a behavior present in this very large and complicated system.
	2. This behavior displays variations that we can't predict any better than suggested by entropy estimators we are using.  
	3. We assume that an attacker similarly can't predict this behavior more easily than we can.
	
	In the new JEnt behavior we have tried to limit the behavior that we are characterizing so that it is as much as possible established only by RAM I/O timings, a characteristic that has been observed as being unpredictable in many systems. Conversely, the raw data output from the base JEnt library is impacted by a wide variety of higher-level emergent timing behaviors, any of which could become dominant, and any of which may ultimately be more predictable than anticipated for a suitably well-informed active attacker.

I encourage anyone interested in this approach to review [Worked Example](https://github.com/joshuaehill/jitterentropy-library/blob/MemOnly/tests/raw-entropy/README.md).
