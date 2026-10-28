
#include "RTAudioDriver.h"
#include "Adapters/W32/Midi/W32MidiService.h"
#include "Application/Player/SyncMaster.h"
#include "Services/Audio/Audio.h"
#include "Services/Time/TimeService.h"
#include "System/Console/Trace.h"
#include "System/System/System.h"
#include <string.h>
#include <vector>

int callback(void *outputBuffer, void *inputBuffer, unsigned int nBufferFrames,
             double streamTime, RtAudioStreamStatus status, void *userData) {
    RTAudioDriver *sound = (RTAudioDriver *)userData;
    sound->fillBuffer((short *)outputBuffer, nBufferFrames);
    return 0;
}

RTAudioDriverThread::RTAudioDriverThread(RTAudioDriver *driver) {
    semaphore_ = SysSemaphore::Create(0, 10000);
    driver_ = driver;
    percentage_ = 0;
};

bool RTAudioDriverThread::Execute() {

    int bufferSize = Audio::GetInstance()->GetAudioBufferSize();
    float cycleTime = bufferSize / 44100.0f;
    TimeService *ts = TimeService::GetInstance();
    while (!shouldTerminate()) {
        semaphore_->Wait();
        float before = float(ts->GetTime());
        driver_->OnNewBufferNeeded();
        float delta = float(ts->GetTime() - before); // in secs
        percentage_ = int(delta / cycleTime * 100);
    };
    delete semaphore_;
    return true;
};

int RTAudioDriverThread::GetPlayedBufferPercentage() { return percentage_; }

void RTAudioDriverThread::Notify() {
    if (semaphore_) {
        semaphore_->Post();
    }
};

RTAudioDriver::RTAudioDriver(RtAudio::Api api, AudioSettings &settings)
    : AudioDriver(settings), unalignedMain_(0), miniBlank_(0), audio_(api) {
    isPlaying_ = false;
    thread_ = 0;
};

bool RTAudioDriver::deviceSupportsSampleRate(const RtAudio::DeviceInfo &info,
                                              unsigned int sampleRate) {
    // Only require a probed stereo output. CoreAudio sample-rate lists are
    // often incomplete (and may omit 44100 when the nominal rate is 48000),
    // so actual support is decided by openStream.
    (void)sampleRate;
    return info.probed && info.outputChannels >= 2;
}

bool RTAudioDriver::tryOpenDevice(unsigned int deviceID,
                                  unsigned int sampleRate,
                                  unsigned int &bufferFrames) {
    RtAudio::StreamParameters params;
    params.deviceId = deviceID;
    params.nChannels = 2;
    params.firstChannel = 0;

    try {
        if (audio_.isStreamOpen()) {
            audio_.closeStream();
        }
        audio_.openStream(&params, NULL, RTAUDIO_SINT16, sampleRate,
                          &bufferFrames, &callback, (void *)this);
        return true;
    } catch (RtError &e) {
        Trace::Error("Error opening audio output stream on device %d: %s",
                     deviceID, e.getMessage().c_str());
        try {
            if (audio_.isStreamOpen()) {
                audio_.closeStream();
            }
        } catch (RtError &) {
        }
        return false;
    }
}

bool RTAudioDriver::InitDriver() {

    // Guard against uninitialized / nonsense rates from AudioSettings —
    // a bad rate here makes samples play too fast or heavily distorted.
    unsigned int sampleRate = 44100;
    if (settings_.sampleRate_ >= 8000 && settings_.sampleRate_ <= 192000) {
        sampleRate = (unsigned int)settings_.sampleRate_;
    }
    unsigned int bufferFrames =
        settings_.bufferSize_ > 0 ? settings_.bufferSize_ : 256;

    std::string configuredName = settings_.audioDevice_;
    std::vector<unsigned int> candidates;
    std::vector<std::string> candidateNames;

    // 1) System default (macOS Sound menu) — always try first when it matches
    //    the selected name or when no explicit device was configured.
    // 2) Explicitly named device from config / stub selection.
    // 3) Other probed stereo outputs as fallback.
    unsigned int systemDefaultId = audio_.getDefaultOutputDevice();
    RtAudio::DeviceInfo systemDefaultInfo =
        audio_.getDeviceInfo(systemDefaultId);
    if (systemDefaultInfo.outputChannels > 0) {
        bool useSystemDefault =
            configuredName.empty() ||
            systemDefaultInfo.name == configuredName ||
            (!configuredName.empty() &&
             systemDefaultInfo.name.find(configuredName) != std::string::npos);
        if (useSystemDefault || configuredName.empty()) {
            candidates.push_back(systemDefaultId);
            candidateNames.push_back(systemDefaultInfo.name);
        }
    }

    for (int pass = 0; pass < 2; pass++) {
        for (unsigned int i = 0; i < audio_.getDeviceCount(); i++) {
            RtAudio::DeviceInfo info = audio_.getDeviceInfo(i);
            if (pass == 0) {
                // Named match even if probe flags are incomplete.
                if (configuredName.empty() || info.name != configuredName) {
                    continue;
                }
                if (info.outputChannels < 1) {
                    continue;
                }
            } else if (!deviceSupportsSampleRate(info, sampleRate)) {
                continue;
            }
            bool alreadyListed = false;
            for (unsigned int c = 0; c < candidates.size(); c++) {
                if (candidates[c] == i) {
                    alreadyListed = true;
                    break;
                }
            }
            if (!alreadyListed) {
                candidates.push_back(i);
                candidateNames.push_back(info.name);
            }
        }
    }

    // Last resort: any output device CoreAudio still advertises.
    if (candidates.empty()) {
        Trace::Log("AUDIO", "No probed stereo outputs; trying raw device list");
        for (unsigned int i = 0; i < audio_.getDeviceCount(); i++) {
            RtAudio::DeviceInfo info = audio_.getDeviceInfo(i);
            if (info.outputChannels > 0) {
                candidates.push_back(i);
                candidateNames.push_back(info.name);
            }
        }
    }

    int openedID = -1;
    std::string deviceName;
    for (unsigned int c = 0; c < candidates.size(); c++) {
        unsigned int tryFrames = bufferFrames;
        Trace::Log("AUDIO", "Trying output device %d: %s", candidates[c],
                   candidateNames[c].c_str());
        if (tryOpenDevice(candidates[c], sampleRate, tryFrames)) {
            openedID = (int)candidates[c];
            deviceName = candidateNames[c];
            bufferFrames = tryFrames;
            break;
        }
    }

    if (openedID < 0) {
        Trace::Error("No usable audio output device found");
        return false;
    }

    fragSize_ = bufferFrames * 4;
    Trace::Log("AUDIO", "RTAudio device %d (%s) successfully open - buffer=%d",
               openedID, deviceName.c_str(), bufferFrames);

    settings_.audioDevice_ = deviceName;
    settings_.sampleRate_ = (int)sampleRate;
    Trace::Log("AUDIO", "Stream sample rate=%u Hz", sampleRate);

    // Allocates a rotating sound buffer
    unalignedMain_ = (char *)SYS_MALLOC(fragSize_ + SOUND_BUFFER_MAX);
    // Make sure the buffer is aligned
#ifdef _64BIT
    mainBuffer_ = (char *)unalignedMain_;
#else
    mainBuffer_ = (char *)((((int)unalignedMain_) + 1) & (0xFFFFFFFC));
#endif

    // Create mini blank buffer in case of underruns

    miniBlank_ = (char *)malloc(fragSize_);
    SYS_MEMSET(miniBlank_, 0, fragSize_);

    return true;
};

