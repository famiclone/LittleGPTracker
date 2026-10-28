#include "SDLAudioCapture.h"
#include "System/Console/Trace.h"
#include <stdlib.h>
#include <string.h>

#ifdef SDL2

SDLAudioCapture::SDLAudioCapture()
    : device_(0), recording_(false), playbackSuspended_(false),
      sampleRate_(44100), channels_(1), format_(AUDIO_S16SYS), maxFrames_(0),
      peak_(0), absPeak_(0) {}

SDLAudioCapture::~SDLAudioCapture() { Stop(); }

void SDLAudioCapture::enableMicPath() {
    // Brick (sunxi audiocodec): Mic1 Capture Switch is often OFF and Mic1
    // Boost / ADC Gain sit at 0 — capture opens but absPeak stays ~hundreds
    // → flat waveform. Best-effort; unknown controls just fail quietly.
    system("tinymix \"Capture MIC Path\" \"Main Mic\" >/dev/null 2>&1");
    system("tinymix \"Capture MIC Path\" \"MIC\" >/dev/null 2>&1");
    system("tinymix \"Capture MIC Path\" 1 >/dev/null 2>&1");

    // sun8i / sunxi analog path (TrimUI audiocodec)
    system("tinymix \"Mic1 Capture Switch\" 1 >/dev/null 2>&1");
    system("tinymix \"Mic2 Capture Switch\" 1 >/dev/null 2>&1");
    system("tinymix \"Mic1 Boost Volume\" 7 >/dev/null 2>&1");
    system("tinymix \"Mic2 Boost Volume\" 7 >/dev/null 2>&1");
    system("tinymix \"ADC Gain Capture Volume\" 7 >/dev/null 2>&1");
    system("tinymix \"ADC Capture Volume\" 255 >/dev/null 2>&1");
    system("tinymix \"AIF1 AD0 Capture Volume\" 160 160 >/dev/null 2>&1");

    system("amixer -q cset name='Mic1 Capture Switch' on >/dev/null 2>&1");
    system("amixer -q cset name='Mic1 Boost Volume' 7 >/dev/null 2>&1");
    system("amixer -q cset name='ADC Gain Capture Volume' 7 >/dev/null 2>&1");
    system("amixer -q sset 'Mic1 Capture Switch' on >/dev/null 2>&1");
    system("amixer -q sset 'Mic1 Boost Volume' 7 >/dev/null 2>&1");
    system("amixer -q sset 'ADC Gain Capture Volume' 7 >/dev/null 2>&1");
    system("amixer -q sset 'Capture MIC Path' 'Main Mic' >/dev/null 2>&1");
    system("amixer -q sset Capture 100% unmute cap >/dev/null 2>&1");
    system("amixer -q sset 'Digital Capture Volume' 100% >/dev/null 2>&1");
    system("amixer -q sset 'Capture Volume' 100% >/dev/null 2>&1");
}

void SDLAudioCapture::suspendPlayback() {
    // Free the codec duplex path so capture can actually see the ADC.
    SDL_PauseAudio(1);
    playbackSuspended_ = true;
}

void SDLAudioCapture::resumePlayback() {
    if (playbackSuspended_) {
        SDL_PauseAudio(0);
        playbackSuspended_ = false;
    }
}

void SDLCALL SDLAudioCapture::callback(void *userdata, Uint8 *stream, int len) {
    SDLAudioCapture *self = (SDLAudioCapture *)userdata;
    if (!self || !self->recording_) {
        return;
    }
    self->onAudio(stream, len);
}

void SDLAudioCapture::onAudio(Uint8 *stream, int len) {
    if ((int)buffer_.size() >= maxFrames_ || len <= 0) {
        return;
    }

    int room = maxFrames_ - (int)buffer_.size();
    int peak = 0;

    if (format_ == AUDIO_S16SYS || format_ == AUDIO_S16LSB ||
        format_ == AUDIO_S16MSB) {
        short *in = (short *)stream;
        int frames = len / ((int)sizeof(short) * channels_);
        if (frames > room) {
            frames = room;
        }
        for (int i = 0; i < frames; i++) {
            int sample;
            if (channels_ >= 2) {
                sample = ((int)in[i * channels_] + (int)in[i * channels_ + 1]) /
                         2;
            } else {
                sample = in[i * channels_];
            }
            if (sample > 32767) {
                sample = 32767;
            }
            if (sample < -32768) {
                sample = -32768;
            }
            short s = (short)sample;
            buffer_.push_back(s);
            int a = s < 0 ? -s : s;
            if (a > peak) {
                peak = a;
            }
        }
    } else if (format_ == AUDIO_F32SYS || format_ == AUDIO_F32LSB ||
               format_ == AUDIO_F32MSB) {
        float *in = (float *)stream;
        int frames = len / ((int)sizeof(float) * channels_);
        if (frames > room) {
            frames = room;
        }
        for (int i = 0; i < frames; i++) {
            float f;
            if (channels_ >= 2) {
                f = 0.5f * (in[i * channels_] + in[i * channels_ + 1]);
            } else {
                f = in[i * channels_];
            }
            if (f > 1.0f) {
                f = 1.0f;
            }
            if (f < -1.0f) {
                f = -1.0f;
            }
            short s = (short)(f * 32767.0f);
            buffer_.push_back(s);
            int a = s < 0 ? -s : s;
            if (a > peak) {
                peak = a;
            }
        }
    } else if (format_ == AUDIO_S32SYS || format_ == AUDIO_S32LSB ||
               format_ == AUDIO_S32MSB) {
        int *in = (int *)stream;
        int frames = len / ((int)sizeof(int) * channels_);
        if (frames > room) {
            frames = room;
        }
        for (int i = 0; i < frames; i++) {
            int sample;
            if (channels_ >= 2) {
                sample = (in[i * channels_] >> 1) + (in[i * channels_ + 1] >> 1);
            } else {
                sample = in[i * channels_];
            }
            short s = (short)(sample >> 16);
            buffer_.push_back(s);
            int a = s < 0 ? -s : s;
            if (a > peak) {
                peak = a;
            }
        }
    } else {
        // Unsupported — treat as silence so we don't corrupt the buffer.
        Trace::Error("CAPTURE", "Unsupported capture format %d", (int)format_);
        return;
    }

    peak_ = peak;
    if (peak > absPeak_) {
        absPeak_ = peak;
    }

    if ((int)buffer_.size() >= maxFrames_) {
        if (device_) {
            SDL_PauseAudioDevice(device_, 1);
        }
        recording_ = false;
    }
}

