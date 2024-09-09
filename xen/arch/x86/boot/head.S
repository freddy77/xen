#include <xen/lib.h>
#include <xen/multiboot.h>
#include <xen/multiboot2.h>
#include <public/xen.h>
#include <asm/asm_defns.h>
#include <asm/fixmap.h>
#include <asm/page.h>
#include <asm/processor.h>
#include <asm/msr-index.h>
#include <asm/cpufeature.h>
#include <public/elfnote.h>

        .section .text.header, "ax", @progbits
        .code32

#define sym_offs(sym)     ((sym) - __XEN_VIRT_START)
#define sym_esi(sym)      sym_offs(sym)(%esi)

#define BOOT_CS32        0x0008
#define BOOT_CS64        0x0010
#define BOOT_DS          0x0018
#define BOOT_PSEUDORM_CS 0x0020
#define BOOT_PSEUDORM_DS 0x0028

#define MB2_HT(name)      (MULTIBOOT2_HEADER_TAG_##name)
#define MB2_TT(name)      (MULTIBOOT2_TAG_TYPE_##name)

#define BOOT_TYPE_BIOS 1
#define BOOT_TYPE_PVH 2

        .macro mb2ht_args arg:req, args:vararg
        .long \arg
        .ifnb \args
        mb2ht_args \args
        .endif
        .endm

        .macro mb2ht_init type:req, req:req, args:vararg
        .balign MULTIBOOT2_TAG_ALIGN, 0xc2 /* Avoid padding with long nops. */
.Lmb2ht_init_start\@:
        .short \type
        .short \req
        .long .Lmb2ht_init_end\@ - .Lmb2ht_init_start\@
        .ifnb \args
        mb2ht_args \args
        .endif
.Lmb2ht_init_end\@:
        .endm

ENTRY(start)
        jmp     __start

        .balign 4
multiboot1_header:             /*** MULTIBOOT1 HEADER ****/
#define MULTIBOOT_HEADER_FLAGS (MULTIBOOT_HEADER_MODS_ALIGNED | \
                                MULTIBOOT_HEADER_WANT_MEMORY)
        /* Magic number indicating a Multiboot header. */
        .long   MULTIBOOT_HEADER_MAGIC
        /* Flags to bootloader (see Multiboot spec). */
        .long   MULTIBOOT_HEADER_FLAGS
        /* Checksum: must be the negated sum of the first two fields. */
        .long   -(MULTIBOOT_HEADER_MAGIC + MULTIBOOT_HEADER_FLAGS)

        .size multiboot1_header, . - multiboot1_header
        .type multiboot1_header, @object

/*** MULTIBOOT2 HEADER ****/
/* Some ideas are taken from grub-2.00/grub-core/tests/boot/kernel-i386.S file. */
        .balign MULTIBOOT2_HEADER_ALIGN, 0xc2  /* Avoid padding the MB1 header with long nops. */

