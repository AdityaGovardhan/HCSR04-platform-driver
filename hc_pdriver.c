#include<linux/init.h>
#include<linux/device.h>
#include<linux/module.h>
#include<linux/fs.h>
#include<linux/slab.h>
#include<linux/types.h>
#include<linux/cdev.h>
#include<linux/uaccess.h>
#include<linux/string.h>
#include<linux/err.h>
#include<linux/semaphore.h>
#include<linux/spinlock.h>
#include<linux/gpio.h>
#include<linux/interrupt.h>
#include<asm/msr.h>
#include<linux/delay.h>
#include<linux/platform_device.h>

#include"hc_pdriver.h"



// static int how_many = DEFAULT_DEVICES;
// module_param(how_many, int, S_IRUGO);

// indicates number of devices created
int count = 0;

// flag for class creation
int class_created = 0;

dev_t devnum;
struct class *dev_class;
static struct device *dev_device;

dev_ptr devices[MAX_DEVICES];



int chrdev_open(struct inode *inode, struct file *filp){
	
	dev_ptr device;

	// obtain the device specific data structure
	device = container_of(inode->i_cdev, struct dev_struct, cdev_struct);
	
	filp->private_data = device;

	printk(KERN_INFO "OPEN %s\n", device->device_name);
	
	return 0;
}

int chrdev_release(struct inode *inode, struct file *filp){
	
	dev_ptr device = (dev_ptr) filp->private_data;

	// free the resources acquired by device
	cbuff_clear(device->cbuff);
	gpio_set_value_cansleep(device->trigger_pin, 0);
	gpio_set_value_cansleep(device->echo_pin, 0);
	free_irq(gpio_to_irq(device->echo_pin), (void *)device);
	gpio_free(device->trigger_pin);
	gpio_free(device->echo_pin);

	printk(KERN_INFO "RELEASE %s\n", device->device_name);
	
	return 0;
}

ssize_t chrdev_read(struct file *filp, char __user *buff, size_t count, loff_t *offp){

	dev_ptr device = (dev_ptr) filp->private_data;
	struct node node;
	void *ret_ptr;

	printk(KERN_ALERT "READ %s call\n", device->device_name);

	// if the device is not empty... read the buffer
	if(!is_cbuff_empty(device->cbuff)){
		printk(KERN_INFO "READ %s readind from buffer\n", device->device_name);
	}
	// if the device is empty...
	else{
		printk(KERN_ALERT "READ %s buffer found empty\n", device->device_name);
		// check if there is an ongoing measurement
		if(down_trylock(&device->sem)){
			printk(KERN_ALERT " READ %s ongoing measurement, blocking read call\n", device->device_name);
			down_interruptible(&device->sem);								
			up(&device->sem);
		// if no ongoing measurement, start a new measurement
		}else{
			printk(KERN_INFO "\nREAD %s measurement routine\n", device->device_name);
			device->enable = 1;
			measurement_routine(device);
			device->enable = 0;
			up(&device->sem);
		}
	}

	node = cbuff_get(device->cbuff);
	ret_ptr = &node;
	if(copy_to_user((void __user *)buff, ret_ptr, sizeof(struct node))){
		printk(KERN_ALERT "READ %s copy to user failed\n", device->device_name);
		return -ENOMEM;
	}
	return sizeof(struct node);
}

ssize_t chrdev_write(struct file *filp, const char __user *buff, size_t count, loff_t *offp){

	dev_ptr device = (dev_ptr) filp->private_data;
	void * buff_ptr;

	printk(KERN_ALERT "WRITE %s call\n", device->device_name);

	if(!(buff_ptr = kmalloc(sizeof(int), GFP_KERNEL))){
		return -ENOMEM;
	}

	if(copy_from_user(buff_ptr, (void __user *) buff, count)){
		printk(KERN_ALERT "WRITE %s copy from user failed\n", device->device_name);
		kfree(buff_ptr);
		return -ENOMEM;
	}

	// if ongoing measurement, return invalid
	if(down_trylock(&device->sem)){
		printk(KERN_ALERT "WRITE %s ongoing measurement, write not possible\n", device->device_name);
		kfree(buff_ptr);
		return -EINVAL;
	}

	// if the write argument is not zero, clear the buffer and start a new measurement
	if(*(int *)buff_ptr != 0){
		cbuff_clear(device->cbuff);
		printk(KERN_ALERT "WRITE %s device buffer cleared\n", device->device_name);
	}

	printk(KERN_INFO "\nWRITE %s measurement routine\n", device->device_name);
	device->enable = 1;
	measurement_routine(device);
	device->enable = 0;
	kfree(buff_ptr);
	up(&device->sem);
	return sizeof(sizeof(struct node));
}

