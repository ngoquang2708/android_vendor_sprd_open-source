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

audio_io_handle_t AudioPolicyManagerSPRD::getOutput(AudioSystem::stream_type stream,
                                    uint32_t samplingRate,
                                    uint32_t format,
                                    uint32_t channelMask,
                                    AudioSystem::output_flags flags)
{
    audio_io_handle_t output = 0;
    uint32_t latency = 0;
    routing_strategy strategy = getStrategy((AudioSystem::stream_type)stream);/////////////////
    audio_devices_t device = getDeviceForStrategy(strategy, false /*fromCache*/);
    ALOGD("getOutput() stream %d, samplingRate %d, format %d, channelMask %x, flags %x",
          stream, samplingRate, format, channelMask, flags);

	ALOGD("getOutput() is_voip_set %d,stream %d,",is_voip_set,stream);
#if 0
	if((!is_voip_set)&&(stream == AudioSystem::VOICE_CALL)) { 
		for (size_t i = 0; i < mOutputs.size(); i++) {
			AudioOutputDescriptor *outputDesc = mOutputs.valueAt(i);
			ALOGD("getOutput() outputDesc->mRefCount[AudioSystem::VOICE_CALL] %d",outputDesc->mRefCount[AudioSystem::VOICE_CALL]);
			if(outputDesc->mRefCount[AudioSystem::VOICE_CALL] == 0) {			
				AudioParameter param;
				param.add(String8("sprd_voip_start"), String8("true"));
				mpClientInterface->setParameters(0, param.toString());
				is_voip_set = true;
			}
		}
	}
#endif
#ifdef AUDIO_POLICY_TEST
    if (mCurOutput != 0) {
        ALOGD("getOutput() test output mCurOutput %d, samplingRate %d, format %d, channelMask %x, mDirectOutput %d",
                mCurOutput, mTestSamplingRate, mTestFormat, mTestChannels, mDirectOutput);

        if (mTestOutputs[mCurOutput] == 0) {
            ALOGD("getOutput() opening test output");
            AudioOutputDescriptor *outputDesc = new AudioOutputDescriptor(NULL);
            outputDesc->mDevice = mTestDevice;
            outputDesc->mSamplingRate = mTestSamplingRate;
            outputDesc->mFormat = mTestFormat;
            outputDesc->mChannelMask = mTestChannels;
            outputDesc->mLatency = mTestLatencyMs;
            outputDesc->mFlags = (audio_output_flags_t)(mDirectOutput ? AudioSystem::OUTPUT_FLAG_DIRECT : 0);
            outputDesc->mRefCount[stream] = 0;
            mTestOutputs[mCurOutput] = mpClientInterface->openOutput(0, &outputDesc->mDevice,
                                            &outputDesc->mSamplingRate,
                                            &outputDesc->mFormat,
                                            &outputDesc->mChannelMask,
                                            &outputDesc->mLatency,
                                            outputDesc->mFlags);
            if (mTestOutputs[mCurOutput]) {
                AudioParameter outputCmd = AudioParameter();
                outputCmd.addInt(String8("set_id"),mCurOutput);
                mpClientInterface->setParameters(mTestOutputs[mCurOutput],outputCmd.toString());
                addOutput(mTestOutputs[mCurOutput], outputDesc);
            }
        }
        return mTestOutputs[mCurOutput];
    }
#endif //AUDIO_POLICY_TEST

    // open a direct output if required by specified parameters
    IOProfile *profile = getProfileForDirectOutput(device,
                                                   samplingRate,
                                                   format,
                                                   channelMask,
                                                   (audio_output_flags_t)flags);
    if (profile != NULL) {

        ALOGD("getOutput() opening direct output device %x", device);

        AudioOutputDescriptor *outputDesc = new AudioOutputDescriptor(profile);
        outputDesc->mDevice = device;
        outputDesc->mSamplingRate = samplingRate;
        outputDesc->mFormat = (audio_format_t)format;
        outputDesc->mChannelMask = (audio_channel_mask_t)channelMask;
        outputDesc->mLatency = 0;
        outputDesc->mFlags = (audio_output_flags_t)(flags | AUDIO_OUTPUT_FLAG_DIRECT);
        outputDesc->mRefCount[stream] = 0;
        outputDesc->mStopTime[stream] = 0;
		
        output = mpClientInterface->openOutput(profile->mModule->mHandle,
                                        &outputDesc->mDevice,
                                        &outputDesc->mSamplingRate,
                                        &outputDesc->mFormat,
                                        &outputDesc->mChannelMask,
                                        &outputDesc->mLatency,
                                        outputDesc->mFlags);

        // only accept an output with the requested parameters
        if (output == 0 ||
            (samplingRate != 0 && samplingRate != outputDesc->mSamplingRate) ||
            (format != 0 && format != outputDesc->mFormat) ||
            (channelMask != 0 && channelMask != outputDesc->mChannelMask)) {
            ALOGD("getOutput() failed opening direct output: output %d samplingRate %d %d,"
                    "format %d %d, channelMask %04x %04x", output, samplingRate,
                    outputDesc->mSamplingRate, format, outputDesc->mFormat, channelMask,
                    outputDesc->mChannelMask);
            if (output != 0) {
                mpClientInterface->closeOutput(output);
            }
            delete outputDesc;
            return 0;
        }
        addOutput(output, outputDesc);
        ALOGD("getOutput() returns direct output %d", output);
        return output;
    }

    // ignoring channel mask due to downmix capability in mixer

    // open a non direct output

    // get which output is suitable for the specified stream. The actual routing change will happen
    // when startOutput() will be called
    SortedVector<audio_io_handle_t> outputs = getOutputsForDevice(device);
	
    output = selectOutput(outputs, flags);

    ALOGW_IF((output ==0), "getOutput() could not find output for stream %d, samplingRate %d,"
            "format %d, channels %x, flags %x", stream, samplingRate, format, channelMask, flags);

    ALOGD("getOutput() returns output %d", output);

    return output;
}

