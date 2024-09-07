/* SPDX-License-Identifier: GPL-2.0 */

#include <xen/compiler.h>
#include <xen/macros.h>
#include <xen/types.h>

void reloc(uint32_t magic, uint32_t in);
const char *detect_cpu(void);
void setup_pages(void);
void reloc_trampoline(void);

const char *common_setup(uint32_t magic, uint32_t in)
{
    const char *err;

    reloc(magic, in);

    err = detect_cpu();
    if ( err )
        return err;

    setup_pages();

    /* Apply relocations to bootstrap trampoline. */
    reloc_trampoline();

    return err;
}
