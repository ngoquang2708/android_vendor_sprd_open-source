#include <fcntl.h>
#include <errno.h>
#include <math.h>
#include <poll.h>
#include <unistd.h>
#include <dirent.h>
#include <stdlib.h>
#include <sys/select.h>

#include <cutils/log.h>

#include "PSensor.h"
#include "sensors.h"


#define SYSFS_PATH_CLASS_CONTROL  "/sys/board_properties/facemode"

/*****************************************************************************/
static struct sensor_t sSensorList[] = {
     { "TP Proximity sensor",
          "LITEON",
          1, SENSORS_PROXIMITY_HANDLE,
          SENSOR_TYPE_PROXIMITY, 1.0f,
          1.0f, 0.005f, 0, 0, 0, {} },	

};

PlsSensor::PlsSensor() :
	SensorBase(NULL, "focaltech_ts"),
		mEnabled(0), mPendingMask(0), mInputReader(32), mHasPendingEvent(false)
{
    memset(mPendingEvents, 0, sizeof(mPendingEvents));

	mPendingEvents[Proximity].version = sizeof(sensors_event_t);
	mPendingEvents[Proximity].sensor = ID_P;
	mPendingEvents[Proximity].type = SENSOR_TYPE_PROXIMITY;

	for (int i = 0; i < numSensors; i++)
		mDelays[i] = 200000000;	// 200 ms by default

}

PlsSensor::~PlsSensor()
{
}

bool PlsSensor::hasPendingEvents() const
{
	return mHasPendingEvent;
}

int PlsSensor::setDelay(int32_t handle, int64_t ns)
{
	return 0;
}

int PlsSensor::setEnable(int32_t handle, int enabled)
{
	int bytes;
	char buffer[8];
	
	int err = 0;

	if (handle != ID_P) 
	{
		return -EINVAL;
    	}

	if (mEnabled <= 0) 
	{
		if (enabled) 
		{
			bytes = sprintf(buffer, "%d\n", 1) + 1;
			err = write_sys_attribute(SYSFS_PATH_CLASS_CONTROL, buffer, bytes);
			system("echo tp_lock >/sys/power/wake_lock");
        	}
    	}
	else if (mEnabled == 1) 
	{
		if (!enabled) 
		{
			bytes = sprintf(buffer, "%d\n", 0) + 1;
			err = write_sys_attribute(SYSFS_PATH_CLASS_CONTROL, buffer, bytes);
			system("echo tp_lock >/sys/power/wake_unlock");
		}
    	}
	if (err != 0) 
	{
		return err;
    	}

	if (enabled) 
	{
		mEnabled++;
		if (mEnabled > 32767)
		{
			mEnabled = 32767;
    		}
    	} 
	else 
	{
        	mEnabled--;
		if (mEnabled < 0)
		{
			mEnabled = 0;
    		}
    	}

    	return err;
}

int PlsSensor::getEnable(int32_t handle)
{
	int enable=0;
	int what = -1;
	switch (handle) 
	{
        	case ID_P:
			what = Proximity;
			break;
		default:
			return -EINVAL;
    	}

	if (uint32_t(what) >= numSensors)
       		return -EINVAL;

	enable = mEnabled & (1 << what);

	if(enable > 0)
		enable = 1;

	return enable;
}

int PlsSensor::readEvents(sensors_event_t * data, int count)
{
	if (count < 1)
		return -EINVAL;

	ssize_t n = mInputReader.fill(data_fd);
	if (n < 0)
		return n;

	int numEventReceived = 0;
	input_event const* event;

	while (count && mInputReader.readEvent(&event)) 
	{
	        int type = event->type;
		if (type == EV_ABS)
		{
			processEvent(event->code, event->value);
			mInputReader.next();
        	}
		else if (type == EV_SYN) 
		{
			int64_t time = timevalToNano(event->time);
			for (int j=0 ; count && mPendingMask && j<numSensors ; j++)
			{
				if (mPendingMask & (1<<j))
				{
					mPendingMask &= ~(1<<j);
					mPendingEvents[j].timestamp = time;
					if (mEnabled & (1<<j))
					{
						*data++ = mPendingEvents[j];
						count--;
						numEventReceived++;
                    			}
                		}
            		}
           		if (!mPendingMask)
			{
                		mInputReader.next();
            		}
		}
		else
		{
            		mInputReader.next();
        	}
    	}

	return numEventReceived;
}

void PlsSensor::processEvent(int code, int value)
{
	switch (code)
	{
		case EVENT_TYPE_PROXIMITY:
			mPendingMask |= 1<<Proximity;
			mPendingEvents[Proximity].distance = value;
			break;
        	default:
			break;
    	}
}

int PlsSensor::populateSensorList(struct sensor_t *list)
{
	memcpy(list, sSensorList, sizeof(struct sensor_t) * numSensors);
	return numSensors;
}

/*****************************************************************************/
