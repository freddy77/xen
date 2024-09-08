/* SPDX-License-Identifier: GPL-2.0 */

#include <xen/efi.h>
#include <xen/init.h>
#include <xen/multiboot2.h>
#include <asm/asm_defns.h>
#include <asm/efibind.h>
#include <efi/efidef.h>
#include <efi/eficapsule.h>
#include <efi/eficon.h>
#include <efi/efidevp.h>
#include <efi/efiapi.h>

void __init efi_multiboot2(EFI_HANDLE ImageHandle,
                           EFI_SYSTEM_TABLE *SystemTable,
                           const char *cmdline);

const char * asmlinkage __init
efi_parse_mbi2(uint32_t magic, const multiboot2_fixed_t *mbi)
{
    const multiboot2_tag_t *tag;
    EFI_HANDLE ImageHandle = NULL;
    EFI_SYSTEM_TABLE *SystemTable = NULL;
    const char *cmdline = NULL;
    bool have_bs = false;

    if ( magic != MULTIBOOT2_BOOTLOADER_MAGIC )
        return "ERR: Not a Multiboot2 bootloader!";

    /* Skip Multiboot2 information fixed part. */
    tag = _p(ROUNDUP((unsigned long)(mbi + 1), MULTIBOOT2_TAG_ALIGN));

    for ( ; (const void *)tag - (const void *)mbi < mbi->total_size
            && tag->type != MULTIBOOT2_TAG_TYPE_END;
          tag = _p(ROUNDUP((unsigned long)((const void *)tag + tag->size),
                   MULTIBOOT2_TAG_ALIGN)) )
    {
        if ( tag->type == MULTIBOOT2_TAG_TYPE_EFI_BS )
            have_bs = true;
        else if ( tag->type == MULTIBOOT2_TAG_TYPE_EFI64 )
            SystemTable = _p(((const multiboot2_tag_efi64_t *)tag)->pointer);
        else if ( tag->type == MULTIBOOT2_TAG_TYPE_EFI64_IH )
            ImageHandle = _p(((const multiboot2_tag_efi64_ih_t *)tag)->pointer);
        else if ( tag->type == MULTIBOOT2_TAG_TYPE_CMDLINE )
            cmdline = ((const multiboot2_tag_string_t *)tag)->string;
    }

    if ( !have_bs )
        return "ERR: Bootloader shutdown EFI x64 boot services!";
    if ( !SystemTable )
        return "ERR: EFI SystemTable is not provided by bootloader!";
    if ( !ImageHandle )
        return "ERR: EFI ImageHandle is not provided by bootloader!";

    efi_multiboot2(ImageHandle, SystemTable, cmdline);

    return NULL;
}