long chrdev_ioctl(struct file *filp, unsigned int cmd, unsigned long arg){
	
	void *param_ptr;
	int trig, echo;
	int m, delta;
	int err;

	dev_ptr device;
	device = (dev_ptr) filp->private_data;

	if(_IOC_TYPE(cmd) != HCSR_IOC_MAGIC){
		return -ENOTTY;
	}

	if(!(param_ptr = kmalloc(_IOC_SIZE(cmd), GFP_KERNEL))){
		return -ENOMEM;
	}

	if(copy_from_user(param_ptr, (void __user *) arg, _IOC_SIZE(cmd))){
		kfree(param_ptr);
		return -ENOMEM;
	}

	switch(cmd){

		case CONFIG_PINS:
			trig = (*(struct device_pins*)param_ptr).trigger_pin;
			echo = (*(struct device_pins*)param_ptr).echo_pin;

			// check for pin validity
			if(validate_pins(trig, echo, device)){
				printk(KERN_ALERT "Invalid pins requested for device %s\n", device->device_name);
				kfree(param_ptr);
				return -EINVAL;
			}
			else{
				// if pins valid, configure them
				if((err = configure_pins(trig, echo, device))){
					printk(KERN_ALERT "Pin configuration failed for device %s\n", device->device_name);
					kfree(param_ptr);
					return err;
				}
				else{
					kfree(param_ptr);
					return 0;
				}
			}
			
		case SET_PARAMETERS:
			m = (*(struct m_delta*)param_ptr).m;
			delta = (*(struct m_delta*)param_ptr).delta;
			
			// check for m validity
			if(m < M_MIN){
				printk(KERN_ALERT "IOCTL %s m can't be smaller than %d\n", device->device_name, M_MIN);
				kfree(param_ptr);
				return -EINVAL;
			}

			// check for delta validity
			if(delta < DELTA_MIN){
				printk(KERN_ALERT "IOCTL %s delta can't be smaller than %d\n", device->device_name, DELTA_MIN);
				kfree(param_ptr);
				return -EINVAL;
			}

			device->m = m;
			device->delta = delta;

			printk(KERN_ALERT "IOCTL %s m = %d and delta = %d configured", device->device_name, device->m, device->delta);

			kfree(param_ptr);
			return 0;

		default:
			kfree(param_ptr);
			return -EINVAL;
	}
}

static struct file_operations fops = {
	.owner 			= 	THIS_MODULE,
	.open 			= 	chrdev_open,
	.release 		= 	chrdev_release,
	.read 			= 	chrdev_read,
	.write 			= 	chrdev_write,
	.unlocked_ioctl = 	chrdev_ioctl,
};

// static int __init chrdev_init(void){

// 	int i;

// 	// check if devices to be instantiated < max
// 	if(how_many > MAX_DEVICES){
// 		printk(KERN_ALERT "INIT maximum %d devices can be created\n", MAX_DEVICES);
// 		return -EINVAL;
// 	}

// 	// register char device numbers
// 	if(alloc_chrdev_region(&devnum, 0, how_many, DRIVER_NAME)){
// 		printk(KERN_ALERT "INIT problem in allocating charater device region\n");
// 		return -1;
// 	}

// 	// create a driver class
// 	dev_class = class_create(THIS_MODULE, DRIVER_NAME);

// 	// create device objects
// 	for(i = 0; i < how_many; i++){
// 		device[i] = kmalloc(sizeof(struct dev_struct), GFP_KERNEL);
// 		device_setup(device[i], i);
// 	}
	
// 	printk(KERN_ALERT "INIT complete\n");
// 	return 0;
// }

