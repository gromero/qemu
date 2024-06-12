/*
 * ARM MemTag Operation Helpers
 *
 * Copyright (c) 2024 Linaro, Ltd.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef TARGET_ARM_MTE_H
#define TARGET_ARM_MTE_H

#include "exec/exec-all.h"
#include "exec/ram_addr.h"
#include "hw/core/tcg-cpu-ops.h"
#include "qemu/log.h"

/**
 * allocation_tag_mem_probe:
 * @env: the cpu environment
 * @ptr_mmu_idx: the addressing regime to use for the virtual address
 * @ptr: the virtual address for which to look up tag memory
 * @ptr_access: the access to use for the virtual address
 * @ptr_size: the number of bytes in the normal memory access
 * @tag_access: the access to use for the tag memory
 * @probe: true to merely probe, never taking an exception
 * @ra: the return address for exception handling
 *
 * Our tag memory is formatted as a sequence of little-endian nibbles.
 * That is, the byte at (addr >> (LOG2_TAG_GRANULE + 1)) contains two
 * tags, with the tag at [3:0] for the lower addr and the tag at [7:4]
 * for the higher addr.
 *
 * Here, resolve the physical address from the virtual address, and return
 * a pointer to the corresponding tag byte.
 *
 * If there is no tag storage corresponding to @ptr, return NULL.
 *
 * If the page is inaccessible for @ptr_access, or has a watchpoint, there are
 * three options:
 * (1) probe = true, ra = 0 : pure probe -- we return NULL if the page is not
 *     accessible, and do not take watchpoint traps. The calling code must
 *     handle those cases in the right priority compared to MTE traps.
 * (2) probe = false, ra = 0 : probe, no fault expected -- the caller guarantees
 *     that the page is going to be accessible. We will take watchpoint traps.
 * (3) probe = false, ra != 0 : non-probe -- we will take both memory access
 *     traps and watchpoint traps.
 * (probe = true, ra != 0 is invalid and will assert.)
 */
