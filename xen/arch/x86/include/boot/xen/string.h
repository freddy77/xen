#ifndef BOOT__XEN__STRING_H
#define BOOT__XEN__STRING_H

#include <xen/types.h>	/* for size_t */

void *memset(void *s, int c, size_t n);
void *memcpy(void *dest, const void *src, size_t n);
void *memmove(void *dest, const void *src, size_t n);

#endif /* BOOT__XEN__STRING_H */
