// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (C) 2022-2024, STMicroelectronics
 */
#include <stm32_ipcc.h>

#include <assert.h>
#include <err.h>
#include <kernel/spinlock.h>
#include <stdbool.h>
#include <stdlib.h>
#include <list.h>
#include <platform/interrupts.h>

#include <mailbox.h>
#include <utils/config_utils.h>
#include <utils/io.h>
#include <utils/mmu_utils.h>
#include <utils/utils.h>

#define TLOG_TAG "stm32_ipcc"
#include <trusty_log.h>

#define IPCC_C1SECCFGR          U(0x80)
#define IPCC_C1PRIVCFGR         U(0x84)
#define IPCC_C1CIDCFGR          U(0x88)
#define IPCC_C2SECCFGR          U(0x90)
#define IPCC_C2PRIVCFGR         U(0x94)
#define IPCC_C2CIDCFGR          U(0x98)
#define IPCC_HWCFGR             U(0x3F0)
#define IPCC_VER                U(0x3F4)

#define IPCC_C1CR               U(0x00)
#define IPCC_C1MR               U(0x04)
#define IPCC_C1SCR              U(0x08)
#define IPPC_C1TOC2SR           U(0x0C)

#define IPCC_C2CR               U(0x10)
#define IPCC_C2MR               U(0x14)
#define IPCC_C2SCR              U(0x18)
#define IPPC_C2TOC1SR           U(0x1C)

/* Offset within a core instance */
#define IPCC_CR                 U(0x0)
#define IPCC_MR                 U(0x4)
#define IPCC_SCR                U(0x8)
#define IPCC_TOSR               U(0xC)
#define IPCC_SECCFGR            U(0x80)
#define IPCC_PRIVCFGR           U(0x84)
#define IPCC_CIDCFGR            U(0x98)

/* Mask for  channel rxo and txf */
#define IPCC_ALL_MR_TXF_CH_MASK     GENMASK_32(31, 16)
#define IPCC_ALL_MR_RXO_CH_MASK     GENMASK_32(15, 0)
#define IPCC_ALL_SR_CH_MASK         GENMASK_32(15, 0)

/* Define for core instance register */
/* PROCESSOR  RECEIVE CHANNEL OCCUPIED NON-SECURE INTERRUPT ENABLE */
#define IPCC_CR_RXOIE           BIT(0)
/* PROCESSOR  RECEIVE CHANNEL OCCUPIED SECURE INTERRUPT ENABLE */
#define IPCC_CR_SECRXOIE        BIT(1)
/* PROCESSOR TRANSMIT CHANNEL FREE NON-SECURE INTERRUPT ENABLE */
#define IPCC_CR_TXFIE           BIT(16)
 /* PROCESSOR TRANSMIT CHANNEL FREE SECURE INTERRUPT ENABLE */
#define IPCC_CR_SECTXFIE        BIT(17)

#define IPCC_MR_CH1FM_POS       U(16)
#define IPCC_SCR_CH1S_POS       U(16)

/*
 * CIDCFGR register bitfields
 */
#define IPCC_CIDCFGR_CFEN       BIT(0)
#define IPCC_CIDCFGR_SCID_MASK  GENMASK_32(6, 4)
#define IPCC_CIDCFGR_CONF_MASK  (_CIDCFGR_CFEN | \
                                 IPCC_CIDCFGR_SCID_MASK)

/*
 * PRIVCFGR register bitfields
 */
#define IPCC_PRIVCFGR_MASK      GENMASK_32(15, 0)

/*
 * SECCFGR register bitfields
 */
#define IPCC_SECCFGR_MASK       GENMASK_32(15, 0)

/*
 * IPCC_HWCFGR register bitfields
 */
#define IPCC_HWCFGR_CHAN_MASK   GENMASK_32(7, 0)

/*
 * Miscellaneous
 */
#define IPCC_NB_MAX_RIF_CHAN    U(16)

