#include "ipcc.h"

#include <lk/init.h>
#include <err.h>

#include <utils/utils.h>

#include <stm32_ipcc.h>

#define TLOG_TAG "ipcc"
#include <trusty_log.h>

struct ipcc_fdt ipcc1_fdt = {
    .reg = 0x40490000,
    .reg_size = 0x400,
    .proc_id = 0,
    .itr_num = {
        [0] = 173 + 32, // RX
        [1] = SIZE_MAX // TX
    },
    .protreg = NULL,
    .nprotreg = 0
};

void *ipcc_get_fdt(enum ipcc_device ipcc)
{
    switch (ipcc) {
    case IPCC1:
        return &ipcc1_fdt;
    default:
        TLOGE("No IPCC device with id %d\n", ipcc);
        return NULL;
    };
}


static void ipcc_init(uint level) {
    int res = NO_ERROR;

    TLOGD("Initializing IPCC1...\n");

    res = stm32_ipcc_probe(&ipcc1_fdt);
    if (res != NO_ERROR) {
        TLOGE("Cannot probe ipcc driver for IPCC1: %d !\n", res);
        return;
    }

    TLOGD("IPCC1 driver initialized !\n");
}
LK_INIT_HOOK(ipcc_driver, ipcc_init, LK_INIT_LEVEL_KERNEL - 1);
