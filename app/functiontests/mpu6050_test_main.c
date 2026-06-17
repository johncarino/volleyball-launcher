#include "hal/mpu6050.h"

#include <stdio.h>
#include <unistd.h>

int main(void)
{
    if (mpu6050_init("/dev/i2c-1") != 0) {
        printf("Failed to initialize MPU6050\n");
        return 1;
    }

    printf("MPU6050 initialized successfully\n");

    while (1) {
        mpu6050_data_t data;

        if (mpu6050_read(&data) != 0) {
            printf("Failed to read MPU6050\n");
            mpu6050_close();
            return 1;
        }

        printf(
            "Pitch raw %7.2f  stable %7.2f  int %3d  |  "
            "Roll raw %7.2f  stable %7.2f  int %3d  |  "
            "Horizontal %7.2f  int %4d  |  "
            "Yaw %7.2f  int %4d  |  "
            "GZ %7.2f dps\n",
            data.pitch_deg,
            data.stable_pitch_deg,
            data.pitch_int,
            data.roll_deg,
            data.stable_roll_deg,
            data.roll_int,
            data.horizontal_deg,
            data.horizontal_int,
            data.yaw_deg,
            data.yaw_int,
            data.gz_dps
        );

        usleep(200000);
    }

    mpu6050_close();

    return 0;
}