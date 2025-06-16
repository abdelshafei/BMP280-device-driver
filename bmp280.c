#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/i2c.h>       // For I2C support
#include <linux/device.h>
#include <linux/delay.h>

#define DRIVER_NAME "bmp280"

struct bmp280_data {
    struct i2c_client *client; // For outside of probe reference to client

    /* Caliberation registers in BMP 280 */
    unsigned short dig_T1, dig_P1; 
    short dig_T2, dig_T3,
    dig_P2, dig_P3, dig_P4, dig_P5,
    dig_P6, dig_P7, dig_P8, dig_P9;
};

/*
 * Purpose:
 *   Sysfs show function for the BMP280 driver.
 *   When userspace reads the sysfs attribute, this function fetches and formats
 *   the latest temperature and pressure readings from the sensor, applies compensation
 *   algorithms, and writes the formatted string to the provided buffer.
 *
 * Parameters:
 *   @dev:  Pointer to the device structure representing the BMP280 sensor.
 *   @attr: Pointer to the device attribute structure (not used here).
 *   @buf:  Output buffer where the result string is written.
 *
 * Return:
 *   On success: Number of bytes written to the buffer (as per sysfs show convention).
 *   On failure: Negative error code (e.g., -EIO) if sensor communication fails.
 *
 * Details:
 *   This function is called each time a user reads the sysfs file (e.g.,
 *   'cat /sys/bus/i2c/devices/1-0076/Bmp280-Calculations').
 *   It reads raw temperature and pressure values from the sensor, applies
 *   Bosch's integer compensation formula, and formats the results as a
 *   human-readable string in buf.
 */
static ssize_t pressureAndTemperature_show(struct device *dev, struct device_attribute *attr, char *buf) {
    printk(KERN_INFO "Measuring and Displaying the calculated temperature and pressure...");

    struct bmp280_data *data = i2c_get_clientdata(to_i2c_client(dev)); // Used to reference the I2C api

    /* Calculating Temperature... */
    int msb_T = i2c_smbus_read_byte_data(data->client, 0xFA);
    int lsb_T = i2c_smbus_read_byte_data(data->client, 0xFB);
    int xlsb_T = i2c_smbus_read_byte_data(data->client, 0xFC);

    if(msb_T < 0 || lsb_T < 0 || xlsb_T < 0) {
        dev_err(&data->client->dev, "Failed to read from raw Temperature data registers\n");
        return -EIO;
    }

    long signed int adc_T = ((msb_T << 12) | (lsb_T << 4) | (xlsb_T >> 4));

    long signed int var1, var2, t_fine, T;
    var1 = ((((adc_T >> 3) - ((int32_t)data->dig_T1 << 1))) * ((int32_t)data->dig_T2)) >> 11;
    var2 = (((((adc_T >> 4) - ((int32_t)data->dig_T1)) * ((adc_T >> 4) - ((int32_t)data->dig_T1))) >> 12) * ((int32_t)data->dig_T3)) >> 14;

    t_fine =  var1 + var2;
    T = (t_fine * 5 + 128) >> 8;

    /* Calculating Pressure */
    int msb_P = i2c_smbus_read_byte_data(data->client, 0xF7);
    int lsb_P = i2c_smbus_read_byte_data(data->client, 0xF8);
    int xlsb_P = i2c_smbus_read_byte_data(data->client, 0xF9);

    if(msb_P < 0 || lsb_P < 0 || xlsb_P < 0) {
        dev_err(&data->client->dev, "Failed to read from raw Pressure data registers\n");
        return -EIO;
    }

    long signed int adc_P = ((msb_P << 12) | (lsb_P << 4) | (xlsb_P >> 4));

    long signed int P;

    var1 = ((int64_t)t_fine) - 128000;
    var2 = var1 * var1 * (int64_t)data->dig_P6;
    var2 = var2 + ((var1 * (int64_t)data->dig_P5) << 17);
    var2 = var2 + (((int64_t)data->dig_P4) << 35);
    var1 = ((var1 * var1 * (int64_t)data->dig_P3) >> 8) + ((var1 * (int64_t)data->dig_P2) << 12);
    var1 = (((((int64_t)1) << 47) + var1)) * ((int64_t)data->dig_P1) >> 33;

    if (var1 == 0) {
        return sprintf(buf, "Temperature: %ld°C\nPressure: 0Pa\n", T/100);
    } 

    P = 1048576 - adc_P;
    P = (((P << 31) - var2) * 3125) / var1;
    var1 = (((int64_t)data->dig_P9) * (P >> 13) * (P >> 13)) >> 25;
    var2 = (((int64_t)data->dig_P8) * P) >> 19;
    P = ((P + var1 + var2) >> 8) + (((int64_t)data->dig_P7) << 4);


    return sprintf(buf, "Temperature: %ld°C\nPressure: %ldPa\n", T/100, P/256);
}
static struct device_attribute dev_attr_pressureAndTemperature = __ATTR(Bmp280-Calculations, 0444, pressureAndTemperature_show, NULL); //Sysfs object that would be pressure file for the device driver