#define DT_INFO_INVALID_REG         ((paddr_t)-1)
#define DT_INFO_INVALID_REG_SIZE    ((size_t)-1)

/* IPCC mbox data */
struct stm32_ipcc_mbx_data {
    /* Lock to protect concurrent access */
    spin_lock_t lock;
    uint32_t channel_sec_mask;
    uint32_t channel_enable_mask;
    unsigned int num_ch;
    mbox_rx_callback_t rx_cb;
    mbox_tx_callback_t tx_cb;
    struct mbox_dev *cb_ctx;
    uint32_t caps;
};

struct ipcc_pdata {
    /*
     * An IPCC has nb_channels_cfg channel configuration for its
     * (nb_channels_cfg / 2) bi-directionnal channels
     */
    unsigned int nb_channels_cfg;
    struct clk *ipcc_clock;
    vaddr_t base;
    /* Remote proc base address */
    vaddr_t rbase;
    /* Local proc base address */
    vaddr_t lbase;
    struct itr_chip *itr_chip[IPCC_ITR_NUM];
    size_t itr_num[IPCC_ITR_NUM];

    /* Single mailbox user (mailbox framework) */
    struct stm32_ipcc_mbx_data data;
    struct list_node link;
};

static struct list_node ipcc_list = LIST_INITIAL_VALUE(ipcc_list);

static bool ipcc_channel_is_active(vaddr_t lbase, unsigned int chn)
{
    return io_read32(lbase + IPCC_TOSR) & BIT(chn);
}

static void ipcc_channel_transmit(vaddr_t lbase, unsigned int chn, bool enable)
{
    if (enable)
        io_clrbits32(lbase + IPCC_MR, BIT(chn + IPCC_MR_CH1FM_POS));
    else
        io_setbits32(lbase + IPCC_MR, BIT(chn + IPCC_MR_CH1FM_POS));
}

static void ipcc_channel_receive(vaddr_t lbase, unsigned int chn, bool enable)
{
    if (enable)
        io_clrbits32(lbase + IPCC_MR, BIT(chn));
    else
        io_setbits32(lbase + IPCC_MR, BIT(chn));
}

static enum handler_return stm32_ipcc_mbox_rxo_isr(void *h)
{
    struct ipcc_pdata *pdata = (struct ipcc_pdata *)(h);
    struct stm32_ipcc_mbx_data *data = &pdata->data;
    unsigned int value = 0;
    uint32_t mask = 0;
    unsigned int i = 0;

    mask = ~io_read32(pdata->lbase + IPCC_MR) & IPCC_ALL_MR_RXO_CH_MASK;
    mask &= io_read32(pdata->rbase + IPCC_TOSR) & IPCC_ALL_SR_CH_MASK;

    /* Get mask for enabled channels only */
    mask &= data->channel_enable_mask;

    for (i = 0; i < data->num_ch; i++) {
        if (!(BIT(i) & mask))
            continue;
        TLOGD("rx channel = %"PRIu32"\n", i);
        if (data->rx_cb)
            data->rx_cb(data->cb_ctx, i, &value);
        /* Clear rxo flag */
        io_write32(pdata->lbase + IPCC_SCR, BIT(i));
    }
    return INT_NO_RESCHEDULE;
}

static enum handler_return stm32_ipcc_mbox_txf_isr(void *h)
{
    struct ipcc_pdata *pdata = (struct ipcc_pdata *)(h);
    struct stm32_ipcc_mbx_data *data = &pdata->data;
    uint32_t mask = 0;
    uint32_t i = 0;

    mask = ~io_read32(pdata->lbase + IPCC_MR) & IPCC_ALL_MR_TXF_CH_MASK;
    mask = mask >> IPCC_MR_CH1FM_POS;

    mask &= ~io_read32(pdata->lbase + IPCC_TOSR) & IPCC_ALL_SR_CH_MASK;

    /* Get mask for enabled channels only */
    mask &= data->channel_enable_mask;

