/* Jitter RNG: Health Tests
 *
 * Copyright (C) 2021 - 2022, Joshua E. Hill <josh@keypair.us>
 * Copyright (C) 2021 - 2022, Stephan Mueller <smueller@chronox.de>
 *
 * License: see LICENSE file in root directory
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE, ALL OF
 * WHICH ARE HEREBY DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OF THIS SOFTWARE, EVEN IF NOT ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 */

#include "jitterentropy-health.h"

static jent_fips_failure_cb fips_cb = NULL;
static int jent_health_cb_switch_blocked = 0;

void jent_health_cb_block_switch(void)
{
	jent_health_cb_switch_blocked = 1;
}

int jent_set_fips_failure_callback_internal(jent_fips_failure_cb cb)
{
	if (jent_health_cb_switch_blocked)
		return -EAGAIN;
	fips_cb = cb;
	return 0;
}
/*
 * We target at least 80% of all observations being in the selected distribution,
 * but this may degrade somewhat in a properly functioning entropy source, due to
 * differences in the mix of activities of the device. As such, we allow this rate
 * to fall to 10% of all measurements in the expected distribution.
 *
 * The distribution health test operates on at least JENT_DIST_WINDOW observations,
 * but it may be more than this. This logic is structured to round down (until
 * there are at least JENT_DIST_WINDOW observations, the cutoff is rounded down to 0).
 * Under a binomial assumption this can be calculated using the following:
 * InverseCDF[BinomialDistribution[JENT_DIST_WINDOW, 1/10], 2^-40]
 *
 * The global cutoffs are calculated using various exponents, starting at 2^9
 * and proceeding to 2^20. The JENT_DIST_WINDOW_EXP should be in this range.
 * Smaller values (JENT_DIST_WINDOW_EXP<=8) result in a test that cannot fail,
 * and larger values delay a possible failure unreasonably in most patterns of use.
 *
 * This table provides these cutoffs (out of JENT_DIST_WINDOW), from
 * JENT_DIST_WINDOW_EXP=9 to JENT_DIST_WINDOW_EXP=20.
 */

#if (JENT_DIST_WINDOW_EXP < 9) || (JENT_DIST_WINDOW_EXP>20)
#error "JENT_DIST_WINDOW_EXP must be between 9 and 20."
#endif
static const uint64_t jent_dist_cutoff_lookup[12] =
        {11, 42, 116, 281, 635, 1374, 2901, 6019, 12348, 25138, 50904, 102699};

#define JENT_DIST_RUNNING_THRES(x) (((x) >> JENT_DIST_WINDOW_EXP)*jent_dist_cutoff_lookup[JENT_DIST_WINDOW_EXP-9])

void jent_dist_init(struct rand_data *ec)
{
	ec->current_data_count = 0;
	ec->current_in_dist_count = 0;
	ec->data_count_history = 0;
	ec->in_dist_count_history = 0;
#ifdef JENT_DIST_DIAG
	ec->preraw_lower_bound = UINT64_MAX;
	ec->preraw_lower_bound_error = UINT64_MAX;
	ec->preraw_lower_bound_average=0.0;
	ec->preraw_upper_bound = 0;
	ec->preraw_upper_bound_error = 0;
	ec->preraw_upper_bound_average=0.0;
	ec->dist_window_count = 0;
#endif
}

/**
 * Reset the dist counters
 *
 * @ec [in] Reference to entropy collector
 */
static void jent_dist_reset(struct rand_data *ec)
{
	ec->in_dist_count_history += ec->current_in_dist_count;
	ec->current_in_dist_count = 0;

	ec->data_count_history += ec->current_data_count;
	ec->current_data_count = 0;
}

#ifdef JENT_DIST_DIAG
int u64comp(const void* a, const void* b)
{
    uint64_t arg1 = *(const uint64_t*)a;
    uint64_t arg2 = *(const uint64_t*)b;

    if (arg1 < arg2) return -1;
    if (arg1 > arg2) return 1;
    return 0;
}
#endif

/**
 * The current result of the dist test
 *
 * @ec [in] Reference to entropy collector
 */