// static void __exit chrdev_exit(void){
	
// 	int i;

// 	// remove device objects
// 	for(i = 0; i < how_many; i++){
// 		device_kill(device[i], i);
// 	}

// 	// remove driver class
// 	class_destroy(dev_class);

// 	// unregister char device numbers
// 	unregister_chrdev_region(devnum, how_many);

// 	printk(KERN_ALERT "EXIT complete\n");
// }

// module_init(chrdev_init);
// module_exit(chrdev_exit);

static ssize_t trigger_pin_show(struct device *dev_device, struct device_attribute *attribute, char *buff){
	dev_ptr device = dev_get_drvdata(dev_device);
	return snprintf(buff, PAGE_SIZE, "%d\n", device->trigger_pin);
}

static ssize_t echo_pin_show(struct device *dev_device, struct device_attribute *attribute, char *buff){
	dev_ptr device = dev_get_drvdata(dev_device);
	return snprintf(buff, PAGE_SIZE, "%d\n", device->echo_pin);
}

static ssize_t number_samples_show(struct device *dev_device, struct device_attribute *attribute, char *buff){
	dev_ptr device = dev_get_drvdata(dev_device);
	return snprintf(buff, PAGE_SIZE, "%d\n", device->m);
}

static ssize_t sampling_period_show(struct device *dev_device, struct device_attribute *attribute, char *buff){
	dev_ptr device = dev_get_drvdata(dev_device);
	return snprintf(buff, PAGE_SIZE, "%d\n", device->delta);
}

static ssize_t enable_show(struct device *dev_device, struct device_attribute *attribute, char *buff){
	dev_ptr device = dev_get_drvdata(dev_device);
	return snprintf(buff, PAGE_SIZE, "%d\n", device->enable);
}

static ssize_t distance_show(struct device *dev_device, struct device_attribute *attribute, char *buff){
	dev_ptr device = dev_get_drvdata(dev_device);
	struct node n1 = cbuff_get(device->cbuff);
	int distance = n1.distance;
	printk("distance = %d\n", distance);
	return snprintf(buff, PAGE_SIZE, "%d\n", distance);
	
}

static ssize_t trigger_pin_store(struct device *dev_device, struct device_attribute *attribute, const char *buff, size_t count){
	dev_ptr device = dev_get_drvdata(dev_device);
	sscanf(buff, "%d", &device->trigger_pin);
	configure_trigger(device);
	return count;

}


static ssize_t echo_pin_store(struct device *dev_device, struct device_attribute *attribute, const char *buff, size_t count){
	dev_ptr device = dev_get_drvdata(dev_device);
	sscanf(buff, "%d", &device->echo_pin);
	configure_echo(device);
	return count;
}

static ssize_t number_samples_store(struct device *dev_device, struct device_attribute *attribute, const char *buff, size_t count){
	dev_ptr device = dev_get_drvdata(dev_device);
	sscanf(buff, "%d", &device->m);
	return count;
}

static ssize_t sampling_period_store(struct device *dev_device, struct device_attribute *attribute, const char *buff, size_t count){
	dev_ptr device = dev_get_drvdata(dev_device);
	sscanf(buff, "%d", &device->delta);
	return count;
}

static ssize_t enable_store(struct device *dev_device, struct device_attribute *attribute, const char *buff, size_t count){

	int enable_data;
	dev_ptr device = dev_get_drvdata(dev_device);
	sscanf(buff, "%d", &enable_data);

	if(enable_data == 1){
		if(down_trylock(&device->sem)){
			printk(KERN_ALERT "%s ongoing measurement\n", device->device_name);
			return count;
		}

		device->enable = 1;
		measurement_routine(device);
		device->enable = 0;
		up(&device->sem);
		return count;
	}

	if(enable_data == 0){
		if(down_trylock(&device->sem)){
			down_interruptible(&device->sem);
			cbuff_get(device->cbuff);
			up(&device->sem);
			return count;
		}else{
			up(&device->sem);
			return count;
		}
	}
	
	printk(KERN_INFO "%s device enable_store finish\n", device->device_name);	
	return count;
}