    for (i = 0; i < data->num_ch; i++) {
        if (BIT(i) & mask) {
            if (data->tx_cb)
                data->tx_cb(data->cb_ctx, i);
            /* Disable txf interrupt */
            if (data->caps & MBOX_TX_NOTIF_CAP)
                ipcc_channel_transmit(pdata->lbase, i, false);
        }
    }
    return INT_NO_RESCHEDULE;
}

static int stm32_ipcc_mbox_send(const struct mbox_desc *desc,
                                unsigned int id,
                                const void *buff, size_t size)
{
    (void)(buff);

    struct ipcc_pdata *pdata = (struct ipcc_pdata *)desc->priv;
    struct stm32_ipcc_mbx_data *data = &pdata->data;

    /* No data transmission, only doorbell */
    if (size)
        return ERR_NOT_SUPPORTED;

    if (id >= data->num_ch || !(BIT(id) & data->channel_sec_mask)) {
        TLOGE("invalid id %u\n", id);
        return ERR_INVALID_ARGS;
    }

    TLOGD("Send msg on channel %u\n", id);

    /* Check that the channel is free */
    if (ipcc_channel_is_active(pdata->lbase, id)) {
        TLOGD("Waiting for channel to be freed\n");
        return ERR_BUSY;
    }
    /* Set channel txs Flag */
    io_write32(pdata->lbase + IPCC_SCR, BIT(id + IPCC_SCR_CH1S_POS));
    /* Enable channel txf interrupt */
    if (data->caps & MBOX_TX_NOTIF_CAP)
        ipcc_channel_transmit(pdata->lbase, id, true);

    return NO_ERROR;
}

static size_t stm32_ipcc_mbox_get_mtu(const struct mbox_desc *desc)
{
    (void)(desc);

    /* No data transfer capability */
    return 0;
}

static size_t stm32_ipcc_mbox_channel_count(const struct mbox_desc *desc)
{
    struct ipcc_pdata *pdata = (struct ipcc_pdata *)desc->priv;
    struct stm32_ipcc_mbx_data *data = &pdata->data;

    assert(data->num_ch);

    return data->num_ch;
}

static int stm32_ipcc_mbox_register_callback(const struct mbox_desc *desc,
                                             mbox_rx_callback_t rx_cb,
                                             mbox_tx_callback_t tx_cb,
                                             struct mbox_dev *cb_ctx)
{
    struct ipcc_pdata *pdata = (struct ipcc_pdata *)desc->priv;
    struct stm32_ipcc_mbx_data *data = &pdata->data;
    spin_lock_saved_state_t exceptions = 0;
    int res = NO_ERROR;

    spin_lock_save(&data->lock, &exceptions, SPIN_LOCK_FLAG_IRQ_FIQ);
    if (!data->rx_cb && !data->tx_cb && !data->cb_ctx) {
        data->rx_cb = rx_cb;
        data->tx_cb = tx_cb;
        data->cb_ctx = cb_ctx;
        res = NO_ERROR;
    } else {
        res = ERR_BAD_STATE;
    }
    spin_unlock_restore(&data->lock, exceptions, SPIN_LOCK_FLAG_IRQ_FIQ);

    return res;
}

