// SPDX-License-Identifier: MIT
/*
 * Fast doubles for the R5900: the libgcc software double routines, done
 * on the FPU in single precision.
 *
 * The R5900 FPU has no double format. GCC compiles each operation on a
 * double into a call to a libgcc routine (__muldf3, __adddf3, ...), which
 * does IEEE 754 arithmetic with integer instructions in 300 to 1,000
 * cycles. Game code is full of doubles by habit, and a profile of Duke
 * Nukem 3D (Polymost) spends half of its time in those routines.
 *
 * This library defines the same routines. Each one takes the bits of
 * the doubles, makes floats of them (a few shifts), does the operation
 * on the FPU, and makes a double of the result. The values keep the
 * size, the alignment and the calling convention of a double, so no
 * source, struct or file format changes. The precision is 24 bits
 * instead of 53. A program links it with
 *
 *   -Wl,--whole-archive -l:libfastdf.a -Wl,--no-whole-archive
 *
 * before libgcc (the recipe class ps2-fastdf does that). The shared
 * libraries (libm, libc) keep their exact doubles.
 *
 * The rules of the R5900 FPU apply to the results: no NaN, no infinity,
 * no denormals, and rounding toward zero. A double too large for a float
 * becomes the largest float, a double too small becomes zero.
 *
 * Not for a program that needs more than 24 bits: a clock in seconds
 * since 1970 (resolution 128 s), coordinates above 16 million with a
 * fraction, or an accumulation of many small terms.
 *
 * The routines work on the two words of a double, not on the 64-bit
 * value: the o32 ABI passes a double in two registers, and the compiler
 * makes better code of two words than of a long long. The common path
 * of the four arithmetic operations and of the comparisons is in
 * fastdf-arith.S, written for the pipeline. The comparisons never touch
 * the FPU: a float without NaN compares as a sign-magnitude integer.
 */

#include <stdint.h>

typedef union {
	double d;
	struct {
		uint32_t lo;	/* little-endian: the low word first */
		uint32_t hi;
	} w;
} dbits;

typedef union {
	float f;
	uint32_t u;
} fbits;

/* Exponent biases: 1023 for a double, 127 for a float */
#define EXP_SHIFT		896	/* 1023 - 127 */
#define EXP_MIN			(EXP_SHIFT + 1)	/* the smallest double exponent a float has */
#define EXP_RANGE		254	/* float exponents 1 to 254 are finite, nonzero */
#define FLOAT_MAX_BITS		0x7f7fffffu

/*
 * The float with the value of a double. Truncates the mantissa (toward
 * zero). The exponent of a double has 11 bits and the one of a float 8.
 * The shift by 4 then 1 drops the sign and the top three exponent bits,
 * and puts the low 8 bits of the double exponent where the float
 * exponent goes. The bias changes by 896, which is 128 modulo 256: a
 * flip of bit 7 of those 8 bits, one XOR, gives the float exponent when
 * the value is in range. The range test is one unsigned compare.
 */
static inline float to_float(uint32_t hi, uint32_t lo)
{
	const uint32_t sign = (hi >> 31) << 31;
	const uint32_t exp = (hi >> 20) & 0x7ffu;
	uint32_t bits = (((hi << 4) >> 1) | (lo >> 29)) ^ (128u << 23);
	fbits out;

	if (exp - EXP_MIN >= EXP_RANGE) {
		/* Zero, a denormal, too small: zero. Too large, infinity, NaN:
		   the largest float. */
		bits = exp > EXP_MIN ? FLOAT_MAX_BITS : 0;
	}
	out.u = sign | bits;
	return out.f;
}

/*
 * The double with the value of a float. Exact. A float with a zero
 * exponent field is zero: the FPU makes no denormals, and one from
 * elsewhere becomes zero here, as it would in the FPU.
 */
static inline double to_double(float f)
{
	fbits in;
	dbits out;

	in.f = f;
	if ((in.u & 0x7f800000u) == 0) {
		out.w.hi = in.u & 0x80000000u;	/* plus or minus zero */
		out.w.lo = 0;
	} else {
		out.w.hi = (in.u & 0x80000000u)
			| (((in.u & 0x7fffffffu) >> 3) + (uint32_t)(EXP_SHIFT << 20));
		out.w.lo = in.u << 29;
	}
	return out.d;
}