static DEVICE_ATTR(trigger_pin, S_IRWXU, trigger_pin_show, trigger_pin_store);
static DEVICE_ATTR(echo_pin, S_IRWXU, echo_pin_show, echo_pin_store);
static DEVICE_ATTR(number_samples, S_IRWXU, number_samples_show, number_samples_store);
static DEVICE_ATTR(sampling_period, S_IRWXU, sampling_period_show, sampling_period_store);
static DEVICE_ATTR(enable, S_IRWXU, enable_show, enable_store);
static DEVICE_ATTR(distance, S_IRWXU, distance_show, NULL);

static int chrdev_probe(struct platform_device *platform_device){
	
	int ret;
	hc_pdevice_ptr pdevice;
	dev_ptr device;

	pdevice = container_of(platform_device, struct hc_pdevice, pd_struct);

	printk(KERN_INFO "PROBE device found %s %d \n", pdevice->name, pdevice->dev_no);

	if(class_created != 1){
		dev_class = class_create(THIS_MODULE, DRIVER_NAME);	
		
		if(alloc_chrdev_region(&devnum, 0, 2, DRIVER_NAME)){
			printk(KERN_ALERT "INIT problem in allocating charater device region\n");
			return -1;
		}

		class_created = 1;
	}

	
	count++; // number of devices created
	device = kmalloc(sizeof(struct dev_struct), GFP_KERNEL);
	device_setup(device, pdevice);
	dev_device = device_create(dev_class, NULL, MKDEV(MAJOR(devnum), count), device, device->device_name);
	devices[count - 1] = device;

	ret = device_create_file(dev_device, &dev_attr_trigger_pin);
	if(ret < 0){
		printk("PROBE can't create %s attribute %s\n", device->device_name, dev_attr_trigger_pin.attr.name);
	}

	ret = device_create_file(dev_device, &dev_attr_echo_pin);
	if(ret < 0){
		printk("PROBE can't create %s attribute %s\n", device->device_name, dev_attr_echo_pin.attr.name);
	}

	ret = device_create_file(dev_device, &dev_attr_number_samples);
	if(ret < 0){
		printk("PROBE can't create %s attribute %s\n", device->device_name, dev_attr_number_samples.attr.name);
	}

	ret = device_create_file(dev_device, &dev_attr_sampling_period);
	if(ret < 0){
		printk("PROBE can't create %s attribute %s\n", device->device_name, dev_attr_sampling_period.attr.name);
	}

	ret = device_create_file(dev_device, &dev_attr_enable);
	if(ret < 0){
		printk("PROBE can't create %s attribute %s\n", device->device_name, dev_attr_enable.attr.name);
	}

	ret = device_create_file(dev_device, &dev_attr_distance);
	if(ret < 0){
		printk("PROBE can't create %s attribute %s\n", device->device_name, dev_attr_enable.attr.name);
	}

	return 0;
}

static int chrdev_remove(struct platform_device *platform_device){
	
	int i = 0;

	for(i = 0; i < 2; i++){
		if(devices[i]->device_name == platform_device->name){
			count--;
			device_destroy(dev_class, MKDEV(MAJOR(devnum), i + 1));
			cdev_del(&devices[i]->cdev_struct);
			cbuff_destroy(devices[i]->cbuff);
			kfree(devices[i]);
		}
	}

	if(count == 0){
		class_destroy(dev_class);
		unregister_chrdev_region(devnum, 2);
	}

	return 0;

}

static const struct platform_device_id chrdev_id_table[] = {
	{HC_PDEVICE1, 0},
	{HC_PDEVICE2, 0},
};

static struct platform_driver chrdev_pdriver = {
	.driver = {
		.name = DRIVER_NAME,
		.owner = THIS_MODULE,
	},
	.probe = chrdev_probe,
	.remove = chrdev_remove,
	.id_table = chrdev_id_table,
};

module_platform_driver(chrdev_pdriver);
MODULE_LICENSE("Dual BSD/GPL");