multiboot2_header:
        /* Magic number indicating a Multiboot2 header. */
        .long   MULTIBOOT2_HEADER_MAGIC
        /* Architecture: i386. */
        .long   MULTIBOOT2_ARCHITECTURE_I386
        /* Multiboot2 header length. */
        .long   .Lmultiboot2_header_end - multiboot2_header
        /* Multiboot2 header checksum. */
        .long   -(MULTIBOOT2_HEADER_MAGIC + MULTIBOOT2_ARCHITECTURE_I386 + \
                        (.Lmultiboot2_header_end - multiboot2_header))

        /* Multiboot2 information request tag. */
        mb2ht_init MB2_HT(INFORMATION_REQUEST), MB2_HT(REQUIRED), \
                   MB2_TT(BASIC_MEMINFO), MB2_TT(MMAP)

        /* Align modules at page boundry. */
        mb2ht_init MB2_HT(MODULE_ALIGN), MB2_HT(REQUIRED)

        /* Load address preference. */
        mb2ht_init MB2_HT(RELOCATABLE), MB2_HT(OPTIONAL), \
                   sym_offs(start), /* Min load address. */ \
                   0xffffffff, /* The end of image max load address (4 GiB - 1). */ \
                   0x200000, /* Load address alignment (2 MiB). */ \
                   MULTIBOOT2_LOAD_PREFERENCE_HIGH

        /* Console flags tag. */
        mb2ht_init MB2_HT(CONSOLE_FLAGS), MB2_HT(OPTIONAL), \
                   MULTIBOOT2_CONSOLE_FLAGS_EGA_TEXT_SUPPORTED

        /* Framebuffer tag. */
        mb2ht_init MB2_HT(FRAMEBUFFER), MB2_HT(OPTIONAL), \
                   0, /* Number of the columns - no preference. */ \
                   0, /* Number of the lines - no preference. */ \
                   0  /* Number of bits per pixel - no preference. */

        /* Request that ExitBootServices() not be called. */
        mb2ht_init MB2_HT(EFI_BS), MB2_HT(OPTIONAL)

        /* EFI64 Multiboot2 entry point. */
        mb2ht_init MB2_HT(ENTRY_ADDRESS_EFI64), MB2_HT(OPTIONAL), \
                   sym_offs(__efi64_mb2_start)

        /* Multiboot2 header end tag. */
        mb2ht_init MB2_HT(END), MB2_HT(REQUIRED)
.Lmultiboot2_header_end:

        .size multiboot2_header, . - multiboot2_header
        .type multiboot2_header, @object

        .section .init.data, "aw", @progbits
        .align 4

        .word   0
gdt_boot_descr:
        .word   .Ltrampoline_gdt_end - trampoline_gdt - 1
gdt_boot_base:
        .long   sym_offs(trampoline_gdt)
        .long   0 /* Needed for 64-bit lgdt */

vga_text_buffer:
        .long   0xb8000

efi_platform:
        .byte   0

        .section .init.text, "ax", @progbits

early_error: /* Here to improve the disassembly. */

.Ldirect_error:
        mov     sym_esi(vga_text_buffer), %edi
        cmpb    $1, (%eax)
        jne     1f
        inc     %eax
        xor     %edi, %edi
1:
        mov     %eax, %esi
        # NOTE: No further use of sym_esi() till the end of the "function"!

        lodsb
        test    %al,%al        # Terminate on '\0' sentinel
        je      .Lhalt
        mov     $0x3f8+5,%dx   # UART Line Status Register
        mov     %al,%bl
2:      in      %dx,%al
        test    $0x20,%al      # Test THR Empty flag
        je      2b
        mov     $0x3f8+0,%dx   # UART Transmit Holding Register
        mov     %bl,%al
        out     %al,%dx        # Send a character over the serial line
        test    %edi,%edi      # Is the VGA text buffer available?
        jz      1b
        stosb                  # Write a character to the VGA text buffer
        mov     $7,%al
        stosb                  # Write an attribute to the VGA text buffer
        jmp     1b
.Lhalt: hlt
        jmp     .Lhalt

        .size early_error, . - early_error
        .type early_error, @function

        .code64

