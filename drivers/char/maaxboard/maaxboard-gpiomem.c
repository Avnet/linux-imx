/**
 * GPIO memory device driver
 *
 * Creates a chardev /dev/gpiomem which will provide user access to
 * the I.MX8MQ/I.MX8M Mini's GPIO registers when it is mmap()'d.
 * No longer need root for user GPIO access, but without relaxing permissions
 * on /dev/mem.
 *
 * Written by Eric <eric.wu@embest-tech.com>
 * Copyright (c) 2020, AVNET Ltd.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The names of the above-listed copyright holders may not be used
 *    to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * ALTERNATIVELY, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2, as published by the Free
 * Software Foundation.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
 * IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/cdev.h>
#include <linux/pagemap.h>
#include <linux/io.h>

#define DEVICE_NAME "maaxboard-gpiomem"
#define DRIVER_NAME "gpiomem-maaxboard"
#define DEVICE_MINOR 0

#define MAAXBOARD_GPIO_BASE_ADDR	0x3020000
#define MAAXBOARD_GPIO_BASE_SIZE	0x160000

/*I.MX8MQ, I.MX8MM GPIO number:
GPIO1 0-29
GPIO2 0-20
GPIO3 0-25
GPIO4 0-31
GPIO5 0-29
total 139 */
#define GPIO_IOMUX_BASE_ADDR			0x30330028  /*0x30330250*/
#define GPIO_PAD_BASE_ADDR				0x30330290  /*0x30330418*/
#define GPIO_PINMUX_AREA_SIZE			0x10000

/*
GPIO1  0x30200000-0x3020001C
GPIO2  0x30210000-0x3021001C
GPIO3  0x30220000-0x3022001C
GPIO4  0x30230000-0x3023001C
GPIO5  0x30240000-0x3024001C
*/
#define GPIO_FUNCTION_BASE_ADDR			0x30200000
#define GPIO_FUNCTION_AREA_SIZE			0x60000

#define GPIO_AREA_SIZE					0x200000


struct maaxboard_gpiomem_instance {
	unsigned long gpio_regs_phys;
	struct device *dev;
};

static struct cdev maaxboard_gpiomem_cdev;
static dev_t maaxboard_gpiomem_devid;
static struct class *maaxboard_gpiomem_class;
static struct device *maaxboard_gpiomem_dev;
static struct maaxboard_gpiomem_instance *inst;


/****************************************************************************
*
*   GPIO mem chardev file ops
*
***************************************************************************/

static int maaxboard_gpiomem_open(struct inode *inode, struct file *file)
{
	int dev = iminor(inode);
	int ret = 0;

	if (dev != DEVICE_MINOR) {
		dev_err(inst->dev, "Unknown minor device: %d", dev);
		ret = -ENXIO;
	}
	return ret;
}

static int maaxboard_gpiomem_release(struct inode *inode, struct file *file)
{
	int dev = iminor(inode);
	int ret = 0;

	if (dev != DEVICE_MINOR) {
		dev_err(inst->dev, "Unknown minor device %d", dev);
		ret = -ENXIO;
	}
	return ret;
}

static const struct vm_operations_struct maaxboard_gpiomem_vm_ops = {
#ifdef CONFIG_HAVE_IOREMAP_PROT
	.access = generic_access_phys
#endif
};

static int maaxboard_gpiomem_mmap(struct file *file, struct vm_area_struct *vma)
{
	/* Ignore what the user says - they're getting the GPIO regs
	   whether they like it or not! */
	//unsigned long gpio_page = inst->gpio_regs_phys >> PAGE_SHIFT;
	unsigned int offset = vma->vm_pgoff << PAGE_SHIFT;
	unsigned int shm_addr = GPIO_FUNCTION_BASE_ADDR;
	unsigned int shm_size = GPIO_AREA_SIZE ;
	unsigned int shm_pfn_addr,size = 0;

	shm_pfn_addr = ((unsigned int )shm_addr >> PAGE_SHIFT );
	printk( KERN_DEBUG"shm_addr=%#x, shm_size=%x, offset = %x, vm_start=%lx,  vm_flags=%lx, vm_page_prot=%lx\n",
					shm_addr,
					shm_size,
					offset,
					vma->vm_start,
					vma->vm_flags,
					pgprot_val(vma->vm_page_prot) );

	if ( !shm_addr ){
		return -ENXIO;
	}

	if ( shm_addr & ( PAGE_SIZE - 1 ) ){
		printk( "\nMmaping to invalid address ... %#x\n",( unsigned int ) shm_addr );
		return -EINVAL;
	}

	if ( offset >= shm_size ){
		return -ESPIPE;
	}

	if ( vma->vm_flags & VM_LOCKED ){
		return -EPERM;
	}

	size = vma->vm_end - vma->vm_start;
	vma->vm_flags |= VM_NORESERVE;
	vma->vm_flags |= VM_IO;

	vma->vm_page_prot = pgprot_noncached (vma->vm_page_prot); //io memory is no cache 

	vma->vm_page_prot = phys_mem_access_prot(file, shm_pfn_addr,
						 PAGE_SIZE,
						 vma->vm_page_prot);

	vma->vm_ops = &maaxboard_gpiomem_vm_ops;

	if (remap_pfn_range(vma, vma->vm_start,
			shm_pfn_addr,
			PAGE_SIZE,
			vma->vm_page_prot)) {
		return -EAGAIN;
	}
	return 0;
}

