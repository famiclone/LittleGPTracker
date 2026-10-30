#ifndef _SDL_AUDIO_CAPTURE_H_
#define _SDL_AUDIO_CAPTURE_H_

#ifdef SDL2
#include <SDL2/SDL.h>
#include <vector>

// Mono S16 capture via SDL2 OpenAudioDevice(iscapture=1).
// On TrimUI Brick / TG5040 the default capture device is the built-in mic,
// but the codec capture path is often left at "MIC OFF" by the CFW.
class SDLAudioCapture {
  public:
    SDLAudioCapture();
    ~SDLAudioCapture();

    bool Start(int sampleRate, int maxSeconds);
    void Stop();
    bool IsRecording() const { return recording_; }
    int GetSampleRate() const { return sampleRate_; }
    int GetFrameCount() const;
    // Copies captured mono frames into dst (up to maxFrames). Returns count.
    int CopyFrames(short *dst, int maxFrames) const;
    // Peak of last callback block, 0..32767 — for UI meter.
    int GetPeak() const { return peak_; }
    // Absolute peak since Start(), 0..32767.
    int GetAbsPeak() const { return absPeak_; }

  private:
    static void SDLCALL callback(void *userdata, Uint8 *stream, int len);
    void onAudio(Uint8 *stream, int len);
    void enableMicPath();
    void suspendPlayback();
    void resumePlayback();

    SDL_AudioDeviceID device_;
    bool recording_;
    bool playbackSuspended_;
    int sampleRate_;
    int channels_;
    SDL_AudioFormat format_;
    int maxFrames_;
    int peak_;
    int absPeak_;
    std::vector<short> buffer_;
};

#else

// SDL1 has no capture API — stub so non-SDL2 platforms still link.
class SDLAudioCapture {
  public:
    SDLAudioCapture() {}
    ~SDLAudioCapture() {}
    bool Start(int, int) { return false; }
    void Stop() {}
    bool IsRecording() const { return false; }
    int GetSampleRate() const { return 0; }
    int GetFrameCount() const { return 0; }
    int CopyFrames(short *, int) const { return 0; }
    int GetPeak() const { return 0; }
    int GetAbsPeak() const { return 0; }
};

#endif // SDL2

#endif
