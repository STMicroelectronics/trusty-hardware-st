// SPDX-License-Identifier: BSD-2-Clause
/*
 * Copyright (c) 2017-2022, STMicroelectronics
 */

#include <kernel/spinlock.h>

#include <utils/io.h>
#include <utils/shared_io.h>
#include <utils/utils.h>

static spin_lock_t shregs_lock = SPIN_LOCK_INITIAL_VALUE;

static spin_lock_saved_state_t lock_stm32shregs(void)
{
    spin_lock_saved_state_t exceptions = 0;
    spin_lock_save(&shregs_lock, &exceptions, SPIN_LOCK_FLAG_IRQ_FIQ);

    return exceptions;
}

static void unlock_stm32shregs(spin_lock_saved_state_t exceptions)
{
    spin_unlock_restore(&shregs_lock, exceptions, SPIN_LOCK_FLAG_IRQ_FIQ);
}

void io_mask32_stm32shregs(vaddr_t va, uint32_t value, uint32_t mask)
{
    spin_lock_saved_state_t exceptions = lock_stm32shregs();

    io_mask32(va, value, mask);

    unlock_stm32shregs(exceptions);
}

void io_clrsetbits32_stm32shregs(vaddr_t va, uint32_t clr, uint32_t set)
{
    spin_lock_saved_state_t exceptions = lock_stm32shregs();

    io_clrsetbits32(va, clr, set);

    unlock_stm32shregs(exceptions);
}
