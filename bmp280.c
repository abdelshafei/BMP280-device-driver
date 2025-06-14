#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/i2c.h>       // For I2C support
#include <linux/device.h>
#include <linux/uaccess.h>   // For copy_to_user if needed

#define DRIVER_NAME "bmp280"

static ssize_t temperature_show(struct device *dev, struct device_attribute *attr, char *buf) {
    return sprintf(buf, "70\n"); // Returns a dummy temperature value
}

static ssize_t pressure_show(struct device *dev, struct device_attribute *attr, char *buf) {
    return sprintf(buf, "10000\n"); // Returns a dummy temperature value
}

static struct device_attribute dev_attr_temperature = __ATTR(temperature, 0444, temperature_show, NULL); //Sysfs object that would be temperature file for the device driver
static struct device_attribute dev_attr_pressure = __ATTR(pressure, 0444, pressure_show, NULL); //Sysfs object that would be pressure file for the device driver

/*
* Purpose: Intializes the sensor driver 
*
*
* Parameters: 
*   struct i2c_client *client (input): Represents the specific I2C device instance that matches this driver.
*       - Can be used for:
*           > Read/Write via I2C
*           > Register sysfs files
*           > Logging things in kernel logs
*
*   const struct i2c_device_id *id (input):  Can Distinguish between devices set on the same I2C.
*         
*
* Return: 
*   <= 0: Indicates Success (with 0 being the highest priority to load)
*   > 0: Indicates Failure to load kernel module 
*/
static int bmp280_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
    printk(KERN_INFO, "BMP280: Probed at address 0x%02x\n", client->addr);

    device_create_file(&client->dev, &dev_attr_temperature);
    device_create_file(&client->dev, &dev_attr_pressure);

    return 0;
}

/*
* Purpose: Cleans up the sensor driver
*
*
* Parameters: 
*   struct i2c_client *client (input): Represents the specific I2C device instance that matches this driver.
*       - Can be used for:
*           > Read/Write via I2C
*           > Removes registered sysfs files
*           > Logging things in kernel logs
*
*         
* Return: 
*   <= 0: Indicates Success (with 0 being the highest priority to load)
*   > 0: Indicates Failure to load kernel module 
*/
static int bmp280_remove(struct i2c_client *client)
{
    printk(KERN_INFO, "BMP280: Removed\n");

    device_remove_file(&client->dev, dev_attr_temperature);
    device_remove_file(&client->dev, dev_attr_pressure);

    return 0;
}

static const struct i2c_device_id bmp280_id[] = { 
    { DRIVER_NAME, 0 }, // Load this driver to an I2C device named "bmp280"
    { }                 // Signifies end of array 
};

static const struct of_device_id bmp280_of_match[] = {
    { .compatible = "bosch,bmp280" }, // Matches device tree node using this string
    {},                               // Signifies end of array
};

/* Exposes table so kernel can auto-load this device driver module */
MODULE_DEVICE_TABLE(i2c, bmp280_id);
MODULE_DEVICE_TABLE(of, bmp280_of_match);

static struct i2c_driver bmp280_driver = {
    .driver = {
        .name = DRIVER_NAME,
        .of_match_table = bmp280_of_match,
    },
    .probe = bmp280_probe,
    .remove = bmp280_remove,
    .id_table = bmp280_id,
};

module_i2c_driver(bmp280_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Abdelrahman ElShafay");
MODULE_DESCRIPTION("BMP280 I2C Driver Module");