bool SDLAudioCapture::Start(int sampleRate, int maxSeconds) {
    Stop();

    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        Trace::Error("CAPTURE", "SDL_INIT_AUDIO failed: %s", SDL_GetError());
        return false;
    }

    enableMicPath();
    suspendPlayback();

    if (sampleRate < 8000 || sampleRate > 96000) {
        sampleRate = 44100;
    }
    if (maxSeconds < 1) {
        maxSeconds = 1;
    }
    if (maxSeconds > 60) {
        maxSeconds = 60;
    }

    sampleRate_ = sampleRate;
    maxFrames_ = sampleRate_ * maxSeconds;
    buffer_.clear();
    buffer_.reserve(maxFrames_);
    peak_ = 0;
    absPeak_ = 0;

    int nCap = SDL_GetNumAudioDevices(SDL_TRUE);
    Trace::Log("CAPTURE", "capture devices: %d (driver=%s)", nCap,
               SDL_GetCurrentAudioDriver() ? SDL_GetCurrentAudioDriver() : "?");
    for (int i = 0; i < nCap; i++) {
        const char *name = SDL_GetAudioDeviceName(i, SDL_TRUE);
        Trace::Log("CAPTURE", "  [%d] %s", i, name ? name : "(null)");
    }

    SDL_AudioSpec want;
    SDL_AudioSpec have;
    SDL_zero(want);
    want.freq = sampleRate_;
    want.format = AUDIO_S16SYS;
    want.channels = 1;
    want.samples = 1024;
    want.callback = callback;
    want.userdata = this;

    // Prefer whatever the hardware actually gives — Brick codecs are often
    // stereo @ 48k; we'll downmix/convert in the callback.
    const int allow = SDL_AUDIO_ALLOW_FREQUENCY_CHANGE |
                      SDL_AUDIO_ALLOW_CHANNELS_CHANGE |
                      SDL_AUDIO_ALLOW_FORMAT_CHANGE;

    device_ = SDL_OpenAudioDevice(NULL, 1, &want, &have, allow);
    if (device_ == 0) {
        // Retry stereo explicitly — some ALSA configs reject mono capture.
        want.channels = 2;
        device_ = SDL_OpenAudioDevice(NULL, 1, &want, &have, allow);
    }
    if (device_ == 0) {
        Trace::Error("CAPTURE", "OpenAudioDevice(capture) failed: %s",
                     SDL_GetError());
        resumePlayback();
        return false;
    }

    sampleRate_ = have.freq > 0 ? have.freq : sampleRate_;
    channels_ = have.channels > 0 ? have.channels : 1;
    format_ = have.format;
    maxFrames_ = sampleRate_ * maxSeconds;
    Trace::Log("CAPTURE", "Opened capture %u rate=%d ch=%d fmt=0x%x",
               (unsigned)device_, have.freq, have.channels, have.format);

    recording_ = true;
    SDL_PauseAudioDevice(device_, 0);
    return true;
}

void SDLAudioCapture::Stop() {
    recording_ = false;
    if (device_) {
        SDL_LockAudioDevice(device_);
        SDL_PauseAudioDevice(device_, 1);
        SDL_UnlockAudioDevice(device_);
        SDL_CloseAudioDevice(device_);
        device_ = 0;
    }
    peak_ = 0;
    resumePlayback();
}

int SDLAudioCapture::GetFrameCount() const { return (int)buffer_.size(); }

int SDLAudioCapture::CopyFrames(short *dst, int maxFrames) const {
    if (!dst || maxFrames <= 0) {
        return 0;
    }
    int n = (int)buffer_.size();
    if (n > maxFrames) {
        n = maxFrames;
    }
    if (n > 0) {
        memcpy(dst, &buffer_[0], n * sizeof(short));
    }
    return n;
}

#endif // SDL2
