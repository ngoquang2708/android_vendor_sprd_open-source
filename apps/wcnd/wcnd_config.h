#ifndef __WCND_CONFIG_H__
#define __WCND_CONFIG_H__


//Macro to control if doing reset
#define CP2_RESET_READY

//Macro to control if polling cp2 assert/watdog interface
#define CP2_WATCHER_ENABLE

//Macro to control if polling cp2 loop interface every 5 seconds
#define LOOP_CHECK

//Macro to enable the Wifi Engineer Mode
#define WIFI_ENGINEER_ENABLE

//Macro to enable CP2 power on/off
//#define WCND_CP2_POWER_ONOFF_ENABLE

//Macro to enable the state machine
//Note: if WCND_CP2_POWER_ONOFF_ENABLE is opened, WCND_STATE_MACHINE_ENABLE should also be opened.
//#define WCND_STATE_MACHINE_ENABLE

#endif
