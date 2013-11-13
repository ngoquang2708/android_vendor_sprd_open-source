/*
 * Copyright (C) 2009 The Android Open Source Project
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

#define LOG_TAG "AudioPolicyManagerSPRD"
//#define LOG_NDEBUG 0
#include <utils/Log.h>
#include "AudioPolicyManagerSPRD.h"
#include <media/mediarecorder.h>

#include <binder/IServiceManager.h>

#include <IVolumeManager.h>

namespace android_audio_legacy {

using namespace android;

// ----------------------------------------------------------------------------
// AudioPolicyManagerSPRD
// ----------------------------------------------------------------------------

// ---  class factory

extern "C" AudioPolicyInterface* createAudioPolicyManager(AudioPolicyClientInterface *clientInterface)
{
    return new AudioPolicyManagerSPRD(clientInterface);
}

extern "C" void destroyAudioPolicyManager(AudioPolicyInterface *interface)
{
    delete interface;
}

// Nothing currently different between the Base implementation.

AudioPolicyManagerSPRD::AudioPolicyManagerSPRD(AudioPolicyClientInterface *clientInterface)
    : AudioPolicyManagerBase(clientInterface),is_voip_set(false)
{
    //loadVolumeProfilesInternal();    
}

AudioPolicyManagerSPRD::~AudioPolicyManagerSPRD()
{
    freeVolumeProfiles();
}

static sp<IVolumeManager> gVolumeManager;
//sp<VolumeManagerClient> gVolumeManagerClient;

// establish binder interface to VolumeManager service
static const sp<IVolumeManager>& get_volume_manager()
{
    if (gVolumeManager.get() == 0) {
        sp<IServiceManager> sm = defaultServiceManager();
        sp<IBinder> binder;
        do {
            binder = sm->getService(String16("sprd.volume_manager"));
            if (binder != 0)
                break;
            ALOGW("VolumeManager not published, waiting...");
            usleep(500000); // 0.5 s
        } while(true);
//        if (gVolumeManagerClient == NULL) {
//            gVolumeManagerClient = new VolumeManagerClient();
//        } else {
//            ALOGE("VolumeManager error");
//        }
//        binder->linkToDeath(gAudioFlingerClient);
        gVolumeManager = interface_cast<IVolumeManager>(binder);
//        gVolumeManager->registerClient(gVolumeManagerClient);
    }
    ALOGE_IF(gVolumeManager==0, "no VolumeManager!?");

    return gVolumeManager;
}

status_t AudioPolicyManagerSPRD::loadVolumeProfiles()
{
    status_t result = loadVolumeProfilesInternal();
    if (result == NO_ERROR) {
        applyVolumeProfiles();
    }
    return result;
}

status_t AudioPolicyManagerSPRD::loadVolumeProfilesInternal()
{
    int rowCount;
    int columeCount;

    memset(mVolumeProfiles, 0, sizeof(mVolumeProfiles));

	sp<IVolumeManager> sVolMgr = get_volume_manager();
    if (sVolMgr.get() == 0) {
        return UNKNOWN_ERROR;
    }

    int status = sVolMgr->open(IVolumeManager::STREAM_DEVICE);
    if (status != NO_ERROR) {
        ALOGE("loadVolumeProfiles, can't load device profile");
        return PERMISSION_DENIED;
    }
    int streamCount = sVolMgr->streamCount(IVolumeManager::STREAM_DEVICE);
    int deviceCount = sVolMgr->deviceCount(IVolumeManager::STREAM_DEVICE);
    if (streamCount < AudioSystem::NUM_STREAM_TYPES || deviceCount != DEVICE_CATEGORY_CNT) {
        ALOGE("corrupt profile data: %d streams should be %d, %d devices",
                streamCount, AUDIO_STREAM_CNT, deviceCount);
        return NOT_ENOUGH_DATA;
    }

    // alloc mem, read all data
    for (int i = 0; i < AudioSystem::NUM_STREAM_TYPES; i++) {
        for (int j = 0; j < DEVICE_CATEGORY_CNT; j++) {
            mVolumeProfiles[i][j] = new VolumeCurvePoint[VOLCNT];
            if (mVolumeProfiles[i][j]) {
                for (int volIdx = 0; volIdx < VOLCNT; volIdx++) {
                    status_t result = sVolMgr->getStreamDevicePointIndex(IVolumeManager::STREAM_DEVICE,
                        i, j, volIdx, mVolumeProfiles[i][j][volIdx].mIndex);
                    if (result != NO_ERROR) {
                        ALOGE("wrong index @%d, %d, %d", i, j, volIdx);
                        goto read_failed;
                    }
                    result = sVolMgr->getStreamDevicePointGain(IVolumeManager::STREAM_DEVICE,
                        i, j, volIdx, mVolumeProfiles[i][j][volIdx].mDBAttenuation);
                    if (result != NO_ERROR) {
                        ALOGE("wrong db @%d, %d %d", i, j, volIdx);
                        goto read_failed;
                    }
                }
            }
        }
    }

    // using new profiles
    for (int i = 0; i < AudioSystem::NUM_STREAM_TYPES; i++) {
        for (int j = 0; j < DEVICE_CATEGORY_CNT; j++) {
            mStreams[i].mVolumeCurve[j] = mVolumeProfiles[i][j];
        }
    }
    sVolMgr->close(IVolumeManager::STREAM_DEVICE);
    return NO_ERROR;

read_failed:
    sVolMgr->close(IVolumeManager::STREAM_DEVICE);
    // cleanup
    freeVolumeProfiles();
    ALOGW("can't load device volume profiles, using default");
    return BAD_VALUE;
}

void AudioPolicyManagerSPRD::freeVolumeProfiles()
{
    for (int i = 0; i < AudioSystem::NUM_STREAM_TYPES; i++) {
        for (int j = 0; j < DEVICE_CATEGORY_CNT; j++) {
            if (mVolumeProfiles[i][j]) {
                delete [] mVolumeProfiles[i][j];
                mVolumeProfiles[i][j] = 0;
            }
        }
    }
}

void AudioPolicyManagerSPRD::applyVolumeProfiles()
{
    for (int stream = 0; stream < AudioSystem::NUM_STREAM_TYPES; stream++) {
        for (size_t i = 0; i < mOutputs.size(); i++) {
            for (int device = 0; device < mStreams[stream].mIndexCur.size(); device++) {
                status_t volStatus = checkAndSetVolume(
                    stream,
                    mStreams[stream].mIndexCur.valueAt(device),
                    mOutputs.keyAt(i),
                    mOutputs.valueAt(i)->device());
                if (volStatus != NO_ERROR) {
                    ALOGW("error setting vol for %d, %d, curIdx %d, output %d, device %d",
                        stream, device,
                        mStreams[stream].mIndexCur.valueAt(device),
                        i,
                        mOutputs.valueAt(i)->device());
                }
            }
        }
    }
}

}; // namespace android_audio_legacy
