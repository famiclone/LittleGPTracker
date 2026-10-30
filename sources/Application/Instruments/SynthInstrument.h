#ifndef _SYNTH_INSTRUMENT_H_
#define _SYNTH_INSTRUMENT_H_

#include "I_Instrument.h"
#include "Application/Model/Song.h"
#include "Foundation/Variables/WatchedVariable.h"
#include "SRPUpdaters.h"
#include <vector>

#define SYP_MODE MAKE_FOURCC('S', 'M', 'O', 'D')
#define SYP_WAVEFORM MAKE_FOURCC('W', 'A', 'V', 'E')
#define SYP_MACROSHAPE MAKE_FOURCC('M', 'S', 'H', 'P')
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

// SYNTH mode parameters
#define SYP_TRANSPOSE MAKE_FOURCC('T', 'R', 'S', 'P')
#define SYP_TBLTIC MAKE_FOURCC('T', 'B', 'T', 'C')
#define SYP_SIZE MAKE_FOURCC('S', 'I', 'Z', 'E')
#define SYP_MULT MAKE_FOURCC('M', 'U', 'L', 'T')
#define SYP_WARP MAKE_FOURCC('W', 'A', 'R', 'P')
#define SYP_SCAN MAKE_FOURCC('S', 'C', 'A', 'N')

// SHAPES mode parameters
#define SYP_TIMBRE MAKE_FOURCC('T', 'M', 'B', 'R')
#define SYP_COLOR MAKE_FOURCC('C', 'O', 'L', 'R')
#define SYP_DEGRADE MAKE_FOURCC('D', 'E', 'G', 'R')
#define SYP_REDUX MAKE_FOURCC('R', 'E', 'D', 'X')

// Shared filter / amp / FX parameters
#define SYP_FILTMODE MAKE_FOURCC('F', 'I', 'M', 'O')
#define SYP_FILTCUT MAKE_FOURCC('F', 'C', 'U', 'T')
#define SYP_FILTRES MAKE_FOURCC('F', 'R', 'E', 'S')
#define SYP_AMP MAKE_FOURCC('A', 'M', 'P', '_')
#define SYP_LIM MAKE_FOURCC('L', 'I', 'M', '_')
#define SYP_CHORUS MAKE_FOURCC('C', 'H', 'O', 'R')
#define SYP_DELSEND MAKE_FOURCC('D', 'L', 'S', 'D')
#define SYP_REVSEND MAKE_FOURCC('R', 'V', 'S', 'D')
#define SYP_TAPESEND MAKE_FOURCC('T', 'P', 'S', 'D')

enum SynthEngineMode {
    SYM_SYNTH = 0,
    SYM_SHAPES = 1,
    SYM_LAST = 2
};

enum SynthWaveform {
    SYW_PULSE12 = 0,
    SYW_PULSE25,
    SYW_SQUARE,
    SYW_PULSE75,
    SYW_SAW,
    SYW_TRIANGLE,
    SYW_SINE,
    SYW_NOISE,
    SYW_SUPERSAW,
    SYW_OVERFLOW,
    SYW_LAST
};

enum MacroShape {
    SMS_CSAW = 0,
    SMS_MORPH,
    SMS_SAW_SQ,
    SMS_SINE_TRI,
    SMS_BUZZ,
    SMS_SUB_SAW,
    SMS_SUB_SQ,
    SMS_SWARM,
    SMS_FM,
    SMS_FB_FM,
    SMS_VOWEL,
    SMS_BELL,
    SMS_PLUCK,
    SMS_KICK,
    SMS_SNARE,
    SMS_CYMBAL,
    SMS_CHORD,
    SMS_SHIMMER,
    SMS_ENSEMBLE,
    SMS_GRAINPAD,
    SMS_ORBIX,
    SMS_LAST
};

enum SynthFilterMode {
    SYF_OFF = 0,
    SYF_LOWPASS,
    SYF_HIGHPASS,
    SYF_BANDPASS,
    SYF_LPHP,
    SYF_LAST
};

