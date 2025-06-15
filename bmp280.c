#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/i2c.h>       // For I2C support
#include <linux/device.h>
#include <linux/uaccess.h>   // For copy_to_user if needed

#define DRIVER_NAME "bmp280"

struct bmp280_data {
    struct i2c_client *client; // For outside of probe reference to client

    /* */
    unsigned short dig_T1, dig_P1; 
    short dig_T2, dig_T3,
    dig_P2, dig_P3, dig_P4, dig_P5,
    dig_P6, dig_P7, dig_P8, dig_P9;
};


static ssize_t temperature_show(struct device *dev, struct device_attribute *attr, char *buf) {
    printk(KERN_INFO, "Measuring and Displaying the calculated temperature...");

    struct bmp280_data *data = i2c_get_clientdata(to_i2c_client(dev)); // Used to reference the I2C api

    u8 measure_status;
    int tries = 10;
    do {
        measure_status = i2c_smbus_read_byte_data(data->client, 0xF3);
        msleep(1);
    } while((measure_status & 0x08) && --tries > 0);

    int msb = i2c_smbus_read_byte_data(data->client, 0xFA);
    int lsb = i2c_smbus_read_byte_data(data->client, 0xFB);
    int xlsb = i2c_smbus_read_byte_data(data->client, 0xFC);

    if(msb < 0 || lsb < 0 || xlsb < 0) {
        dev_err(&data->client->dev, "Failed to read from raw temperature data registers\n");
        return -EIO
    }

    long signed int adc = ((msb << 12) | (lsb << 4) | (xlsb >> 4));

    long signed int var1, var2, t_fine, T;
    var1 = ((((adc >> 3) - ((int32_t)data->dig_T1 << 1))) * ((int32_t)data->dig_T2)) >> 11;
    var2 = (((((adc >> 4) - ((int32_t)data->dig_T1)) * ((adc >> 4) - ((int32_t)data->dig_T1))) >> 12) * ((int32_t)data->dig_T3)) >> 14;

    t_fine =  var1 + var2;
    T = (t_fine * 5 + 128) >> 8;

    return sprintf(buf, "The Surrounding Temperature is: %d°C\n", T);
}

static ssize_t pressure_show(struct device *dev, struct device_attribute *attr, char *buf) {
    printk(KERN_INFO, "Measuring and Displaying the calculated pressure...");

    return sprintf(buf, "10000\n"); // Returns a dummy  pressure value
}

static struct device_attribute dev_attr_temperature = __ATTR(temperature, 0444, temperature_show, NULL); //Sysfs object that would be temperature file for the device driver
static struct device_attribute dev_attr_pressure = __ATTR(pressure, 0444, pressure_show, NULL); //Sysfs object that would be pressure file for the device driver

static unsigned short read_u16_from_i2c(struct i2c_client *client, u8 addr)
{
    int lsb = i2c_smbus_read_byte_data(client, addr);
    int msb = i2c_smbus_read_byte_data(client, addr + 1);
    if (lsb < 0 || msb < 0)
        return -1; 
    return (msb << 8) | lsb;
}

