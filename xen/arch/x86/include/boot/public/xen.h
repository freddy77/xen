/* SPDX-License-Identifier: GPL-2.0-only */

/* This header allows the inclusion of public xen.h */

#ifndef BOOT__PUBLIC__XEN_H
#define BOOT__PUBLIC__XEN_H

#if !defined(__XEN__) || defined(__XEN_TOOLS__) || __XEN__ != 1
#error Unexpected defines
#endif

#include <xen/types.h>

#ifdef __i386__

# define __XEN_TOOLS__ 1
# undef __XEN__
# include <public/arch-x86/xen.h>
# define __XEN__ 1
# undef __XEN_TOOLS__

#else

# include <public/arch-x86/xen.h>

#endif

#endif /* BOOT__PUBLIC__XEN_H */
