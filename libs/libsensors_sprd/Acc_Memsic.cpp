/*
 * Copyright (C) 2011 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <fcntl.h>
#include <errno.h>
#include <math.h>
#include <poll.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/select.h>

#include <cutils/log.h>

#include "AccSensor.h"

#define	MXC622X_ACC_IOCTL_BASE 77
/** The following define the IOCTL command values via the ioctl macros */
#define	MXC622X_ACC_IOCTL_SET_DELAY		_IOW(MXC622X_ACC_IOCTL_BASE, 0, int)
#define	MXC622X_ACC_IOCTL_GET_DELAY		_IOR(MXC622X_ACC_IOCTL_BASE, 1, int)
#define	MXC622X_ACC_IOCTL_SET_ENABLE		_IOW(MXC622X_ACC_IOCTL_BASE, 2, int)
#define	MXC622X_ACC_IOCTL_GET_ENABLE		_IOR(MXC622X_ACC_IOCTL_BASE, 3, int)
#define	MXC622X_ACC_IOCTL_GET_COOR_XYZ       _IOW(MXC622X_ACC_IOCTL_BASE, 22, int)
#define	MXC622X_ACC_IOCTL_GET_CHIP_ID        _IOR(MXC622X_ACC_IOCTL_BASE, 255, char[32])

#define MEMSIC_UNIT_CONVERSION(value) ((value) * GRAVITY_EARTH / (64.0f))
#define MEMSIC_ACC_INPUT_NAME  	"accelerometer" 
#define MEMSIC_ACC_DEV_NAME		"/dev/mxc622x"
/*****************************************************************************/
static struct sensor_t sSensorList[] = {
	  { "Memsic MXC622x 2-axis Accelerometer",
          "Memsic",
          1, SENSORS_ACCELERATION_HANDLE,
          SENSOR_TYPE_ACCELEROMETER, (GRAVITY_EARTH * 2.0f),
		  (GRAVITY_EARTH) / 64.0f, 0.7f, 10000,0,0, { } },
};

AccSensor::AccSensor() :
	SensorBase(MEMSIC_ACC_DEV_NAME, MEMSIC_ACC_INPUT_NAME),
      mEnabled(0),mDelay(-1),mInputReader(8),mHasPendingEvent(false),
	  mSensorCoordinate()
{
    mPendingEvent.version = sizeof(sensors_event_t);
    mPendingEvent.sensor = ID_A;
    mPendingEvent.type = SENSOR_TYPE_ACCELEROMETER;
     memset(mPendingEvent.data, 0, sizeof(mPendingEvent.data));


    // read the actual value of all sensors if they're enabled already
    int flags = 0;

    open_device();

    if (!ioctl(dev_fd, MXC622X_ACC_IOCTL_GET_ENABLE, &flags)) {
        mEnabled = 1;
        if (flags) {
            setInitialState();
        }
    }
    if (!mEnabled) {
        close_device();
    }
}

AccSensor::~AccSensor() 
{
    if (mEnabled) {
        setEnable(0, 0);
    }

	close_device();
}

int AccSensor::setInitialState()
{
    struct input_absinfo absinfo;

	if (!ioctl(data_fd, EVIOCGABS(EVENT_TYPE_ACCEL_X), &absinfo)) {
		mPendingEvent.acceleration.x = MEMSIC_UNIT_CONVERSION(absinfo.value);
	}
	if (!ioctl(data_fd, EVIOCGABS(EVENT_TYPE_ACCEL_Y), &absinfo)) {
		mPendingEvent.acceleration.y = MEMSIC_UNIT_CONVERSION(absinfo.value);
	}
	if (!ioctl(data_fd, EVIOCGABS(EVENT_TYPE_ACCEL_Z), &absinfo)) {
		mPendingEvent.acceleration.z = MEMSIC_UNIT_CONVERSION(absinfo.value);
	}
    return 0;
}
bool AccSensor::hasPendingEvents() const
{
	return mHasPendingEvent;
}

int AccSensor::setEnable(int32_t handle, int en)
{
    int newState = en ? 1 : 0;
    int err = 0;
    if (newState != mEnabled) {
        if (!mEnabled) {
            open_device();
        }
        int flags = newState;
    	err = ioctl(dev_fd, MXC622X_ACC_IOCTL_SET_ENABLE, &en);
        err = err<0 ? -errno : 0;
        //LOGE_IF(err, "MXC622X_IOCTL_ENABLE failed (%s)", strerror(-err));
 
        if (!err) {
            mEnabled = newState;
            if (en) {
                setInitialState();
            }
        }
        if (!mEnabled) {
            close_device();
        }
    }
    return err;
}

int AccSensor::setDelay(int32_t handle, int64_t ns)
{
    if (ns < 0)
        return -EINVAL;

    mDelay = ns;

    if (mEnabled) {
        uint64_t wanted = -1LLU;
		uint64_t ns = mDelay;
		wanted = wanted < ns ? wanted : ns;
        int delay = int64_t(wanted) / 1000000;
        if (ioctl(dev_fd, MXC622X_ACC_IOCTL_SET_DELAY, &delay)) {
            return -errno;
        }
    }

    return 0;
}

int64_t AccSensor::getDelay(int32_t handle)
{
	return (handle == ID_A) ? mDelay : 0;
}

int AccSensor::getEnable(int32_t handle)
{
	return (handle == ID_A) ? mEnabled : 0;
}

int AccSensor::readEvents(sensors_event_t * data, int count)
{
    if (count < 1)
        return -EINVAL;

    if (mHasPendingEvent) {
        mHasPendingEvent = false;
        mPendingEvent.timestamp = getTimestamp();
        *data = mPendingEvent;
        return mEnabled ? 1 : 0;
    }

    ssize_t n = mInputReader.fill(data_fd);
    if (n < 0)
        return n;

    int numEventReceived = 0;
    input_event const* event;

    while (count && mInputReader.readEvent(&event)) {
        int type = event->type;
        if (type == EV_ABS) {
            switch (event->code) {
                case EVENT_TYPE_ACCEL_X:
                    mPendingEvent.acceleration.x = MEMSIC_UNIT_CONVERSION(event->value);
                    break;
                case EVENT_TYPE_ACCEL_Y:
                    mPendingEvent.acceleration.y = MEMSIC_UNIT_CONVERSION(event->value);
                    break;
                case EVENT_TYPE_ACCEL_Z:
                    mPendingEvent.acceleration.z = MEMSIC_UNIT_CONVERSION(event->value);
                    break;
            }
        } else if (type == EV_SYN) {
            mPendingEvent.timestamp = timevalToNano(event->time);
            if (mEnabled) {
                *data++ = mPendingEvent;
                count--;
                numEventReceived++;
            }
        } else {
            //LOGE("MemsicSensor: unknown event (type=%d, code=%d)",type, event->code);
        }
        mInputReader.next();
    }

    return numEventReceived;
}
int AccSensor::populateSensorList(struct sensor_t *list)
{
	memcpy(list, sSensorList, sizeof(struct sensor_t) * numSensors);
	return numSensors;
}
