#ifndef STM32_RCC_H
#define STM32_RCC_H

struct pll_dt {

};

struct rcc_dt {
    uint32_t *busclk;
    uint32_t nbusclk;
    uint32_t *flexgen;
    uint32_t nflexgen;
    uint32_t *kernelclk;
    uint32_t nkernelclk;

};

#endif /* !STM32_RCC_H */