void RTAudioDriver::CloseDriver() {

    try {
        if (audio_.isStreamOpen()) {
            audio_.closeStream();
        }
    } catch (RtError &e) {
        Trace::Error("Error closing audio output stream: %s",
                     e.getMessage().c_str());
    }

    if (miniBlank_) {
        SYS_FREE(miniBlank_);
        miniBlank_ = 0;
    }

    if (unalignedMain_) {
        SYS_FREE(unalignedMain_);
        unalignedMain_ = 0;
    };
};

bool RTAudioDriver::StartDriver() {
    if (!audio_.isStreamOpen()) {
        Trace::Error("Cannot start audio: stream is not open");
        return false;
    }

    thread_ = new RTAudioDriverThread(this);
    thread_->Start();

    SYS_MEMSET(miniBlank_, 0, fragSize_);
    bufferPos_ = 0;
    bufferSize_ = 0;

    for (int i = 0; i < settings_.preBufferCount_; i++) {
        AddBuffer((short *)miniBlank_, fragSize_ / 4);
        MidiService::GetInstance()->AdvancePlayQueue();
    }

    if (settings_.preBufferCount_ == 0) {
        thread_->Notify();
    }
    try {
        audio_.startStream();
    } catch (RtError &e) {
        Trace::Error("Error starting output stream: %s",
                     e.getMessage().c_str());
        return false;
    };
    return true;
}

void RTAudioDriver::StopDriver() {
    try {
        if (audio_.isStreamOpen()) {
            audio_.abortStream();
        }
    } catch (RtError &e) {
        Trace::Error("Error stopping output stream: %s",
                     e.getMessage().c_str());
    }
};

double RTAudioDriver::GetStreamTime() {
    try {
        if (audio_.isStreamOpen()) {
            return audio_.getStreamTime();
        }
    } catch (RtError &e) {
        Trace::Error("Error reading stream time: %s", e.getMessage().c_str());
    }
    return 0.0;
}

void RTAudioDriver::fillBuffer(short *stream, int frameCount) {
    int len = frameCount * 4;

    // Look if we have enough data in main buffer

    while (bufferSize_ - bufferPos_ < len) {
        // First move remaining bytes at the front
        // then get next queued buffer and copy data from it

        if (pool_[poolPlayPosition_].buffer_ == 0) {
            // underrun, let's fill the buffer with blank and bail out
            SYS_MEMSET(stream, 0, len);
            return;
        } else {
            memcpy(mainBuffer_, mainBuffer_ + bufferPos_,
                   bufferSize_ - bufferPos_);
            memcpy(mainBuffer_ + bufferSize_ - bufferPos_,
                   pool_[poolPlayPosition_].buffer_,
                   pool_[poolPlayPosition_].size_);

            MidiService::GetInstance()->Flush();

            // Adapt buffer variables

            bufferSize_ =
                bufferSize_ - bufferPos_ + pool_[poolPlayPosition_].size_;
            bufferPos_ = 0;

            SYS_FREE(pool_[poolPlayPosition_].buffer_);

            pool_[poolPlayPosition_].buffer_ = 0;
            poolPlayPosition_ = (poolPlayPosition_ + 1) % SOUND_BUFFER_COUNT;
            thread_->Notify();
        }
    }
    SYS_MEMCPY(stream, (short *)(mainBuffer_ + bufferPos_), len);
    onAudioBufferTick();
    bufferPos_ += len;
}

int RTAudioDriver::GetPlayedBufferPercentage() {
    return thread_->GetPlayedBufferPercentage();
}
