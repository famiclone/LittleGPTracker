#include "Mixer.h"
#include "Application/Utils/HexBuffers.h"

static const MixerFxInfo kMixerFxRegistry[MIXER_FX_COUNT] = {
    {MIXER_FX_HPF, "HPF", "High Pass Filter"},
    {MIXER_FX_LPF, "LPF", "Low Pass Filter"},
    {MIXER_FX_DLY, "DLY", "Tempo Delay"},
    {MIXER_FX_REV, "REV", "Room Reverb"},
    {MIXER_FX_TPE, "TPE", "Tape / Lo-Fi"},
    {MIXER_FX_SCH, "SCH", "Sidechain Duck"}
};

Mixer::Mixer():Persistent("MIXER")  {
	Clear() ;
} ;

Mixer::~Mixer() {
} ;

int Mixer::GetAvailableFxCount() {
    return MIXER_FX_COUNT;
}

const MixerFxInfo &Mixer::GetFxInfo(int fxId) {
    if (fxId < 0 || fxId >= MIXER_FX_COUNT) {
        return kMixerFxRegistry[0];
    }
    return kMixerFxRegistry[fxId];
}

int Mixer::IndexOfAddedFx(unsigned char fxId) const {
    for (int i = 0; i < addedFxCount_; i++) {
        if (addedFx_[i] == fxId) {
            return i;
        }
    }
    return -1;
}

bool Mixer::IsFxAdded(unsigned char fxId) const {
    return IndexOfAddedFx(fxId) >= 0;
}

bool Mixer::AddFx(unsigned char fxId) {
    if (fxId >= MIXER_FX_COUNT || IsFxAdded(fxId) ||
        addedFxCount_ >= MIXER_FX_COUNT) {
        return false;
    }
    addedFx_[addedFxCount_++] = fxId;
    return true;
}

void Mixer::ResetFxOnChannel(unsigned char fxId, int col) {
    if (col < 0 || col >= MIXER_CHANNEL_COUNT) {
        return;
    }
    switch (fxId) {
    case MIXER_FX_HPF:
        channelHPF_[col] = 0;
        break;
    case MIXER_FX_LPF:
        channelLPF_[col] = 0;
        break;
    case MIXER_FX_DLY:
        channelDelaySteps_[col] = 0;
        break;
    case MIXER_FX_REV:
        channelReverbSize_[col] = 0;
        break;
    case MIXER_FX_TPE:
        channelTapeAmt_[col] = 0;
        break;
    case MIXER_FX_SCH:
        channelSidechainSrc_[col] = 0xFF;
        break;
    default:
        break;
    }
}

bool Mixer::RemoveFx(unsigned char fxId) {
    int idx = IndexOfAddedFx(fxId);
    if (idx < 0) {
        return false;
    }
    for (int i = idx; i < addedFxCount_ - 1; i++) {
        addedFx_[i] = addedFx_[i + 1];
    }
    addedFx_[addedFxCount_ - 1] = MIXER_FX_NONE;
    addedFxCount_--;
    for (int c = 0; c < MIXER_CHANNEL_COUNT; c++) {
        ResetFxOnChannel(fxId, c);
    }
    return true;
}

void Mixer::Clear() {

	for (int i=0;i<SONG_CHANNEL_COUNT;i++) {
		channelBus_[i]=i ;
    }
    for (int i = 0; i < MIXER_CHANNEL_COUNT; i++) {
        channelVolume_[i] = 0xFF;
        channelHPF_[i] = 0; // 0=OFF, 1=20Hz, 2=90Hz
        channelLPF_[i] = 0; // 0=OFF, else frequency in Hz (20-20000)
        channelDelaySteps_[i] = 0; // 0=OFF, else delay in phrase steps
        channelDelayAmt_[i] = 0x80; // wet + feedback
        channelReverbSize_[i] = 0;  // 0=OFF, else room size 1-16
        channelReverbAmt_[i] = 0x60; // wet mix
        channelTapeAmt_[i] = 0;      // 0=OFF, else tape effect 0-255
        channelSidechainSrc_[i] = 0xFF; // 0xFF=OFF, else trigger channel 0..7
        channelSidechainAmt_[i] = 0xB0; // ducking depth 0-255
    }
    addedFxCount_ = 0;
    for (int i = 0; i < MIXER_FX_COUNT; i++) {
        addedFx_[i] = MIXER_FX_NONE;
    }
} ;

