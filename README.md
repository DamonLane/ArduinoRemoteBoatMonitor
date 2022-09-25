# ArduinoRemoteBoatMonitor
An Arduino microcontroller with water sensors and connections to the battery management system's (BMS's) CANbus outputs that periodically posts to an IoT website, and immediately dispatches an email if water is detected or the battery voltage/SoC are low with the help of the IoT website.

My project uses an Arduino MRK GSM 1400, which is amazingly well suited, even more than I thought. It needs a battery for extra amps when using the GSM connection, and it can also automatically switch to run off that battery if the normal power supply goes down; perfect! I want to know if the power goes down. It has an integrated real time clock and low sleep power consumption, so it can be set up to use very little energy, especially important as it draws from the boat's battery that is not being charged in winter.

