#ifndef GPS_PC_MODE_H
#define GPS_PC_MODE_H

#define WARM_START 1
#define COLD_START 125
#define HOT_START 1024
#define FAC_START 65535
#define LOG_ENABLE 136
#define LOG_DISABLE 520

extern void set_pc_mode(char input_pc_mode);

extern int gps_export_start(void);

extern int gps_export_stop(void);

extern int get_nmea_data(char *nbuff);

extern int set_gps_mode(unsigned int mode);

extern int get_init_mode(void);

extern int get_stop_mode(void);

#endif
