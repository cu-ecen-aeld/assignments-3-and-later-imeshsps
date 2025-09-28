/**
 * @file main.c
 * @brief AESD char driver implementation (C90 compliant) with llseek and ioctl support
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
#include <linux/uaccess.h>  /* copy_to_user, copy_from_user */

#include "aesdchar.h"
#include "aesd-circular-buffer.h"
#include "aesd_ioctl.h"

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

/* Helper function to calculate total buffer size */
static size_t aesd_get_total_size(struct aesd_circular_buffer *buffer)
{
    size_t total_size = 0;
    uint8_t i;
    struct aesd_buffer_entry *entry;
    
    for (i = 0; i < AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED; i++) {
        entry = &buffer->entry[i];
        if (entry->buffptr) {
            total_size += entry->size;
        }
    }
    return total_size;
}

/* Helper function to convert command index and offset to absolute file position */
static loff_t aesd_cmd_offset_to_fpos(struct aesd_circular_buffer *buffer, 
                                     uint32_t cmd_idx, uint32_t cmd_offset)
{
    loff_t fpos = 0;
    uint8_t i;
    uint8_t actual_idx;
    struct aesd_buffer_entry *entry;
    
    /* Calculate the actual buffer index considering out_offs */
    for (i = 0; i < cmd_idx; i++) {
        actual_idx = (buffer->out_offs + i) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
        entry = &buffer->entry[actual_idx];
        if (entry->buffptr) {
            fpos += entry->size;
        }
    }
    
    fpos += cmd_offset;
    return fpos;
}

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
            break;  /* No more entries */
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

/* llseek implementation */
loff_t aesd_llseek(struct file *filp, loff_t off, int whence)
{
    struct aesd_dev *dev = filp->private_data;
    loff_t newpos;
    size_t total_size;
    
    MUTEX_LOCK(&dev->lock);
    
    total_size = aesd_get_total_size(&dev->circ_buf);
    
    switch (whence) {
        case SEEK_SET:
            newpos = off;
            break;
            
        case SEEK_CUR:
            newpos = filp->f_pos + off;
            break;
            
        case SEEK_END:
            newpos = total_size + off;
            break;
            
        default:
            MUTEX_UNLOCK(&dev->lock);
            return -EINVAL;
    }
    
    /* Check bounds */
    if (newpos < 0 || newpos > total_size) {
        MUTEX_UNLOCK(&dev->lock);
        return -EINVAL;
    }
    
    filp->f_pos = newpos;
    MUTEX_UNLOCK(&dev->lock);
    
    return newpos;
}

/* ioctl implementation */
long aesd_unlocked_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    struct aesd_dev *dev = filp->private_data;
    struct aesd_seekto seekto;
    uint8_t cmd_count = 0;
    uint8_t i;
    uint8_t actual_idx;
    struct aesd_buffer_entry *entry;
    loff_t new_fpos;
    
    /* Check magic number and command number */
    if (_IOC_TYPE(cmd) != AESD_IOC_MAGIC) return -ENOTTY;
    if (_IOC_NR(cmd) > AESDCHAR_IOC_MAXNR) return -ENOTTY;
    
    switch (cmd) {
        case AESDCHAR_IOCSEEKTO:
            if (copy_from_user(&seekto, (struct aesd_seekto __user *)arg, sizeof(seekto))) {
                return -EFAULT;
            }
            
            MUTEX_LOCK(&dev->lock);
            
            /* Count valid commands in circular buffer */
            for (i = 0; i < AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED; i++) {
                actual_idx = (dev->circ_buf.out_offs + i) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
                entry = &dev->circ_buf.entry[actual_idx];
                if (entry->buffptr) {
                    cmd_count++;
                } else {
                    break;
                }
            }
            
            /* Validate command index */
            if (seekto.write_cmd >= cmd_count) {
                MUTEX_UNLOCK(&dev->lock);
                return -EINVAL;
            }
            
            /* Get the specific command entry */
            actual_idx = (dev->circ_buf.out_offs + seekto.write_cmd) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
            entry = &dev->circ_buf.entry[actual_idx];
            
            /* Validate offset within command */
            if (seekto.write_cmd_offset >= entry->size) {
                MUTEX_UNLOCK(&dev->lock);
                return -EINVAL;
            }
            
            /* Calculate new file position */
            new_fpos = aesd_cmd_offset_to_fpos(&dev->circ_buf, seekto.write_cmd, seekto.write_cmd_offset);
            filp->f_pos = new_fpos;
            
            MUTEX_UNLOCK(&dev->lock);
            return 0;
            
        default:
            return -ENOTTY;
    }
}

/* File operations structure */
struct file_operations aesd_fops = {
    .owner = THIS_MODULE,
    .read = aesd_read,
    .write = aesd_write,
    .open = aesd_open,
    .release = aesd_release,
    .llseek = aesd_llseek,
    .unlocked_ioctl = aesd_unlocked_ioctl,
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