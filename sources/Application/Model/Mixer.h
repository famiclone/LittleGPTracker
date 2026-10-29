
#ifndef _MIXER_H_
#define _MIXER_H_

#include "Foundation/T_Singleton.h"
#include "Application/Persistency/Persistent.h"

#include "Song.h"
#include "Application/Utils/fixed.h"

class Mixer:public T_Singleton<Mixer>,Persistent {
public:
	Mixer() ;
	~Mixer() ;
	void Clear() ;

	inline int GetBus(int i) { return channelBus_[i]  ; } ;
    inline void SetBus(int i, int value) { channelBus_[i] = value; };

    inline int GetChannelVolume(int i) { return channelVolume_[i]; };
    inline void SetChannelVolume(int i, int value) { channelVolume_[i] = (unsigned char)value; };
    inline int GetChannelHPF(int i) { return channelHPF_[i]; };
    inline void SetChannelHPF(int i, int value) { channelHPF_[i] = (unsigned char)value; };
    inline unsigned short GetChannelLPF(int i) { return channelLPF_[i]; };
    inline void SetChannelLPF(int i, unsigned short value) { channelLPF_[i] = value; };
    // Echo/delay: steps=0 off; amt is wet mix and feedback (0-255)
    inline unsigned char GetChannelDelaySteps(int i) { return channelDelaySteps_[i]; };
    inline void SetChannelDelaySteps(int i, unsigned char value) {
        channelDelaySteps_[i] = value;
    };
    inline unsigned char GetChannelDelayAmt(int i) { return channelDelayAmt_[i]; };
    inline void SetChannelDelayAmt(int i, unsigned char value) {
        channelDelayAmt_[i] = value;
    };
    // Reverb: size=0 off (1-16 room); amt is wet mix (0-255)
    inline unsigned char GetChannelReverbSize(int i) { return channelReverbSize_[i]; };
    inline void SetChannelReverbSize(int i, unsigned char value) {
        channelReverbSize_[i] = value;
    };
    inline unsigned char GetChannelReverbAmt(int i) { return channelReverbAmt_[i]; };
    inline void SetChannelReverbAmt(int i, unsigned char value) {
        channelReverbAmt_[i] = value;
    };
    inline unsigned char GetChannelTapeAmt(int i) { return channelTapeAmt_[i]; };
    inline void SetChannelTapeAmt(int i, unsigned char value) {
        channelTapeAmt_[i] = value;
    };

	virtual void SaveContent(TiXmlNode *node) ;
	virtual void RestoreContent(TiXmlElement *element);
private:
	char channelBus_[SONG_CHANNEL_COUNT] ;
    unsigned char channelVolume_[SONG_CHANNEL_COUNT];
    unsigned char channelHPF_[SONG_CHANNEL_COUNT];
    unsigned short channelLPF_[SONG_CHANNEL_COUNT];
    unsigned char channelDelaySteps_[SONG_CHANNEL_COUNT];
    unsigned char channelDelayAmt_[SONG_CHANNEL_COUNT];
    unsigned char channelReverbSize_[SONG_CHANNEL_COUNT];
    unsigned char channelReverbAmt_[SONG_CHANNEL_COUNT];
    unsigned char channelTapeAmt_[SONG_CHANNEL_COUNT];
} ;	

#endif
