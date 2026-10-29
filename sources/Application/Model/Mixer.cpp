
#include "Mixer.h"
#include "Application/Utils/HexBuffers.h"

Mixer::Mixer():Persistent("MIXER")  {
	Clear() ;
} ;

Mixer::~Mixer() {
} ;

void Mixer::Clear() {

	for (int i=0;i<SONG_CHANNEL_COUNT;i++) {
		channelBus_[i]=i ;
        channelVolume_[i] = 0xFF;
        channelHPF_[i] = 0; // 0=OFF, 1=20Hz, 2=90Hz
        channelLPF_[i] = 0; // 0=OFF, else frequency in Hz (20-20000)
        channelDelaySteps_[i] = 0; // 0=OFF, else delay in phrase steps
        channelDelayAmt_[i] = 0x80; // wet + feedback
        channelReverbSize_[i] = 0;  // 0=OFF, else room size 1-16
        channelReverbAmt_[i] = 0x60; // wet mix
    }
} ;

void Mixer::SaveContent(TiXmlNode *node) {
    saveHexBuffer(node, "BUS", (unsigned char *)channelBus_,
                  SONG_CHANNEL_COUNT);
    saveHexBuffer(node, "VOL", channelVolume_, SONG_CHANNEL_COUNT);
    saveHexBuffer(node, "HPF", channelHPF_, SONG_CHANNEL_COUNT);
    saveHexBuffer(node, "LPF", channelLPF_, SONG_CHANNEL_COUNT);
    saveHexBuffer(node, "DLYS", channelDelaySteps_, SONG_CHANNEL_COUNT);
    saveHexBuffer(node, "DLYA", channelDelayAmt_, SONG_CHANNEL_COUNT);
    saveHexBuffer(node, "REVS", channelReverbSize_, SONG_CHANNEL_COUNT);
    saveHexBuffer(node, "REVA", channelReverbAmt_, SONG_CHANNEL_COUNT);
} ;

void Mixer::RestoreContent(TiXmlElement *element) {
    TiXmlElement *current = element->FirstChildElement();
    while (current) {
        const char *value = current->Value();
        if (!strcmp("BUS", value)) {
            restoreHexBuffer(current, (unsigned char *)channelBus_);
        } else if (!strcmp("VOL", value)) {
            restoreHexBuffer(current, channelVolume_);
        } else if (!strcmp("HPF", value)) {
            restoreHexBuffer(current, channelHPF_);
        } else if (!strcmp("LPF", value)) {
            restoreHexBuffer(current, (unsigned char *)channelLPF_);
        } else if (!strcmp("DLYS", value)) {
            restoreHexBuffer(current, channelDelaySteps_);
        } else if (!strcmp("DLYA", value)) {
            restoreHexBuffer(current, channelDelayAmt_);
        } else if (!strcmp("REVS", value)) {
            restoreHexBuffer(current, channelReverbSize_);
        } else if (!strcmp("REVA", value)) {
            restoreHexBuffer(current, channelReverbAmt_);
        }
        current = current->NextSiblingElement();
    }
}
