#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/smp.h>
#include <linux/cpumask.h>  
#include <linux/sched.h>
#include <linux/sched/task.h>
#include <linux/pid.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/mm.h>
#include <linux/highmem.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/types.h>    // phys_addr_t
#include <linux/errno.h>    // error codes like -EFAULT


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Cole");
MODULE_DESCRIPTION("DTU Rutime Driver");
MODULE_VERSION("0.1");





#define IOCTL_NEW_PA        _IO('a', 1)
#define IOCTL_REMAP_PA      _IO('a', 2)
#define DEVICE_NAME         "DTU-RUNTIME-DRIVER"
#define Kilobyte            1024
#define Megabyte            Kilobyte*1024


//extern unsigned long max_pfn;

static int major_number;
static struct cdev my_cdev;
uint64_t ReadyEphemeralAllocAddr;

/*
    We use the following convention:

    u_ --> defines a variable that is set up by userspace
    k_ --> defines a varaible that is set up by kernelspace
*/

unsigned long ceil_div(unsigned long a, unsigned long b) {
    return (a + b - 1) / b;
}

unsigned long _max_(unsigned long a, unsigned long b)
{
    if (a >= b)
        return a;
    return b;
}




typedef struct RemapPARequest
{
    void* u_VA

}RemapPARequest;




/*
    Even assuming that we have a terabyte of RAM (2^40), we would need do to:

    (2^64-2^40)/(256 * 2^20) = 68.7 Billion 256MB allocations to wrap around addresses

    So for now, we do not worry about wrap around. If really paranoid a cleanup mechanism can be
    employed on the rare occasion this happens.
*/
phys_addr_t AllocEphemeralPhysRange(unsigned long RequestRegionSize)
{
    unsigned long number_regions_needed = ceil_div(RequestRegionSize, Megabyte);
    phys_addr_t begin = ReadyEphemeralAllocAddr;
    ReadyEphemeralAllocAddr += number_regions_needed*Megabyte;
    return begin;
}




int Handle_IOCTL_REMAP_PA(struct RemapPARequest* RemapPAReq)
{
    struct vm_area_struct * vma;
    struct vm_area_struct* prev;
    unsigned long addr = (unsigned long)RemapPAReq->u_VA;

    bool found = false;
    down_read(&current->mm->mmap_lock);
    vma = vma_lookup(current->mm, addr);
    pr_info("VMA: 0x%lx - 0x%lx, flags=0x%lx\n",
            vma->vm_start, vma->vm_end, vma->vm_flags);
    up_read(&current->mm->mmap_lock);
    if (!vma)
        return -EFAULT;
    unsigned long region_size = vma->vm_end - vma->vm_start;
    down_write(&current->mm->mmap_lock);

    /* Unmap existing PTEs for the VMA */
    zap_vma_ptes(vma, vma->vm_start, region_size);

    up_write(&current->mm->mmap_lock);


    
    phys_addr_t new_phys_ephemeral = AllocEphemeralPhysRange(region_size);
    unsigned long pfn  = new_phys_ephemeral >> PAGE_SHIFT;
    if(remap_pfn_range(vma, addr, pfn, region_size, vma->vm_page_prot))
        return -EAGAIN;
    return 0;
}   



static phys_addr_t get_highest_used_pa(void)
{
    struct resource *res;
    phys_addr_t max = 0;

    for (res = &iomem_resource; res; res = res->sibling) {
        if (res->end > max)
            max = res->end;
    }


#ifdef get_max_pfn
    if ((get_max_pfn() << PAGE_SHIFT) > max)
        max = (get_max_pfn() << PAGE_SHIFT);
#endif

    return max;
}