void device_setup(dev_ptr device, hc_pdevice_ptr pdevice){

	// int ret;

	device->pdevice = pdevice;

	if(pdevice->dev_no == 1){
		device->device_name = HC_PDEVICE1;		
	}

	if(pdevice->dev_no == 2){
		device->device_name = HC_PDEVICE2;
	}

	// device->device_name = pdevice->name;

	device->trigger_pin = -1;
	device->echo_pin = -1;
	device->m = -1;
	device->delta = -1;
	device->temp_measure = 0;
	device->t1 = 0;
	device->t2 = 0;
	device->state = 0;
	device->enable = 0;

	 // +1 to accomodate the sacrificial place of circular buffer
	device->cbuff = cbuff_init(DEVICE_FIFO_SIZE + 1);

	// semaphore needs to be initialized before cdev is initialized
	sema_init(&device->sem, 1);
	spin_lock_init(&device->mr_lock);

	// initialize a cdev structure with these file operations
	cdev_init(&device->cdev_struct, &fops);
	device->cdev_struct.owner = THIS_MODULE;

	// add a char device to the system with this cdev structure and this device number
	if(cdev_add(&device->cdev_struct, MKDEV(MAJOR(devnum), count), 1) < 0){
		printk(KERN_ALERT "INIT %s cdev allocation failed\n", device->device_name);
	}
}

void configure_trigger(dev_ptr device){

	// request trigger pin gpio number
	gpio_request(device->trigger_pin, device->device_name);

	// set direction of trigger pin out
	gpio_direction_output(device->trigger_pin, 0);
}

void configure_echo(dev_ptr device){

	int irq_line;

	// request trigger pin gpio number
	gpio_request(device->echo_pin, device->device_name);

	// set direction of trigger pin out
	gpio_direction_input(device->echo_pin);

	// get irq_line
	irq_line = gpio_to_irq(device->echo_pin);

	// request irq_line
	request_irq(irq_line, (irq_handler_t) irq_handler, IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING, device->device_name, (void *) device);
}

/*
 * Used to check if the user program has requested eligible pins
 * returns a negative error number on failure, otherwise 0
 * @trig - trigger pin number
 * @echo - echo pin number
 */
int validate_pins(int trig, int echo, dev_ptr device){

	int i,j;
	int trig_found = 0, echo_found = 0;


	// mistakenly both pins equal?
	if(trig == echo){
		printk(KERN_ALERT "IOCTL %s both pins can't be equal\n", device->device_name);
		return -EINVAL;
	}

	// check for trig pin
	for(i = 0; i < TRIGGER_PIN_COUNT; i++){
		if(trig == trigger_pins[i]){
			trig_found = 1;
			break;
		}
	}

	// check for echo pin
	for(j = 0; j < ECHO_PIN_COUNT; j++){
		if(echo == echo_pins[j]){
			echo_found = 1;
			break;
		}
	}

	if(trig_found == 0 && echo_found == 0){
		printk(KERN_ALERT "IOCTL %s trigger and echo pins invalid\n", device->device_name);
		return -1;
	}
	else if(trig_found == 0){
		printk(KERN_ALERT "IOCTL %s trigger pin invalid\n", device->device_name);
		return -1;
	}
	else if(echo_found == 0){
		printk(KERN_ALERT "IOCTL %s echo pin invalid\n", device->device_name);
		return -1;
	}
	else{
		return 0;
	}
}

/*
 * Used to configure the gpio pins as per requirements
 * returns a negative error number on failure, otherwise 0
 * @trig - trigger pin number
 * @echo - echo pin number
 * @device - device for which the configuration is done
 */
int configure_pins(int trig, int echo, dev_ptr device){
	int err;
	int irq_line;
	
	// request trigger pin gpio number
	if((err = gpio_request(trig, device->device_name))){
		printk(KERN_ALERT "IOCTL %s 11111111111111111\n", device->device_name);
		return err;
	}

	// request echo pin gpio number
	if((err = gpio_request(echo, device->device_name))){
		printk(KERN_ALERT "IOCTL %s 22222222222222222\n", device->device_name);
		return err;
	}
	
	// set direction of trigger pin out
	if((err = gpio_direction_output(trig, 0))){
		printk(KERN_ALERT "IOCTL %s 33333333333333333\n", device->device_name);
		return err;
	}

	// set direction of echo pin in
	if((err = gpio_direction_input(echo))){
		printk(KERN_ALERT "IOCTL %s 44444444444444444\n", device->device_name);
		return err;
	}

	// request irq line for echo pin
	if((irq_line = gpio_to_irq(echo)) < 0){
		printk(KERN_ALERT "IOCTL %s 55555555555555555\n", device->device_name);
		return irq_line;
	}


	// attach interrupt handler for obtained irq line
	if((err = request_irq(irq_line, (irq_handler_t) irq_handler, IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING, device->device_name, (void *) device))){
		printk(KERN_ALERT "IOCTL %s request_irq failure\n", device->device_name);
		return err;
	}

	device->trigger_pin = trig;
	device->echo_pin = echo;

	printk(KERN_INFO "IOCTL %s trig = %d and echo = %d configured\n", device->device_name, device->trigger_pin, device->echo_pin);
	printk(KERN_INFO "IOCTL %s irq_line = %d assigned\n", device->device_name, irq_line);

	return 0;
}

