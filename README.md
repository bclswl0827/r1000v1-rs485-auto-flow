# r1000v1-rs485-autoflow

This module fixes RS-485 flow control issue on reComputer R1000 v1.0 by hooking `uart_write` function.

## Preview

https://github.com/user-attachments/assets/3b47fc9a-640f-4344-9b04-8943205af887

## Installation

Make sure you're running on a reComputer R1000 v1.0 [with its drivers installed](https://wiki.seeedstudio.com/recomputer_r1000_flash_OS/#install-recomputer-r1000-drivers-after-flashing-new-raspbian-os).

```bash
$ sudo apt update
$ sudo apt install raspberrypi-linux-headers git make gcc -y
$ git clone https://github.com/bclswl0827/r1000v1-rs485-auto-flow.git
$ cd r1000v1-rs485-auto-flow
$ make
$ sudo make install
```

## Load the module

To load the module, use the `modprobe` command:

```bash
$ sudo modprobe r1000v1_rs485_autoflow
```

If the kernel module is successfully loaded, you should see the following message in the kernel log:

```
[  256.037465] r1000v1_rs485_autoflow: RS-485 interface has been hooked successfully
```

## Unload the module

To unload the module, use the `rmmod` command:

```bash
$ sudo rmmod r1000v1_rs485_autoflow
```

If the kernel module is successfully unloaded, you should see the following message in the kernel log:

```
[  315.105485] r1000v1_rs485_autoflow: RS-485 interface has been unhooked successfully
```

## Load the module at boot

You can load the module at boot by adding the module name to the `/etc/modules` file:

```bash
$ echo "r1000v1_rs485_autoflow" | sudo tee -a /etc/modules
```

Note that this will require a reboot for the changes to take effect.

## About DKMS

This module is compatible with [DKMS](https://www.kernel.org/doc/html/latest/admin-guide/kernel-parameters.html#dkms), which is a kernel module management system. If you're planning to use this module with DKMS, please make sure to install the `dkms` package and use the `make dkms_install` command to install the module.

## License

This module is released under the [MIT License](https://github.com/bclswl0827/r1000v1-rs485-auto-flow/blob/main/LICENSE).
