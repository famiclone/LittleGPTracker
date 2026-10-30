#ifndef _INSTRUMENT_VIEW_H_
#define _INSTRUMENT_VIEW_H_

#include "Application/FX/FxPrinter.h"
#include "BaseClasses/FieldView.h"
#include "BaseClasses/UIActionField.h"
#include "BaseClasses/UIIntVarField.h"
#include "Foundation/Observable.h"
#include "Foundation/Variables/Variable.h"
#include "ViewData.h"

#define IVP_TYPE MAKE_FOURCC('I', 'T', 'Y', 'P')
#define IVP_START_MARKER MAKE_FOURCC('I', 'S', 'T', 'M')
#define IVP_END_MARKER MAKE_FOURCC('I', 'E', 'N', 'M')
#define IV_ACTION_NAME MAKE_FOURCC('I', 'N', 'A', 'M')
#define IV_ACTION_PLAY MAKE_FOURCC('I', 'P', 'L', 'Y')
#define IV_ACTION_LOAD MAKE_FOURCC('I', 'L', 'O', 'D')
#define IV_ACTION_SAVE MAKE_FOURCC('I', 'S', 'A', 'V')

class InstrumentView : public FieldView, public I_Observer {
  public:
    InstrumentView(GUIWindow &w, ViewData *data);
    virtual ~InstrumentView();

    virtual void ProcessButtonMask(unsigned short mask, bool pressed);
    virtual void DrawView();
    virtual void DrawOverlay();
    virtual void OnPlayerUpdate(PlayerEventType, unsigned int){};
    virtual void OnFocus();

    void OnSetInstrumentName(const char *name);
    void OnSaveInstrumentFile(const char *name);
    void OnLoadInstrumentFile(const char *name);

    static bool EnsureInstrumentsDir();
    static void ListInstrumentFiles(std::vector<std::string> &out);

  protected:
    void warpToNext(int offset);
    void onInstrumentChange();
    void fillHeaderParameters();
    void fillSampleParameters();
    void fillMidiParameters();
    void fillSynthParameters();
    void applyTypeChangeIfNeeded();
    void applySlicesUiIfNeeded();
    void syncSampleRangeFromMarkers();
    void refreshNameFieldText();
    void refreshPlayFieldText();
    void toggleInstrumentAudition(bool forceRetrigger = false);
    void stopInstrumentAudition();
    bool isActionField(const UIField *field) const;
    void drawModeLabels();
    void drawParamBarsOverlay();
    void ensureWaveformCache(short *pcm, int nFrames, int nCh, int sampleIdx);
    void clearWaveformStrip(int px, int py, int pw, int ph);
    void drawSampleOverlay(int px, int py, int pw, int ph);
    void drawSynthOverlay(int px, int py, int pw, int ph);
    int synthPreviewKey() const;
    int paramBarsKey() const;
    InstrumentType getInstrumentType();
    void Update(Observable &o, I_ObservableData *d);
    GUIPoint paramAnchor() const;

  private:
    enum { kWaveBins = 64, kWaveStripRows = 5, kMaxBarItems = 24 };

    struct BarItem {
        int charX_;
        int charY_;
        int charW_;
        FourCC varId_;
        int maxVal_;
        bool centered_;
    };

    void addBarItem(int charX, int charY, int charW, FourCC varId,
                    int maxVal = 255, bool centered = false);

    Project *project_;
    FourCC lastFocusID_;
    I_Instrument *current_;
    Variable *typeVar_;
    Variable *startMarkerVar_;
    Variable *endMarkerVar_;
    int paramBaseY_;
    UIActionField *nameField_;
    UIActionField *playField_;
    UIActionField *loadField_;
    UIActionField *saveField_;
    UIIntVarField *startMarkerField_;
    UIIntVarField *endMarkerField_;
    char nameLabelBuf_[32];
    char playLabelBuf_[8];
    bool isAuditioning_;
    int auditionChannel_;
    bool inProcessButton_;
    bool pendingInstrumentChange_;

    BarItem barItems_[kMaxBarItems];
    int barItemCount_;

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
    int overlayDrawnBarsKey_;
    signed char synthWaveBin_[kWaveBins];
    signed char synthLfoBin_[kWaveBins];
};
#endif
