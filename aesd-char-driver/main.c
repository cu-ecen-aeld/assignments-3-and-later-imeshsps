/**
 * @file main.c
 * @brief AESD char driver implementation (C90 compliant)
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/fs.h>       /* file_operations */
#include <linux/slab.h>     /* kmalloc, kfree */
#include <linux/kernel.h>   /* min() macro */
#include <linux/mutex.h>    /* mutex */

#include "aesdchar.h"
#include "aesd-circular-buffer.h"

int aesd_major = 0;  /* dynamic major */
int aesd_minor = 0;

MODULE_AUTHOR("Imesh Sachinda");
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

/* Define MUTEX_LOCK and MUTEX_UNLOCK macros if not already defined */
#ifndef MUTEX_LOCK
#define MUTEX_LOCK(lock) mutex_lock(lock)
#endif

#ifndef MUTEX_UNLOCK
#define MUTEX_UNLOCK(lock) mutex_unlock(lock)
#endif

/* Open */
int aesd_open(struct inode *inode, struct file *filp)
{
    PDEBUG("open");
    filp->private_data = &aesd_device;
    return 0;
}

/* Release */
int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("release");
    return 0;
}

/* Read */
ssize_t aesd_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
    ssize_t bytes_read = 0;
    size_t offset;
    struct aesd_buffer_entry *entry;
    struct aesd_dev *dev = filp->private_data;
    
    MUTEX_LOCK(&dev->lock);
    
    while (bytes_read < count) {
        entry = aesd_circular_buffer_find_entry_offset_for_fpos(&dev->circ_buf, *f_pos, &offset);
        if (!entry || !entry->buffptr) {
            break;  // No more entries
        }
        
        size_t bytes_to_copy = min(count - bytes_read, entry->size - offset);
        if (copy_to_user(buf + bytes_read, entry->buffptr + offset, bytes_to_copy)) {
            MUTEX_UNLOCK(&dev->lock);
            return -EFAULT;
        }
        
        bytes_read += bytes_to_copy;
        *f_pos += bytes_to_copy;
    }
    
    MUTEX_UNLOCK(&dev->lock);
    return bytes_read;
}

/* Write */
ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
    ssize_t retval;
    char *kbuf;
    char *entry_buf;
    char *combined;
    size_t start;
    size_t i;
    size_t len;
    struct aesd_buffer_entry new_entry;
    struct aesd_buffer_entry *last_entry;
    struct aesd_dev *dev = filp->private_data;

    PDEBUG("write %zu bytes with offset %lld", count, *f_pos);

    if (!buf || count == 0)
        return -EINVAL;

    kbuf = kmalloc(count, GFP_KERNEL);
    if (!kbuf)
        return -ENOMEM;

    if (copy_from_user(kbuf, buf, count)) {
        kfree(kbuf);
        return -EFAULT;
    }

    retval = count;

    mutex_lock(&dev->lock);

    /* Check last entry */
    last_entry = NULL;
    if (dev->circ_buf.in_offs != 0 || dev->circ_buf.full) {
        last_entry = &dev->circ_buf.entry[(dev->circ_buf.in_offs + AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED - 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED];
    }

    if (last_entry && last_entry->size > 0 && last_entry->buffptr[last_entry->size - 1] != '\n') {
        combined = kmalloc(last_entry->size + count, GFP_KERNEL);
        if (!combined) {
            mutex_unlock(&dev->lock);
            kfree(kbuf);
            return -ENOMEM;
        }
        memcpy(combined, last_entry->buffptr, last_entry->size);
        memcpy(combined + last_entry->size, kbuf, count);
        kfree((void*)last_entry->buffptr);  /* Cast away const for freeing */
        last_entry->buffptr = combined;
        last_entry->size += count;

        mutex_unlock(&dev->lock);
        kfree(kbuf);
        return retval;
    }

    /* Split writes terminated by '\n' */
    start = 0;
    for (i = 0; i < count; i++) {
        if (kbuf[i] == '\n' || i == count - 1) {
            len = i - start + 1;
            entry_buf = kmalloc(len, GFP_KERNEL);
            if (!entry_buf) {
                mutex_unlock(&dev->lock);
                kfree(kbuf);
                return -ENOMEM;
            }
            memcpy(entry_buf, kbuf + start, len);

            new_entry.buffptr = entry_buf;  /* This is now valid since buffptr is const char* */
            new_entry.size = len;

            /* Check if we need to free an old entry before overwriting */
            if (dev->circ_buf.full && dev->circ_buf.entry[dev->circ_buf.in_offs].buffptr) {
                kfree((void*)dev->circ_buf.entry[dev->circ_buf.in_offs].buffptr);
            }

            aesd_circular_buffer_add_entry(&dev->circ_buf, &new_entry);
            start = i + 1;
        }
    }

    mutex_unlock(&dev->lock);
    kfree(kbuf);

    return retval;
}

/* File operations structure */
struct file_operations aesd_fops = {
    .owner = THIS_MODULE,
    .read = aesd_read,
    .write = aesd_write,
    .open = aesd_open,
    .release = aesd_release,
};

/* Setup cdev */
static int aesd_setup_cdev(struct aesd_dev *dev)
{
    int err;
    dev_t devno = MKDEV(aesd_major, aesd_minor);

    cdev_init(&dev->cdev, &aesd_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &aesd_fops;

    err = cdev_add(&dev->cdev, devno, 1);
    if (err) {
        printk(KERN_ERR "Error %d adding aesd cdev", err);
    }
    return err;
}

/* Module init */
int aesd_init_module(void)
{
    dev_t dev = 0;
    int result;

    result = alloc_chrdev_region(&dev, aesd_minor, 1, "aesdchar");
    aesd_major = MAJOR(dev);
    if (result < 0) {
        printk(KERN_WARNING "Can't get major %d\n", aesd_major);
        return result;
    }

    memset(&aesd_device, 0, sizeof(struct aesd_dev));

    /* Initialize AESD circular buffer and mutex */
    aesd_circular_buffer_init(&aesd_device.circ_buf);
    mutex_init(&aesd_device.lock);

    result = aesd_setup_cdev(&aesd_device);

    if (result) {
        unregister_chrdev_region(dev, 1);
    }

    return result;
}

/* Module exit */
void aesd_cleanup_module(void)
{
    uint8_t i;
    struct aesd_buffer_entry *entry;
    dev_t devno = MKDEV(aesd_major, aesd_minor);

    cdev_del(&aesd_device.cdev);

    /* Free all allocated memory in circular buffer */
    for (i = 0; i < AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED; i++) {
        entry = &aesd_device.circ_buf.entry[i];
        if (entry->buffptr)
            kfree((void*)entry->buffptr);  /* Cast away const for freeing */
    }

    unregister_chrdev_region(devno, 1);
}

module_init(aesd_init_module);
module_exit(aesd_cleanup_module);