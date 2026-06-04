// SPDX-License-Identifier: BSD-2-Clause
/*
 * Copyright (C) 2024, STMicroelectronics
 */
#include <mailbox.h>

#include <assert.h>
#include <err.h>
#include <list.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <kernel/spinlock.h>

#define TLOG_TAG "mailbox"
#include <trusty_log.h>

/*
 * MBOX_EVENT_RX Receive a message
 * MBOX_EVENT_TX Transmit a message
 */
enum mbox_event {
    MBOX_EVENT_RX,
    MBOX_EVENT_TX,
    MBOX_EVENT_NUM
};

/*
 * struct mbox_chan - Mailbox channel handle
 *
 * @id: Channel identifier
 * @notify: Boolean reflecting notif_id being allocated
 * @notify_id: Async notification ID used for the mailbox events or 0
 * @cb: Notification callback function
 * @cookie: Consumer data callback parameter
 * @mbox_dev: Mailbox device info
 * @lock: Lock for mailbox channel concurrent access
 * @used: Boolean for marking channel usage
 * @rx_count: Counter of rx event received from device
 * @rx_done: Counter of rx event provided to channel consumer
 * @data: Pointer to data information from device
 */
struct mbox_chan {
    unsigned int id;
    bool notify[MBOX_EVENT_NUM];
    uint32_t notify_id[MBOX_EVENT_NUM];
    mbox_callback_t cb[MBOX_EVENT_NUM];
    void *cookie;
    struct mbox_dev *mbox_dev;
    spin_lock_t lock;
    bool used;
    size_t rx_count;
    size_t rx_done;
    void *data;
};

/*
 * struct mbox_dev - Mailbox device descriptor
 *
 * @phandle: Device's handle
 * @num_chan: Number of @hdl array element
 * $caps: Device capabilities
 * @desc: Mailbox descriptor
 * @link: Link to next mbox_dev
 * @hdl: Array for channel consumer handle
 */
struct mbox_dev {
    void *phandle;
    size_t num_chan;
    uint32_t caps;
    const struct mbox_desc *desc;
    struct list_node link;
    /* Flexible array size must be last */
    struct mbox_chan hdl[];
};

/* Device list variable */
static struct list_node mailbox_device_list = LIST_INITIAL_VALUE(mailbox_device_list);

/* Lock to prevent Device list concurrent access */
static spin_lock_t list_lock = SPIN_LOCK_INITIAL_VALUE;

static void mbox_process_cb(const struct mbox_dev *mbx_dev, unsigned int id,
                            bool notify, uint32_t notify_id, uint32_t evt)
{
    if (!mbx_dev->hdl[id].cb[evt] && notify) {
        /* Notify the context */
        /* notif_send_async(notify_id); */
    } else if (mbx_dev->hdl[id].cb[evt]) {
        /* Callback consumer function with data */
        mbx_dev->hdl[id].cb[evt](mbx_dev->hdl[id].cookie);
    }
}

/* Notifier rx callback with consumer data */
static void mbox_rx_callback(const struct mbox_dev *mbx_dev,
                             unsigned int id, void *data)
{
    spin_lock_saved_state_t exceptions = 0;
    bool notify = false;
    struct mbox_chan *handle = NULL;
    uint32_t notify_id = 0;

    if (id < mbx_dev->num_chan) {
        handle = (struct mbox_chan *)&mbx_dev->hdl[id];
        spin_lock_save(&handle->lock, &exceptions, SPIN_LOCK_FLAG_IRQ_FIQ);

        if (handle->used) {
            /* Increase reception count */
            handle->rx_count++;
            if (((size_t)(handle->rx_done + 1)) != handle->rx_count) {
                TLOGE("Received Event Not consumed\n");
                handle->rx_done++;
                assert(handle->rx_done == handle->rx_count);
            }
            /* Retrieve data */
            handle->data = data;
            notify = handle->notify[MBOX_EVENT_RX];
            notify_id = handle->notify_id[MBOX_EVENT_RX];
            spin_unlock_restore(&handle->lock, exceptions,
                                SPIN_LOCK_FLAG_IRQ_FIQ);
            mbox_process_cb(mbx_dev, id, notify, notify_id, MBOX_EVENT_RX);
            return;
        }
        spin_unlock_restore(&handle->lock, exceptions, SPIN_LOCK_FLAG_IRQ_FIQ);
    }
    TLOGE("Unexpected interrupt\n");
}