__efi64_mb2_start:
        /*
         * Multiboot2 spec says that here CPU is in 64-bit mode. However,
         * there is also guarantee that all code and data is always put
         * by the bootloader below 4 GiB. Hence, we can safely truncate
         * addresses to 32-bits in most cases below.
         */

        cld

        /* VGA is not available on EFI platforms. */
        movl   $0,vga_text_buffer(%rip)

        /*
         * Align the stack as UEFI spec requires. Keep it aligned
         * before efi_parse_mbi2() call by pushing/popping even
         * numbers of items on it.
         */
        and     $~15, %rsp

        /*
         * Initialize BSS (no nasty surprises!).
         * It must be done earlier than in BIOS case
         * because efi_parse_mbi2() touches it.
         */
        mov     %eax, %edx
        lea     __bss_start(%rip), %edi
        lea     __bss_end(%rip), %ecx
        sub     %edi, %ecx
        shr     $3, %ecx
        xor     %eax, %eax
        rep stosq

        /* Save Multiboot2 magic on the stack. */
        push    %rdx

        /* Save Multiboot2 pointer on the stack, keep the stack aligned. */
        push    %rbx

        /*
         * efi_parse_mbi2() is called according to System V AMD64 ABI:
         *   - IN:  %edi - Multiboot2 magic, %rsi - Multiboot2 pointer.
         *   - OUT: %rax - error string.
         */
        mov     %edx, %edi
        mov     %rbx, %rsi
        call    efi_parse_mbi2
        lea     .Ldirect_error(%rip), %r15
        test    %rax, %rax
        jnz     x86_32_switch

        /* Restore Multiboot2 pointer. */
        pop     %rbx

        /* Restore Multiboot2 magic. */
        pop     %rax

        /* We are on EFI platform and EFI boot services are available. */
        incb    efi_platform(%rip)

        /*
         * Disable real mode and other legacy stuff which should not
         * be run on EFI platforms.
         */
        incb    skip_realmode(%rip)

        /* Jump to trampoline_setup after switching CPU to x86_32 mode. */
        lea     trampoline_setup(%rip),%r15

x86_32_switch:
        mov     %r15,%rdi

        /* Store Xen image load base address in place accessible for 32-bit code. */
        lea     __image_base__(%rip),%esi

        cli

        /* Initialize GDTR. */
        add     %esi,gdt_boot_base(%rip)
        lgdt    gdt_boot_descr(%rip)

        /* Reload code selector. */
        pushq   $BOOT_CS32
        lea     cs32_switch(%rip),%edx
        push    %rdx
        lretq

        .code32

cs32_switch:
        /* Initialize basic data segments. */
        mov     $BOOT_DS,%edx
        mov     %edx,%ds
        mov     %edx,%es
        mov     %edx,%ss
        /* %esp is initialized later. */

        /* Load null descriptor to unused segment registers. */
        xor     %edx,%edx
        mov     %edx,%fs
        mov     %edx,%gs

        /* Disable paging. */
        mov     %cr0,%edx
        and     $(~X86_CR0_PG),%edx
        mov     %edx,%cr0

        /* Jump to earlier loaded address. */
        jmp     *%edi

#ifdef CONFIG_PVH_GUEST
ELFNOTE(Xen, XEN_ELFNOTE_PHYS32_ENTRY, .long sym_offs(__pvh_start))

__pvh_start:
        mov     $BOOT_TYPE_PVH, %dl
        jmp     .Lcommon_bios_pvh
#endif /* CONFIG_PVH_GUEST */

__start:
        mov     $BOOT_TYPE_BIOS, %dl

.Lcommon_bios_pvh:
        cli
        cld

        /*
         * Multiboot (both 1 and 2) and PVH specify the stack pointer as
         * undefined.  This is unhelpful for relocatable images, where one
         * call (i.e. push) is required to calculate the image's load address.
         *
         * Durig BIOS boot, there is one area of memory we know about with
         * reasonable confidence that it isn't overlapped by Xen, and that's
         * the Multiboot info structure in %ebx.  Use it as a temporary stack.
         *
         * During PVH boot use info structure in %ebx.
         */

        /* Preserve the field we're about to clobber. */
        mov     (%ebx), %ecx
        lea     4(%ebx), %esp

        /* Calculate the load base address. */
        call    1f