status_t AudioPolicyManagerSPRD::startOutput(audio_io_handle_t output,
                                             AudioSystem::stream_type stream,
                                             int session)
{
    ALOGD("startOutput() output %d, stream %d, session %d", output, stream, session);
    ssize_t index = mOutputs.indexOfKey(output);
    if (index < 0) {
        ALOGW("startOutput() unknow output %d", output);//output thread number
        return BAD_VALUE;
    }


    AudioOutputDescriptor *outputDesc = mOutputs.valueAt(index);//dsp level
    
    // increment usage count for this stream on the requested output:
    // NOTE that the usage count is the same for duplicated output and hardware output which is
    // necessary for a correct control of hardware output routing by startOutput() and stopOutput()
    outputDesc->changeRefCount(stream, 1);

	ALOGD("startOutput() is_voip_set %d,stream %d,",is_voip_set,stream);
	if((!is_voip_set)&&(stream == AudioSystem::VOICE_CALL)) { 
		for (size_t i = 0; i < mOutputs.size(); i++) {
		    AudioOutputDescriptor *outputDesc = mOutputs.valueAt(i);
			ALOGD("startOutput() outputDesc->mRefCount[AudioSystem::VOICE_CALL] %d",outputDesc->mRefCount[AudioSystem::VOICE_CALL]);
			if(outputDesc->mRefCount[AudioSystem::VOICE_CALL] == 1) {
				AudioParameter param;
    			param.add(String8("sprd_voip_start"), String8("true"));
				mpClientInterface->setParameters(0, param.toString());
				is_voip_set = true;
			}
		}
	}

    if (outputDesc->mRefCount[stream] == 1) {
        audio_devices_t prevDevice = outputDesc->device();
        audio_devices_t newDevice = getNewDevice(output, false /*fromCache*/);
		
        routing_strategy strategy = getStrategy(stream);
        bool shouldWait = (strategy == STRATEGY_SONIFICATION) ||
                            (strategy == STRATEGY_SONIFICATION_RESPECTFUL);
        uint32_t waitMs = 0;
        bool force = false;
		
        for (size_t i = 0; i < mOutputs.size(); i++) {
            AudioOutputDescriptor *desc = mOutputs.valueAt(i);
            if (desc != outputDesc) {
                // force a device change if any other output is managed by the same hw
                // module and has a current device selection that differs from selected device.
                // In this case, the audio HAL must receive the new device selection so that it can
                // change the device currently selected by the other active output.
                if (outputDesc->sharesHwModuleWith(desc) &&
                    desc->device() != newDevice) {
                    force = true;
                }
                // wait for audio on other active outputs to be presented when starting
                // a notification so that audio focus effect can propagate.
                if (shouldWait && (desc->refCount() != 0) && (waitMs < desc->latency())) {
                    waitMs = desc->latency();
                }
            }
        }
		
        uint32_t muteWaitMs = setOutputDevice(output, newDevice, force);

        // handle special case for sonification while in call
        if (isInCall()) {
            handleIncallSonification(stream, true, false);
        }

        // apply volume rules for current stream and device if necessary
        checkAndSetVolume(stream,
                          mStreams[stream].getVolumeIndex(stream,(audio_devices_t)newDevice),
                          output,
                          newDevice);

        // update the outputs if starting an output with a stream that can affect notification
        // routing
        handleNotificationRoutingForStream(stream);
        if (waitMs > muteWaitMs) {
            usleep((waitMs - muteWaitMs) * 2 * 1000);
        }
    }
    return NO_ERROR;
}