/* Notifier tx callback with consumer data */
static void mbox_tx_callback(const struct mbox_dev *mbx_dev,
                             unsigned int id)
{
    spin_lock_saved_state_t exceptions = 0;
    bool notify = false;
    uint32_t notify_id = 0;
    struct mbox_chan *handle = NULL;
    bool used = false;

    if (id >= mbx_dev->num_chan) {
        TLOGE("Unexpected interrupt\n");
        return;
    }
    handle = (struct mbox_chan *)&mbx_dev->hdl[id];
    spin_lock_save(&handle->lock, &exceptions, SPIN_LOCK_FLAG_IRQ_FIQ);
    notify = handle->notify[MBOX_EVENT_TX];
    notify_id = handle->notify_id[MBOX_EVENT_TX];
    used = mbx_dev->hdl[id].used;
    spin_unlock_restore(&handle->lock, exceptions, SPIN_LOCK_FLAG_IRQ_FIQ);
    if (used)
        mbox_process_cb(mbx_dev, id, notify, notify_id, MBOX_EVENT_TX);
    else
        TLOGE("Unused interrupt %u\n", id);
}

/* Driver interface */
static int mbox_register_internal(void *phandle,
                                  const struct mbox_desc *desc)
{
    struct mbox_dev *mdev = NULL;
    spin_lock_saved_state_t exceptions = 0;
    size_t num_chan = desc->ops->max_channel_count(desc);
    size_t i = 0;
    int res = ERR_GENERIC;

    /* Allocate a mailbox channel desc */
    mdev = calloc(1, sizeof(*mdev) + num_chan * sizeof(*mdev->hdl));
    if (!mdev)
        return ERR_NO_MEMORY;

    /* Populate mailbox channel desc */
    mdev->phandle = phandle;
    mdev->desc = desc;
    mdev->num_chan = num_chan;
    for (i = 0; i < num_chan; i++)
        mdev->hdl[i].lock = SPIN_LOCK_INITIAL_VALUE;
    /* Retrieve device capabilities, if device api is available*/
    if (desc->ops->capabilities)
        mdev->caps = desc->ops->capabilities(desc);
    else
        mdev->caps = MBOX_RX_NOTIF_CAP | MBOX_TX_NOTIF_CAP;

    /* Register incoming message mailbox Framework call bacK */
    res = desc->ops->register_callback(desc, mbox_rx_callback,
                                       mbox_tx_callback, mdev);
    if (!res) {
        /* Insert device in mailbox device list */
        spin_lock_save(&list_lock, &exceptions, SPIN_LOCK_FLAG_IRQ_FIQ);
        list_add_head(&mailbox_device_list, &mdev->link);
        spin_unlock_restore(&list_lock, exceptions, SPIN_LOCK_FLAG_IRQ_FIQ);
    } else {
        free(mdev);
    }
    return res;
}

int mbox_register(const struct mbox_desc *desc)
{
    return mbox_register_internal(NULL, desc);
}

int mbox_dt_register(void *phandle, const struct mbox_desc *desc)
{
    return mbox_register_internal(phandle, desc);
}

static int mbox_register_chan_internal(mbox_callback_t rcv_cb,
                                       mbox_callback_t txc_cb,
                                       void *cookie,
                                       struct mbox_dev *mbx_dev,
                                       unsigned int chan_id,
                                       struct mbox_chan **chan)
{
    int res = ERR_GENERIC;
    struct mbox_chan *hdl = NULL;
    spin_lock_saved_state_t exceptions = 0;
    const struct mbox_ops *ops = NULL;

    /* Check channel usable */
    if (chan_id >= mbx_dev->num_chan)
        return ERR_INVALID_ARGS;