1:      pop     %esi
        sub     $sym_offs(1b), %esi

        /* Restore the clobbered field. */
        mov     %ecx, (%ebx)

        /* Set up stack. */
        lea     STACK_SIZE - CPUINFO_sizeof + sym_esi(cpu0_stack), %esp

        /* Initialize GDTR and basic data segments. */
        add     %esi, sym_esi(gdt_boot_base)
        lgdt    sym_esi(gdt_boot_descr)

        mov     $BOOT_DS, %ecx
        mov     %ecx, %ds
        mov     %ecx, %es
        mov     %ecx, %ss

        /* Load null selector to unused segment registers. */
        xor     %ecx, %ecx
        mov     %ecx, %fs
        mov     %ecx, %gs

	/* Initialise the BSS. */
        mov     %eax, %ebp
        lea     sym_esi(__bss_start), %edi
        lea     sym_esi(__bss_end), %ecx
        sub     %edi, %ecx
        xor     %eax, %eax
        shr     $2, %ecx
        rep stosl
        mov     %ebp, %eax

#ifdef CONFIG_PVH_GUEST
        cmp     $BOOT_TYPE_PVH, %dl
        jne     1f

        mov     %ebx, sym_esi(pvh_start_info_pa)

        /* Force xen console.  Will revert to user choice in init code. */
        movb    $-1, sym_esi(opt_console_xen)

        /* Skip bootloader setup and bios setup, go straight to trampoline */
        movb    $1, sym_esi(pvh_boot)
        movb    $1, sym_esi(skip_realmode)

        /* Set trampoline_phys to use mfn 1 to avoid having a mapping at VA 0 */
        movl    $PAGE_SIZE, sym_esi(trampoline_phys)
        mov     (%ebx), %eax /* mov $XEN_HVM_START_MAGIC_VALUE, %eax */
        jmp     trampoline_setup
1:
#endif /* CONFIG_PVH_GUEST */

        push    %eax
        mov     %ebx, %edx
        call    bios_setup
        test    %eax, %eax
        jnz    .Ldirect_error
        pop     %eax

trampoline_setup:
        /*
         * Called on legacy BIOS and EFI platforms.
         */

        /* Relocate pagetables to point at Xen's current location in memory. */
        mov     $_PAGE_PRESENT, %edx
        lea     sym_esi(__page_tables_start), %edi
        lea     sym_esi(__page_tables_end), %ecx

1:      test    %edx, (%edi) /* if page present */
        jz      2f
        add     %esi, (%edi) /* pte += base */
2:      add     $8, %edi

        cmp     %ecx, %edi
        jb      1b

        .if !IS_ALIGNED(sym_offs(0), 1 << L2_PAGETABLE_SHIFT)
        .error "Symbol offset calculation breaks alignment"
        .endif

        /* Save Xen image load base address for later use. */
        mov     %esi, sym_esi(xen_phys_start)
        mov     %esi, sym_esi(trampoline_xen_phys_start)

        /* Save Multiboot / PVH info struct (after relocation) for later use. */
        mov     %ebx, %edx          /* Multiboot / PVH information address. */
        movzbl  sym_esi(efi_platform), %ecx
        /*      common_setup(magic/eax, info/edx, efi_platform/ecx) using fastcall. */
        call    common_setup
        test    %eax, %eax
        jnz     .Ldirect_error

        /*
         * Now trampoline_phys points to the following structure (lowest address
         * is at the bottom):
         *
         * +------------------------+
         * | TRAMPOLINE_STACK_SPACE |
         * +------------------------+
         * |     Data (MBI / PVH)   |
         * +- - - - - - - - - - - - +
         * |    TRAMPOLINE_SPACE    |
         * +------------------------+
         *
         * Data grows downwards from the highest address of TRAMPOLINE_SPACE
         * region to the end of the trampoline. The rest of TRAMPOLINE_SPACE is
         * reserved for trampoline code and data.
         */

        /* Switch to low-memory stack which lives at the end of trampoline region. */
        mov     sym_esi(trampoline_phys), %edi
        lea     TRAMPOLINE_SPACE+TRAMPOLINE_STACK_SPACE(%edi),%esp
        lea     trampoline_boot_cpu_entry-trampoline_start(%edi),%eax
        pushl   $BOOT_CS32
        push    %eax

        /* Jump into the relocated trampoline. */
        lret

ENTRY(trampoline_start)
#include "trampoline.S"
ENTRY(trampoline_end)

#include "x86_64.S"
