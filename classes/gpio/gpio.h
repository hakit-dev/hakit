/*
 * HAKit - The Home Automation KIT - www.hakit.net
 * Copyright (C) 2014 Sylvain Giroudon
 *
 * GPIO access primitives
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

#ifndef __HAKIT_GPIO_H__
#define __HAKIT_GPIO_H__

typedef void (*gpio_input_func_t)(void *user_data, int n, int value);

extern void gpio_export(int n);
extern void gpio_unexport(int n);

extern void gpio_set_active_low(int n, int enable);
extern int gpio_set_output(int n);
extern int gpio_set_input(int n, gpio_input_func_t func, void *user_data);
extern void gpio_set_value(int n, int value);
extern int gpio_get_value(int n);

#endif /* __HAKIT_GPIO_H__ */