    /*  Check Capability Support */
    if ((!(mbx_dev->caps & MBOX_RX_NOTIF_CAP) && rcv_cb) ||
        (!(mbx_dev->caps & MBOX_TX_NOTIF_CAP) && txc_cb)
       )
        return ERR_NOT_SUPPORTED;

    /* Protect against potential asynchronous event */
    spin_lock_save(&mbx_dev->hdl[chan_id].lock, &exceptions,
                   SPIN_LOCK_FLAG_IRQ_FIQ);
    if (mbx_dev->hdl[chan_id].used) {
        spin_unlock_restore(&mbx_dev->hdl[chan_id].lock, exceptions,
                            SPIN_LOCK_FLAG_IRQ_FIQ);
        return ERR_BAD_STATE;
    }

    /* Check callback against OPTEE configuration */
/*
    if (!rcv_cb || !txc_cb) {
        if (!IS_ENABLED(CFG_CORE_ASYNC_NOTIF)) {
*/
            /*
             * When no callback, recv API is blocking,
             * and async notif support is required.
             */
/*
            spin_unlock_restore(&mbx_dev->hdl[chan_id].lock, exceptions,
                                SPIN_LOCK_FLAG_IRQ_FIQ);
            return ERR_NOT_SUPPORTED;
        }
    }
*/

    /* Fill channel mailbox handle */
    hdl = &mbx_dev->hdl[chan_id];
    hdl->cb[MBOX_EVENT_RX] = rcv_cb;
    hdl->cb[MBOX_EVENT_TX] = txc_cb;

    hdl->cookie = cookie;
    hdl->mbox_dev = mbx_dev;
    hdl->id = chan_id;
/*
    if (!rcv_cb && (mbx_dev->caps & MBOX_RX_NOTIF_CAP)) {
        res = notif_alloc_async_value(&hdl->notify_id[MBOX_EVENT_RX]);
        if (res) {
            spin_unlock_restore(&mbx_dev->hdl[chan_id].lock, exceptions,
                                SPIN_LOCK_FLAG_IRQ_FIQ);
            return res;
        }
    }
    if (!txc_cb && (mbx_dev->caps & MBOX_TX_NOTIF_CAP)) {
        res = notif_alloc_async_value(&hdl->notify_id[MBOX_EVENT_TX]);
        if (res) {
            spin_unlock_restore(&mbx_dev->hdl[chan_id].lock, exceptions,
                                SPIN_LOCK_FLAG_IRQ_FIQ);
            return res;
        }
    }
*/
    hdl->used = true;
    spin_unlock_restore(&mbx_dev->hdl[chan_id].lock, exceptions,
                        SPIN_LOCK_FLAG_IRQ_FIQ);

    /* Enable mailbox channel */
    ops = mbx_dev->desc->ops;
    res = ops->channel_interrupt(mbx_dev->desc, true, chan_id);
    if (res) {
        spin_lock_save(&mbx_dev->hdl[chan_id].lock, &exceptions,
                       SPIN_LOCK_FLAG_IRQ_FIQ);
        hdl->used = false;
        spin_unlock_restore(&mbx_dev->hdl[chan_id].lock, exceptions,
                            SPIN_LOCK_FLAG_IRQ_FIQ);
        return res;
    }
    *chan = hdl;

    return NO_ERROR;
}

/*  Framework API */
int mbox_dt_register_chan_by_name(mbox_callback_t rcv_cb,
                                  mbox_callback_t txc_cb,
                                  void *cookie, const struct mbox_fdt *mboxes_fdt,
                                  size_t nmboxes, const char *name,
                                  struct mbox_chan **chan)
{
    size_t idx = 0;
    void *phandle;
    struct mbox_dev *mdev = NULL;
    struct mbox_dev *mbx_dev = NULL;
    spin_lock_saved_state_t exceptions = 0;
    unsigned int chan_id = 0;

    if (!mboxes_fdt || !name || !chan)
        return ERR_INVALID_ARGS;

    for (idx = 0; idx < nmboxes; ++idx)
        if (!strcmp(mboxes_fdt[idx].name, name))
            break;

