#ifndef RADAR_CONFIG_H
#define RADAR_CONFIG_H

#include "a121_radar_driver.h"
#include "acc_sensor.h"

typedef enum
{
	VITALS_PRESET_NONE,
	VITALS_PRESET_SITTING,
	VITALS_PRESET_SLEEPING,
	VITALS_PRESET_INFANT,
} vitals_preset_t;

typedef struct {
    a121_vitals_config_t     *config;
    a121_vitals_handle_t     *handle;
    acc_sensor_t                *sensor;
    acc_cal_result_t             sensor_cal_result;
    void                        *buffer;
    uint32_t                     buffer_size;
    uint32_t                     sensor_id;
} radar_system_t;

void radar_config_set_preset(a121_vitals_config_t *config, vitals_preset_t preset);

radar_system_t *radar_system_create(vitals_preset_t preset);

bool radar_system_calibrate(radar_system_t *system);

void radar_system_destroy(radar_system_t *system);

#endif
