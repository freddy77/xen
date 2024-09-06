/* SPDX-License-Identifier: GPL-2.0 */

#include <xen/compiler.h>
#include <xen/stdint.h>
#include <xen/config.h>
#define __XEN_TOOLS__ 1
#undef __XEN__
#include <public/arch-x86/xen.h>
#define __XEN__ 1
#include <xen/mm.h>

/* Disable no execute setting. */
#undef _PAGE_NX
#define _PAGE_NX 0

#if defined(__i386__)
void setup_pages32(void)
#elif defined (__x86_64__)
void setup_pages64(void)
#else
#error Unknow architecture
#endif
{
    unsigned int i;

    /*
     * Map Xen into the higher mappings, using 2M superpages.
     *
     * NB: We are currently in physical mode, so a RIP-relative relocation
     * against _start/_end result in our arbitrary placement by the bootloader
     * in memory, rather than the intended high mappings position.  Subtract
     * xen_phys_start to get the appropriate slots in l2_xenmap[].
     */
    for ( i =  l2_table_offset((unsigned long)_start   - xen_phys_start);
          i <= l2_table_offset((unsigned long)_end - 1 - xen_phys_start); ++i )
        l2_xenmap[i] =
            l2e_from_paddr(xen_phys_start + (i << L2_PAGETABLE_SHIFT),
                           PAGE_HYPERVISOR_RWX | _PAGE_PSE);

    /* Check that there is at least 4G of mapping space in l2_*map[] */
#ifndef __i386__
    BUILD_BUG_ON((sizeof(l2_bootmap)   / L2_PAGETABLE_ENTRIES) < 4);
    BUILD_BUG_ON((sizeof(l2_directmap) / L2_PAGETABLE_ENTRIES) < 4);
#endif

    /* Initialize L3 boot-map page directory entries. */
    for ( i = 0; i < 4; ++i )
        l3_bootmap[i] = l3e_from_paddr((unsigned long)l2_bootmap + i * PAGE_SIZE,
                                       __PAGE_HYPERVISOR);
    /*
     * Map Xen into the directmap (needed for early-boot pagetable
     * handling/walking), and identity map Xen into bootmap (needed for the
     * transition from the EFI pagetables to Xen), using 2M superpages.
     *
     * NB: We are currently in physical mode, so a RIP-relative relocation
     * against _start/_end gets their real position in memory, which are the
     * appropriate l2 slots to map.
     */
#define l2_4G_offset(a)                                                 \
    (((a) >> L2_PAGETABLE_SHIFT) & (4 * L2_PAGETABLE_ENTRIES - 1))

    for ( i  = l2_4G_offset((unsigned long)_start);
          i <= l2_4G_offset((unsigned long)_end - 1); ++i )
    {
        l2_pgentry_t pte = l2e_from_paddr(i << L2_PAGETABLE_SHIFT,
                                          __PAGE_HYPERVISOR | _PAGE_PSE);

        l2_bootmap[i] = pte;

        /* Bootmap RWX/Non-global.  Directmap RW/Global. */
        l2e_add_flags(pte, PAGE_HYPERVISOR);

        l2_directmap[i] = pte;
    }
#undef l2_4G_offset
}
