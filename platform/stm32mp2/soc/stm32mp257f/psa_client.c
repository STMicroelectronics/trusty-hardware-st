#include <lk/init.h>
#include <err.h>
#include <string.h>

#include <utils/utils.h>

#include <stm32_psa_client.h>

#include "ipcc.h"

// SOC definitions
/*#include <stm32mp-rif.h>*/

#define TLOG_TAG "psa_client"
#include <trusty_log.h>

#define MBOX_NAME "psa_tfm"

struct shmem_fdt psa_client_shmem = {
    .reg = 0xa043000,
    .reg_size = 0x1000
};

struct mbox_fdt mboxes[] = {
    {
        .phandle = NULL,
        .name = MBOX_NAME,
        .chan_id = RIF_IPCC_CPU1_CHANNEL(14)
    }
};

struct psa_client_fdt psa_client_fdt = {
    .mboxes = mboxes,
    .nmboxes = ARRAY_SIZE(mboxes),
    .shmem = &psa_client_shmem
};

static void psa_client_init(uint level) {
    int res = NO_ERROR;

    TLOGD("Initializing psa_client...\n");

    mboxes[0].phandle = ipcc_get_fdt(IPCC1);

    res = stm32mp_start_psa_service(&psa_client_fdt);
    if (res != NO_ERROR) {
        TLOGE("Cannot initialize psa_client: %d !\n", res);
        return;
    }

    TLOGD("psa_client initialized !\n");
}

LK_INIT_HOOK(psa_client, psa_client_init, LK_INIT_LEVEL_KERNEL + 1);