static int stm32_ipcc_mbox_enable(const struct mbox_desc *desc,
                                  bool enable, unsigned int id)
{
    struct ipcc_pdata *pdata = (struct ipcc_pdata *)desc->priv;
    struct stm32_ipcc_mbx_data *data = &pdata->data;
    uint32_t channel_enable_mask = 0;
    spin_lock_saved_state_t exceptions = 0;

    if (!(BIT(id) & data->channel_sec_mask))
        return ERR_INVALID_ARGS;

    spin_lock_save(&data->lock, &exceptions, SPIN_LOCK_FLAG_IRQ_FIQ);

    if (enable)
        channel_enable_mask = (BIT(id) & data->channel_sec_mask) |
            data->channel_enable_mask;
    else
        channel_enable_mask = ~(BIT(id) & data->channel_sec_mask) &
            data->channel_enable_mask;

    if (channel_enable_mask && !data->channel_enable_mask) {
        /* Enable secure txf and rxo interrupt */
        io_setbits32(pdata->lbase + IPCC_CR,
                 IPCC_CR_SECTXFIE | IPCC_CR_SECRXOIE);
    }

    if (!channel_enable_mask && data->channel_enable_mask) {
        /* Disable secure txf and rxo interrupt */
        io_clrbits32(pdata->lbase + IPCC_CR,
                 IPCC_CR_SECTXFIE | IPCC_CR_SECRXOIE);
    }

    /*
     * Update mask and then enable rxo interrupt
     * since mask is used within rxo interrupt handler and
     * do the opposite for disable
     */
    if (enable)
        data->channel_enable_mask = channel_enable_mask;
    ipcc_channel_receive(pdata->lbase, id, enable);
    if (!enable)
        data->channel_enable_mask = channel_enable_mask;

    spin_unlock_restore(&data->lock, exceptions, SPIN_LOCK_FLAG_IRQ_FIQ);

    return NO_ERROR;
}

static int stm32_ipcc_mbox_complete(const struct mbox_desc *desc,
                                    unsigned int id)
{
    struct ipcc_pdata *pdata = (struct ipcc_pdata *)desc->priv;
    struct stm32_ipcc_mbx_data *data = &pdata->data;

    if (!(BIT(id) & data->channel_enable_mask))
        return ERR_INVALID_ARGS;

    return NO_ERROR;
}

static uint32_t stm32_ipcc_mbox_capabilities(const struct mbox_desc *desc)
{
    struct ipcc_pdata *pdata = (struct ipcc_pdata *)desc->priv;
    struct stm32_ipcc_mbx_data *data = &pdata->data;

    return data->caps;
}

static const struct mbox_ops stm32_ipcc_mbox_ops = {
    .send = stm32_ipcc_mbox_send,
    .register_callback = stm32_ipcc_mbox_register_callback,
    .max_data_size = stm32_ipcc_mbox_get_mtu,
    .max_channel_count = stm32_ipcc_mbox_channel_count,
    .channel_interrupt = stm32_ipcc_mbox_enable,
    .complete = stm32_ipcc_mbox_complete,
    .capabilities = stm32_ipcc_mbox_capabilities,
};

