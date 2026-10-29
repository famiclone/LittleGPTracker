
#ifndef _PLAYER_CHANNEL_H_
#define _PLAYER_CHANNEL_H_

#include "Services/Audio/AudioModule.h"
#include "Application/Instruments/I_Instrument.h"
#include "Application/Mixer/MixBus.h"
#include "Application/Utils/fixed.h"

// Max delay ring length in frames (~1s at 44.1k). Tempo-synced step count
// is clamped so steps * samplesPerStep fits in this buffer.
enum { CHANNEL_DELAY_MAX_FRAMES = 44100 };

enum {
    CHANNEL_REVERB_COMBS = 4,
    CHANNEL_REVERB_APS = 2,
    CHANNEL_REVERB_MAX_SIZE = 16,
    // Max stereo frames across all comb+allpass lines at size=16
    // (bases*2 scale). Contiguous allocation for one channel.
    CHANNEL_REVERB_BUF_FRAMES = 24000
};

class PlayerChannel: public AudioModule {
public:
	PlayerChannel(int index) ;
	virtual ~PlayerChannel() ;
	virtual bool Render(fixed *buffer,int samplecount) ;
	void StartInstrument(I_Instrument *instr,unsigned char note,bool cleanStart) ;
	void StopInstrument() ;
	I_Instrument *GetInstrument() ;
	void SetMute(bool muted) ;
	bool IsMuted() ;
	void SetMixBus(int i) ;
    void SetVolume(fixed volume);
    void SetHPFMode(unsigned char mode);
    void ApplyHPF(fixed *buffer, int samplecount);
    void SetLPFFreq(unsigned short freq);
    void SetDelay(unsigned char steps, unsigned char amt);
    void SetReverb(unsigned char size, unsigned char amt);
    void Reset();

  private:
	void ensureDelayBuffer() ;
	void syncDelayLength() ;
	bool applyDelay(fixed *buffer, int samplecount) ;
	void ensureReverbBuffer() ;
	void syncReverb() ;
	bool applyReverb(fixed *buffer, int samplecount) ;

	int index_ ;
	I_Instrument *instr_ ;
	bool muted_ ;
    fixed volume_;
    int busIndex_ ;
	MixBus *mixBus_ ;
    fixed hpfPrevInput_[2];
    fixed hpfPrevOutput_[2];
	fixed hpfAlpha_;
	unsigned char hpfMode_;
    fixed lpfPrevOutput_[2];
    fixed lpfAlpha_;
    unsigned short lpfFreq_;
    unsigned char delaySteps_;
    unsigned char delayAmt_;
    int delayLen_;
    int delayWritePos_;
    fixed *delayBuf_;
    unsigned char reverbSize_;
    unsigned char reverbAmt_;
    fixed reverbFb_;
    fixed *reverbBuf_;
    int combOff_[CHANNEL_REVERB_COMBS];
    int combLen_[CHANNEL_REVERB_COMBS];
    int combPos_[CHANNEL_REVERB_COMBS];
    int apOff_[CHANNEL_REVERB_APS];
    int apLen_[CHANNEL_REVERB_APS];
    int apPos_[CHANNEL_REVERB_APS];
};

#endif
