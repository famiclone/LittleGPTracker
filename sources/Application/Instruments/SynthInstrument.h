#ifndef _SYNTH_INSTRUMENT_H_
#define _SYNTH_INSTRUMENT_H_

#include "I_Instrument.h"
#include "Application/Model/Song.h"
#include "Foundation/Variables/WatchedVariable.h"
#include "SRPUpdaters.h"
#include <vector>

#define SYP_WAVEFORM MAKE_FOURCC('W', 'A', 'V', 'E')
#define SYP_VOLUME MAKE_FOURCC('V', 'O', 'L', 'M')
#define SYP_PAN MAKE_FOURCC('P', 'A', 'N', ' ')
#define SYP_ROOTNOTE MAKE_FOURCC('R', 'O', 'O', 'T')
#define SYP_FINETUNE MAKE_FOURCC('F', 'T', 'U', 'N')
#define SYP_ATTACK MAKE_FOURCC('A', 'T', 'C', 'K')
#define SYP_DECAY MAKE_FOURCC('D', 'C', 'A', 'Y')
#define SYP_SUSTAIN MAKE_FOURCC('S', 'U', 'S', 'T')
#define SYP_RELEASE MAKE_FOURCC('R', 'E', 'L', 'S')
#define SYP_LFORATE MAKE_FOURCC('L', 'F', 'O', 'R')
#define SYP_LFODEPTH MAKE_FOURCC('L', 'F', 'O', 'D')
#define SYP_TABLE MAKE_FOURCC('T', 'A', 'B', 'L')
#define SYP_TABLEAUTO MAKE_FOURCC('T', 'B', 'L', 'A')

enum SynthWaveform {
    SYW_SAW = 0,
    SYW_TRIANGLE,
    SYW_SINE,
    SYW_SQUARE,
    SYW_NOISE,
    SYW_LAST
};

class SynthInstrument : public I_Instrument {
  public:
    SynthInstrument();
    virtual ~SynthInstrument();

    virtual bool Init();
    virtual bool Start(int channel, unsigned char note, int flags = 1);
    virtual void Stop(int channel);
    virtual bool Render(int channel, fixed *buffer, int size, int flags);
    virtual void ProcessCommand(int channel, FourCC cc, ushort value);
    virtual bool IsInitialized();
    virtual bool IsEmpty();
    virtual InstrumentType GetType() { return IT_SYNTH; }
    virtual const char *GetName();
    virtual void OnStart();
    virtual void Purge();
    virtual int GetTable();
    virtual bool GetTableAutomation();
    virtual void GetTableState(TableSaveState &state);
    virtual void SetTableState(TableSaveState &state);

    // Phrase-strip preview: signed -128..127 per bin (osc*ADSR and LFO).
    void FillPhrasePreview(signed char *waveBins, signed char *lfoBins,
                           int nBins) const;

  private:
    enum EnvStage { ENV_OFF = 0, ENV_ATTACK, ENV_DECAY, ENV_SUSTAIN, ENV_RELEASE };

    struct Voice {
        bool active_;
        EnvStage stage_;
        float env_;
        float phase_;
        float baseInc_; // cycles/sample at note pitch (no FX)
        float curInc_;  // after pitch FX
        float lfoPhase_;
        float pitchMod_; // LFO pitch factor, refreshed at k-rate
        int note_;
        int lastNote_;
        unsigned int noise_;

        float baseVol_; // 0..255
        float vol_;
        float basePan_; // 0..254
        float pan_;

        int krateCount_;
        bool retrig_;
        int retrigLoop_;
        int retrigCount_;

        VolumeRamp volumeRamp_;
        Panner panner_;
        LinSpeedRamp speedRamp_;
        LogSpeedRamp legato_;
        LogSpeedRamp pfin_;
        Arp arp_;
        std::vector<I_SRPUpdater *> updaters_;
        std::vector<I_SRPUpdater *> activeUpdaters_;
    };

    float envTimeToInc(int param, float sampleRate) const;
    float sampleWave(int wave, float phase, unsigned int &noise) const;
    float previewEnvAt(float t) const;
    void enableUpdater(Voice &v, I_SRPUpdater &u);
    void applyUpdaters(Voice &v);
    void restartEnvelope(Voice &v);

    Voice voice_[SONG_CHANNEL_COUNT];
    char name_[20];
    TableSaveState tableState_;

    Variable *waveform_;
    Variable *volume_;
    Variable *pan_;
    Variable *rootNote_;
    Variable *fineTune_;
    Variable *attack_;
    Variable *decay_;
    Variable *sustain_;
    Variable *release_;
    Variable *lfoRate_;
    Variable *lfoDepth_;
    Variable *table_;
    Variable *tableAuto_;
};

#endif
