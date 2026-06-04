// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2021-2023, STMicroelectronics
 */
#ifndef __MMU_UTILS_H
#define __MMU_UTILS_H

#include <lk/compiler.h>
#include <lk/types.h>
#include <kernel/vm.h>

#include <utils/utils.h>

struct io_pa_va {
    paddr_t pa;
    vaddr_t va;
};

enum memtypes {
    MEM_AREA_END = 0,
    MEM_AREA_TEE_RAM,
    MEM_AREA_TEE_RAM_RX,
    MEM_AREA_TEE_RAM_RO,
    MEM_AREA_TEE_RAM_RW,
    MEM_AREA_INIT_RAM_RO,
    MEM_AREA_INIT_RAM_RX,
    MEM_AREA_NEX_RAM_RO,
    MEM_AREA_NEX_RAM_RW,
    MEM_AREA_TEE_COHERENT,
    MEM_AREA_TEE_ASAN,
    MEM_AREA_IDENTITY_MAP_RX,
    MEM_AREA_TA_RAM,
    MEM_AREA_NSEC_SHM,
    MEM_AREA_NEX_NSEC_SHM,
    MEM_AREA_RAM_NSEC,
    MEM_AREA_RAM_SEC,
    MEM_AREA_ROM_SEC,
    MEM_AREA_IO_NSEC,
    MEM_AREA_IO_SEC,
    MEM_AREA_EXT_DT,
    MEM_AREA_MANIFEST_DT,
    MEM_AREA_RES_VASPACE,
    MEM_AREA_SHM_VASPACE,
    MEM_AREA_TS_VASPACE,
    MEM_AREA_PAGER_VASPACE,
    MEM_AREA_SDP_MEM,
    MEM_AREA_DDR_OVERALL,
    MEM_AREA_SEC_RAM_OVERALL,
    MEM_AREA_MAXTYPE
};

static inline const char *memtype_name(enum memtypes type)
{
    static const char * const names[] = {
        [MEM_AREA_END] = "END",
        [MEM_AREA_TEE_RAM] = "TEE_RAM_RWX",
        [MEM_AREA_TEE_RAM_RX] = "TEE_RAM_RX",
        [MEM_AREA_TEE_RAM_RO] = "TEE_RAM_RO",
        [MEM_AREA_TEE_RAM_RW] = "TEE_RAM_RW",
        [MEM_AREA_INIT_RAM_RO] = "INIT_RAM_RO",
        [MEM_AREA_INIT_RAM_RX] = "INIT_RAM_RX",
        [MEM_AREA_NEX_RAM_RO] = "NEX_RAM_RO",
        [MEM_AREA_NEX_RAM_RW] = "NEX_RAM_RW",
        [MEM_AREA_TEE_ASAN] = "TEE_ASAN",
        [MEM_AREA_IDENTITY_MAP_RX] = "IDENTITY_MAP_RX",
        [MEM_AREA_TEE_COHERENT] = "TEE_COHERENT",
        [MEM_AREA_TA_RAM] = "TA_RAM",
        [MEM_AREA_NSEC_SHM] = "NSEC_SHM",
        [MEM_AREA_NEX_NSEC_SHM] = "NEX_NSEC_SHM",
        [MEM_AREA_RAM_NSEC] = "RAM_NSEC",
        [MEM_AREA_RAM_SEC] = "RAM_SEC",
        [MEM_AREA_ROM_SEC] = "ROM_SEC",
        [MEM_AREA_IO_NSEC] = "IO_NSEC",
        [MEM_AREA_IO_SEC] = "IO_SEC",
        [MEM_AREA_EXT_DT] = "EXT_DT",
        [MEM_AREA_MANIFEST_DT] = "MANIFEST_DT",
        [MEM_AREA_RES_VASPACE] = "RES_VASPACE",
        [MEM_AREA_SHM_VASPACE] = "SHM_VASPACE",
        [MEM_AREA_TS_VASPACE] = "TS_VASPACE",
        [MEM_AREA_PAGER_VASPACE] = "PAGER_VASPACE",
        [MEM_AREA_SDP_MEM] = "SDP_MEM",
        [MEM_AREA_DDR_OVERALL] = "DDR_OVERALL",
        [MEM_AREA_SEC_RAM_OVERALL] = "SEC_RAM_OVERALL",
    };

    STATIC_ASSERT(ARRAY_SIZE(names) == MEM_AREA_MAXTYPE);
    return names[type];
}

vaddr_t io_pa_or_va_secure(struct io_pa_va *p, size_t len);
vaddr_t io_pa_or_va_nsec(struct io_pa_va *p, size_t len);
vaddr_t io_pa_or_va(struct io_pa_va *p, size_t len);

#endif /* !__MMU_UTILS_H */
