#ifndef _INSTRUMENT_VIEW_H_
#define _INSTRUMENT_VIEW_H_

#include "Application/FX/FxPrinter.h"
#include "BaseClasses/FieldView.h"
#include "BaseClasses/UIIntVarField.h"
#include "Foundation/Observable.h"
#include "Foundation/Variables/Variable.h"
#include "ViewData.h"

#define IVP_TYPE MAKE_FOURCC('I', 'T', 'Y', 'P')

class InstrumentView : public FieldView, public I_Observer {
  public:
    InstrumentView(GUIWindow &w, ViewData *data);
    virtual ~InstrumentView();

    virtual void ProcessButtonMask(unsigned short mask, bool pressed);
    virtual void DrawView();
    virtual void DrawOverlay();
    virtual void OnPlayerUpdate(PlayerEventType, unsigned int){};
    virtual void OnFocus();

  protected:
    void warpToNext(int offset);
    void onInstrumentChange();
    void fillSampleParameters();
    void fillMidiParameters();
    void fillSynthParameters();
    void addTypeField();
    void applyTypeChangeIfNeeded();
    void applySlicesUiIfNeeded();
    void refreshWaveformFieldPosition();
    void drawTypeTabs();
    void drawWaveformPicker();
    void ensureWaveformCache(short *pcm, int domain, int nCh, int sampleIdx,
                             int nSlices);
    void clearWaveformStrip(int px, int py, int pw, int ph);
    void drawSampleOverlay(int px, int py, int pw, int ph);
    void drawSynthOverlay(int px, int py, int pw, int ph);
    int synthPreviewKey() const;
    InstrumentType getInstrumentType();
    void Update(Observable &o, I_ObservableData *d);
    GUIPoint paramAnchor() const;

  private:
    enum { kWaveBins = 48, kWaveStripRows = 5 };

    Project *project_;
    FourCC lastFocusID_;
    I_Instrument *current_;
    Variable *typeVar_;
    int paramBaseY_;
    UIIntVarField *waveField_;

    unsigned char wavePeak_[kWaveBins];
    int waveCacheSample_;
    int waveCacheDomain_;
    int waveCacheSlices_;
    bool overlayValid_;
    int overlayDrawnSample_;
    int overlayDrawnDomain_;
    int overlayDrawnSlices_;
    int overlayDrawnSynthKey_;
    signed char synthWaveBin_[kWaveBins];
    signed char synthLfoBin_[kWaveBins];
};
#endif
