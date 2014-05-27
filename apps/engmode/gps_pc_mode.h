#ifndef GPS_PC_MODE_H
#define GPS_PC_MODE_H

#define WARM_START 1
#define COLD_START 125
#define HOT_START 1024
#define FAC_START 65535
#define LOG_ENABLE 136
#define LOG_DISABLE 520

void set_pc_mode(char input_pc_mode);

int gps_export_start(void);

int gps_export_stop(void);

int get_nmea_data(char *nbuff);

void set_gps_mode(unsigned int mode);

#endif
