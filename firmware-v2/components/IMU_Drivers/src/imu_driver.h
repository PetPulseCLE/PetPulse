#ifndef IMU_DRIVER_H
#define IMU_DRIVER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void imu_events_init(void);

void imu_data_task(void *pv);
void imu_motion_task(void *pv);

void imu_flush_daily_steps(void);
uint32_t imu_get_daily_steps(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* IMU_DRIVER_H */
