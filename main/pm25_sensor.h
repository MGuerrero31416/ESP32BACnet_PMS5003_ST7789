#ifndef PM25_SENSOR_H
#define PM25_SENSOR_H

#include <stdbool.h>

// Public function declarations
void pm25_sensor_init(void);
float pm25_get_pm1_0(void);
float pm25_get_pm2_5(void);
float pm25_get_pm10(void);

#endif // PM25_SENSOR_H