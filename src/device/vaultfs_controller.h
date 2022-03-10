#ifndef CONTROLLER_H
#define CONTROLLER_H

#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/string.h>
#include <linux/crypto.h>
#include <crypto/hash.h>

#include "../vaultfs.h"
#include "../vaultfs_security.h"

#define DEVICE_NAME "vaultfs_controller"
#define DEVICE_CLASS "vaultfs_controller_class"


int register_controller(void);
void unregister_controller(void);


#endif