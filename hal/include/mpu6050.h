#ifndef HAL_MPU6050_H
#define HAL_MPU6050_H

#include <stdint.h>

typedef struct {
    int16_t ax_raw;
    int16_t ay_raw;
    int16_t az_raw;

    int16_t gx_raw;
    int16_t gy_raw;
    int16_t gz_raw;

    double ax_g;
    double ay_g;
    double az_g;

    double gx_dps;
    double gy_dps;
    double gz_dps;

    double pitch_deg;
    double roll_deg;

    double stable_pitch_deg;
    double stable_roll_deg;

    int pitch_int;
    int roll_int;

    double horizontal_deg;
    int horizontal_int;

    double yaw_deg;
    int yaw_int;
} mpu6050_data_t;

int mpu6050_init(const char *i2c_device);
int mpu6050_read(mpu6050_data_t *data);
void mpu6050_close(void);

#endif