static inline uint8_t *allocation_tag_mem_probe(CPUARMState *env, int ptr_mmu_idx,
                                         uint64_t ptr, MMUAccessType ptr_access,
                                         int ptr_size, MMUAccessType tag_access,
                                         bool probe, uintptr_t ra)
{
#ifdef CONFIG_USER_ONLY
    uint64_t clean_ptr = useronly_clean_ptr(ptr);
    int flags = page_get_flags(clean_ptr);
    uint8_t *tags;
    uintptr_t index;

    assert(!(probe && ra));

    if (!(flags & (ptr_access == MMU_DATA_STORE ? PAGE_WRITE_ORG : PAGE_READ))) {
        if (probe) {
            return NULL;
        }
        cpu_loop_exit_sigsegv(env_cpu(env), ptr, ptr_access,
                              !(flags & PAGE_VALID), ra);
    }

    /* Require both MAP_ANON and PROT_MTE for the page. */
    if (!(flags & PAGE_ANON) || !(flags & PAGE_MTE)) {
        return NULL;
    }

    tags = page_get_target_data(clean_ptr);

    index = extract32(ptr, LOG2_TAG_GRANULE + 1,
                      TARGET_PAGE_BITS - LOG2_TAG_GRANULE - 1);
    return tags + index;
#else
    CPUTLBEntryFull *full;
    MemTxAttrs attrs;
    int in_page, flags;
    hwaddr ptr_paddr, tag_paddr, xlat;
    MemoryRegion *mr;
    ARMASIdx tag_asi;
    AddressSpace *tag_as;
    void *host;

    /*
     * Probe the first byte of the virtual address.  This raises an
     * exception for inaccessible pages, and resolves the virtual address
     * into the softmmu tlb.
     *
     * When RA == 0, this is either a pure probe or a no-fault-expected probe.
     * Indicate to probe_access_flags no-fault, then either return NULL
     * for the pure probe, or assert that we received a valid page for the
     * no-fault-expected probe.
     */
    flags = probe_access_full(env, ptr, 0, ptr_access, ptr_mmu_idx,
                              ra == 0, &host, &full, ra);
    if (probe && (flags & TLB_INVALID_MASK)) {
        return NULL;
    }
    assert(!(flags & TLB_INVALID_MASK));

    /* If the virtual page MemAttr != Tagged, access unchecked. */
    if (full->extra.arm.pte_attrs != 0xf0) {
        return NULL;
    }

    /*
     * If not backed by host ram, there is no tag storage: access unchecked.
     * This is probably a guest os bug though, so log it.
     */
    if (unlikely(flags & TLB_MMIO)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "Page @ 0x%" PRIx64 " indicates Tagged Normal memory "
                      "but is not backed by host ram\n", ptr);
        return NULL;
    }

    /*
     * Remember these values across the second lookup below,
     * which may invalidate this pointer via tlb resize.
     */
    ptr_paddr = full->phys_addr | (ptr & ~TARGET_PAGE_MASK);
    attrs = full->attrs;
    full = NULL;

    /*
     * The Normal memory access can extend to the next page.  E.g. a single
     * 8-byte access to the last byte of a page will check only the last
     * tag on the first page.
     * Any page access exception has priority over tag check exception.
     */
    in_page = -(ptr | TARGET_PAGE_MASK);
    if (unlikely(ptr_size > in_page)) {
        flags |= probe_access_full(env, ptr + in_page, 0, ptr_access,
                                   ptr_mmu_idx, ra == 0, &host, &full, ra);
        assert(!(flags & TLB_INVALID_MASK));
    }

    /* Any debug exception has priority over a tag check exception. */
    if (!probe && unlikely(flags & TLB_WATCHPOINT)) {
        int wp = ptr_access == MMU_DATA_LOAD ? BP_MEM_READ : BP_MEM_WRITE;
        assert(ra != 0);
        cpu_check_watchpoint(env_cpu(env), ptr, ptr_size, attrs, wp, ra);
    }

    /* Convert to the physical address in tag space.  */
    tag_paddr = ptr_paddr >> (LOG2_TAG_GRANULE + 1);

    /* Look up the address in tag space. */
    tag_asi = attrs.secure ? ARMASIdx_TagS : ARMASIdx_TagNS;
    tag_as = cpu_get_address_space(env_cpu(env), tag_asi);
    mr = address_space_translate(tag_as, tag_paddr, &xlat, NULL,
                                 tag_access == MMU_DATA_STORE, attrs);

    /*
     * Note that @mr will never be NULL.  If there is nothing in the address
     * space at @tag_paddr, the translation will return the unallocated memory
     * region.  For our purposes, the result must be ram.
     */
    if (unlikely(!memory_region_is_ram(mr))) {
        /* ??? Failure is a board configuration error. */
        qemu_log_mask(LOG_UNIMP,
                      "Tag Memory @ 0x%" HWADDR_PRIx " not found for "
                      "Normal Memory @ 0x%" HWADDR_PRIx "\n",
                      tag_paddr, ptr_paddr);
        return NULL;
    }

    /*
     * Ensure the tag memory is dirty on write, for migration.
     * Tag memory can never contain code or display memory (vga).
     */
    if (tag_access == MMU_DATA_STORE) {
        ram_addr_t tag_ra = memory_region_get_ram_addr(mr) + xlat;
        cpu_physical_memory_set_dirty_flag(tag_ra, DIRTY_MEMORY_MIGRATION);
    }

    return memory_region_get_ram_ptr(mr) + xlat;
#endif
}

static inline int load_tag1(uint64_t ptr, uint8_t *mem)
{
    int ofs = extract32(ptr, LOG2_TAG_GRANULE, 1) * 4;
    return extract32(*mem, ofs, 4);
}

/* For use in a non-parallel context, store to the given nibble.  */
static inline void store_tag1(uint64_t ptr, uint8_t *mem, int tag)
{
    int ofs = extract32(ptr, LOG2_TAG_GRANULE, 1) * 4;
    *mem = deposit32(*mem, ofs, 4, tag);
}

#endif /* TARGET_ARM_MTE_H */
