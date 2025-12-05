/*************************************************************************
@File Name: mctp_bridge.c
@Desc: MCTP Bridge Kernel Module
@Author: Andy-wei.hou
@Mail: wei.hou@scaleflux.com
@Created Time: 2025-11-19 18:39:43
@Log: Fixed for Linux kernel 6.14+
************************************************************************/
// mctp_bridge.c – Fixed for Linux kernel 6.14+
// Changes:
// 1. Added #include <linux/skbuff.h>
// 2. Removed dev->tx_queue usage
// 3. Correctly use struct sk_buff_head
// 4. Clean minimal bridge prototype

#include <linux/module.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/etherdevice.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/wait.h>
#include <linux/poll.h>
#include <linux/mutex.h>
#include <linux/if_arp.h>
#include <net/mctp.h>

#include <linux/version.h>

/*
 * Kernel 6.4 removed class_create(THIS_MODULE, name),
 * replacing it with class_create(name).
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6,4,0)
#define CLASS_CREATE(name) class_create(name)
#else
#define CLASS_CREATE(name) class_create(THIS_MODULE, name)
#endif


#define DRV_NAME "mctp_bridge"
#define DEV_NAME "mctp_bridge"
#define DEFAULT_DEVNAME "mctp_bridge0"
#define CHAR_DEV_BUF_MAX (64 * 1024)
#define MCTP_DEFAULT_EID 8  /* Default MCTP Endpoint ID */
#define MCTP_MTU 1024       /* MCTP Maximum Transmission Unit */

static struct net_device *mbridge_dev;
static dev_t mbridge_devnum;
static struct cdev mbridge_cdev;
static struct class *mbridge_class;

/* correct type for skb queue */
static struct sk_buff_head tx_to_daemon;
static wait_queue_head_t tx_wq;
static DEFINE_MUTEX(tx_queue_lock);  // Add mutex lock

/* netdev ops */
static netdev_tx_t mbridge_start_xmit(struct sk_buff *skb, struct net_device *dev);
static int mbridge_open(struct net_device *dev);
static int mbridge_stop(struct net_device *dev);

static const struct net_device_ops mbridge_netdev_ops = {
    .ndo_open       = mbridge_open,
    .ndo_stop       = mbridge_stop,
    .ndo_start_xmit = mbridge_start_xmit,
};

static int mbridge_open(struct net_device *dev)
{
    netif_start_queue(dev);
    netif_carrier_on(dev);
    pr_info("%s: opened\n", dev->name);
    return 0;
}

static int mbridge_stop(struct net_device *dev)
{
    netif_stop_queue(dev);
    netif_carrier_off(dev);
    pr_info("%s: stopped\n", dev->name);
    return 0;
}

static netdev_tx_t mbridge_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
    struct sk_buff *nskb;

    nskb = skb_clone(skb, GFP_ATOMIC);
    if (!nskb) {
        dev->stats.tx_dropped++;
        dev_kfree_skb_any(skb);
        return NETDEV_TX_OK;
    }

    /* Queue for the char device reader */
    mutex_lock(&tx_queue_lock);  // Lock

    /* Print SKB key information and first 5 bytes */
    dev_dbg(&dev->dev, "%s SKB info: len=%u, protocol=0x%04x, data=%*ph\n",
            __func__,nskb->len,  ntohs(nskb->protocol), 5, nskb->data);

    skb_queue_tail(&tx_to_daemon, nskb);
    mutex_unlock(&tx_queue_lock);  // Unlock
    wake_up_interruptible(&tx_wq);

    dev->stats.tx_packets++;
    dev->stats.tx_bytes += nskb->len;

    dev_kfree_skb_any(skb);
    return NETDEV_TX_OK;
}

/* char device read/write */
static ssize_t mbridge_chr_read(struct file *file, char __user *buf,
                                size_t count, loff_t *ppos)
{
    struct sk_buff *skb;
    size_t to_copy;
    int ret = 0;

    if (count == 0)
        return 0;

    if (*ppos != 0)
        return 0;

    mutex_lock(&tx_queue_lock);
    skb = skb_dequeue(&tx_to_daemon);
    mutex_unlock(&tx_queue_lock);

    if (!skb) {
        if (file->f_flags & O_NONBLOCK)
            return -EAGAIN;

        ret = wait_event_interruptible(tx_wq,
                !skb_queue_empty(&tx_to_daemon));
        if (ret)
            return ret;

        mutex_lock(&tx_queue_lock);
        skb = skb_dequeue(&tx_to_daemon);
        mutex_unlock(&tx_queue_lock);

        if (!skb)
            return -EIO;
    }

    /* Print SKB key information and first 5 bytes */
    dev_dbg(&mbridge_dev->dev, "%s SKB info: len=%u, protocol=0x%04x, data=%*ph\n",
            __func__, skb->len,  ntohs(skb->protocol), 5, skb->data);

    to_copy = min(count, (size_t)skb->len);

    if (copy_to_user(buf, skb->data, to_copy)) {
        kfree_skb(skb);
        return -EFAULT;
    }

    kfree_skb(skb);
    return to_copy;
}

