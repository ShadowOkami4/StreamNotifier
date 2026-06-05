#pragma once

#if defined(__clang__) && defined(_MSC_VER) && defined(_M_X64)
#include <intrin.h>

static inline unsigned __int64 stream_notifier_udiv128(unsigned __int64 high,
						       unsigned __int64 low,
						       unsigned __int64 divisor,
						       unsigned __int64 *remainder)
{
	unsigned __int64 quotient = 0;
	unsigned __int64 rem = high;

	for (int bit = 63; bit >= 0; --bit) {
		const bool carry = (rem >> 63) != 0;
		rem = (rem << 1) | ((low >> bit) & 1);

		if (carry || rem >= divisor) {
			rem -= divisor;
			quotient |= (1ULL << bit);
		}
	}

	if (remainder)
		*remainder = rem;

	return quotient;
}

#define _udiv128 stream_notifier_udiv128
#endif
