#include <asm/io.h>
#include <linux/delay.h>
#include <linux/kprobes.h>
#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/workqueue.h>

#ifndef MODULE_NAME
#define MODULE_NAME "r1000v1_rs485_autoflow"
#endif

#ifndef MODULE_VER
#define MODULE_VER "custom"
#endif

MODULE_DESCRIPTION("This module fixes RS-485 flow control issue on reComputer R1000 v1.0 by hooking `uart_write` function.");
MODULE_AUTHOR("Joshua Lee <chengxun.li@seeed.cc>");
MODULE_LICENSE("Dual MIT/GPL");
MODULE_VERSION(MODULE_VER);

#define BCM2711_GPIO_BASE (0xfe000000 + 0x200000)

volatile unsigned int* GPFSEL0;                  // Function selector for GPIO 0-9, for CM4_RS485_1_DTR at GPIO_6.
volatile unsigned int* GPFSEL1;                  // Function selector for GPIO 10-19, for CM4_RS485_2_DTR at GPIO_17.
volatile unsigned int* GPFSEL2;                  // Function selector for GPIO 20-29, for CM4_RS485_3_DTR at GPIO_24.
volatile unsigned int* GPSET0;                   // Register to set GPIO 0-31 to high.
volatile unsigned int* GPCLR0;                   // Register to set GPIO 0-31 to low.
volatile unsigned int* GPIO_PUP_PDN_CNTRL_REG0;  // Register to set pull up/down control of GPIO 0-15.
volatile unsigned int* GPIO_PUP_PDN_CNTRL_REG1;  // Register to set pull up/down control of GPIO 16-31.

static void rs485_dtr_init(void) {
    // Re-map GPIO registers, offsets are given in the datasheet
    GPFSEL0 = (volatile unsigned int*)ioremap(BCM2711_GPIO_BASE + 0x00, 4);
    GPFSEL1 = (volatile unsigned int*)ioremap(BCM2711_GPIO_BASE + 0x04, 4);
    GPFSEL2 = (volatile unsigned int*)ioremap(BCM2711_GPIO_BASE + 0x08, 4);
    GPSET0 = (volatile unsigned int*)ioremap(BCM2711_GPIO_BASE + 0x1c, 4);
    GPCLR0 = (volatile unsigned int*)ioremap(BCM2711_GPIO_BASE + 0x28, 4);
    GPIO_PUP_PDN_CNTRL_REG0 = (volatile unsigned int*)ioremap(BCM2711_GPIO_BASE + 0xe4, 4);
    GPIO_PUP_PDN_CNTRL_REG1 = (volatile unsigned int*)ioremap(BCM2711_GPIO_BASE + 0xe8, 4);

    // Set CM4_RS485_1_DTR at GPIO_6 to output mode (GPFSEL0[20:18]), no internal pull
    *GPFSEL0 &= ~(7 << 18);
    *GPFSEL0 |= (1 << 18);
    *GPIO_PUP_PDN_CNTRL_REG0 &= ~(3 << 12);
    *GPIO_PUP_PDN_CNTRL_REG0 |= (0 << 12);
    // Set CM4_RS485_2_DTR at GPIO_17 to output mode (GPFSEL1[23:21]), no internal pull
    *GPFSEL1 &= ~(7 << 21);
    *GPFSEL1 |= (1 << 21);
    *GPIO_PUP_PDN_CNTRL_REG1 &= ~(3 << 2);
    *GPIO_PUP_PDN_CNTRL_REG1 |= (0 << 2);
    // Set CM4_RS485_3_DTR at GPIO_24 to output mode (GPFSEL2[14:12]), no internal pull
    *GPFSEL2 &= ~(7 << 12);
    *GPFSEL2 |= (1 << 12);
    *GPIO_PUP_PDN_CNTRL_REG1 &= ~(3 << 16);
    *GPIO_PUP_PDN_CNTRL_REG1 |= (0 << 16);
    // Set all DTR pins to low
    *GPCLR0 = (1 << 6) | (1 << 17) | (1 << 24);
}

static void rs485_dtr_deinit(void) {
    // Set all DTR pins to low
    *GPCLR0 = (1 << 6) | (1 << 17) | (1 << 24);
    // Unmap GPIO registers
    iounmap(GPFSEL0);
    iounmap(GPFSEL1);
    iounmap(GPFSEL2);
    iounmap(GPSET0);
    iounmap(GPCLR0);
    iounmap(GPIO_PUP_PDN_CNTRL_REG0);
    iounmap(GPIO_PUP_PDN_CNTRL_REG1);
}

static bool rs485_is_builtin_dev(struct tty_struct* tty) {
    // `ttyAMA` is for built-in RS-485 interface
    return strcmp(tty->driver->name, "ttyAMA") == 0;
}

static void rs485_dtr_set(int dev_num, bool enable) {
    switch (dev_num) {
        case 2:  // ttyAMA2
            if (enable) {
                *GPSET0 = (1 << 6);
            } else {
                *GPCLR0 = (1 << 6);
            }
            break;
        case 3:  // ttyAMA3
            if (enable) {
                *GPSET0 = (1 << 17);
            } else {
                *GPCLR0 = (1 << 17);
            }
            break;
        case 5:  // ttyAMA5
            if (enable) {
                *GPSET0 = (1 << 24);
            } else {
                *GPCLR0 = (1 << 24);
            }
            break;
    }
}

static int rs485_get_dev_num(struct tty_struct* tty) {
    if (tty->index == 2 || tty->index == 3 || tty->index == 5) {
        return tty->index;
    }
    return -EINVAL;
}

