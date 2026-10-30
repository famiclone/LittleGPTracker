#ifndef _MIXER_H_
#define _MIXER_H_

#include "Foundation/T_Singleton.h"
#include "Application/Persistency/Persistent.h"

#include "Song.h"
#include "Application/Utils/fixed.h"

enum {
    MASTER_CHANNEL_INDEX = SONG_CHANNEL_COUNT,
    MIXER_CHANNEL_COUNT = SONG_CHANNEL_COUNT + 1
};

enum MixerFxId {
    MIXER_FX_HPF = 0,
    MIXER_FX_LPF = 1,
    MIXER_FX_DLY = 2,
    MIXER_FX_REV = 3,
    MIXER_FX_TPE = 4,
    MIXER_FX_SCH = 5,
    MIXER_FX_COUNT = 6,
    MIXER_FX_NONE = 0xFF
};

struct MixerFxInfo {
    unsigned char id;
    const char *shortName;
    const char *fullName;
};

class Mixer:public T_Singleton<Mixer>,Persistent {
public:
	Mixer() ;
	~Mixer() ;
	void Clear() ;

	inline int GetBus(int i) { return (i >= 0 && i < SONG_CHANNEL_COUNT) ? channelBus_[i] : 0; } ;
    inline void SetBus(int i, int value) {
        if (i >= 0 && i < SONG_CHANNEL_COUNT)
            channelBus_[i] = value;
    };

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
    inline unsigned char GetChannelSidechainSrc(int i) {
        return channelSidechainSrc_[i];
    };
    inline void SetChannelSidechainSrc(int i, unsigned char value) {
        channelSidechainSrc_[i] = value;
    };
    inline unsigned char GetChannelSidechainAmt(int i) {
        return channelSidechainAmt_[i];
    };
    inline void SetChannelSidechainAmt(int i, unsigned char value) {
        channelSidechainAmt_[i] = value;
    };

    // Dynamic Mixer FX registry and active list
    static int GetAvailableFxCount();
    static const MixerFxInfo &GetFxInfo(int fxId);
    int GetAddedFxCount() const { return addedFxCount_; }
    unsigned char GetAddedFxAt(int index) const {
        return (index >= 0 && index < addedFxCount_) ? addedFx_[index]
                                                     : (unsigned char)MIXER_FX_NONE;
    }
    int IndexOfAddedFx(unsigned char fxId) const;
    bool IsFxAdded(unsigned char fxId) const;
    bool AddFx(unsigned char fxId);
    bool RemoveFx(unsigned char fxId);
    void ResetFxOnChannel(unsigned char fxId, int col);

	virtual void SaveContent(TiXmlNode *node) ;
	virtual void RestoreContent(TiXmlElement *element);
private:
	char channelBus_[SONG_CHANNEL_COUNT] ;
    unsigned char channelVolume_[MIXER_CHANNEL_COUNT];
    unsigned char channelHPF_[MIXER_CHANNEL_COUNT];
    unsigned short channelLPF_[MIXER_CHANNEL_COUNT];
    unsigned char channelDelaySteps_[MIXER_CHANNEL_COUNT];
    unsigned char channelDelayAmt_[MIXER_CHANNEL_COUNT];
    unsigned char channelReverbSize_[MIXER_CHANNEL_COUNT];
    unsigned char channelReverbAmt_[MIXER_CHANNEL_COUNT];
    unsigned char channelTapeAmt_[MIXER_CHANNEL_COUNT];
    unsigned char channelSidechainSrc_[MIXER_CHANNEL_COUNT];
    unsigned char channelSidechainAmt_[MIXER_CHANNEL_COUNT];
    int addedFxCount_;
    unsigned char addedFx_[MIXER_FX_COUNT];
} ;	

#endif