void jent_dist_test(struct rand_data *ec)
{
	/*
	 * If the proportion of observed values is outside the sought distribution
	 * too often, trigger a health test failure.
	 */
	if(ec->current_data_count >= JENT_DIST_WINDOW) {
#ifdef JENT_DIST_DIAG
		uint64_t cur_preraw_lb, cur_preraw_ub;
		ec->dist_window_count++;
		/*Find the middle 80% of the distribution.*/
		qsort(ec->preraw_history, JENT_DIST_WINDOW, sizeof(uint64_t), u64comp);
		cur_preraw_lb = ec->preraw_history[(JENT_DIST_WINDOW*1)/10];
		cur_preraw_ub = ec->preraw_history[(JENT_DIST_WINDOW*9)/10];

		/* Update the bounds. */
		if(cur_preraw_lb < ec->preraw_lower_bound) ec->preraw_lower_bound = cur_preraw_lb;
		if(cur_preraw_ub > ec->preraw_upper_bound) ec->preraw_upper_bound = cur_preraw_ub;

		/* Update the averages. */
		/* Note that x-bar_n 	= 1/n sum(x_j, j=1..n)
		 *			= 1/n ( sum(x_j, j=1..n-1) + x_n )
		 *			= 1/n ( (n-1) * x-bar_(n-1) + x_n )
		 *			= 1/n ( n*x-bar_(n-1) - x-bar_(n-1) + x_n )
		 *			= x-bar_(n-1) + 1/n ( x_n - x-bar_(n-1) )
		 */
		ec->preraw_lower_bound_average = ec->preraw_lower_bound_average + ((double)cur_preraw_lb - ec->preraw_lower_bound_average)/((double)ec->dist_window_count);
		ec->preraw_upper_bound_average = ec->preraw_upper_bound_average + ((double)cur_preraw_ub - ec->preraw_upper_bound_average)/((double)ec->dist_window_count);
#endif
		if(JENT_DIST_RUNNING_THRES(ec->current_data_count) > ec->current_in_dist_count) {
			ec->health_failure |= JENT_DIST_FAILURE;
#ifdef JENT_DIST_DIAG
			ec->preraw_lower_bound_error = cur_preraw_lb;
			ec->preraw_upper_bound_error = cur_preraw_ub;
#endif
		}

		jent_dist_reset(ec);
	}
}

/**
 * Insert a new entropy event into the dist test
 *
 * @ec [in] Reference to entropy collector
 * @current_delta [in] Current time delta
 */
unsigned int jent_dist_insert(struct rand_data *ec, uint64_t current_delta)
{
#ifdef JENT_DIST_DIAG
        /* Save the pre-raw history in a circular buffer. */
	ec->preraw_history[(ec->current_data_count) & JENT_DIST_MASK] = current_delta;
#endif

	ec->current_data_count++;
	/* Is this in the reference distribution? */
	if((current_delta >= ec->distribution_min) && (current_delta <= ec->distribution_max)) {
		ec->current_in_dist_count++;
		return 1U;
	} else {
		return 0U;
	}
}

/***************************************************************************
 * Lag Predictor Test
 *
 * This test is a vendor-defined conditional test that is designed to detect
 * a known failure mode where the result becomes mostly deterministic
 * Note that (lag_observations & JENT_LAG_MASK) is the index where the next
 * value provided will be stored.
 ***************************************************************************/

#ifdef JENT_HEALTH_LAG_PREDICTOR

/*
 * These cutoffs are configured using an entropy estimate of 1/osr under an
 * alpha=2^(-22) for a window size of 131072. The other health tests use
 * alpha=2^-30, but operate on much smaller window sizes. This larger selection
 * of alpha makes the behavior per-lag-window similar to the APT test.
 *
 * The global cutoffs are calculated using the
 * InverseBinomialCDF(n=(JENT_LAG_WINDOW_SIZE-JENT_LAG_HISTORY_SIZE), p=2^(-1/osr); 1-alpha)
 * The local cutoffs are somewhat more complicated. For background, see Feller's
 * _Introduction to Probability Theory and It's Applications_ Vol. 1,
 * Chapter 13, section 7 (in particular see equation 7.11, where x is a root
 * of the denominator of equation 7.6).
 *
 * We'll proceed using the notation of SP 800-90B Section 6.3.8 (which is
 * developed in Kelsey-McKay-Turan paper "Predictive Models for Min-entropy
 * Estimation".)
 *
 * Here, we set p=2^(-1/osr), seeking a run of successful guesses (r) with
 * probability of less than (1-alpha). That is, it is very very likely
 * (probability 1-alpha) that there is _no_ run of length r in a block of size
 * JENT_LAG_WINDOW_SIZE-JENT_LAG_HISTORY_SIZE.
 *
 * We have to iteratively look for an appropriate value for the cutoff r.
 */