/*
 * measurement routine for the device
 * @device - device for which the measurement routine is carried out
 */
void measurement_routine(dev_ptr device){
	int i, j, k;
	int l_index, h_index;
	
	int sum = 0;
	int measurement;
	unsigned long long timestamp;
	unsigned long flags;

	int temp_measures[device->m + 2];

	// take m+2 measurements, accounts for interrupt
	printk(KERN_INFO "%s m = %d\n", device->device_name, device->m);
	device->temp_measure = 0;
	gpio_set_value_cansleep(device->trigger_pin, 0);

	for(i = 0; i < (device->m + 2); i++){
		spin_lock_irqsave(&device->mr_lock, flags);
		gpio_set_value_cansleep(device->trigger_pin, 1);
		udelay(120);
		gpio_set_value_cansleep(device->trigger_pin, 0);
		spin_unlock_irqrestore(&device->mr_lock, flags);

		mdelay(device->delta);

		spin_lock_irqsave(&device->mr_lock, flags);
		temp_measures[i] = device->temp_measure;
		printk(KERN_INFO "%s #%d dist = %d\n", device->device_name, i + 1, temp_measures[i]);
		spin_unlock_irqrestore(&device->mr_lock, flags);
	}

	// steps to discard highest and lowest values
	if(temp_measures[0] > temp_measures[1]){
		h_index = 0;
		l_index = 1;
	}else{
		l_index = 0;
		h_index = 1;
	}
	for(j = 2; j < (device->m + 2); j++){
		if(temp_measures[j] < temp_measures[l_index]){
			l_index = j;
		}
		else if(temp_measures[j] > temp_measures[h_index]){
			h_index = j;
		}
	}

	temp_measures[l_index] = 0;
	temp_measures[h_index] = 0;

	// take the average and write to the ring buffer
	for(k = 0; k < (device->m + 2); k++){
		sum = sum + temp_measures[k];
	}

	measurement = sum / device->m;

	rdtscll(timestamp);

	printk(KERN_ALERT "%s measurement = %d timestamp = %lld\n\n", device->device_name, measurement, timestamp);
	cbuff_put(device->cbuff, (struct node){measurement, timestamp});
}

/*
 * echo pin interrupt handler
 * @irq - irq line assigned to device
 * @device - device for which the interrupt occured
 */
irqreturn_t irq_handler(int irq, void *device){
	unsigned long long t;
	unsigned long flags;
	spin_lock_irqsave(&((dev_ptr)device)->mr_lock, flags);
	rdtscll(t);
	if(((dev_ptr)device)->state == 0){
		((dev_ptr)device)->t1 = t;
		((dev_ptr)device)->state = 1;
		// printk(KERN_INFO "%s low to high - %lld\n", ((dev_ptr)device)->device_name, t);
	}
	else if(((dev_ptr)device)->state == 1){
		((dev_ptr)device)->t2 = t;
		((dev_ptr)device)->state = 0;
		((dev_ptr)device)->temp_measure = (int)(((dev_ptr)device)->t2 - ((dev_ptr)device)->t1)/588235;
		// printk(KERN_INFO "%s high to low - %lld\n", ((dev_ptr)device)->device_name, t);
	}
	spin_unlock_irqrestore(&((dev_ptr)device)->mr_lock, flags);	
	return IRQ_HANDLED;
}