    if (idx >= nmboxes) {
        TLOGE("dt field mbox-name not found\n");
        return ERR_GENERIC;
    }

    phandle = mboxes_fdt[idx].phandle;
    chan_id = mboxes_fdt[idx].chan_id;

    /* Search phandle matching a registered mailbox device */
    spin_lock_save(&list_lock, &exceptions, SPIN_LOCK_FLAG_IRQ_FIQ);
    list_for_every_entry(&mailbox_device_list, mdev, struct mbox_dev, link)
        if (phandle == mdev->phandle)
            mbx_dev = mdev;
    spin_unlock_restore(&list_lock, exceptions, SPIN_LOCK_FLAG_IRQ_FIQ);
    if (!mbx_dev)
        return ERR_NOT_READY;

    return mbox_register_chan_internal(rcv_cb, txc_cb, cookie, mbx_dev,
                                       chan_id, chan);
}

int mbox_dt_register_chan(mbox_callback_t rcv_cb,
                          mbox_callback_t txc_cb, void *cookie,
                          const struct mbox_fdt *fdt, size_t nmboxes,
                          struct mbox_chan **chan)
{
    void *phandle;
    uint32_t chan_id = 0;
    struct mbox_dev *mbx_dev = NULL;
    spin_lock_saved_state_t exceptions = 0;
    struct mbox_dev *mdev = NULL;

    if (!fdt || !chan)
        return ERR_INVALID_ARGS;

    phandle = fdt[0].phandle;
    chan_id = fdt[0].chan_id;

    /* Search phandle matching a registered mailbox device */
    spin_lock_save(&list_lock, &exceptions, SPIN_LOCK_FLAG_IRQ_FIQ);
    list_for_every_entry(&mailbox_device_list, mdev, struct mbox_dev, link)
        if (phandle == mdev->phandle)
            mbx_dev = mdev;
    spin_unlock_restore(&list_lock, exceptions, SPIN_LOCK_FLAG_IRQ_FIQ);
    if (!mbx_dev)
        return ERR_NOT_READY;

    return mbox_register_chan_internal(rcv_cb, txc_cb, cookie, mbx_dev,
                                       chan_id, chan);
}


int mbox_register_chan(mbox_callback_t rcv_cb,
                       mbox_callback_t txc_cb, void *cookie,
                       const struct mbox_desc *desc,
                       unsigned int chan_id,
                       struct mbox_chan **chan)
{
    struct mbox_dev *mbx_dev = NULL;
    struct mbox_dev *mdev = NULL;
    spin_lock_saved_state_t exceptions = 0;

    if (!chan || !desc)
        return ERR_INVALID_ARGS;
    /* Search mbox_desc matching a registered mailbox device */
    spin_lock_save(&list_lock, &exceptions, SPIN_LOCK_FLAG_IRQ_FIQ);
    list_for_every_entry(&mailbox_device_list, mdev, struct mbox_dev, link)
        if (desc == mdev->desc)
            mbx_dev = mdev;
    spin_unlock_restore(&list_lock, exceptions, SPIN_LOCK_FLAG_IRQ_FIQ);
    if (!mbx_dev)
        return ERR_NOT_READY;

    return mbox_register_chan_internal(rcv_cb, txc_cb, cookie, mbx_dev,
                                       chan_id, chan);
}

int mbox_data_max_size(const struct mbox_chan *handle, size_t *size)
{
    const struct mbox_ops *ops = handle->mbox_dev->desc->ops;

    *size = ops->max_data_size(handle->mbox_dev->desc);

    return NO_ERROR;
}