/*
 * Purpose:
 *   Helper function for the BMP280 driver to read a 16-bit unsigned value
 *   from two consecutive I2C registers. Used to fetch calibration parameters
 *   and other multi-byte values stored in LSB-MSB order.
 *
 * Parameters:
 *   @client: Pointer to the I2C client structure representing the BMP280 sensor.
 *   @addr:   Register address of the least significant byte (LSB). The function
 *            reads from 'addr' and 'addr + 1' to assemble the full value.
 *
 * Return:
 *   On success: 16-bit unsigned value read from the sensor, assembled as (MSB << 8) | LSB.
 *   On failure: -1 (all bits set) if either I2C read fails.
 *
 * Details:
 *   Reads the LSB at 'addr', then the MSB at 'addr + 1', combining them into a single
 *   unsigned short value as specified by the BMP280 datasheet (little-endian order).
 *   The function is intended for use during driver initialization to fetch sensor
 *   calibration constants.
 */
static unsigned short read_u16_from_i2c(struct i2c_client *client, u8 addr)
{
    int lsb = i2c_smbus_read_byte_data(client, addr);
    int msb = i2c_smbus_read_byte_data(client, addr + 1);
    if (lsb < 0 || msb < 0)
        return -1; 
    return (msb << 8) | lsb;
}

/*
 * Purpose:
 *   Helper function for the BMP280 driver to read a 16-bit signed value
 *   from two consecutive I2C registers. Used to fetch calibration parameters
 *   and other multi-byte values stored in LSB-MSB order.
 *
 * Parameters:
 *   @client: Pointer to the I2C client structure representing the BMP280 sensor.
 *   @addr:   Register address of the least significant byte (LSB). The function
 *            reads from 'addr' and 'addr + 1' to assemble the full value.
 *
 * Return:
 *   On success: 16-bit unsigned value read from the sensor, assembled as (MSB << 8) | LSB.
 *   On failure: -1 (all bits set) if either I2C read fails.
 *
 * Details:
 *   Reads the LSB at 'addr', then the MSB at 'addr + 1', combining them into a single
 *   unsigned short value as specified by the BMP280 datasheet (little-endian order).
 *   The function is intended for use during driver initialization to fetch sensor
 *   calibration constants.
 */
static short read_s16_from_i2c(struct i2c_client *client, u8 addr)
{
    int lsb = i2c_smbus_read_byte_data(client, addr);
    int msb = i2c_smbus_read_byte_data(client, addr + 1);
    if (lsb < 0 || msb < 0)
        return -1; 
    return ((msb << 8) | lsb);
}

/*
 * Purpose:
 *   Probe function for the BMP280 driver, called by the I2C subsystem when the
 *   driver is matched to a device. Initializes the sensor, verifies the chip ID,
 *   resets configuration registers, reads calibration parameters, and creates
 *   sysfs entries for user access.
 *
 * Parameters:
 *   @client: Pointer to the I2C client structure representing the BMP280 device.
 *            Used for I2C communication, registering sysfs files, and device logging.
 *
 * Return:
 *   0 on success (driver initialized and ready).
 *   Negative error code (e.g., -ENODEV, -EIO) on failure to communicate with the sensor,
 *   invalid chip ID, or any hardware initialization error.
 *
 * Details:
 *   This function is responsible for preparing the BMP280 sensor for operation:
 *     - Checks the sensor's chip ID to ensure correct device.
 *     - Resets and configures sensor registers for normal mode.
 *     - Reads and stores calibration constants from sensor NVM.
 *     - Registers sysfs attributes to expose sensor readings to userspace.
 *     - Retrieves calibration register values to compensate for reading values.
 *   Called automatically by the kernel when the driver matches an I2C device.
 */
static int bmp280_probe(struct i2c_client *client)
{
    printk(KERN_INFO "BMP280: Probed at address 0x%02x\n", client->addr);

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
    * |-------------------------|-------------------------|----------|-----------|
    * |       t_sb[2 : 0]       |      filter[2 : 0]      |(reserved)|spi3w_en[0]|
    * |-------------------------|-------------------------|----------|-----------|
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


    if(device_create_file(&client->dev, &dev_attr_pressureAndTemperature) < 0) {
        dev_err(&client->dev, "Failed to load the sysfs file");
        return -EIO;
    }

    return 0;
}

/*
 * Purpose:
 *   Remove function for the BMP280 driver, called when the driver is unloaded
 *   or the device is removed. Cleans up driver resources, disables the sensor,
 *   and removes sysfs entries created during initialization.
 *
 * Parameters:
 *   @client: Pointer to the I2C client structure representing the BMP280 device.
 *            Used for I2C communication, sysfs file management, and device logging.
 *
 * Return:
 *   None. (This function returns void.)
 *
 * Details:
 *   This function is responsible for:
 *     - Setting the sensor into sleep mode to reduce power consumption.
 *     - Removing any sysfs attributes/files associated with the device.
 *   Called automatically by the kernel when the device is removed or the driver is unloaded.
 */
static void bmp280_remove(struct i2c_client *client)
{
    printk(KERN_INFO "BMP280: Removed\n");

    // Sets the 0xF4 register to sleep mode
    i2c_smbus_write_byte_data(client, 0xF4, 0x00);

    device_remove_file(&client->dev, &dev_attr_pressureAndTemperature);
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