static int mmap_callback(struct file *filp, struct vm_area_struct *vma)
{
    unsigned long request_size = vma->vm_end - vma->vm_start;

    /*
        VM_IO           = do not swap out, do not include in core dump
        VM_DONTEXPAND   = do not grow region
        VM_DONTDUMP     = do not include in process core dump 
    */
    vma->vm_flags |= VM_IO | VM_DONTEXPAND | VM_DONTDUMP;

    phys_addr_t begin_phys = AllocEphemeralPhysRange(request_size);
    pr_info("[DTU Runtime Driver]: mmap_callback() begin_phys=0x%llx\n", begin_phys);
    if (begin_phys == 0x0)
        return -EFAULT;

    if (remap_pfn_range(vma,
                        vma->vm_start,
                        begin_phys >> PAGE_SHIFT,
                        request_size,
                        vma->vm_page_prot))
        return -EAGAIN;
    return 0;
}
/*
    I think we only need the runtime driver to remap physical addresses.
    We should be able to manage the backing store via a user mode allocator
*/

static long IOCTL_Dispatch(struct file *file, unsigned int cmd, unsigned long arg) {
    
    switch (cmd) {
        case IOCTL_REMAP_PA:
        {
            struct RemapPARequest req;
            int err;
            if ((err = copy_from_user(&req, (struct RemapPARequest *)arg, sizeof(struct RemapPARequest))))
                return -EFAULT; // Error copying data
            return Handle_IOCTL_REMAP_PA(&req);
        }
        default:
            return -EINVAL; // Invalid command
    }
    return 0;
}


static int device_open(struct inode *inode, struct file *file) {
    printk(KERN_INFO "Device opened\n");
    return 0;
}

static int device_release(struct inode *inode, struct file *file) {
    printk(KERN_INFO "Device closed\n");
    return 0;
}


static struct file_operations fops = {
    .open = device_open,
    .release = device_release,
    .unlocked_ioctl = IOCTL_Dispatch,
    .mmap = mmap_callback,
};




static int __init my_driver_init(void) {
    dev_t dev;
    int result;
    printk("DriverLoad() entry.\n");
    // Allocate device number
    result = alloc_chrdev_region(&dev, 0, 1, DEVICE_NAME);
    if (result < 0) {
        printk(KERN_ERR "Failed to allocate device number\n");
        return result;
    }

    major_number = MAJOR(dev);

    // Initialize cdev structure
    cdev_init(&my_cdev, &fops);
    my_cdev.owner = THIS_MODULE;

    // Add character device to the system
    result = cdev_add(&my_cdev, dev, 1);
    if (result < 0) {
        printk(KERN_ERR "Failed to add device to the system\n");
        unregister_chrdev_region(MKDEV(major_number, 0), 1);
        return result;
    }




    // Create device class
    struct class* my_class = class_create(THIS_MODULE, "dtu_part_class");
    if (IS_ERR(my_class)) {
        printk(KERN_ERR "Failed to create class\n");
        cdev_del(&my_cdev);
        unregister_chrdev_region(dev, 1);
        return PTR_ERR(my_class);
    }

    // Create device node
    if (IS_ERR(device_create(my_class, NULL, dev, NULL, DEVICE_NAME))) {
        printk(KERN_ERR "Failed to create device node\n");
        class_destroy(my_class);
        cdev_del(&my_cdev);
        unregister_chrdev_region(dev, 1);
        return -1;
    }

    

    ReadyEphemeralAllocAddr = _max_(((get_highest_used_pa() + Megabyte + 1) - ((get_highest_used_pa() + Megabyte + 1) % Megabyte)),
                                     1024UL*1024UL*1024UL*1024UL);



    pr_info("[DTU Runtime Driver]: Begin Ephemeral Address Pool: 0x%llx\n", ReadyEphemeralAllocAddr);


    printk(KERN_INFO "DriverLoad() successful.\n");

    return 0;
}

static void __exit my_driver_exit(void) {
    // Remove character device
    cdev_del(&my_cdev);

    // Release allocated device number
    unregister_chrdev_region(MKDEV(major_number, 0), 1);

    printk(KERN_INFO "Driver unloaded successfully\n");
}

module_init(my_driver_init);
module_exit(my_driver_exit);