static const unsigned int jent_lag_global_cutoff_lookup[20] =
	{ 66443,  93504, 104761, 110875, 114707, 117330, 119237, 120686, 121823,
	 122739, 123493, 124124, 124660, 125120, 125520, 125871, 126181, 126457,
	 126704, 126926 };
static const unsigned int jent_lag_local_cutoff_lookup[20] =
	{  38,  75, 111, 146, 181, 215, 250, 284, 318, 351,
	  385, 419, 452, 485, 518, 551, 584, 617, 650, 683 };

void jent_lag_init(struct rand_data *ec, unsigned int osr)
{
	/*
	 * Establish the lag global and local cutoffs based on the presumed
	 * entropy rate of 1/osr.
	 */
	if (osr > ARRAY_SIZE(jent_lag_global_cutoff_lookup)) {
		ec->lag_global_cutoff =
			jent_lag_global_cutoff_lookup[
				ARRAY_SIZE(jent_lag_global_cutoff_lookup) - 1];
	} else {
		ec->lag_global_cutoff = jent_lag_global_cutoff_lookup[osr - 1];
	}

	if (osr > ARRAY_SIZE(jent_lag_local_cutoff_lookup)) {
		ec->lag_local_cutoff =
			jent_lag_local_cutoff_lookup[
				ARRAY_SIZE(jent_lag_local_cutoff_lookup) - 1];
	} else {
		ec->lag_local_cutoff = jent_lag_local_cutoff_lookup[osr - 1];
	}
}

/**
 * Reset the lag counters
 *
 * @ec [in] Reference to entropy collector
 */
static void jent_lag_reset(struct rand_data *ec)
{
	unsigned int i;

	/* Reset Lag counters */
	ec->lag_prediction_success_count = 0;
	ec->lag_prediction_success_run = 0;
	ec->lag_best_predictor = 0; /* The first guess is basically arbitrary. */
	ec->lag_observations = 0;

	for (i = 0; i < JENT_LAG_HISTORY_SIZE; i++) {
		ec->lag_scoreboard[i] = 0;
		ec->lag_delta_history[i] = 0;
	}
}

/*
 * A macro for accessing the history. Index 0 is the last observed symbol
 * index 1 is the symbol observed two inputs ago, etc.
 */
#define JENT_LAG_HISTORY(EC,LOC)					       \
	((EC)->lag_delta_history[((EC)->lag_observations - (LOC) - 1) &	       \
	 JENT_LAG_MASK])

/**
 * Insert a new entropy event into the lag predictor test
 *
 * @ec [in] Reference to entropy collector
 * @current_delta [in] Current time delta
 */
static void jent_lag_insert(struct rand_data *ec, uint64_t current_delta)
{
	uint64_t prediction;
	unsigned int i;

	/* Initialize the delta_history */
	if (ec->lag_observations < JENT_LAG_HISTORY_SIZE) {
		ec->lag_delta_history[ec->lag_observations] = current_delta;
		ec->lag_observations++;
		return;
	}

	/*
	 * The history is initialized. First make a guess and examine the
	 * results.
	 */
	prediction = JENT_LAG_HISTORY(ec, ec->lag_best_predictor);

	if (prediction == current_delta) {
		/* The prediction was correct. */
		ec->lag_prediction_success_count++;
		ec->lag_prediction_success_run++;

		if ((ec->lag_prediction_success_run >= ec->lag_local_cutoff) ||
		    (ec->lag_prediction_success_count >= ec->lag_global_cutoff))
			ec->health_failure |= JENT_LAG_FAILURE;
	} else {
		/* The prediction wasn't correct. End any run of successes.*/
		ec->lag_prediction_success_run = 0;
	}

	/* Now update the predictors using the current data. */
	for (i = 0; i < JENT_LAG_HISTORY_SIZE; i++) {
		if (JENT_LAG_HISTORY(ec, i) == current_delta) {
			/*
			 * The ith predictor (which guesses i + 1 symbols in
			 * the past) successfully guessed.
			 */
			ec->lag_scoreboard[i] ++;

			/*
			 * Keep track of the best predictor (tie goes to the
			 * shortest lag)
			 */
			if (ec->lag_scoreboard[i] >
			    ec->lag_scoreboard[ec->lag_best_predictor])
				ec->lag_best_predictor = i;
		}
	}

	/*
	 * Finally, update the lag_delta_history array with the newly input
	 * value.
	 */
	ec->lag_delta_history[(ec->lag_observations) & JENT_LAG_MASK] =
								current_delta;
	ec->lag_observations++;

	/*
	 * lag_best_predictor now is the index of the predictor with the largest
	 * number of correct guesses.
	 * This establishes our next guess.
	 */

	/* Do we now need a new window? */
	if (ec->lag_observations >= JENT_LAG_WINDOW_SIZE)
		jent_lag_reset(ec);
}