static const struct file_operations
maaxboard_gpiomem_fops = {
	.owner = THIS_MODULE,
	.open = maaxboard_gpiomem_open,
	.release = maaxboard_gpiomem_release,
	.mmap = maaxboard_gpiomem_mmap,
};


 /****************************************************************************
*
*   Probe and remove functions
*
***************************************************************************/


static int maaxboard_gpiomem_probe(struct platform_device *pdev)
{
	int err;
	void *ptr_err;
	struct device *dev = &pdev->dev;
	struct resource *ioresource;

	/* Allocate buffers and instance data */

	inst = kzalloc(sizeof(struct maaxboard_gpiomem_instance), GFP_KERNEL);

	if (!inst) {
		err = -ENOMEM;
		goto failed_inst_alloc;
	}

	inst->dev = dev;

#if 0
	ioresource = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (ioresource) {
		inst->gpio_regs_phys = ioresource->start;
	} else {
		dev_err(inst->dev, "failed to get IO resource");
		err = -ENOENT;
		goto failed_get_resource;
	}
#endif

	/* Create character device entries */

	err = alloc_chrdev_region(&maaxboard_gpiomem_devid,
				  DEVICE_MINOR, 1, DEVICE_NAME);
	if (err != 0) {
		dev_err(inst->dev, "unable to allocate device number");
		goto failed_alloc_chrdev;
	}
	cdev_init(&maaxboard_gpiomem_cdev, &maaxboard_gpiomem_fops);
	maaxboard_gpiomem_cdev.owner = THIS_MODULE;
	err = cdev_add(&maaxboard_gpiomem_cdev, maaxboard_gpiomem_devid, 1);
	if (err != 0) {
		dev_err(inst->dev, "unable to register device");
		goto failed_cdev_add;
	}

	/* Create sysfs entries */

	maaxboard_gpiomem_class = class_create(THIS_MODULE, DEVICE_NAME);
	ptr_err = maaxboard_gpiomem_class;
	if (IS_ERR(ptr_err))
		goto failed_class_create;

	maaxboard_gpiomem_dev = device_create(maaxboard_gpiomem_class, NULL,
					maaxboard_gpiomem_devid, NULL,
					"gpiomem");
	ptr_err = maaxboard_gpiomem_dev;
	if (IS_ERR(ptr_err))
		goto failed_device_create;

	dev_info(inst->dev, "Initialised: Registers at 0x%08lx",
		inst->gpio_regs_phys);

	return 0;

failed_device_create:
	class_destroy(maaxboard_gpiomem_class);
failed_class_create:
	cdev_del(&maaxboard_gpiomem_cdev);
	err = PTR_ERR(ptr_err);
failed_cdev_add:
	unregister_chrdev_region(maaxboard_gpiomem_devid, 1);
failed_alloc_chrdev:
failed_get_resource:
	kfree(inst);
failed_inst_alloc:
	dev_err(inst->dev, "could not load maaxboard_gpiomem");
	return err;
}

static int maaxboard_gpiomem_remove(struct platform_device *pdev)
{
	struct device *dev = inst->dev;

	kfree(inst);
	device_destroy(maaxboard_gpiomem_class, maaxboard_gpiomem_devid);
	class_destroy(maaxboard_gpiomem_class);
	cdev_del(&maaxboard_gpiomem_cdev);
	unregister_chrdev_region(maaxboard_gpiomem_devid, 1);

	dev_info(dev, "GPIO mem driver removed - OK");
	return 0;
}

 /****************************************************************************
*
*   Register the driver with device tree
*
***************************************************************************/

static const struct of_device_id maaxboard_gpiomem_of_match[] = {
	{.compatible = "maaxboard,maaxboard-gpiomem",},
	{ /* sentinel */ },
};

MODULE_DEVICE_TABLE(of, maaxboard_gpiomem_of_match);

static struct platform_driver maaxboard_gpiomem_driver = {
	.probe = maaxboard_gpiomem_probe,
	.remove = maaxboard_gpiomem_remove,
	.driver = {
		   .name = DRIVER_NAME,
		   .owner = THIS_MODULE,
		   .of_match_table = maaxboard_gpiomem_of_match,
		   },
};

module_platform_driver(maaxboard_gpiomem_driver);

MODULE_ALIAS("platform:gpiomem-maaxboard");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("gpiomem driver for accessing GPIO from userspace");
MODULE_AUTHOR("Eric.Wu <eric.wu@eric.wu@embest-tech.com>");
