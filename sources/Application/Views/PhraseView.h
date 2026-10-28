
#ifndef _PHRASE_VIEW_H_
#define _PHRASE_VIEW_H_

#include "BaseClasses/UIBigHexVarField.h"
#include "BaseClasses/View.h"
#include "ViewData.h"

#define FCC_EDIT MAKE_FOURCC('V', 'O', 'L', 'M')

class I_Instrument;

class PhraseView : public View {

  public:
    PhraseView(GUIWindow &w, ViewData *viewData);
    ~PhraseView();
    virtual void ProcessButtonMask(unsigned short mask, bool pressed);
    virtual void DrawView();
    virtual void OnPlayerUpdate(PlayerEventType, unsigned int tick = 0);
    virtual void OnFocus();
    virtual void DrawOverlay();
    void onCommandSelectorResult(ModalView &d);
    void onCommandSelectorPreview(ModalView &d);

  protected:
    void updateCursor(int dx, int dy);
    void stopAudition();
    void updateCursorValue(ViewUpdateDirection offset, int xOffset = 0,
                           int yOffset = 0);
    bool isCommandColumn() const;
    FourCC *getCurrentCommandPointer();
    void updateSelectionValue(ViewUpdateDirection direction);
    void warpToNeighbour(int offset);
    void warpInChain(int offset);
    void cutPosition();
    void pasteLast();

    void extendSelection();

    GUIRect getSelectionRect();
    void fillClipboardData();
    void interpolateSelection();
    void copySelection();
    void cutSelection();
    void pasteClipboard();

    void unMuteAll();
    void toggleMute();
    void switchSoloMode();

    void processNormalButtonMask(unsigned short mask);
    void processSelectionButtonMask(unsigned short mask);

    void setTextProps(GUITextProperties &props, int row, int col, bool restore);
    void drawSampleStrip();
    void ensureWaveformCache(short *pcm, int domain, int nCh, int sampleIdx,
                             int nSlices);
    int resolvePlayingSlice(int instrId, I_Instrument *instr, int nSlices);

  private:
    enum { kWaveBins = 48 };
    int row_;
    int col_;
    int lastNote_;
    int lastInstr_;
    int lastCmd_;
    int lastParam_;
    bool commandSelectorModalActive_;
    Phrase *phrase_;
    int lastPlayingPos_;
    Variable cmdEdit_;
    UIBigHexVarField *cmdEditField_;
    void printHelpLegend(FourCC command, GUITextProperties props);
    void enterCommandSelector();
    int findClosestInstrumentFor(int);
    unsigned char wavePeak_[kWaveBins];
    int waveCacheSample_;
    int waveCacheDomain_;
    int waveCacheSlices_;
    // Skip redrawing the pixel strip when nothing visible changed —
    // Flush runs on every player tick and was starving audio on Phrase.
    bool overlayValid_;
    int overlayDrawnSample_;
    int overlayDrawnDomain_;
    int overlayDrawnSlices_;
    int overlayDrawnSlice_;
    int stickyPlayingSlice_;
    struct clipboard {
        bool active_;
        int col_;
        int row_;
        int width_;
        int height_;
        uchar note_[16];
        uchar instr_[16];
        uint cmd1_[16];
        ushort param1_[16];
        uint cmd2_[16];
        ushort param2_[16];
    } clipboard_;

    int saveCol_;
    int saveRow_;

    static short offsets_[2][4];
};

#endif