static inline uint64_t jent_delta2(struct rand_data *ec, uint64_t current_delta)
{
	/* Note that delta2_n = delta_n - delta_{n-1} */
	return jent_delta(JENT_LAG_HISTORY(ec, 0), current_delta);
}

static inline uint64_t jent_delta3(struct rand_data *ec, uint64_t delta2)
{
	/*
	 * Note that delta3_n = delta2_n - delta2_{n-1}
	 *		      = delta2_n - (delta_{n-1} - delta_{n-2})
	 */
	return jent_delta(jent_delta(JENT_LAG_HISTORY(ec, 1),
				     JENT_LAG_HISTORY(ec, 0)), delta2);
}

#else /* JENT_HEALTH_LAG_PREDICTOR */

static inline void jent_lag_insert(struct rand_data *ec, uint64_t current_delta)
{
	(void)ec;
	(void)current_delta;
}

static inline uint64_t jent_delta2(struct rand_data *ec, uint64_t current_delta)
{
	uint64_t delta2 = jent_delta(ec->last_delta, current_delta);

	ec->last_delta = current_delta;
	return delta2;
}

static inline uint64_t jent_delta3(struct rand_data *ec, uint64_t delta2)
{
	uint64_t delta3 = jent_delta(ec->last_delta2, delta2);

	ec->last_delta2 = delta2;
	return delta3;
}

#endif /* JENT_HEALTH_LAG_PREDICTOR */

/***************************************************************************
 * Adaptive Proportion Test
 *
 * This test complies with SP800-90B section 4.4.2.
 ***************************************************************************/

/*
 * See the SP 800-90B comment #10b for the corrected cutoff for the SP 800-90B
 * APT.
 * http://www.untruth.org/~josh/sp80090b/UL%20SP800-90B-final%20comments%20v1.9%2020191212.pdf
 * In in the syntax of R, this is C = 2 + qbinom(1 − 2^(−30), 511, 2^(-1/osr)).
 * (The original formula wasn't correct because the first symbol must
 * necessarily have been observed, so there is no chance of observing 0 of these
 * symbols.)
 *
 * For any value above 14, this yields the maximal allowable value of 512
 * (by FIPS 140-2 IG 7.19 Resolution # 16, we cannot choose a cutoff value that
 * renders the test unable to fail).
 */
static const unsigned int jent_apt_cutoff_lookup[15]=
	{ 325, 422, 459, 477, 488, 494, 499, 502,
	  505, 507, 508, 509, 510, 511, 512 };

void jent_apt_init(struct rand_data *ec, unsigned int osr)
{
	/*
	 * Establish the apt_cutoff based on the presumed entropy rate of
	 * 1/osr.
	 */
	if (osr >= ARRAY_SIZE(jent_apt_cutoff_lookup)) {
		ec->apt_cutoff = jent_apt_cutoff_lookup[
					ARRAY_SIZE(jent_apt_cutoff_lookup) - 1];
	} else {
		ec->apt_cutoff = jent_apt_cutoff_lookup[osr - 1];
	}
}

/**
 * Reset the APT counter
 *
 * @ec [in] Reference to entropy collector
 */
static void jent_apt_reset(struct rand_data *ec)
{
	/* When reset, accept the _next_ value input as the new base. */
	ec->apt_base_set = 0;
}

/**
 * Insert a new entropy event into APT
 *
 * @ec [in] Reference to entropy collector
 * @current_delta [in] Current time delta
 */
static void jent_apt_insert(struct rand_data *ec, uint64_t current_delta)
{
	/* Initialize the base reference */
	if (!ec->apt_base_set) {
		ec->apt_base = current_delta;	/* APT Step 1 */
		ec->apt_base_set = 1;		/* APT Step 2 */

		/*
		 * Reset APT counter
		 * Note that we've taken in the first symbol in the window.
		 */
		ec->apt_count = 1;		/* B = 1 */
		ec->apt_observations = 1;

		return;
	}

	if (current_delta == ec->apt_base) {
		ec->apt_count++;		/* B = B + 1 */

		/* Note, ec->apt_count starts with one. */
		if (ec->apt_count >= ec->apt_cutoff)
			ec->health_failure |= JENT_APT_FAILURE;
	}

	ec->apt_observations++;

	/* Completed one window, the next symbol input will be new apt_base. */
	if (ec->apt_observations >= JENT_APT_WINDOW_SIZE)
		jent_apt_reset(ec);		/* APT Step 4 */
}

