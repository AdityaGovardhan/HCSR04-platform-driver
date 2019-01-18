#include<linux/init.h>
#include<linux/kernel.h>
#include<linux/module.h>
#include<linux/platform_device.h>
#include<linux/semaphore.h>
#include<linux/cdev.h>
#include<linux/interrupt.h>
#include<linux/fs.h>

#include"hc_pdriver.h" 

static struct hc_pdevice hc_pdevice1 = {
	.name = HC_PDEVICE1,
	.dev_no = 1,
	.pd_struct = {
		.name = HC_PDEVICE1,
		.id = -1,
	}
};

static struct hc_pdevice hc_pdevice2 = {
	.name = HC_PDEVICE2,
	.dev_no = 2,
	.pd_struct = {
		.name = HC_PDEVICE2,
		.id = -1,
	}
};

static int __init pdevice_init(void){

	platform_device_register(&hc_pdevice1.pd_struct);
	printk(KERN_INFO "PDEVICE INIT %s registered\n", hc_pdevice1.name);

	platform_device_register(&hc_pdevice2.pd_struct);
	printk(KERN_INFO "PDEVICE INIT %s registered\n", hc_pdevice1.name);

	return 0;
}

static void __exit pdevice_exit(void){

	platform_device_unregister(&hc_pdevice1.pd_struct);
	printk(KERN_INFO "PDEVICE EXIT %s unregistered\n", hc_pdevice1.name);

	platform_device_unregister(&hc_pdevice2.pd_struct);
	printk(KERN_INFO "PDEVICE EXIT %s unregistered\n", hc_pdevice1.name);
}

module_init(pdevice_init);
module_exit(pdevice_exit);
MODULE_LICENSE("Dual BSD/GPL");