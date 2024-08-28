/* SPDX-License-Identifier: GPL-2.0 */

#include <xen/compiler.h>
#include <xen/stdint.h>
#include <asm/trampoline.h>

extern const int32_t __trampoline_rel_start[], __trampoline_rel_stop[];
extern const int32_t __trampoline_seg_start[], __trampoline_seg_stop[];

#if defined(__i386__)
void reloc_trampoline32(void)
#elif defined (__x86_64__)
void reloc_trampoline64(void)
#else
#error Unknow architecture
#endif
{
    unsigned long phys = trampoline_phys;
    const int32_t *trampoline_ptr;

    /* Apply relocations to trampoline. */
    for ( trampoline_ptr = __trampoline_rel_start;
          trampoline_ptr < __trampoline_rel_stop;
          ++trampoline_ptr )
        *(uint32_t *)(*trampoline_ptr + (long)trampoline_ptr) += phys;
    for ( trampoline_ptr = __trampoline_seg_start;
          trampoline_ptr < __trampoline_seg_stop;
          ++trampoline_ptr )
        *(uint16_t *)(*trampoline_ptr + (long)trampoline_ptr) = phys >> 4;
}
