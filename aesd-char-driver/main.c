/**
 * @file aesdchar.c
 * @brief Functions and data related to the AESD char driver implementation
 *
 * Based on the implementation of the "scull" device driver, found in
 * Linux Device Drivers example code.
 *
 * @author Dan Walkes
 * @date 2019-10-22
 * @copyright Copyright (c) 2019
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/fs.h> // file_operations
#include "aesdchar.h"

#include <linux/slab.h>     // For kmalloc, krealloc, kfree
#include <linux/uaccess.h>  // For copy_to_user, copy_from_user
#include <linux/string.h>   // For memchr
#include <linux/mutex.h>    // For mutexes

int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

MODULE_AUTHOR("Varun Shah"); /** TODO: fill in your name **/
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

int aesd_open(struct inode *inode, struct file *filp)
{
    struct aesd_dev *dev;
    PDEBUG("open");
    /**
     * TODO: handle open
     */
    dev = container_of(inode->i_cdev, struct aesd_dev, cdev);
    filp->private_data = dev;
    return 0;
}

int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("release");
    /**
     * TODO: handle release
     */
    return 0;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                loff_t *f_pos)
{
    
    ssize_t retval = 0;

    // 1. Extract your device pointer
    struct aesd_dev *dev = filp->private_data;
    struct aesd_buffer_entry *entry = NULL;
    size_t entry_offset = 0;
    size_t bytes_to_read = 0;
    unsigned long bytes_not_copied = 0;

    PDEBUG("read %zu bytes with offset %lld",count,*f_pos);
    /**
     * TODO: handle read
     */
    // 2. Lock the mutex
    if (mutex_lock_interruptible(&dev->lock)) {
        return -ERESTARTSYS; // Return safely if interrupted by a signal
    }

    // 3. Find the entry and offset for the current file position
    entry = aesd_circular_buffer_find_entry_offset_for_fpos(&dev->circular_buffer, *f_pos, &entry_offset);

    // 4. If it returns NULL, we are at the end of the file (EOF)
    if (entry == NULL) {
        mutex_unlock(&dev->lock);
        return 0; 
    }

    // 5. Calculate how many bytes we can actually read from this specific entry
    bytes_to_read = entry->size - entry_offset;

    // 6. If the user requested fewer bytes than available, only send what they asked for
    if (bytes_to_read > count) {
        bytes_to_read = count;
    }

    // 7. Safely copy the data to user space
    // copy_to_user returns the number of bytes that FAILED to copy (0 means success)
    bytes_not_copied = copy_to_user(buf, entry->buffptr + entry_offset, bytes_to_read);
    
    if (bytes_not_copied != 0) {
        mutex_unlock(&dev->lock);
        return -EFAULT; // Bad memory address error
    }

    // 8. Update the file position and our return value
    *f_pos += bytes_to_read;
    retval = bytes_to_read;

    // 9. Unlock the mutex
    mutex_unlock(&dev->lock);
    return retval;
}

// ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
//                 loff_t *f_pos)
// {
//     ssize_t retval = -ENOMEM;
//     PDEBUG("write %zu bytes with offset %lld",count,*f_pos);
//     /**
//      * TODO: handle write
//      */
//     return retval;
// }
ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
    ssize_t retval = -ENOMEM;
    
    // 1. Extract your device pointer and declare variables
    struct aesd_dev *dev = filp->private_data;
    char *new_buffer = NULL;
    unsigned long bytes_not_copied = 0;
    const char *overwritten_buffptr = NULL;

    PDEBUG("write %zu bytes with offset %lld", count, *f_pos);

    // 2. Lock the mutex
    if (mutex_lock_interruptible(&dev->lock)) {
        return -ERESTARTSYS;
    }

    // 3. Allocate/Reallocate memory
    // krealloc handles both the initial allocation (if buffptr is NULL) and resizing
    new_buffer = krealloc(dev->partial_entry.buffptr, dev->partial_entry.size + count, GFP_KERNEL);
    if (!new_buffer) {
        mutex_unlock(&dev->lock);
        return -ENOMEM;
    }

    // Update the partial entry with the new pointer (krealloc might move the memory block)
    dev->partial_entry.buffptr = new_buffer;

    // 4. Copy the new data from user space to the end of your new buffer
    // We offset the destination pointer by the existing size of the partial entry
    bytes_not_copied = copy_from_user((char *)dev->partial_entry.buffptr + dev->partial_entry.size, buf, count);
    
    if (bytes_not_copied != 0) {
        mutex_unlock(&dev->lock);
        return -EFAULT; // Bad address space
    }

    // 5. Update the size
    dev->partial_entry.size += count;

    // 6. Check for a newline character
    if (memchr(dev->partial_entry.buffptr, '\n', dev->partial_entry.size) != NULL) {
        
        // 7. If a newline is found:
        // Push the completed entry to the circular buffer
        overwritten_buffptr = aesd_circular_buffer_add_entry(&dev->circular_buffer, &dev->partial_entry);
        
        // Crucial: Free the oldest entry if the buffer was full and we overwrote it
        if (overwritten_buffptr != NULL) {
            kfree(overwritten_buffptr);
        }
        
        // Reset the partial entry so the next write starts fresh
        dev->partial_entry.buffptr = NULL;
        dev->partial_entry.size = 0;
    }

    // 8. Update file position and return value
    *f_pos += count;
    retval = count;

    // 9. Unlock the mutex
    mutex_unlock(&dev->lock);

    return retval;
}
struct file_operations aesd_fops = {
    .owner =    THIS_MODULE,
    .read =     aesd_read,
    .write =    aesd_write,
    .open =     aesd_open,
    .release =  aesd_release,
};

static int aesd_setup_cdev(struct aesd_dev *dev)
{
    int err, devno = MKDEV(aesd_major, aesd_minor);

    cdev_init(&dev->cdev, &aesd_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &aesd_fops;
    err = cdev_add (&dev->cdev, devno, 1);
    if (err) {
        printk(KERN_ERR "Error %d adding aesd cdev", err);
    }
    return err;
}



int aesd_init_module(void)
{
    dev_t dev = 0;
    int result;
    result = alloc_chrdev_region(&dev, aesd_minor, 1,
            "aesdchar");
    aesd_major = MAJOR(dev);
    if (result < 0) {
        printk(KERN_WARNING "Can't get major %d\n", aesd_major);
        return result;
    }
    memset(&aesd_device,0,sizeof(struct aesd_dev));
    

    /**
     * TODO: initialize the AESD specific portion of the device
     */
    mutex_init(&aesd_device.lock);
    aesd_circular_buffer_init(&aesd_device.circular_buffer);
    result = aesd_setup_cdev(&aesd_device);

    if( result ) {
        unregister_chrdev_region(dev, 1);
    }
    return result;

}

void aesd_cleanup_module(void)
{
    dev_t devno = MKDEV(aesd_major, aesd_minor);

    cdev_del(&aesd_device.cdev);

    /**
     * TODO: cleanup AESD specific poritions here as necessary
     */
    uint8_t index;
    struct aesd_buffer_entry *entry;
    
    // Free any remaining entries in the circular buffer
    AESD_CIRCULAR_BUFFER_FOREACH(entry, &aesd_device.circular_buffer, index) {
        if (entry->buffptr != NULL) {
            kfree(entry->buffptr);
        }
    }
    
    // Free the partial entry if the module is unloaded while holding an unterminated write
    if (aesd_device.partial_entry.buffptr != NULL) {
        kfree(aesd_device.partial_entry.buffptr);
    }

    mutex_destroy(&aesd_device.lock);
    unregister_chrdev_region(devno, 1);
}



module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
