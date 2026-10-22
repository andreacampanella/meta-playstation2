/* SPDX-License-Identifier: MIT */
/*
 * The arithmetic and the comparisons of fastdf in assembly: the common
 * path of __adddf3, __subdf3, __muldf3, __divdf3 and the six comparison
 * routines, written for the R5900 pipeline, which issues two
 * instructions a cycle when they do not depend on each other. About 50
 * instructions and one branch for an operation, 40 for a comparison.
 *
 * The o32 ABI with -msingle-float passes a double in two integer
 * registers: a in $4 (low word) and $5 (high word), b in $6 and $7. The
 * result goes in $2 (low) and $3 (high).
 *
 * The conversion is the one of fastdf.c (to_float, to_double), with
 * the same rules: a double too small for a float, zero and a denormal
 * give zero. A double too large for a float, infinity and NaN give the
 * largest float. The C versions of these routines are in fastdf.c,
 * under FASTDF_REFERENCE, and the two must agree bit for bit.
 *
 * The float exponent is the low 8 bits of the double exponent with bit 7
 * flipped (the bias changes by 896, which is 128 modulo 256), valid when
 * the double exponent is 897 to 1150. Below 897 the value is zero in a
 * float. Above 1150 it does not fit.
 */

#define EXP_MIN		897		/* the smallest double exponent a float has */
#define EXP_RANGE	254		/* 897 to 1150: a finite, nonzero float */
#define EXP_TOO_LARGE	1151
#define FLOAT_MAX_BITS	0x7f7fffff

	.set	noreorder
	.text

/*
 * Convert the two operands side by side, so that the pipeline can pair
 * the instructions of one with those of the other:
 *
 *   e = (hi >> 20) & 0x7ff                   the double exponent
 *   float = (((hi << 4) >> 1) | (lo >> 29)) ^ (128 << 23)
 *                                            valid for e in 897..1150
 *   float &= -(e - 897 < 254)                zero below 897
 *
 * Leaves the magnitude of a in $12 and of b in $13, without the sign,
 * the exponents in $8 and $9, and the sign words untouched in $5 and $7.
 * A magnitude too large for a float, infinity and NaN are zero here, and
 * TOO_LARGE makes them the largest float.
 */
#define CONVERT						\
	srl	$8, $5, 20;				\
	srl	$9, $7, 20;				\
	andi	$8, $8, 0x7ff;		/* the exponent of a */	\
	andi	$9, $9, 0x7ff;		/* the exponent of b */	\
	addiu	$10, $8, -EXP_MIN;			\
	addiu	$11, $9, -EXP_MIN;			\
	sltiu	$10, $10, EXP_RANGE;	/* 1: a is a normal float */ \
	sltiu	$11, $11, EXP_RANGE;			\
	negu	$10, $10;		/* the masks: all ones or zero */ \
	negu	$11, $11;				\
	sll	$12, $5, 4;				\
	sll	$13, $7, 4;				\
	srl	$12, $12, 1;				\
	srl	$13, $13, 1;				\
	srl	$4, $4, 29;				\
	srl	$6, $6, 29;				\
	or	$12, $12, $4;				\
	or	$13, $13, $6;				\
	lui	$14, 0x4000;		/* 128 << 23 */		\
	xor	$12, $12, $14;				\
	xor	$13, $13, $14;				\
	and	$12, $12, $10;				\
	and	$13, $13, $11;				\
	sltiu	$8, $8, EXP_TOO_LARGE;	/* 1: a fits a float */	\
	sltiu	$9, $9, EXP_TOO_LARGE;			\
	and	$15, $8, $9

/* The rare case: the largest float for an operand that does not fit. */
#define TOO_LARGE					\
	lui	$14, (FLOAT_MAX_BITS >> 16);		\
	ori	$14, $14, (FLOAT_MAX_BITS & 0xffff);	\
	addiu	$8, $8, -1;		/* all ones when too large */ \
	addiu	$9, $9, -1;				\
	and	$8, $8, $14;				\
	and	$9, $9, $14;				\
	or	$12, $12, $8;				\
	or	$13, $13, $9

/*
 * One operation: the conversions, the signs, the FPU, and the result
 * back to a double:
 *
 *   hi = sign | ((float & 0x7fffffff) >> 3) + (896 << 20)
 *   lo = float << 29
 *
 * with zero (an exponent field of zero) as the sign and nothing else.
 */
#define ARITH(name, op)					\
	.globl	name;					\
	.ent	name;					\
name:							\
	CONVERT;					\
	beqz	$15, 9f;				\
	 lui	$14, 0x8000;				\
2:	and	$5, $5, $14;		/* the sign of a */	\
	and	$7, $7, $14;		/* the sign of b */	\
	or	$12, $12, $5;				\
	or	$13, $13, $7;				\
	mtc1	$12, $f0;				\
	mtc1	$13, $f2;				\
	op	$f0, $f0, $f2;				\
	lui	$14, 0x3800;		/* 896 << 20 */		\
	mfc1	$2, $f0;				\
	nop;						\
	sll	$3, $2, 1;		/* the magnitude, times two */ \
	srl	$8, $3, 24;		/* the exponent field */ \
	beqz	$8, 8f;			/* zero: the sign alone */ \
	 srl	$3, $3, 4;		/* the magnitude >> 3 */ \
	addu	$3, $3, $14;				\
	srl	$8, $2, 31;				\
	sll	$8, $8, 31;		/* the sign */		\
	or	$3, $3, $8;				\
	jr	$31;					\
	 sll	$2, $2, 29;		/* the low word */	\
8:	srl	$3, $2, 31;				\
	sll	$3, $3, 31;		/* plus or minus zero */ \
	jr	$31;					\
	 move	$2, $0;					\
9:	TOO_LARGE;					\
	b	2b;					\
	 lui	$14, 0x8000;				\
	.end	name

/*
 * The comparison: the key of each operand as a float (the magnitude,
 * negated for a negative number, zero for both zeros), then
 * (a > b) - (a < b). The six libgcc comparison routines are this one.
 */
	.globl	__eqdf2
	.globl	__nedf2
	.globl	__ltdf2
	.globl	__ledf2
	.globl	__gtdf2
	.globl	__gedf2
	.ent	__ltdf2
__eqdf2:
__nedf2:
__ltdf2:
__ledf2:
__gtdf2:
__gedf2:
	CONVERT
	beqz	$15, 9f
	 sra	$5, $5, 31		/* all ones when a is negative */
2:	sra	$7, $7, 31
	xor	$12, $12, $5
	xor	$13, $13, $7
	subu	$12, $12, $5		/* the key of a: negated when negative */
	subu	$13, $13, $7		/* the key of b */
	slt	$2, $13, $12		/* 1: a > b */
	slt	$3, $12, $13		/* 1: a < b */
	jr	$31
	 subu	$2, $2, $3
9:	TOO_LARGE
	b	2b
	 sra	$5, $5, 31
	.end	__ltdf2

ARITH(__adddf3, add.s)
ARITH(__subdf3, sub.s)
ARITH(__muldf3, mul.s)
ARITH(__divdf3, div.s)
