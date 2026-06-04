#ifndef IPCC_H
#define IPCC_H

enum ipcc_device {
    IPCC1,
    IPCC2
};

void *ipcc_get_fdt(enum ipcc_device);

#endif /* !IPCC_H */