enum SynthLimiterMode {
    SYL_CLIP = 0,
    SYL_SIN,
    SYL_FOLD,
    SYL_WRAP,
    SYL_POST,
    SYL_LAST
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

    virtual unsigned char GetDelaySend();
    virtual unsigned char GetReverbSend();
    virtual unsigned char GetTapeSend();

    int GetEngineMode() const;
    void SetEngineMode(int mode);

    static const char *GetShapeLabel(int shape);
    static const char *GetMacroShapeLabel(int shape);
    static const char *GetFilterLabel(int mode);
    static const char *GetLimiterLabel(int mode);

    // Phrase-strip preview: signed -128..127 per bin (osc*ADSR and LFO).
    void FillPhrasePreview(signed char *waveBins, signed char *lfoBins,
                           int nBins) const;

  private:
    enum EnvStage { ENV_OFF = 0, ENV_ATTACK, ENV_DECAY, ENV_SUSTAIN, ENV_RELEASE };

    struct Voice {
        bool active_;
        EnvStage stage_;
        float env_;
        float noteAge_; // elapsed seconds since note trigger (for KICK/SNARE/PLUCK/BELL)
        float phase_;
        float phase2_;
        float multPhase_;
        float baseInc_; // cycles/sample at note pitch (no FX)
        float curInc_;  // after pitch FX
        float lfoPhase_;
        float pitchMod_; // LFO pitch factor, refreshed at k-rate
        int note_;
        int lastNote_;
        unsigned int noise_;
        float holdSampleL_;
        float holdSampleR_;
        int degradeCounter_;
        float fmPrev_;

        float baseVol_; // 0..255
        float vol_;
        float basePan_; // 0..254
        float pan_;
        float baseCut_; // 0..1
        float cut_;
        float baseRes_; // 0..1
        float res_;
        float svfLpL_;
        float svfBpL_;
        float svfLpR_;
        float svfBpR_;

        int krateCount_;
        bool retrig_;
        int retrigLoop_;
        int retrigCount_;

        VolumeRamp volumeRamp_;
        Panner panner_;
        LinSpeedRamp speedRamp_;
        LogSpeedRamp legato_;
        LogSpeedRamp pfin_;
        LinSpeedRamp cutRamp_;
        LinSpeedRamp resRamp_;
        Arp arp_;
        std::vector<I_SRPUpdater *> updaters_;
        std::vector<I_SRPUpdater *> activeUpdaters_;
    };

    float envTimeToInc(int param, float sampleRate) const;
    float shapePhase(float phase, int sizeParam, int warpParam,
                     int scanParam) const;
    float applyLimiter(float s, int ampParam, int limMode) const;
    float sampleWave(int wave, float phase, unsigned int &noise) const;
    float sampleMacroShape(int shape, float phase, float subPhase,
                           float noteAge, int timbreParam, int colorParam,
                           unsigned int &noise, float &fmPrev,
                           float &pitchMultiplierOut) const;
    float previewEnvAt(float t) const;
    void enableUpdater(Voice &v, I_SRPUpdater &u);
    void applyUpdaters(Voice &v);
    void restartEnvelope(Voice &v);

    Voice voice_[SONG_CHANNEL_COUNT];
    char name_[24];
    TableSaveState tableState_;

    Variable *mode_;
    Variable *waveform_;
    Variable *macroShape_;
    Variable *transpose_;
    Variable *tblTic_;
    Variable *size_;
    Variable *mult_;
    Variable *warp_;
    Variable *scan_;
    Variable *timbre_;
    Variable *color_;
    Variable *degrade_;
    Variable *redux_;
    Variable *filterMode_;
    Variable *filterCut_;
    Variable *filterRes_;
    Variable *amp_;
    Variable *lim_;
    Variable *volume_;
    Variable *pan_;
    Variable *chorus_;
    Variable *delSend_;
    Variable *revSend_;
    Variable *tapeSend_;
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
