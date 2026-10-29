#ifndef _INSTRUMENT_VIEW_H_
#define _INSTRUMENT_VIEW_H_

#include "Application/FX/FxPrinter.h"
#include "BaseClasses/FieldView.h"
#include "BaseClasses/UIIntVarField.h"
#include "Foundation/Observable.h"
#include "Foundation/Variables/Variable.h"
#include "ViewData.h"

#define IVP_TYPE MAKE_FOURCC('I', 'T', 'Y', 'P')
#define IVP_START_MARKER MAKE_FOURCC('I', 'S', 'T', 'M')
#define IVP_END_MARKER MAKE_FOURCC('I', 'E', 'N', 'M')

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
    void syncSampleRangeFromMarkers();
    void refreshWaveformFieldPosition();
    void drawTypeTabs();
    void drawWaveformPicker();
    void ensureWaveformCache(short *pcm, int nFrames, int nCh, int sampleIdx);
    void clearWaveformStrip(int px, int py, int pw, int ph);
    void drawSampleOverlay(int px, int py, int pw, int ph);
    void drawSynthOverlay(int px, int py, int pw, int ph);
    int synthPreviewKey() const;
    InstrumentType getInstrumentType();
    void Update(Observable &o, I_ObservableData *d);
    GUIPoint paramAnchor() const;

  private:
    enum { kWaveBins = 64, kWaveStripRows = 5 };

    Project *project_;
    FourCC lastFocusID_;
    I_Instrument *current_;
    Variable *typeVar_;
    Variable *startMarkerVar_;
    Variable *endMarkerVar_;
    int paramBaseY_;
    UIIntVarField *waveField_;
    UIIntVarField *startMarkerField_;
    UIIntVarField *endMarkerField_;
    bool inProcessButton_;
    bool pendingInstrumentChange_;

    unsigned char wavePeak_[kWaveBins];
    int waveCacheSample_;
    int waveCacheFrames_;
    bool overlayValid_;
    int overlayDrawnSample_;
    int overlayDrawnStart_;
    int overlayDrawnEnd_;
    int overlayDrawnSlices_;
    int overlayDrawnFocusMarker_;
    int overlayDrawnSynthKey_;
    signed char synthWaveBin_[kWaveBins];
    signed char synthLfoBin_[kWaveBins];
};
#endif