struct rs485_worker_t {
    struct delayed_work work;
    struct tty_struct* tty;
};
static struct workqueue_struct* rs485_worker_queues[3];  // 3 queues for 3 RS-485 interfaces (ttyAMA2, ttyAMA3, ttyAMA5)

static int rs485_get_worker_index(int dev_num) {
    if (dev_num == 2) {
        return 0;
    } else if (dev_num == 3) {
        return 1;
    } else if (dev_num == 5) {
        return 2;
    }
    return -EINVAL;
}

static void rs485_worker_oncomplete(struct work_struct* work) {
    struct rs485_worker_t* rs485_worker = container_of(work, struct rs485_worker_t, work.work);
    // Wait until data is sent out, then set DTR to low
    if (rs485_worker->tty->ops->write_room(rs485_worker->tty) == 0) {
        schedule_delayed_work(&rs485_worker->work, usecs_to_jiffies(1));
        return;
    }

    // Wait for some time before setting DTR to low, delay is based on baudrate
    // Each character takes (10 * 1000 / baudrate) milliseconds
    // Plus 60ns for transceiver mode switch (mentionned in TPT7487 datasheet) 
    int baudrate = tty_get_baud_rate(rs485_worker->tty);
    msleep((10 * 1000) / baudrate);
    ndelay(60);
    rs485_dtr_set(rs485_worker->tty->index, false);
    kfree(rs485_worker);
}

static void hook_uart_write_onreturn(struct kprobe* p, struct pt_regs* regs, unsigned long flags) {
    struct tty_struct* tty = (struct tty_struct*)regs->regs[0];
    if (rs485_is_builtin_dev(tty)) {
        int dev_num = rs485_get_dev_num(tty);
        if (dev_num != -EINVAL) {
            struct rs485_worker_t* rs485_worker = kmalloc(sizeof(*rs485_worker), GFP_KERNEL);
            rs485_worker->tty = tty;
            if (rs485_worker) {
                INIT_DELAYED_WORK(&rs485_worker->work, rs485_worker_oncomplete);
                int queue_index = rs485_get_worker_index(dev_num);
                if (queue_index != -EINVAL) {
                    queue_delayed_work(rs485_worker_queues[queue_index], &rs485_worker->work, 0);
                }
            }
        }
    }
}

static int hook_uart_write_onstart(struct kprobe* p, struct pt_regs* regs) {
    struct tty_struct* tty = (struct tty_struct*)regs->regs[0];
    if (rs485_is_builtin_dev(tty)) {
        int dev_num = rs485_get_dev_num(tty);
        rs485_dtr_set(dev_num, true);
    }

    return 0;
}

static unsigned long get_fn_addr(const char* symbol_name) {
    struct kprobe temp_kp = {.symbol_name = symbol_name};
    int ret = register_kprobe(&temp_kp);
    unsigned long fn_addr = (unsigned long)temp_kp.addr;

    unregister_kprobe(&temp_kp);
    if (ret < 0) {
        return ret;
    }
    if (temp_kp.addr == NULL) {
        return -EFAULT;
    }

    return fn_addr;
}

#define LOG_PREFIX MODULE_NAME ": "
struct kprobe hook_uart_write;

static int module_init_fn(void) {
    rs485_dtr_init();

    // Create worker queues for each RS-485 interface
    rs485_worker_queues[0] = create_singlethread_workqueue(MODULE_NAME "_worker_queue_2");
    if (rs485_worker_queues[0] == NULL) {
        printk(KERN_ERR LOG_PREFIX "Failed to create worker queue for ttyAMA2\n");
        return -ENOMEM;
    }
    rs485_worker_queues[1] = create_singlethread_workqueue(MODULE_NAME "_worker_queue_3");
    if (rs485_worker_queues[1] == NULL) {
        printk(KERN_ERR LOG_PREFIX "Failed to create worker queue for ttyAMA3\n");
        return -ENOMEM;
    }
    rs485_worker_queues[2] = create_singlethread_workqueue(MODULE_NAME "_worker_queue_5");
    if (rs485_worker_queues[2] == NULL) {
        printk(KERN_ERR LOG_PREFIX "Failed to create worker queue for ttyAMA5\n");
        return -ENOMEM;
    }

    // Hook `uart_write` function
    unsigned long target_fn_addr = get_fn_addr("uart_write");
    if (target_fn_addr < 0) {
        printk(KERN_ERR LOG_PREFIX "Failed to get address for `uart_write`, returned code: %ld\n", target_fn_addr);
        return target_fn_addr;
    }
    hook_uart_write.addr = (kprobe_opcode_t*)target_fn_addr;
    hook_uart_write.pre_handler = (void*)hook_uart_write_onstart;
    hook_uart_write.post_handler = (void*)hook_uart_write_onreturn;
    int ret = register_kprobe(&hook_uart_write);
    if (ret < 0) {
        printk(KERN_ERR LOG_PREFIX "Failed to register kprobe for `uart_write`, returned code: %d\n", ret);
        return ret;
    }

    printk(KERN_INFO LOG_PREFIX "RS-485 interface has been hooked successfully\n");
    return 0;
}

static void module_exit_fn(void) {
    unregister_kprobe(&hook_uart_write);
    for (int i = 0; i < sizeof(rs485_worker_queues) / sizeof(rs485_worker_queues[0]); i++) {
        if (rs485_worker_queues[i]) {
            destroy_workqueue(rs485_worker_queues[i]);
        }
    }
    rs485_dtr_deinit();

    printk(KERN_INFO LOG_PREFIX "RS-485 interface has been unhooked successfully\n");
}

module_init(module_init_fn);
module_exit(module_exit_fn);
