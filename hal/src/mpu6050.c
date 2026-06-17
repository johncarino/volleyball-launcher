#include "hal/mpu6050.h"

#include <fcntl.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>

#define MPU6050_ADDR 0x68

#define REG_PWR_MGMT_1 0x6B
#define REG_GYRO_CONFIG 0x1B
#define REG_ACCEL_CONFIG 0x1C
#define REG_ACCEL_XOUT_H 0x3B
#define REG_GYRO_XOUT_H 0x43

#define ACCEL_SCALE_2G 16384.0
#define GYRO_SCALE_250DPS 131.0

#define FILTER_SIZE 10

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int mpu_fd = -1;

static double pitch_buffer[FILTER_SIZE];
static double roll_buffer[FILTER_SIZE];

static int filter_index = 0;
static int filter_count = 0;

static double yaw_angle_deg = 0.0;
static struct timespec last_time;
static int yaw_initialized = 0;

static int write_reg(int fd, uint8_t reg, uint8_t value)
{
    uint8_t data[2] = {reg, value};

    if (write(fd, data, 2) != 2) {
        return -1;
    }

    return 0;
}

static int read_regs(int fd, uint8_t reg, uint8_t *buffer, size_t length)
{
    if (write(fd, &reg, 1) != 1) {
        return -1;
    }

    if (read(fd, buffer, length) != (ssize_t)length) {
        return -1;
    }

    return 0;
}

static int16_t to_int16(uint8_t high, uint8_t low)
{
    return (int16_t)((high << 8) | low);
}

static double average_buffer(const double *buffer, int count)
{
    double sum = 0.0;

    for (int i = 0; i < count; i++) {
        sum += buffer[i];
    }

    return sum / count;
}

static void update_filter(double pitch, double roll, double *stable_pitch, double *stable_roll)
{
    pitch_buffer[filter_index] = pitch;
    roll_buffer[filter_index] = roll;

    filter_index = (filter_index + 1) % FILTER_SIZE;

    if (filter_count < FILTER_SIZE) {
        filter_count++;
    }

    *stable_pitch = average_buffer(pitch_buffer, filter_count);
    *stable_roll = average_buffer(roll_buffer, filter_count);
}

static double get_dt_seconds(void)
{
    struct timespec now;

    clock_gettime(CLOCK_MONOTONIC, &now);

    if (!yaw_initialized) {
        last_time = now;
        yaw_initialized = 1;
        return 0.0;
    }

    double dt = (double)(now.tv_sec - last_time.tv_sec);
    dt += (double)(now.tv_nsec - last_time.tv_nsec) / 1000000000.0;

    last_time = now;

    return dt;
}

static double normalize_angle(double angle)
{
    while (angle > 180.0) {
        angle -= 360.0;
    }

    while (angle < -180.0) {
        angle += 360.0;
    }

    return angle;
}

int mpu6050_init(const char *i2c_device)
{
    if (i2c_device == NULL) {
        i2c_device = "/dev/i2c-1";
    }

    mpu_fd = open(i2c_device, O_RDWR);
    if (mpu_fd < 0) {
        perror("Failed to open MPU6050 I2C device");
        return -1;
    }

    if (ioctl(mpu_fd, I2C_SLAVE, MPU6050_ADDR) < 0) {
        perror("Failed to select MPU6050 address");
        close(mpu_fd);
        mpu_fd = -1;
        return -1;
    }

    if (write_reg(mpu_fd, REG_PWR_MGMT_1, 0x00) < 0) {
        perror("Failed to wake MPU6050");
        close(mpu_fd);
        mpu_fd = -1;
        return -1;
    }

    usleep(100000);

    if (write_reg(mpu_fd, REG_GYRO_CONFIG, 0x00) < 0) {
        perror("Failed to configure MPU6050 gyro");
        close(mpu_fd);
        mpu_fd = -1;
        return -1;
    }

    if (write_reg(mpu_fd, REG_ACCEL_CONFIG, 0x00) < 0) {
        perror("Failed to configure MPU6050 accelerometer");
        close(mpu_fd);
        mpu_fd = -1;
        return -1;
    }

    for (int i = 0; i < FILTER_SIZE; i++) {
        pitch_buffer[i] = 0.0;
        roll_buffer[i] = 0.0;
    }

    filter_index = 0;
    filter_count = 0;

    yaw_angle_deg = 0.0;
    yaw_initialized = 0;

    return 0;
}

int mpu6050_read(mpu6050_data_t *data)
{
    uint8_t accel_buffer[6];
    uint8_t gyro_buffer[6];

    if (mpu_fd < 0 || data == NULL) {
        return -1;
    }

    if (read_regs(mpu_fd, REG_ACCEL_XOUT_H, accel_buffer, sizeof(accel_buffer)) < 0) {
        perror("Failed to read MPU6050 accelerometer data");
        return -1;
    }

    if (read_regs(mpu_fd, REG_GYRO_XOUT_H, gyro_buffer, sizeof(gyro_buffer)) < 0) {
        perror("Failed to read MPU6050 gyro data");
        return -1;
    }

    data->ax_raw = to_int16(accel_buffer[0], accel_buffer[1]);
    data->ay_raw = to_int16(accel_buffer[2], accel_buffer[3]);
    data->az_raw = to_int16(accel_buffer[4], accel_buffer[5]);

    data->gx_raw = to_int16(gyro_buffer[0], gyro_buffer[1]);
    data->gy_raw = to_int16(gyro_buffer[2], gyro_buffer[3]);
    data->gz_raw = to_int16(gyro_buffer[4], gyro_buffer[5]);

    data->ax_g = data->ax_raw / ACCEL_SCALE_2G;
    data->ay_g = data->ay_raw / ACCEL_SCALE_2G;
    data->az_g = data->az_raw / ACCEL_SCALE_2G;

    data->gx_dps = data->gx_raw / GYRO_SCALE_250DPS;
    data->gy_dps = data->gy_raw / GYRO_SCALE_250DPS;
    data->gz_dps = data->gz_raw / GYRO_SCALE_250DPS;

    data->pitch_deg = atan2(
        data->ax_g,
        sqrt((data->ay_g * data->ay_g) + (data->az_g * data->az_g))
    ) * 180.0 / M_PI;

    data->roll_deg = atan2(
        data->ay_g,
        sqrt((data->ax_g * data->ax_g) + (data->az_g * data->az_g))
    ) * 180.0 / M_PI;

    update_filter(
        data->pitch_deg,
        data->roll_deg,
        &data->stable_pitch_deg,
        &data->stable_roll_deg
    );

    data->pitch_int = (int)lround(data->stable_pitch_deg);
    data->roll_int = (int)lround(data->stable_roll_deg);

    data->horizontal_deg = atan2(data->ay_g, data->ax_g) * 180.0 / M_PI;
    data->horizontal_deg = normalize_angle(data->horizontal_deg);
    data->horizontal_int = (int)lround(data->horizontal_deg);

    double dt = get_dt_seconds();

    yaw_angle_deg += data->gz_dps * dt;
    yaw_angle_deg = normalize_angle(yaw_angle_deg);

    data->yaw_deg = yaw_angle_deg;
    data->yaw_int = (int)lround(data->yaw_deg);

    return 0;
}

void mpu6050_close(void)
{
    if (mpu_fd >= 0) {
        close(mpu_fd);
        mpu_fd = -1;
    }
}