#ifndef STM32_SHMEM_DTS_H
#define STM32_SHMEM_DTS_H

#include <lk/types.h>

struct shmem_fdt {
    paddr_t reg;
    size_t reg_size;
};

#endif /* !STM32_SHMEM_H */