static void stm32_ipcc_mbox_init(struct ipcc_fdt *fdt, struct ipcc_pdata *pdata)
{
    uint32_t i = 0;
    struct stm32_ipcc_mbx_data *data = &pdata->data;
    struct mbox_desc *desc = NULL;

    /* Null phandle means no client of this node, no mailbox support */
/* TODO
    if (!fdt_get_phandle(fdt, node))
        return;
*/

    pdata->itr_num[IPCC_ITR_RXO] = fdt->itr_num[IPCC_ITR_RXO];
    if (pdata->itr_num[IPCC_ITR_RXO] == SIZE_MAX)
        panic("rx interrupt needed !\n");
    data->caps = MBOX_RX_NOTIF_CAP;

    pdata->itr_num[IPCC_ITR_TXF] = fdt->itr_num[IPCC_ITR_TXF];
    if (pdata->itr_num[IPCC_ITR_TXF] != SIZE_MAX) {
        /*
         * This support is possible, if non secure is not using ipcc
         * channel , this limitation come with channel interrupt mask
         * that can not be written in an atomic access , on ip version
         * major 2 and minor 0
         */
        uint32_t ver = io_read32(pdata->base + IPCC_VER);
        uint8_t major = ver >> 4 & 0xf;
        uint8_t minor = ver & 0xf;

        if (major == 0x2 && minor == 0)
            panic("tx intterupt not supported !\n");
        data->caps |= MBOX_TX_NOTIF_CAP;
    }

    desc = calloc(1, sizeof(*desc));
    if (!desc)
        panic();

    desc->ops = &stm32_ipcc_mbox_ops;
    desc->priv = pdata;

    /* Disable secure rxo and txf interrupts */
    io_clrbits32(pdata->lbase + IPCC_CR, IPCC_CR_SECTXFIE |
             IPCC_CR_SECRXOIE);

    /* Fill max Channel */
    data->num_ch = io_read32(pdata->base + IPCC_HWCFGR) &
        IPCC_HWCFGR_CHAN_MASK;

    /* Set channel_sec_mask according to rif protection */
    data->channel_sec_mask = io_read32(pdata->lbase + IPCC_SECCFGR);
    data->channel_enable_mask = 0;

    /* Fix Me : Possibly Add Check on cid filtering and privileged */

    for (i = 0; i < data->num_ch; i++) {
        /* Clear rxo status */
        io_write32(pdata->lbase + IPCC_SCR, BIT(i));
        /* Mask channel rxo and txf interrupts */
        ipcc_channel_receive(pdata->lbase, i, false);
        ipcc_channel_transmit(pdata->lbase, i, false);
    }
    /* Register irq handler */
    register_int_handler(pdata->itr_num[IPCC_ITR_RXO], stm32_ipcc_mbox_rxo_isr, pdata);

    if (data->caps & MBOX_TX_NOTIF_CAP) {
        register_int_handler(pdata->itr_num[IPCC_ITR_TXF], stm32_ipcc_mbox_txf_isr, pdata);

        unmask_interrupt(pdata->itr_num[IPCC_ITR_TXF]);
    }
    unmask_interrupt(pdata->itr_num[IPCC_ITR_RXO]);

    /* Register device to mailbox framework */
    if (mbox_dt_register(fdt, desc))
        panic("Failed to register ipcc mbox");
}

static int parse_dt(struct ipcc_fdt *fdt, struct ipcc_pdata *ipcc_d)
{
    const uint32_t *cuint = NULL;
    struct io_pa_va addr = { };

    assert(fdt->reg != DT_INFO_INVALID_REG &&
           fdt->reg_size != DT_INFO_INVALID_REG_SIZE);

    addr.pa = fdt->reg;
    ipcc_d->base = io_pa_or_va_secure(&addr, fdt->reg_size);
    assert(ipcc_d->base);
    /*
     * Field st-proc-id, fixes CPU core on which driver is instantiated
     * according to proc ID mailbox lbase. rbase mailbox are swapped.
     */
    if (fdt->proc_id == 1) {
        ipcc_d->lbase = ipcc_d->base + IPCC_C2CR;
        ipcc_d->rbase = ipcc_d->base + IPCC_C1CR;
    } else {
        ipcc_d->lbase = ipcc_d->base + IPCC_C1CR;
        ipcc_d->rbase = ipcc_d->base + IPCC_C2CR;
    }

    /* Gate the IP */
    /*ipcc_d->ipcc_clock = fdt->ipcc_clock;*/

    /* M33 TDCID no RIF configuration on A35 */
    cuint = fdt->protreg;
    if (!cuint) {
        TLOGD("No RIF configuration available\n");
        return NO_ERROR;
    }

    return NO_ERROR;
}

int stm32_ipcc_probe(struct ipcc_fdt *fdt)
{
    int res = ERR_GENERIC;
    struct ipcc_pdata *ipcc_d = NULL;

    ipcc_d = calloc(1, sizeof(*ipcc_d));
    if (!ipcc_d)
        return ERR_NO_MEMORY;

    res = parse_dt(fdt, ipcc_d);
    if (res)
        goto err;
    list_add_tail(&ipcc_list, &ipcc_d->link);

     if (IS_ENABLED(CFG_DRIVERS_MAILBOX))
        stm32_ipcc_mbox_init(fdt, ipcc_d);

    return NO_ERROR;

err:
    /* Free all allocated resources */
    free(ipcc_d);

    return res;
}

