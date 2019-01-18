#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <time.h>
#include "hc_ioctl.h"
#include <stdint.h>
#include <poll.h>

#define BUFFER_SIZE  5															/*Ring buffer size*/
#define TRIG1 7														/*Trigger pin device 0*/
#define ECHO1 8															/*Echo pin device 0*/

#define TRIG2 5													/*Trigger pin device 1*/
#define ECHO2 9														/*Echo pin device 1*/

#define GPIO_MUX_1 22															/*Mux pin for echo pin of device 0*/
#define GPIO_MUX_2 70															/*Mux pin for echo pin of device 1*/
#define GPIO_MUX_3 18
#define GPIO_MUX_4 66


void GPIO_INIT(void)
{
	int Export,Test;
	Export = open("/sys/class/gpio/export", O_WRONLY);
	if (Export < 0)
		printf("\n gpio export open failed");
	
	if(write(Export,"22",2)<0)													/*Pin number "74" can be changed to other pins*/
		printf("error Export 22");
	
	if (Export < 0)
		printf("\n gpio export open failed");
	else printf("\n gpio%d export successful",GPIO_MUX_1);

	if(write(Export,"70",2)<0)													/*Pin number "66" can be changed to other pins*/
		printf("error Export 70");
	else printf("\n gpio%d export successful",GPIO_MUX_2);

	if (Export < 0)
		printf("\n gpio export open failed");
	
	if(write(Export,"18",2)<0)													/*Pin number "74" can be changed to other pins*/
		printf("error Export 18");
	
	if (Export < 0)
		printf("\n gpio export open failed");
	else printf("\n gpio%d export successful",GPIO_MUX_3);

	if(write(Export,"66",2)<0)													/*Pin number "66" can be changed to other pins*/
		printf("error Export 66");
	else printf("\n gpio%d export successful",GPIO_MUX_4);
	
	close(Export);

//Set the value for the pins
	Test = open("/sys/class/gpio/gpio22/value", O_WRONLY);									/*Pin number "74" can be changed to other pins*/
	if (Test < 0)
		printf("\n gpio%d value open failed",GPIO_MUX_1);

	if(write(Test ,"0",1)<0)
		printf("error gpio%d value",GPIO_MUX_1 );

	Test = open("/sys/class/gpio/gpio70/value", O_WRONLY);									/*Pin number "66" can be changed to other pins*/
	if (Test < 0)		
		printf("\n gpio%d value open failed", GPIO_MUX_2);

	if(write(Test,"0",1)<0)
		printf("error gpio%d value", GPIO_MUX_2);

	Test = open("/sys/class/gpio/gpio18/value", O_WRONLY);									/*Pin number "74" can be changed to other pins*/
	if (Test < 0)
		printf("\n gpio%d value open failed",GPIO_MUX_3);

	if(write(Test ,"0",1)<0)
		printf("error gpio%d value",GPIO_MUX_3 );

	Test = open("/sys/class/gpio/gpio66/value", O_WRONLY);									/*Pin number "66" can be changed to other pins*/
	if (Test < 0)		
		printf("\n gpio%d value open failed", GPIO_MUX_4);

	if(write(Test,"0",1)<0)
		printf("error gpio%d value", GPIO_MUX_4);

}


int main(int argc, char **argv)
{
	

	GPIO_INIT();	

return 0;	
}
