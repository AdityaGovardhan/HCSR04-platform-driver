#!/bin/bash

#Setting trigger and echo pins for device 1
	echo -n "Enter trigger pin for device 1 -> "
	read trigger_pin
	echo "$trigger_pin" > /sys/class/HC_PDRIVER/HC_PDEVICE1/trigger_pin
	echo -n "Enter echo pin  for device 1 -> "
	read echo_pin
	echo "$echo_pin" > /sys/class/HC_PDRIVER/HC_PDEVICE1/echo_pin
	
#Setting trigger and echo pins for device 2
	echo -n "Enter trigger pin for device 2 -> "
	read trigger_pin
	echo "$trigger_pin" > /sys/class/HC_PDRIVER/HC_PDEVICE2/trigger_pin
	echo -n "Enter echo pin  for device 2 -> "
	read echo_pin
	echo "$echo_pin" > /sys/class/HC_PDRIVER/HC_PDEVICE2/echo_pin
	
#Taking m and delta values for device 1
	echo -n "Enter number of samples(m)-> for device 1 "
	read number_samples
	echo "$number_samples" > /sys/class/HC_PDRIVER/HC_PDEVICE1/number_samples
	echo -n "Enter sampling_period for device 1 -> "
	read sampling_period
	echo "$sampling_period" > /sys/class/HC_PDRIVER/HC_PDEVICE1/sampling_period
	
#Taking m and delta values for device 2
	echo -n "Enter number of samples(m)-> for device 2 "
	read number_samples
	echo "$number_samples" > /sys/class/HC_PDRIVER/HC_PDEVICE2/number_samples
	echo -n "Enter sampling_period for device 2 -> "
	read sampling_period
	echo "$sampling_period" > /sys/class/HC_PDRIVER/HC_PDEVICE2/sampling_period

sleep 3
	echo "0" > /sys/class/HC_PDRIVER/HC_PDEVICE1/Enable
	
#Reading distance value device 1
	echo "One shot distance measurement device 1"
	cat /sys/class/HC_PDRIVER/HC_PDEVICE1/distance

sleep 3
	echo "0" > /sys/class/HC_PDRIVER/HC_PDEVICE2/Enable

#Reading distance value device 2
	echo "Last value of distance measured device 2"
	cat /sys/class/HC_PDRIVER/HC_PDEVICE2/distance