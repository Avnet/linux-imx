#ifndef __MAAXBOARD_GPIOMEM_H__
#define __MAAXBOARD_GPIOMEM_H__

#include <linux/ioctl.h>
#include <linux/cdev.h>

//for ioctl
#define MAAX_IOC_MAGIC 					'X'
#define MAAX_IOC_MMAP_PINMUX	_IOWR(MAAX_IOC_MAGIC, 0,unsigned short)
#define MAAX_IOC_MMAP_CTRL		_IOWR(MAAX_IOC_MAGIC, 1,unsigned short)


// for mmap area
#define DEVICE_NAME "maaxboard-gpiomem"
#define DRIVER_NAME "gpiomem-maaxboard"
#define DEVICE_MINOR 0

/*I.MX8MQ, I.MX8MM GPIO number:
GPIO1 0-29
GPIO2 0-20
GPIO3 0-25
GPIO4 0-31
GPIO5 0-29
total 139 */
#define GPIO_IOMUX_BASE_ADDR			0x30330028  /*0x30330250*/
#define GPIO_PAD_BASE_ADDR				0x30330290  /*0x30330418*/

#define GPIO_PINMUX_AREA_BASE			0x30330000  
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

//#define GPIO_AREA_SIZE					0x200000


struct maaxboard_gpiomem_instance {
	unsigned long gpio_regs_phys;
	struct device *dev;
};


#endif