/***************************************************************************
 * Stuck Test and its use as Repetition Count Test
 *
 * The Jitter RNG uses an enhanced version of the Repetition Count Test
 * (RCT) specified in SP800-90B section 4.4.1. Instead of counting identical
 * back-to-back values, the input to the RCT is the counting of the stuck
 * values during the generation of one Jitter RNG output block.
 *
 * The RCT is applied with an alpha of 2^{-30} compliant to FIPS 140-2 IG 9.8.
 *
 * During the counting operation, the Jitter RNG always calculates the RCT
 * cut-off value of C. If that value exceeds the allowed cut-off value,
 * the Jitter RNG output block will be calculated completely but discarded at
 * the end. The caller of the Jitter RNG is informed with an error code.
 ***************************************************************************/

/**
 * Repetition Count Test as defined in SP800-90B section 4.4.1
 *
 * @ec [in] Reference to entropy collector
 * @stuck [in] Indicator whether the value is stuck
 */
static void jent_rct_insert(struct rand_data *ec, int stuck)
{
	/*
	 * If we have a count less than zero, a previous RCT round identified
	 * a failure. We will not overwrite it.
	 */
	if (ec->rct_count < 0)
		return;

	if (stuck) {
		ec->rct_count++;

		/*
		 * The cutoff value is based on the following consideration:
		 * alpha = 2^-30 as recommended in FIPS 140-2 IG 9.8.
		 * In addition, we require an entropy value H of 1/osr as this
		 * is the minimum entropy required to provide full entropy.
		 * Note, we collect (DATA_SIZE_BITS + ENTROPY_SAFETY_FACTOR)*osr
		 * deltas for inserting them into the conditioning function which should
		 * then have (close to) DATA_SIZE_BITS bits of entropy in the
		 * conditioned output.
		 *
		 * Note, ec->rct_count (which equals to value B in the pseudo
		 * code of SP800-90B section 4.4.1) starts with zero. Hence
		 * we need to subtract one from the cutoff value as calculated
		 * following SP800-90B. Thus C = ceil(-log_2(alpha)/H) = 30*osr.
		 */
		if ((unsigned int)ec->rct_count >= (30 * ec->osr)) {
			ec->rct_count = -1;
			ec->health_failure |= JENT_RCT_FAILURE;
		}
	} else {
		ec->rct_count = 0;
	}
}

/**
 * Stuck test by checking the:
 * 	1st derivative of the jitter measurement (time delta)
 * 	2nd derivative of the jitter measurement (delta of time deltas)
 * 	3rd derivative of the jitter measurement (delta of delta of time deltas)
 *
 * All values must always be non-zero.
 *
 * @ec [in] Reference to entropy collector
 * @current_delta [in] Jitter time delta
 *
 * @return
 * 	0 jitter measurement not stuck (good bit)
 * 	1 jitter measurement stuck (reject bit)
 */
unsigned int jent_stuck(struct rand_data *ec, uint64_t current_delta)
{
	uint64_t delta2 = jent_delta2(ec, current_delta);
	uint64_t delta3 = jent_delta3(ec, delta2);

	/*
	 * Insert the result of the comparison of two back-to-back time
	 * deltas.
	 */
	jent_apt_insert(ec, current_delta);
	jent_lag_insert(ec, current_delta);

	if (!current_delta || !delta2 || !delta3) {
		/* RCT with a stuck bit */
		jent_rct_insert(ec, 1);
		return 1;
	}

	/* RCT with a non-stuck bit */
	jent_rct_insert(ec, 0);

	return 0;
}

/**
 * Report any health test failures
 *
 * @ec [in] Reference to entropy collector
 *
 * @return a bitmask indicating which tests failed
 * 	0 No health test failure
 * 	1 RCT failure
 * 	2 APT failure
 * 	4 Lag predictor test failure
 */
unsigned int jent_health_failure(struct rand_data *ec)
{
	/* Test is only enabled in FIPS mode */
	if (!ec->fips_enabled)
		return 0;

	if (fips_cb && ec->health_failure) {
		fips_cb(ec, ec->health_failure);
	}


	return ec->health_failure;
}
