/* SPDX-License-Identifier: GPL-2.0 */

#include <xen/compiler.h>
#include <xen/macros.h>
#include <xen/types.h>
#include <xen/multiboot.h>
#include <xen/multiboot2.h>
#include <xen/page-size.h>

#include <asm/trampoline.h>

void reloc(uint32_t magic, uint32_t in);
const char *detect_cpu(void);
void setup_pages(void);
void reloc_trampoline(void);
void cmdline_parse_early(void);
void copy_trampoline(void);

const char *common_setup(uint32_t magic, uint32_t in, bool efi_platform)
{
    const char *err;

    reloc(magic, in);

    err = detect_cpu();
    if ( err )
        return err;

    setup_pages();

    /* Apply relocations to bootstrap trampoline. */
    reloc_trampoline();

    /* Do not parse command line on EFI platform here. */
    if ( !efi_platform )
        cmdline_parse_early();

    /* Copy bootstrap trampoline to low memory, below 1MB. */
    copy_trampoline();

    return err;
}

static const char *bios_setup_mb1(const multiboot_info_t *in, uint32_t *mem_lower)
{
    if ( in->flags & MBI_MEMLIMITS )
        *mem_lower = in->mem_lower;
    return NULL;
}

static const char *bios_setup_mb2(const multiboot2_fixed_t * mbi_fix, uint32_t *mem_lower)
{
    const multiboot2_tag_t *tag;

    /* Skip Multiboot2 information fixed part. */
    for ( tag = _p(ROUNDUP((unsigned long)(mbi_fix + 1), MULTIBOOT2_TAG_ALIGN));
          (void *)tag - (void *)mbi_fix < mbi_fix->total_size && tag->type != MULTIBOOT2_TAG_TYPE_END;
          tag = _p(ROUNDUP((unsigned long)tag + tag->size, MULTIBOOT2_TAG_ALIGN)) )
    {
        if ( tag->type == MULTIBOOT2_TAG_TYPE_EFI32 )
            return "ERR: EFI IA-32 platforms are not supported!";
        if ( tag->type == MULTIBOOT2_TAG_TYPE_EFI64 )
            return "ERR: Bootloader shutdown EFI x64 boot services!";
        if ( tag->type == MULTIBOOT2_TAG_TYPE_BASIC_MEMINFO )
            *mem_lower = ((const multiboot2_tag_basic_meminfo_t *)tag)->mem_lower;
    }
    return NULL;
}

const char *bios_setup(uint32_t magic, uint32_t in)
{
    uint32_t mem_lower = 0;
    uint32_t trampoline;
    const char *err = NULL;

    switch ( magic )
    {
    case MULTIBOOT_BOOTLOADER_MAGIC:
        err = bios_setup_mb1(_p(in), &mem_lower);
        break;
    case MULTIBOOT2_BOOTLOADER_MAGIC:
        err = bios_setup_mb2(_p(in), &mem_lower);
        break;
    default:
        err = "ERR: Not a Multiboot bootloader!";
        break;
    }

    if ( err )
        return err;

    /* Set up trampoline segment 64k below EBDA */
    trampoline = *(const uint16_t *)_p(0x40e);   /* EBDA segment */
    /* sanity check */
    if ( trampoline >= 0xa000 || trampoline < 0x4000 )
    {
        /* use base memory size on failure */
        trampoline = *(const uint16_t *)_p(0x413);
        trampoline <<= 10 - 4;
    }

    /*
     * Compare the value in the BDA with the information from the
     * multiboot structure (if available) and use the smallest.
     */
    if ( mem_lower >= 0x100 )
    {
        mem_lower <<= 10 - 4;
        /* compare with BDA value and use the smaller */
        if ( mem_lower < trampoline )
            trampoline = mem_lower;
    }

    /* Reserve memory for the trampoline and the low-memory stack. */
    trampoline -= (TRAMPOLINE_SPACE + TRAMPOLINE_STACK_SPACE) >> 4;

    /* From arch/x86/smpboot.c: start_eip had better be page-aligned! */
    trampoline_phys = (trampoline & -256) << 4;

    return err;
}