int mbox_send(const struct mbox_chan *handle, bool wait,
              const void *data, size_t size)
{
    struct mbox_chan *hdl = (struct mbox_chan *)handle;
    const struct mbox_ops *ops = hdl->mbox_dev->desc->ops;
    spin_lock_saved_state_t except = 0;
    int res = ERR_GENERIC;

    /* TODO: JQ: support wait */
    if (wait)
        return ERR_NOT_SUPPORTED;

    if (wait && !(hdl->mbox_dev->caps & MBOX_TX_NOTIF_CAP))
        return ERR_NOT_SUPPORTED;

    if (wait && hdl->cb[MBOX_EVENT_TX])
        return ERR_INVALID_ARGS;

    if (ops->max_data_size(hdl->mbox_dev->desc) < size)
        return ERR_TOO_BIG;

    spin_lock_save(&hdl->lock, &except, SPIN_LOCK_FLAG_IRQ_FIQ);
    if (hdl->notify[MBOX_EVENT_TX]) {
        spin_unlock_restore(&hdl->lock, except, SPIN_LOCK_FLAG_IRQ_FIQ);
        TLOGE("Unexpected simultaneous send on a channel\n");
        return ERR_BAD_STATE;
    }
/*
    if (wait)
        hdl->notify[MBOX_EVENT_TX] = true;
*/
    spin_unlock_restore(&hdl->lock, except, SPIN_LOCK_FLAG_IRQ_FIQ);
    res = ops->send(hdl->mbox_dev->desc, hdl->id, data, size);
/*
    if (wait) {
        if (!res)
            notif_wait(hdl->notify_id[MBOX_EVENT_TX]);
        spin_lock_save(&hdl->lock, &except, SPIN_LOCK_FLAG_IRQ_FIQ);
        hdl->notify[MBOX_EVENT_TX] = false;
        spin_unlock_restore(&hdl->lock, except, SPIN_LOCK_FLAG_IRQ_FIQ);
    }
*/
    return res;
}

int mbox_recv(const struct mbox_chan *handle, bool wait,
              void *data, size_t size)
{
    struct mbox_chan *hdl = (struct mbox_chan *)handle;
    const struct mbox_ops *ops = hdl->mbox_dev->desc->ops;
    spin_lock_saved_state_t except = 0;
    void *copy_data = NULL;
    vaddr_t s = 0;

    /* TODO: JQ: support wait */
    if (wait)
        return ERR_NOT_SUPPORTED;

    if (wait && !(hdl->mbox_dev->caps & MBOX_RX_NOTIF_CAP))
        return ERR_NOT_SUPPORTED;

    if (wait && hdl->cb[MBOX_EVENT_RX])
        return ERR_INVALID_ARGS;

    if (ops->max_data_size(hdl->mbox_dev->desc) < size)
        return ERR_TOO_BIG;

    if ((size && !data) ||
        (data && size && __builtin_add_overflow((vaddr_t)data, size, &s)))
        return ERR_INVALID_ARGS;

    spin_lock_save(&hdl->lock, &except, SPIN_LOCK_FLAG_IRQ_FIQ);
    if (hdl->notify[MBOX_EVENT_RX]) {
        spin_unlock_restore(&hdl->lock, except, SPIN_LOCK_FLAG_IRQ_FIQ);
        return ERR_BAD_STATE;
    }

    if (hdl->rx_count == hdl->rx_done) {
        if (!wait) {
            spin_unlock_restore(&hdl->lock, except, SPIN_LOCK_FLAG_IRQ_FIQ);
            return ERR_NO_MSG;
        }
        /*
        hdl->notify[MBOX_EVENT_RX] = true;
        spin_unlock_restore(&hdl->lock, except, SPIN_LOCK_FLAG_IRQ_FIQ);
        notif_wait(hdl->notify_id[MBOX_EVENT_RX]);
        spin_lock_save(&hdl->lock, &except, SPIN_LOCK_FLAG_IRQ_FIQ);
        hdl->notify[MBOX_EVENT_RX] = false;
        */
    }

    if (size && !hdl->data) {
        spin_unlock_restore(&hdl->lock, except, SPIN_LOCK_FLAG_IRQ_FIQ);
        return ERR_INVALID_ARGS;
    }

    copy_data = hdl->data;
    hdl->data = NULL;
    hdl->rx_done++;
    spin_unlock_restore(&hdl->lock, except, SPIN_LOCK_FLAG_IRQ_FIQ);
    if (size)
        memcpy(data, copy_data, size);
    /* Acknowledge receive interrupt */
    ops->complete(hdl->mbox_dev->desc, hdl->id);

    return NO_ERROR;
}
