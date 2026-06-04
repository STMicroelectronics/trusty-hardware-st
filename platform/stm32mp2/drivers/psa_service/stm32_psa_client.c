/*
 * Copyright (c) 2015 Google Inc. All rights reserved
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files
 * (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <err.h>
#include <inttypes.h>
#include <kernel/vm.h>
#include <lk/init.h>
#include <string.h>

#include <mailbox.h>
#include <stm32_psa_client.h>

#include <rse_comms.h>

#define TLOG_TAG "psa_client"
#include <trusty_log.h>

/* Arbitrary default Client */
#define ST_PSA_CLIENT_ID (-19)

static struct mbox_chan *handle_send_receive;

struct stm32_rse_shmem {
    uint32_t size;
    uint8_t payload[];
};

static struct stm32_rse_shmem *shmem;
static size_t shmem_size;

static int rse_mbox_send_data(const uint8_t *send_buffer, size_t size)
{
    int res = NO_ERROR;

    memcpy(shmem->payload, send_buffer, size);
    shmem->size = size;
    res = mbox_send(handle_send_receive, false, NULL, 0);
    if (res != NO_ERROR) {
        TLOGE("Send/receive IPCC : send failed\n");
        return res;
    }
    return 0;
}

static int rse_mbox_rcv_data(const uint8_t *rcv_buffer, size_t *size)
{
    int res = NO_ERROR;

    do {
        res = mbox_recv(handle_send_receive, false, NULL, 0);
    } while (res == ERR_NO_MSG);
    if (res != NO_ERROR) {
        TLOGE("Send/receive : failed to receive message\n");
        return res;
    }
    *size = shmem->size;
    memcpy((void *)rcv_buffer, shmem->payload, *size);
    return 0;
}

static int rse_mbox_size_data(size_t *size)
{
    *size = shmem_size;
    if (!shmem_size)
        return -1;
    else
        return 0;
}

int stm32mp_start_psa_service(struct psa_client_fdt *fdt)
{
    int res = NO_ERROR;
    struct shmem_fdt *shmem_fdt;

    if (!fdt)
        return NO_ERROR;

    res = mbox_dt_register_chan_by_name(NULL, NULL, NULL,
                                        fdt->mboxes, fdt->nmboxes,
                                        "psa_tfm", &handle_send_receive);
    if (res) {
        TLOGD("No PSA mailbox channel %d\n", res);
        return ERR_NOT_READY;
    }

    shmem_fdt = fdt->shmem;
    if (!shmem_fdt) {
        TLOGE("No Shared memory\n");
        panic();
    }

    res = vmm_alloc_physical(vmm_get_kernel_aspace(), "psa_client",
                                 shmem_fdt->reg_size, (void **)&shmem, 0,
                                 shmem_fdt->reg, 0,
                                 ARCH_MMU_FLAG_UNCACHED_DEVICE | ARCH_MMU_FLAG_PERM_NO_EXECUTE);
    if (res != NO_ERROR) {
        TLOGE("No Shared memory\n");
        panic();
    }

    rse_register_cb(rse_mbox_send_data, rse_mbox_rcv_data,
                    rse_mbox_size_data);
    /* set a default client id */
    rse_set_client_id(ST_PSA_CLIENT_ID);

    return res;
}
