#ifndef LRT_CONTROLLER_H_
#define LRT_CONTROLLER_H_

#include <stdint.h>

/* Runs the whole suite on no-OS I2C device_id i2c_id; returns failures */
int lrt_controller_run(uint32_t i2c_id);

#endif /* LRT_CONTROLLER_H_ */
