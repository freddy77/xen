/* SPDX-License-Identifier: GPL-2.0 */

#include <xen/compiler.h>
#include <xen/macros.h>
#include <xen/types.h>

#include <xen/kconfig.h>
#include <xen/stdint.h>

#define __XEN_TOOLS__ 1
#undef __XEN__
#include <public/arch-x86/xen.h>
#define __XEN__ 1

#include <xen/rangeset.h>
#include <asm/setup.h>
#include <asm/msr.h>
#include <asm/cpufeatureset.h>

#if defined(__i386__)
/* cmdline.c */
int strncmp(const char *cs, const char *ct, size_t count);
#endif

const char *detect_cpu(void)
{
    uint32_t eax = cpuid_eax(0x80000000U);
    uint32_t *caps = boot_cpu_data.x86_capability;
    uint32_t e1d_caps;

    /* Stash TSC to calculate a good approximation of time-since-boot */
    boot_tsc_stamp = rdtsc();

    caps[FEATURESET_1c] = cpuid_ecx(1);

    e1d_caps = 0;
    if ( (eax >> 16) == 0x8000 && eax > 0x80000000U )
        e1d_caps = cpuid_edx(0x80000001U);
    caps[FEATURESET_e1d] = e1d_caps;

#ifndef __x86_64__
    if ( !boot_cpu_has(X86_FEATURE_LM) )
        return "ERR: Not a 64-bit CPU!";

    if ( !boot_cpu_has(X86_FEATURE_NX) )
    {
        /*
         * NX appears to be unsupported, but it might be hidden.
         *
         * The feature is part of the AMD64 spec, but the very first Intel
         * 64bit CPUs lacked the feature, and thereafter there was a
         * firmware knob to disable the feature. Undo the disable if
         * possible.
         *
         * All 64bit Intel CPUs support this MSR. If virtualised, expect
         * the hypervisor to either emulate the MSR or give us NX.
         */
        char vendor[12];
        cpuid(0x00000000, &eax,
              (uint32_t *)&vendor[0],
              (uint32_t *)&vendor[8],
              (uint32_t *)&vendor[4]);
        if ( strncmp(vendor, "GenuineIntel", 12) == 0 )
        {
            uint32_t hi, lo;

            /* Clear the XD_DISABLE bit */
            rdmsr(MSR_IA32_MISC_ENABLE, lo, hi);
            if ( hi & 2 )
            {
                hi ^= 2;
                wrmsr(MSR_IA32_MISC_ENABLE, lo, hi);
                trampoline_misc_enable_off |= MSR_IA32_MISC_ENABLE_XD_DISABLE;

                /* Check again for NX */
                caps[FEATURESET_e1d] = cpuid_edx(0x80000001U);
            }
        }
    }
#endif

    /*
     * This check purposefully doesn't use cpu_has_nx because
     * cpu_has_nx bypasses the boot_cpu_data read if Xen was compiled
     * with CONFIG_REQUIRE_NX
     */
    if ( IS_ENABLED(CONFIG_REQUIRE_NX) &&
         !boot_cpu_has(X86_FEATURE_NX) )
        return "This build of Xen requires NX support";

    if ( cpu_has_nx )
        trampoline_efer |= EFER_NXE;

    return NULL;
}