static ssize_t mbridge_chr_write(struct file *file, const char __user *buf,
                                 size_t count, loff_t *ppos)
{
    struct sk_buff *skb;
    void *data;

    if (!mbridge_dev)
        return -ENODEV;

    if (count == 0)
        return 0;

    if (count > CHAR_DEV_BUF_MAX)
        return -E2BIG;

    if (*ppos != 0)
        return 0;

    skb = netdev_alloc_skb_ip_align(mbridge_dev, count + NET_IP_ALIGN);
    if (!skb)
        return -ENOMEM;

    data = skb_put(skb, count);

    if (copy_from_user(data, buf, count)) {
        kfree_skb(skb);
        return -EFAULT;
    }

    skb->dev = mbridge_dev;
    skb->protocol = htons(ETH_P_MCTP);  // Set to MCTP protocol
    /*init magic for mctp cb*/
    __mctp_cb(skb);


    skb->mac_header = skb->data;
    skb_reset_network_header(skb);

    mbridge_dev->stats.rx_packets++;
    mbridge_dev->stats.rx_bytes += count;


   /* Print SKB key information and first bytes */
    dev_dbg(&mbridge_dev->dev, "%s SKB info: len=%u, protocol=0x%04x, data=%*ph\n",
            __func__, skb->len,  ntohs(skb->protocol), (int)count, skb->data);

    if (netif_rx(skb) != NET_RX_SUCCESS) {
        dev_err(&mbridge_dev->dev, "Failed to enqueue packet for processing\n");
        dev_kfree_skb_any(skb);
        return -EIO;
    }

    return count;
}

static unsigned int mbridge_chr_poll(struct file *file, poll_table *wait)
{
    unsigned int mask = 0;

    poll_wait(file, &tx_wq, wait);

    if (!skb_queue_empty(&tx_to_daemon))
        mask |= POLLIN | POLLRDNORM;

    return mask;
}

static const struct file_operations mbridge_fops = {
    .owner   = THIS_MODULE,
    .read    = mbridge_chr_read,
    .write   = mbridge_chr_write,
    .poll    = mbridge_chr_poll,
};

// Custom MCTP device setup function
static void mctp_setup(struct net_device *dev)
{
    dev->type = ARPHRD_MCTP;  // Use MCTP device type

    dev->mtu = MCTP_MTU;  // Set MCTP MTU
    dev->hard_header_len = 0;  // MCTP doesn't require hardware header
    dev->addr_len = 0;
    dev->flags = IFF_NOARP;  // MCTP doesn't require ARP

    // Set MCTP-specific parameters
    // Set endpoint ID in device address
    // memset(dev->dev_addr, 0, sizeof(dev->dev_addr));
    // dev->dev_addr[0] = MCTP_DEFAULT_EID;
    //
    dev->netdev_ops = &mbridge_netdev_ops;
}

/* init / exit */
static int __init mbridge_init(void)
{
    int ret;

    skb_queue_head_init(&tx_to_daemon);
    init_waitqueue_head(&tx_wq);

    mbridge_dev = alloc_netdev(0, DEFAULT_DEVNAME, NET_NAME_ENUM, mctp_setup);
    if (!mbridge_dev)
        return -ENOMEM;

    ret = register_netdev(mbridge_dev);
    if (ret)
        goto err_free_netdev;

    /* char device for userspace read/write/poll*/
    ret = alloc_chrdev_region(&mbridge_devnum, 0, 1, DEV_NAME);
    if (ret)
        goto err_unregister_netdev;

    cdev_init(&mbridge_cdev, &mbridge_fops);
    ret = cdev_add(&mbridge_cdev, mbridge_devnum, 1);
    if (ret)
        goto err_unregister_chrdev;

    mbridge_class = CLASS_CREATE("mctp_bridge_class");
    if (IS_ERR(mbridge_class)) {
        ret = PTR_ERR(mbridge_class);
        pr_err("mctp_bridge: failed to create class\n");
        goto err_cdev_del;
    }

    device_create(mbridge_class, NULL, mbridge_devnum, NULL, DEV_NAME);

    pr_info("mctp_bridge: loaded OK\n");
    return 0;

err_cdev_del:
    cdev_del(&mbridge_cdev);
err_unregister_chrdev:
    unregister_chrdev_region(mbridge_devnum, 1);
err_unregister_netdev:
    unregister_netdev(mbridge_dev);
err_free_netdev:
    free_netdev(mbridge_dev);
    return ret;
}

static void __exit mbridge_exit(void)
{
    if (mbridge_class && !IS_ERR(mbridge_class)) {
        device_destroy(mbridge_class, mbridge_devnum);
        class_destroy(mbridge_class);
    }

    if (mbridge_devnum) {
        cdev_del(&mbridge_cdev);
        unregister_chrdev_region(mbridge_devnum, 1);
    }

    if (mbridge_dev) {
        unregister_netdev(mbridge_dev);
        free_netdev(mbridge_dev);
    }

    mutex_lock(&tx_queue_lock);  // Lock during cleanup
    skb_queue_purge(&tx_to_daemon);
    mutex_unlock(&tx_queue_lock);  // Unlock

    pr_info("mctp_bridge: unloaded\n");
}

module_init(mbridge_init);
module_exit(mbridge_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("mctp bridge");
MODULE_DESCRIPTION("mctp_bridge netdev + char device for BridgeDaemon");