void Mixer::SaveContent(TiXmlNode *node) {
    saveHexBuffer(node, "BUS", (unsigned char *)channelBus_,
                  SONG_CHANNEL_COUNT);
    saveHexBuffer(node, "VOL", channelVolume_, MIXER_CHANNEL_COUNT);
    saveHexBuffer(node, "HPF", channelHPF_, MIXER_CHANNEL_COUNT);
    saveHexBuffer(node, "LPF", channelLPF_, MIXER_CHANNEL_COUNT);
    saveHexBuffer(node, "DLYS", channelDelaySteps_, MIXER_CHANNEL_COUNT);
    saveHexBuffer(node, "DLYA", channelDelayAmt_, MIXER_CHANNEL_COUNT);
    saveHexBuffer(node, "REVS", channelReverbSize_, MIXER_CHANNEL_COUNT);
    saveHexBuffer(node, "REVA", channelReverbAmt_, MIXER_CHANNEL_COUNT);
    saveHexBuffer(node, "TAPE", channelTapeAmt_, MIXER_CHANNEL_COUNT);
    saveHexBuffer(node, "SCSRC", channelSidechainSrc_, MIXER_CHANNEL_COUNT);
    saveHexBuffer(node, "SCAMT", channelSidechainAmt_, MIXER_CHANNEL_COUNT);
    unsigned char fxCnt = (unsigned char)addedFxCount_;
    saveHexBuffer(node, "FXCNT", &fxCnt, 1);
    saveHexBuffer(node, "FXLST", addedFx_, MIXER_FX_COUNT);
} ;

void Mixer::RestoreContent(TiXmlElement *element) {
    bool hasFxList = false;
    unsigned char rawFxCnt = 0;
    unsigned char rawFxList[MIXER_FX_COUNT];
    for (int i = 0; i < MIXER_FX_COUNT; i++) {
        rawFxList[i] = MIXER_FX_NONE;
    }

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
        } else if (!strcmp("TAPE", value)) {
            restoreHexBuffer(current, channelTapeAmt_);
        } else if (!strcmp("SCSRC", value)) {
            restoreHexBuffer(current, channelSidechainSrc_);
        } else if (!strcmp("SCAMT", value)) {
            restoreHexBuffer(current, channelSidechainAmt_);
        } else if (!strcmp("FXCNT", value)) {
            restoreHexBuffer(current, &rawFxCnt);
            hasFxList = true;
        } else if (!strcmp("FXLST", value)) {
            restoreHexBuffer(current, rawFxList);
            hasFxList = true;
        }
        current = current->NextSiblingElement();
    }

    addedFxCount_ = 0;
    for (int i = 0; i < MIXER_FX_COUNT; i++) {
        addedFx_[i] = MIXER_FX_NONE;
    }

    if (hasFxList) {
        int count = (int)rawFxCnt;
        if (count > MIXER_FX_COUNT) {
            count = MIXER_FX_COUNT;
        }
        for (int i = 0; i < count; i++) {
            if (rawFxList[i] < MIXER_FX_COUNT) {
                AddFx(rawFxList[i]);
            }
        }
    } else {
        // Auto-populate active effects from older projects without FXLST
        for (int c = 0; c < MIXER_CHANNEL_COUNT; c++) {
            if (channelHPF_[c] != 0)
                AddFx(MIXER_FX_HPF);
            if (channelLPF_[c] != 0)
                AddFx(MIXER_FX_LPF);
            if (channelDelaySteps_[c] != 0)
                AddFx(MIXER_FX_DLY);
            if (channelReverbSize_[c] != 0)
                AddFx(MIXER_FX_REV);
            if (channelTapeAmt_[c] != 0)
                AddFx(MIXER_FX_TPE);
            if (channelSidechainSrc_[c] < SONG_CHANNEL_COUNT)
                AddFx(MIXER_FX_SCH);
        }
    }
}