status_t AudioPolicyManagerSPRD::stopOutput(audio_io_handle_t output,
                                            AudioSystem::stream_type stream,
                                            int session)
{
    ALOGD("stopOutput() output %d, stream %d, session %d", output, stream, session);
    ssize_t index = mOutputs.indexOfKey(output);
    if (index < 0) {
        ALOGW("stopOutput() unknow output %d", output);
        return BAD_VALUE;
    }

    AudioOutputDescriptor *outputDesc = mOutputs.valueAt(index);

    // handle special case for sonification while in call
    if (isInCall()) {
        handleIncallSonification(stream, false, false);
    }

	ALOGD("stopOutput() is_voip_set %d,stream %d,output size %d",is_voip_set,stream,mOutputs.size());
	if(is_voip_set &&(stream == AudioSystem::VOICE_CALL)) { 
		for (size_t i = 0; i < mOutputs.size(); i++) {
		    AudioOutputDescriptor *outputDesc = mOutputs.valueAt(i);
			ALOGD("stopOutput() outputDesc->mRefCount[AudioSystem::VOICE_CALL] %d",outputDesc->mRefCount[AudioSystem::VOICE_CALL]);
			if(outputDesc->mRefCount[AudioSystem::VOICE_CALL] == 1) {
				AudioParameter param;
    			param.add(String8("sprd_voip_start"), String8("false"));
				mpClientInterface->setParameters(0, param.toString());
				is_voip_set = false;
			}
		}
	}

    if (outputDesc->mRefCount[stream] > 0) {
        // decrement usage count of this stream on the output
        outputDesc->changeRefCount(stream, -1);
        // store time at which the stream was stopped - see isStreamActive()
        if (outputDesc->mRefCount[stream] == 0) {
            outputDesc->mStopTime[stream] = systemTime();
            audio_devices_t newDevice = getNewDevice(output, false /*fromCache*/);
            // delay the device switch by twice the latency because stopOutput() is executed when
            // the track stop() command is received and at that time the audio track buffer can
            // still contain data that needs to be drained. The latency only covers the audio HAL
            // and kernel buffers. Also the latency does not always include additional delay in the
            // audio path (audio DSP, CODEC ...)
            setOutputDevice(output, newDevice, false, outputDesc->mLatency*2);

            // force restoring the device selection on other active outputs if it differs from the
            // one being selected for this output
            for (size_t i = 0; i < mOutputs.size(); i++) {
                audio_io_handle_t curOutput = mOutputs.keyAt(i);
                AudioOutputDescriptor *desc = mOutputs.valueAt(i);
                if (curOutput != output &&
                        desc->refCount() != 0 &&
                        outputDesc->sharesHwModuleWith(desc) &&
                        newDevice != desc->device()) {
                    setOutputDevice(curOutput,
                                    getNewDevice(curOutput, false /*fromCache*/),
                                    true,
                                    outputDesc->mLatency*2);
                }
            }
            // update the outputs if stopping one with a stream that can affect notification routing
            handleNotificationRoutingForStream(stream);
        }
        return NO_ERROR;
    } else {
        ALOGW("stopOutput() refcount is already 0 for output %d", output);
        return INVALID_OPERATION;
    }
}

void AudioPolicyManagerSPRD::releaseOutput(audio_io_handle_t output)
{
    ALOGD("releaseOutput() %d", output);
    ssize_t index = mOutputs.indexOfKey(output);
    if (index < 0) {
        ALOGW("releaseOutput() releasing unknown output %d", output);
        return;
    }

	if(is_voip_set) { 
	    AudioOutputDescriptor *outputDesc = mOutputs.valueAt(index);
		if(outputDesc->mRefCount[AudioSystem::VOICE_CALL] == 0) {
			AudioParameter param;
			param.add(String8("sprd_voip_start"), String8("false"));
			mpClientInterface->setParameters(0, param.toString());
			is_voip_set = false;
		}
	}

#ifdef AUDIO_POLICY_TEST
    int testIndex = testOutputIndex(output);
    if (testIndex != 0) {
        AudioOutputDescriptor *outputDesc = mOutputs.valueAt(index);
        if (outputDesc->refCount() == 0) {
            mpClientInterface->closeOutput(output);
            delete mOutputs.valueAt(index);
            mOutputs.removeItem(output);
            mTestOutputs[testIndex] = 0;
        }
        return;
    }
#endif //AUDIO_POLICY_TEST

    if (mOutputs.valueAt(index)->mFlags & AudioSystem::OUTPUT_FLAG_DIRECT) {
        mpClientInterface->closeOutput(output);
        delete mOutputs.valueAt(index);
        mOutputs.removeItem(output);
    }

}

void AudioPolicyManagerSPRD::handleNotificationRoutingForStream(AudioSystem::stream_type stream) {
    switch(stream) {
    case AudioSystem::MUSIC:
        checkOutputForStrategy(STRATEGY_SONIFICATION_RESPECTFUL);
        updateDeviceForStrategy();
        break;
    default:
        break;
    }
}


}; // namespace android_audio_legacy
