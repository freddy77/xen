/* const.h: Macros for dealing with constants.  */

#ifndef __XEN_CONST_H__
#define __XEN_CONST_H__

/* Some constant macros are used in both assembler and
 * C code.  Therefore we cannot annotate them always with
 * 'UL' and other type specifiers unilaterally.  We
 * use the following macros to deal with this.
 *
 * Similarly, _AT() will cast an expression with a type in C, but
 * leave it unchanged in asm.
 */

#ifdef __ASSEMBLY__
#define _AC(X,Y)	X
#define _AT(T,X)	X
#define UINT64_C(X)     X
#define INT64_C(X)      X
#else
#define __AC(X,Y)	(X##Y)
#define _AC(X,Y)	__AC(X,Y)
#define _AT(T,X)	((T)(X))
#if __SIZEOF_LONG__ >= 8
#define UINT64_C(X)     X ## UL
#define INT64_C(X)      X ## L
#else
#define UINT64_C(X)     X ## ULL
#define INT64_C(X)      X ## LL
#endif
#endif

#define BIT(pos, sfx)   (_AC(1, sfx) << (pos))

#endif /* __XEN_CONST_H__ */