static short read_s16_from_i2c(struct i2c_client *client, u8 addr)
{
    int lsb = i2c_smbus_read_byte_data(client, addr);
    int msb = i2c_smbus_read_byte_data(client, addr + 1);
    if (lsb < 0 || msb < 0)
        return -1; 
    return ((msb << 8) | lsb);
}

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

    u8 chip_id = i2c_smbus_read_byte_data(client, 0xD0);  // Confirms the sensor chip id is 0x58
    if (chip_id != 0x58) {
        dev_err(&client->dev, "Unexpected chip ID: 0x%x\n", chip_id);
        return -ENODEV;
    }

    // Resets the sensor old configurations
    if(i2c_smbus_write_byte_data(client, 0xE0, 0xB6) < 0) {
        dev_err(&client->dev, "Failed to reset sensor\n");
        return -EIO;
    }
    msleep(5);

    /* 
    *  Status register has two bits:
    *   • measuring[0] at bit 3 which is set to 1 when conversion is running or 0 when results are transferred to the registers
    *   • im_update[0] at bit 0 which is set to 1 where its copying images to NVM or 0 when idle
    *
    *   We have to make sure the im_update bit is 0 to start communicating or else garbage data will result from it.
    */
    u8 status;
    int tries = 10; // Gives enough time for the NVM to perform data copy
    do {

        status = i2c_smbus_read_byte_data(client, 0xF3);
        msleep(1);

    } while((status & 0x01) && --tries > 0);

    // Setting up the measurement register

    /*
                    *** ACCORDING TO THE REGISTER DATASHEET FOR BMP 280 ***
    * For a simple bmp 280 device we are dealing with we need to go for the safest route:
    *   • For mode[1 : 0] we go with normal mode which is 11
    *   • For osrs_p[2 : 0] which oversamples x4 for pressure we need to go with the standard resolution which is 011
    *   • For osrs_t[2 : 0] which oversamples x1 for temperature we need to go with the standard resolution which is 001
    * This leaves us with a byte value of 0b00101111 (a.k.a 0x2F)for the 0xF4 register which is segmented as such:
    * |-------------------------|-------------------------|-------------------|
    * |      osrs_t[2 : 0]      |      osrs_p[2 : 0]      |    mode[1 : 0]    |
    * |-------------------------|-------------------------|-------------------|
    */
    if(i2c_smbus_write_byte_data(client, 0xF4, 0x2F) < 0) {
        dev_err(&client->dev, "Failed to configure the ctrl_meas register\n");
        return -EIO;
    }

    //Setting up the config register

    /*
                    *** ACCORDING TO THE REGISTER DATASHEET FOR BMP 280 ***
    * For spi3w_en[0] we set it to 0 since it sets up SPI interface and we are already using I2C
    * For filter[2 : 0] we set it to IIR filter coeffecient of 4 which is 010 for low filtering to reduce short-term disturbances
    * For t_sb[2 : 0]  we set it to 125 ms since the IIR fc is 4 and we are going with the standard resolution method which is bit value of 010
    *
    * This leaves us with a byte value of 0b01001000 (a.k.a 0x48) which is segmented as follows:
    * |-------------------------|-------------------------|------|-----------|
    * |       t_sb[2 : 0]       |      filter[2 : 0]      |      |spi3w_en[0]|
    * |-------------------------|-------------------------|------|-----------|
    */
    if(i2c_smbus_write_byte_data(client, 0xF5, 0x48) < 0) {
        dev_err(&client->dev, "Failed to configure the config register\n");
        return -EIO;
    }

    struct bmp280_data *data = devm_kzalloc(&client->dev, sizeof(*data), GFP_KERNEL);
    data->client = client;
    i2c_set_clientdata(client, data);

    /* Intialization of calibration registers for temp/pressure calculations */
    data->dig_T1 = read_u16_from_i2c(client, 0x88);
    data->dig_T2 = read_s16_from_i2c(client, 0x8A);
    data->dig_T3 = read_s16_from_i2c(client, 0x8C);

    data->dig_P1 = read_u16_from_i2c(client, 0x8E);
    data->dig_P2 = read_s16_from_i2c(client, 0x90);
    data->dig_P3 = read_s16_from_i2c(client, 0x92);
    data->dig_P4 = read_s16_from_i2c(client, 0x94);
    data->dig_P5 = read_s16_from_i2c(client, 0x96);
    data->dig_P6 = read_s16_from_i2c(client, 0x98);
    data->dig_P7 = read_s16_from_i2c(client, 0x9A);
    data->dig_P8 = read_s16_from_i2c(client, 0x9C);
    data->dig_P9 = read_s16_from_i2c(client, 0x9E);


    if(device_create_file(&client->dev, &dev_attr_temperature) < 0) {
        dev_err(&client_dev, "Failed to load the temperature file");
        return -EIO;
    }

    if(device_create_file(&client->dev, &dev_attr_pressure) < 0) {
        dev_err(&client_dev, "Failed to load the pressure file");
        return -EIO;
    }

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
*   <= 0: Indicates Success 
*   > 0: Indicates Failure to remove kernel module 
*/
static int bmp280_remove(struct i2c_client *client)
{
    printk(KERN_INFO, "BMP280: Removed\n");

    // Sets the 0xF4 register to sleep mode
    i2c_smbus_write_byte_data(client, 0xF4, 0x00);

    device_remove_file(&client->dev, &dev_attr_temperature);
    device_remove_file(&client->dev, &dev_attr_pressure);

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