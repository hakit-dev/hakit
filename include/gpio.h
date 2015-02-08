#ifndef __HAKIT_GPIO_H__
#define __HAKIT_GPIO_H__

extern void gpio_export(int n);
extern void gpio_unexport(int n);

extern void gpio_set_active_low(int n, int enable);
extern void gpio_set_output(int n);
extern void gpio_set_value(int n, int value);
extern int gpio_get_value(int n);

#endif /* __HAKIT_GPIO_H__ */
