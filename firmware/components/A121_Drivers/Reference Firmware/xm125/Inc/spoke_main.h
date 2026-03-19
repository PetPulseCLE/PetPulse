// Copyright (c) 2025 Sensor Bar Project
// All rights reserved
// Spoke Main Application Header

#ifndef SPOKE_MAIN_H_
#define SPOKE_MAIN_H_

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Main entry point for spoke firmware
 * 
 * This function initializes the sensor system, I2C protocol, and enters
 * the main loop that processes Hub commands and performs measurements.
 * 
 * @param argc Argument count (unused, for compatibility)
 * @param argv Argument values (unused, for compatibility)
 * @return int Exit code (should never return in normal operation)
 */
int spoke_main(int argc, char *argv[]);

#ifdef __cplusplus
}
#endif

#endif // SPOKE_MAIN_H_
