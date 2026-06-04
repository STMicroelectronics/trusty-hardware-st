#ifndef STM32_IPCC_DTS_H
#define STM32_IPCC_DTS_H

#include <lk/types.h>

#define U(_x) (_x##U)

enum {
    IPCC_ITR_RXO,
    IPCC_ITR_TXF,
    IPCC_ITR_NUM,
};

struct ipcc_fdt {
    paddr_t reg;
    size_t reg_size;
    uint32_t proc_id;
    size_t itr_num[IPCC_ITR_NUM];
   /* struct clk *ipcc_clock;*/
    uint32_t *protreg;
    size_t nprotreg;
};

int stm32_ipcc_probe(struct ipcc_fdt *dt);

#endif /* !STM32_IPCC_H */
