## Implementation notes for IMU driver

### Questions: 
##### Does the already library do it?
##### Should I build a wrapper around the library implementation? 

## TO-DO: 

- Imlpement sensor calibration functions  
- Implement easy frs read and write functions for sensor specific data 


## Notes:

- Need initial startup of sig motion, once sig motion fires, get accel data and if accel drops below a certain threshold, suspened the data processing task then resume sig motion task 

- Universal getter and setter that allows register value 

- "C file should have a flexible interface to allow you to communicate with anything possible on the IC (universal setters[write on bus] and getters[read on bus] that allows you that takes in a register argument [from header file defines])"


- Calibration for gyro and magnet

- If accel drops below a certain threshold (i.e subject is static) pause the data processing task, and resume sig motion task then sleep device. 

- Only raw sensor reports gives timestamps