/* --- arithmetic ---------------------------------------------------------- */

/*
 * The four operations are in fastdf-arith.S. These are the same
 * operations in C, for reading, and for a check of the assembly: a
 * build with -DFASTDF_REFERENCE defines them under the libgcc names.
 */
#ifdef FASTDF_REFERENCE
#define ARITH(name, op) \
	double name(double a, double b) \
	{ \
		dbits x, y; \
		x.d = a; \
		y.d = b; \
		return to_double(to_float(x.w.hi, x.w.lo) op to_float(y.w.hi, y.w.lo)); \
	}

ARITH(__adddf3, +)
ARITH(__subdf3, -)
ARITH(__muldf3, *)
ARITH(__divdf3, /)
#endif

double __negdf2(double a)
{
	dbits v;

	v.d = a;
	v.w.hi ^= 0x80000000u;
	return v.d;
}

/* --- comparisons --------------------------------------------------------- */

/*
 * libgcc returns an int for the compiler to test: below zero for a < b,
 * zero for equal, above zero for a > b. Without NaN every pair is ordered,
 * so all the comparison routines are the same function.
 *
 * The comparison is the one of the two floats, so that a value from the
 * FPU and the literal it came from still compare equal: a double literal
 * has bits below the float precision, and the FPU result has not. The
 * magnitude of a float is an integer in the order of the values, and
 * the negated magnitude is a key for a negative float. Both zeros give
 * the key zero, so they compare equal.
 *
 * The comparison is in fastdf-arith.S too. This is the same comparison
 * in C, under FASTDF_REFERENCE.
 */
#ifdef FASTDF_REFERENCE
static inline int32_t float_key(float f)
{
	fbits in;
	int32_t magnitude;

	in.f = f;
	magnitude = (int32_t)(in.u & 0x7fffffffu);
	return (int32_t)in.u < 0 ? -magnitude : magnitude;
}

static inline int compare(double a, double b)
{
	dbits x, y;
	int32_t kx, ky;

	x.d = a;
	y.d = b;
	kx = float_key(to_float(x.w.hi, x.w.lo));
	ky = float_key(to_float(y.w.hi, y.w.lo));
	return (kx > ky) - (kx < ky);
}

int __eqdf2(double a, double b) { return compare(a, b); }
int __nedf2(double a, double b) { return compare(a, b); }
int __ltdf2(double a, double b) { return compare(a, b); }
int __ledf2(double a, double b) { return compare(a, b); }
int __gtdf2(double a, double b) { return compare(a, b); }
int __gedf2(double a, double b) { return compare(a, b); }
#endif

/* Unordered: 1 if either is NaN. The FPU has no NaN. */
int __unorddf2(double a, double b)
{
	(void)a;
	(void)b;
	return 0;
}

/* --- conversions --------------------------------------------------------- */

double __extendsfdf2(float f)
{
	return to_double(f);
}

float __truncdfsf2(double d)
{
	dbits v;

	v.d = d;
	return to_float(v.w.hi, v.w.lo);
}

int __fixdfsi(double d)
{
	dbits v;

	v.d = d;
	return (int)to_float(v.w.hi, v.w.lo);
}

unsigned int __fixunsdfsi(double d)
{
	dbits v;

	v.d = d;
	return (unsigned int)to_float(v.w.hi, v.w.lo);
}

long long __fixdfdi(double d)
{
	dbits v;

	v.d = d;
	return (long long)to_float(v.w.hi, v.w.lo);
}

unsigned long long __fixunsdfdi(double d)
{
	dbits v;

	v.d = d;
	return (unsigned long long)to_float(v.w.hi, v.w.lo);
}

double __floatsidf(int i)
{
	return to_double((float)i);
}

double __floatunsidf(unsigned int i)
{
	return to_double((float)i);
}

double __floatdidf(long long i)
{
	return to_double((float)i);
}

double __floatundidf(unsigned long long i)
{
	return to_double((float)i);
}
