#ifndef ASMARM_ARCH_UART_H
#define ASMARM_ARCH_UART_H

struct device;

struct pxauart_platform_data {
	int (*open)(struct device *, void *);
	void (*close)(struct device *, void *);
	void *data;
};

#endif
