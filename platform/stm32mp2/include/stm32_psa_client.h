#ifndef STM32_PSA_CLIENT_DTS_H
#define STM32_PSA_CLIENT_DTS_H

#include <lk/types.h>

#include <mailbox.h>
#include <stm32_shmem.h>

#define RIF_IPCC_CPU1_CHANNEL(x)        (x - 1)

struct psa_client_fdt {
    struct mbox_fdt *mboxes;
    size_t nmboxes;
    struct shmem_fdt *shmem;
};

int stm32mp_start_psa_service(struct psa_client_fdt *fdt);

#endif /* !STM32_PSA_CLIENT_DTS_H */
