// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2021-2023, STMicroelectronics
 */
#include <stdbool.h>
#include <string.h>

#include <kernel/vm.h>

#include <utils/mmu_utils.h>
#include <utils/utils.h>

static bool pa_is_in_map(struct mmu_initial_mapping *map, paddr_t pa, size_t len)
{
    paddr_t end_pa = 0;

    if (!map)
        return false;

    if (SUB_OVERFLOW(len, 1, &end_pa) || ADD_OVERFLOW(pa, end_pa, &end_pa))
        return false;

    return (pa >= map->phys && end_pa <= map->phys + map->size - 1);
}

static struct mmu_initial_mapping *
find_map_by_type_and_pa(enum memtypes type, paddr_t pa, size_t len)
{
    struct mmu_initial_mapping *map = mmu_initial_mappings;
    while (map->size > 0) {
        /* TODO */
        if (false)
            continue;
        if (pa_is_in_map(map, pa, len))
            return map;

        map++;
    }

    return NULL;
}

static void *map_pa2va(struct mmu_initial_mapping *map, paddr_t pa, size_t len)
{
    return (void *)(map->virt + (pa - map->phys));
}

static void *phys_to_virt(paddr_t pa, enum memtypes m, size_t len)
{
    void *va = map_pa2va(find_map_by_type_and_pa(m, pa, len), pa, len);

    return va;
}

static void *phys_to_virt_io(paddr_t pa, size_t len)
{
    struct mmu_initial_mapping *map = NULL;
    void *va = NULL;

    map = find_map_by_type_and_pa(MEM_AREA_IO_SEC, pa, len);
    if (!map)
        map = find_map_by_type_and_pa(MEM_AREA_IO_NSEC, pa, len);
    if (!map)
        return NULL;
    va = map_pa2va(map, pa, len);
    return va;
}

vaddr_t io_pa_or_va(struct io_pa_va *p, size_t len)
{
    assert(p->pa);

    if (!p->va)
        p->va = (vaddr_t)phys_to_virt_io(p->va, len);

    assert(p->va);
    return p->va;
}

vaddr_t io_pa_or_va_secure(struct io_pa_va *p, size_t len)
{
    assert(p->pa);

    if (!p->va)
        p->va = (vaddr_t)phys_to_virt(p->pa, MEM_AREA_IO_SEC, len);

    assert(p->va);
    return p->va;
}

vaddr_t io_pa_or_va_nsec(struct io_pa_va *p, size_t len)
{
    assert(p->pa);

    if (!p->va)
        p->va = (vaddr_t)phys_to_virt(p->va, MEM_AREA_IO_NSEC, len);

    assert(p->va);
    return p->va;
}

