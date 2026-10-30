#include "PhraseView.h"
#include "Application/AppWindow.h"
#include "Application/Instruments/CommandList.h"
#include "Application/Instruments/SampleInstrument.h"
#include "Application/Instruments/SamplePool.h"
#include "Application/Instruments/SynthInstrument.h"
#include "Application/Model/Scale.h"
#include "Application/Model/Table.h"
#include "Application/Player/PlayerMixer.h"
#include "Application/Utils/HelpLegend.h"
#include "Application/Utils/char.h"
#include "Application/Views/CommandSelectorCommon.h"
#include "Application/Views/ModalDialogs/CommandSelectorModal.h"
#include "System/Console/Trace.h"
#include "UIController.h"
#include <stdlib.h>
#include <string.h>

short PhraseView::offsets_[2][4] = {-1, 1, 12, -12, -1, 1, 16, -16};

static void CommandSelectorCallback(View &v, ModalView &d) {
    ((PhraseView &)v).onCommandSelectorResult(d);
}

static void CommandSelectorPreviewCallback(View &v, ModalView &d) {
    ((PhraseView &)v).onCommandSelectorPreview(d);
}

PhraseView::PhraseView(GUIWindow &w, ViewData *viewData)
    : View(w, viewData), cmdEdit_("edit", FCC_EDIT, 0) {
    phrase_ = viewData_->song_->phrase_;
    lastPlayingPos_ = 0;
    GUIPoint pos(0, 10);
    cmdEditField_ =
        new UIBigHexVarField(pos, cmdEdit_, 4, "%4.4X", 0, 0xFFFF, 16, true);
    row_ = 0;
    viewData->phraseCurPos_ = 0;
    col_ = 0;
    lastNote_ = 60;
    lastInstr_ = 0;
    lastCmd_ = I_CMD_NONE;
    lastParam_ = 0;
    commandSelectorModalActive_ = false;
    waveCacheSample_ = -1;
    waveCacheFrames_ = -1;
    memset(wavePeak_, 0, sizeof(wavePeak_));
    overlayValid_ = false;
    overlayDrawnSample_ = -1;
    overlayDrawnStart_ = -1;
    overlayDrawnEnd_ = -1;
    overlayDrawnSlices_ = -1;
    overlayDrawnSlice_ = -2;
    overlayDrawnSliceToEnd_ = false;
    stickyPlayingSlice_ = -1;
    overlayDrawnSynthKey_ = 0;
    memset(synthWaveBin_, 0, sizeof(synthWaveBin_));
    memset(synthLfoBin_, 0, sizeof(synthLfoBin_));

    clipboard_.active_ = false;
    clipboard_.width_ = 0;
    clipboard_.height_ = 0;

    for (int i = 0; i < 16; i++) {
        clipboard_.note_[i] = 0xFF;
        clipboard_.instr_[i] = 0;
    };
    View::EnableNotification();
}

PhraseView::~PhraseView() { delete cmdEditField_; };

void PhraseView::updateCursor(int dx, int dy) {

    col_ += dx;
    row_ += dy;
    if (col_ > 5)
        col_ = 5;
    if (col_ < 0)
        col_ = 0;
    if (row_ > 15) {
        // Try to see if the current chain has a phrase after this one

        if ((viewMode_ != VM_SELECTION) && (viewData_->chainRow_ < 15)) {
            viewData_->chainRow_++;
            unsigned char *p = viewData_->GetCurrentChainPointer();
            if (*p != 0xFF) {
                viewData_->currentPhrase_ = *p;
                row_ = 0;
            } else { // rollback
                viewData_->chainRow_--;
                row_ = 15;
            }
        } else {
            row_ = 15;
        }
    }
    if (row_ < 0) {

        // Try to see if the current chain has a phrase before this one

        if ((viewMode_ != VM_SELECTION) && (viewData_->chainRow_ > 0)) {
            viewData_->chainRow_--;
            unsigned char *p = viewData_->GetCurrentChainPointer();
            if (*p != 0xFF) {
                viewData_->currentPhrase_ = *p;
                row_ = 15;
            } else { // rollback
                viewData_->chainRow_++;
                row_ = 0;
            }
        } else {
            row_ = 0;
        }
    }
    GUIPoint anchor = GetAnchor();
    GUIPoint p(anchor);
    switch (col_) {
    case 3:
        p._x += 13;
        p._y += row_;
        cmdEditField_->SetPosition(p);
        cmdEdit_.SetInt(
            *(phrase_->param1_ + (16 * viewData_->currentPhrase_ + row_)));
        break;
    case 5:
        p._x += 23;
        p._y += row_;
        cmdEditField_->SetPosition(p);
        cmdEdit_.SetInt(
            *(phrase_->param2_ + (16 * viewData_->currentPhrase_ + row_)));
        break;
    };
    viewData_->phraseCurPos_ = row_;
    isDirty_ = true;
}

void PhraseView::stopAudition() {
    Player *player = Player::GetInstance();
    if (viewData_->playMode_ == PM_AUDITION)
        player->Stop();
}

bool PhraseView::isCommandColumn() const { return col_ == 2 || col_ == 4; }

FourCC *PhraseView::getCurrentCommandPointer() {
    return CommandSelectorCommon::getCommandPointerByCol(
        col_, 2, phrase_->cmd1_ + (16 * viewData_->currentPhrase_ + row_), 4,
        phrase_->cmd2_ + (16 * viewData_->currentPhrase_ + row_));
}

void PhraseView::enterCommandSelector() {
    FourCC *cmdPtr = getCurrentCommandPointer();
    if (!cmdPtr) return;
    commandSelectorModalActive_ = true;
    DoModal(new CommandSelectorModal(*this, cmdPtr, CommandSelectorPreviewCallback),
            CommandSelectorCallback);
}

void PhraseView::onCommandSelectorResult(ModalView &d) {
    commandSelectorModalActive_ = false;
    CommandSelectorModal &modal = (CommandSelectorModal &)d;
    if (modal.GetReturnCode() == 1) {
        FourCC *cmd = getCurrentCommandPointer();
        if (cmd) {
            lastCmd_ = *cmd;
        }
    }
    isDirty_ = true;
}

void PhraseView::onCommandSelectorPreview(ModalView &) {
    isDirty_ = true;
    Player *player = Player::GetInstance();
    // Don't audition when in playback, allow when browsing around
    if (!player->IsRunning() || viewData_->playMode_ == PM_AUDITION) {
        player->OnStartButton(PM_AUDITION, viewData_->songX_, false,
                              viewData_->chainRow_);
    }
}

void PhraseView::updateCursorValue(ViewUpdateDirection direction, int xOffset,
                                   int yOffset) {

    unsigned char *c = 0;
    unsigned char limit = 0;
    bool wrap = false;
    FourCC *cc;

    switch (col_ + xOffset) {
    case 0:
        c = phrase_->note_ + (16 * viewData_->currentPhrase_ + row_ + yOffset);
        limit = 119;
        wrap = true;
        break;
    case 1:
        c = phrase_->instr_ + (16 * viewData_->currentPhrase_ + row_ + yOffset);
        limit = MAX_INSTRUMENT_COUNT - 1;
        wrap = true;
        break;
    case 2:
        cc = phrase_->cmd1_ + (16 * viewData_->currentPhrase_ + row_ + yOffset);
        switch (direction) {
        case VUD_RIGHT:
            *cc = CommandList::GetNext(*cc);
            break;
        case VUD_UP:
            *cc = CommandList::GetNextAlpha(*cc);
            break;
        case VUD_LEFT:
            *cc = CommandList::GetPrev(*cc);
            break;
        case VUD_DOWN:
            *cc = CommandList::GetPrevAlpha(*cc);
            break;
        }
        lastCmd_ = *cc;
        break;
    case 3:
        switch (direction) {
        case VUD_RIGHT:
            cmdEditField_->ProcessArrow(EPBM_RIGHT);
            break;
        case VUD_UP:
            cmdEditField_->ProcessArrow(EPBM_UP);
            break;
        case VUD_LEFT:
            cmdEditField_->ProcessArrow(EPBM_LEFT);
            break;
        case VUD_DOWN:
            cmdEditField_->ProcessArrow(EPBM_DOWN);
            break;
        }
        *(phrase_->param1_ + (16 * viewData_->currentPhrase_ + row_ +
                              yOffset)) = cmdEdit_.GetInt();
        lastParam_ = cmdEdit_.GetInt();
        break;
    case 4:
        cc = phrase_->cmd2_ + (16 * viewData_->currentPhrase_ + row_ + yOffset);
        switch (direction) {
        case VUD_RIGHT:
            *cc = CommandList::GetNext(*cc);
            break;
        case VUD_UP:
            *cc = CommandList::GetNextAlpha(*cc);
            break;
        case VUD_LEFT:
            *cc = CommandList::GetPrev(*cc);
            break;
        case VUD_DOWN:
            *cc = CommandList::GetPrevAlpha(*cc);
            break;
        }
        lastCmd_ = *cc;
        break;
    case 5:
        switch (direction) {
        case VUD_RIGHT:
            cmdEditField_->ProcessArrow(EPBM_RIGHT);
            break;
        case VUD_UP:
            cmdEditField_->ProcessArrow(EPBM_UP);
            break;
        case VUD_LEFT:
            cmdEditField_->ProcessArrow(EPBM_LEFT);
            break;
        case VUD_DOWN:
            cmdEditField_->ProcessArrow(EPBM_DOWN);
            break;
        }
        *(phrase_->param2_ + (16 * viewData_->currentPhrase_ + row_ +
                              yOffset)) = cmdEdit_.GetInt();
        lastParam_ = cmdEdit_.GetInt();
        break;
    }
    if ((c) && (*c != 0xFF)) {
        int offset = offsets_[col_ + xOffset][direction];
        // if note column apply the set scale
        if (col_ + xOffset == 0) {
            // Add/remove from offset to match selected scale
            int scale = viewData_->project_->GetScale();
            while (!scaleSteps[scale][(*c + offset) % 12]) {
                offset > 0 ? offset++ : offset--;
            }
        }
        updateData(c, offset, limit, wrap);
        switch (col_ + xOffset) {
        case 0:
            lastNote_ = *c;
            break;
        case 1:
            lastInstr_ = *c;
            break;
        }
    }
    Player *player = Player::GetInstance();
    // Phrase FX params are currently not applied to preview
    if (col_ == 0 || col_ == 1 || col_ == 2 || col_ == 3 || col_ == 4 ||
        col_ == 5) {
        if (player->IsRunning()) {
            if ((viewData_->playMode_ == PM_AUDITION)) {
                player->Stop();
                player->OnStartButton(PM_AUDITION, viewData_->songX_, false,
                                      viewData_->chainRow_);
            }
        } else {
            player->OnStartButton(PM_AUDITION, viewData_->songX_, false,
                                  viewData_->chainRow_);
        }
    }
    isDirty_ = true;
}

// If we're on an empty spot, we past the last element
// otherwise we take the current phrase as last

void PhraseView::pasteLast() {

    uchar *c = 0;
    uint *i = 0;

    switch (col_) {
    case 0:
        c = phrase_->note_ + (16 * viewData_->currentPhrase_ + row_);
        if ((*c == 0xFF)) {
            *c = lastNote_;
            c = phrase_->instr_ + (16 * viewData_->currentPhrase_ + row_);
            *c = lastInstr_;
            isDirty_ = true;
        } else {
            lastNote_ = *c;
            c = phrase_->instr_ + (16 * viewData_->currentPhrase_ + row_);
            lastInstr_ = *c;
        }
        break;
    case 1:
        c = phrase_->instr_ + (16 * viewData_->currentPhrase_ + row_);
        if ((*c == 0xFF)) {
            *c = lastInstr_;
            isDirty_ = true;
        } else {
            lastInstr_ = *c;
        }
        break;
    case 2:
        i = phrase_->cmd1_ + (16 * viewData_->currentPhrase_ + row_);
        if (*i == I_CMD_NONE) {
            *i = lastCmd_;
            isDirty_ = true;
        } else {
            lastCmd_ = *i;
        }
        break;

    case 3:
        /*			s=phrase_->param1_+(16*viewData_->currentPhrase_+row_) ;
                    if (*s==0) {
                        *s=lastParam_ ;
                        cmdEdit_.SetInt(lastParam_) ;
                        isDirty_=true ;
                    }
        �*/
        break;

    case 4:
        i = phrase_->cmd2_ + (16 * viewData_->currentPhrase_ + row_);
        if (*i == I_CMD_NONE) {
            *i = lastCmd_;
            isDirty_ = true;
        } else {
            lastCmd_ = *i;
        }
        break;

    case 5:
        /*			s=phrase_->param2_+(16*viewData_->currentPhrase_+row_) ;
                    if (*s==0) {
                        *s=lastParam_ ;
                        isDirty_=true ;
                        cmdEdit_.SetInt(lastParam_) ;
                    }
        */
        break;
    }
}

void PhraseView::cutPosition() {

    clipboard_.active_ = true;
    clipboard_.row_ = row_;
    clipboard_.col_ = col_;
    saveRow_ = row_;
    saveCol_ = col_;

    if (col_ % 2 == 0)
        col_ += 1; // This way, A+B on note cuts
                   // the instruments too and parameters get cut with commands
    cutSelection();
};

void PhraseView::warpInChain(int offset) {

    int currentRow = viewData_->chainRow_;
    viewData_->chainRow_ += offset;
    if ((viewData_->chainRow_ < 16) && (viewData_->chainRow_ >= 0)) {
        unsigned char *p = viewData_->GetCurrentChainPointer();
        if (*p != 0xFF) {
            viewData_->currentPhrase_ = *p;
            switch (col_) {
            case 3:
                cmdEdit_.SetInt(*(phrase_->param1_ +
                                  (16 * viewData_->currentPhrase_ + row_)));
                break;
            case 5:
                cmdEdit_.SetInt(*(phrase_->param2_ +
                                  (16 * viewData_->currentPhrase_ + row_)));
                break;
            };
        } else { // rollback
            viewData_->chainRow_ = currentRow;
        }
    } else { // rollback
        viewData_->chainRow_ = currentRow;
    }
    isDirty_ = true;
}

void PhraseView::warpToNeighbour(int offset) {
    // save current data
    int saveX = viewData_->songX_;
    int saveOffset = viewData_->songOffset_;
    int newPos = saveX + offset;

    while ((newPos > -1) && (newPos < SONG_CHANNEL_COUNT)) {
        // Go to neighbout song channel
        viewData_->songX_ = newPos;
        unsigned char *c = viewData_->GetCurrentSongPointer();
        // is there a chain ?
        unsigned char oldChain = viewData_->currentChain_;
        if (*c != 0xFF) {
            // go to chain
            viewData_->currentChain_ = *c;
            // get phrase at location
            unsigned char *p = viewData_->GetCurrentChainPointer();
            // is there a phrase ?
            if (*p != 0xFF) {
                viewData_->currentPhrase_ = *p;
                updateCursor(0, 0);
                isDirty_ = true;
                return;
            } else {
                viewData_->currentPhrase_ = 0xFE;
                viewData_->currentChain_ = *c;
                updateCursor(0, 0);
                isDirty_ = true;
                return;
            }
        } else {
            // no chain, to neighbour song channel
            newPos += offset;
        }
    }
    // restore song
    viewData_->songX_ = saveX;
    viewData_->songOffset_ = saveOffset;
}

/******************************************************
 getSelectionRect:
        gets the normalized rectangle of the current
        selection. Valid only while selection is drawn
 ******************************************************/

GUIRect PhraseView::getSelectionRect() {
    GUIRect r(clipboard_.col_, clipboard_.row_, col_, row_);
    r.Normalize();
    return r;
};

/******************************************************
 fillClipboardData:

        copies the necessary information from the
        current selection to the clipboard for future
        paste. We're copying data all across the row
        because we"re too lazy to try to figure a better
        procedure
 ******************************************************/

void PhraseView::fillClipboardData() {

    // Get Current normalized selection rect

    GUIRect selRect = getSelectionRect();

    // Get size & store in clipboard

    clipboard_.width_ = selRect.Width() + 1;
    clipboard_.height_ = selRect.Height() + 1;
    clipboard_.row_ = selRect.Top();
    clipboard_.col_ = selRect.Left();

    // Copy the data

    uchar *src1 =
        viewData_->song_->phrase_->note_ + 16 * viewData_->currentPhrase_;
    uchar *dst1 = clipboard_.note_;
    uchar *src2 =
        viewData_->song_->phrase_->instr_ + 16 * viewData_->currentPhrase_;
    uchar *dst2 = clipboard_.instr_;
    uint *src3 =
        viewData_->song_->phrase_->cmd1_ + 16 * viewData_->currentPhrase_;
    uint *dst3 = clipboard_.cmd1_;
    ushort *src4 =
        viewData_->song_->phrase_->param1_ + 16 * viewData_->currentPhrase_;
    ushort *dst4 = clipboard_.param1_;
    uint *src5 =
        viewData_->song_->phrase_->cmd2_ + 16 * viewData_->currentPhrase_;
    uint *dst5 = clipboard_.cmd2_;
    ushort *src6 =
        viewData_->song_->phrase_->param2_ + 16 * viewData_->currentPhrase_;
    ushort *dst6 = clipboard_.param2_;

    for (int i = 0; i < clipboard_.height_; i++) {
        dst1[i] = src1[clipboard_.row_ + i];
        dst2[i] = src2[clipboard_.row_ + i];
        dst3[i] = src3[clipboard_.row_ + i];
        dst4[i] = src4[clipboard_.row_ + i];
        dst5[i] = src5[clipboard_.row_ + i];
        dst6[i] = src6[clipboard_.row_ + i];
    };
    updateCursor(0, 0);
};

void PhraseView::updateSelectionValue(ViewUpdateDirection direction) { // HERE

    saveRow_ = row_;
    saveCol_ = col_;

    GUIRect r = getSelectionRect();
    col_ = r.Left();
    row_ = r.Top();

    for (int i = 0; i <= r.Width(); i++) {
        for (int j = 0; j <= r.Height(); j++) {
            if (col_ + i < 2) {
                updateCursorValue(direction, i, j);
            }
        }
    }
    row_ = saveRow_;
    col_ = saveCol_;
}

void PhraseView::extendSelection() {
    GUIRect rect = getSelectionRect();
    if (rect.Left() > 0 || rect.Right() < 5) {
        if (col_ < clipboard_.col_) {
            col_ = 0;
            clipboard_.col_ = 5;
        } else {
            col_ = 5;
            clipboard_.col_ = 0;
        }
        isDirty_ = true;
    } else {
        if (row_ < clipboard_.row_) {
            row_ = 0;
            clipboard_.row_ = 15;
        } else {
            clipboard_.row_ = 0;
            row_ = 15;
        }
        isDirty_ = true;
    }
}

/******************************************************
 interpolateSelection:
        expands the lowest value of selection to the highest
 ******************************************************/
void PhraseView::interpolateSelection() {
    if (!clipboard_.active_) {
        return;
    }

    GUIRect rect = getSelectionRect();
    // Only interpolate if we're in note (0) or param (3, 5) columns
    int col = rect.Left();
    if (col != rect.Right() || (col != 0 && col != 3 && col != 5)) {
        return;
    }

    int startRow = rect.Top();
    int endRow = rect.Bottom();
    // Need at least 2 rows to interpolate
    if (endRow - startRow < 1) {
        return;
    }

    // Select the appropriate data array based on column
    if (col == 0) {
        // Note column
        uchar *noteData = phrase_->note_ + (16 * viewData_->currentPhrase_);

        uchar startNote = noteData[startRow];
        uchar endNote = noteData[endRow];

        if (startNote == 0xFF || endNote == 0xFF) {
            View::SetNotification("No note info");
            return;
        }

        int numSteps = endRow - startRow;
        int noteDiff = (int)endNote - (int)startNote;

        for (int step = 0; step <= numSteps; step++) {
            int row = startRow + step;
            int value = startNote + (noteDiff * step) / (numSteps);
            noteData[row] = (uchar)value;
        }
    } else {
        // Parameter columns (3 or 5)
        ushort *paramData = (col == 3) ? 
            phrase_->param1_ + (16 * viewData_->currentPhrase_) :
            phrase_->param2_ + (16 * viewData_->currentPhrase_);

        ushort startParam = paramData[startRow];
        ushort endParam = paramData[endRow];

        int numSteps = endRow - startRow;
        int paramDiff = (int)endParam - (int)startParam;

        for (int step = 0; step <= numSteps; step++) {
            int row = startRow + step;
            int value = startParam + (paramDiff * step) / (numSteps);
            paramData[row] = (ushort)value;
        }
    }
    isDirty_ = true;
}

/******************************************************
 copySelection:
        copies data in the current selection to the
        clipboard & end selection process
 ******************************************************/

void PhraseView::copySelection() {

    // Keep up with row,col of selection coz
    // fillClipboardData will trash it

    fillClipboardData();

    clipboard_.active_ = false;
    viewMode_ = VM_NORMAL;
    row_ = saveRow_;
    col_ = saveCol_;

    View::SetNotification("Copied selection");
};

/******************************************************
 cut:  copies data in the current selection to the
       clipboard, clear selection content & end selection
       process
 ******************************************************/

void PhraseView::cutSelection() {

    // Keep up with row,col of selection coz
    // fillClipboardData will trash it

    fillClipboardData();

    // Loop over selection col, row & clear data inside it

    uchar *dst1 =
        viewData_->song_->phrase_->note_ + 16 * viewData_->currentPhrase_;
    uchar *dst2 =
        viewData_->song_->phrase_->instr_ + 16 * viewData_->currentPhrase_;
    uint *dst3 =
        viewData_->song_->phrase_->cmd1_ + 16 * viewData_->currentPhrase_;
    ushort *dst4 =
        viewData_->song_->phrase_->param1_ + 16 * viewData_->currentPhrase_;
    uint *dst5 =
        viewData_->song_->phrase_->cmd2_ + 16 * viewData_->currentPhrase_;
    ushort *dst6 =
        viewData_->song_->phrase_->param2_ + 16 * viewData_->currentPhrase_;

    for (int i = 0; i < clipboard_.width_; i++) {
        for (int j = 0; j < clipboard_.height_; j++) {
            switch (i + clipboard_.col_) {
            case 0:
                dst1[j + clipboard_.row_] = 0xFF;
                break;
            case 1:
                dst2[j + clipboard_.row_] = 0xFF;
                break;
            case 2:
                dst3[j + clipboard_.row_] = I_CMD_NONE;
                break;
            case 3:
                dst4[j + clipboard_.row_] = 0x0000;
                break;
            case 4:
                dst5[j + clipboard_.row_] = I_CMD_NONE;
                break;
            case 5:
                dst6[j + clipboard_.row_] = 0x0000;
                break;
            }
        }
    }

    // Clear selection, end selection process & reposition cursor

    clipboard_.active_ = false;
    viewMode_ = VM_NORMAL;
    row_ = saveRow_;
    col_ = saveCol_;
    updateCursor(0, 0);
    isDirty_ = true;
};

/******************************************************
 pasteClipboard:
        copies data in the clipboard to the current step
 ******************************************************/

void PhraseView::pasteClipboard() {

    // Get number of row to paste

    int height = clipboard_.height_;
    /*    if (row_+height>16) {
            height=16-row_ ;
        }
      */
    uchar *dst1 =
        viewData_->song_->phrase_->note_ + 16 * viewData_->currentPhrase_;
    uchar *src1 = clipboard_.note_;
    uchar *dst2 =
        viewData_->song_->phrase_->instr_ + 16 * viewData_->currentPhrase_;
    uchar *src2 = clipboard_.instr_;
    uint *dst3 =
        viewData_->song_->phrase_->cmd1_ + 16 * viewData_->currentPhrase_;
    uint *src3 = clipboard_.cmd1_;
    ushort *dst4 =
        viewData_->song_->phrase_->param1_ + 16 * viewData_->currentPhrase_;
    ushort *src4 = clipboard_.param1_;
    uint *dst5 =
        viewData_->song_->phrase_->cmd2_ + 16 * viewData_->currentPhrase_;
    uint *src5 = clipboard_.cmd2_;
    ushort *dst6 =
        viewData_->song_->phrase_->param2_ + 16 * viewData_->currentPhrase_;
    ushort *src6 = clipboard_.param2_;

    uint *noCmd = (uint *)-1;
    ushort *noPrm = (ushort *)-1;
    uint *srcCmd[5] = {noCmd, noCmd, src3, noCmd, src5};
    ushort *srcPrm[6] = {noPrm, noPrm, noPrm, src4, noPrm, src6};
    uint *dstCmd[5] = {noCmd, noCmd, dst3, noCmd, dst5};
    ushort *dstPrm[6] = {noPrm, noPrm, noPrm, dst4, noPrm, dst6};

    bool wasUpdated = false;

    for (int i = 0; i < clipboard_.width_; i++) {
        for (int j = 0; j < height; j++) {
            switch (i + clipboard_.col_) {
            case 0:
                dst1[(j + row_) % 16] = src1[j];
                wasUpdated = true;
                break;
            case 1:
                dst2[(j + row_) % 16] = src2[j];
                wasUpdated = true;
                break;
            case 2:
            case 4:
                if ((col_ + i) == 2 ||
                    (col_ + i) == 4) { // Don't allow commands in notes, etc
                    dstCmd[col_ + i][(row_ + j) % 16] =
                        srcCmd[clipboard_.col_ + i][j];
                    wasUpdated = true;
                }
                break;
            case 3:
            case 5:
                if ((col_ + i) == 3 || (col_ + i) == 5) {
                    dstPrm[col_ + i][(row_ + j) % 16] =
                        srcPrm[clipboard_.col_ + i][j];
                    wasUpdated = true;
                }
                break;
            }
        }
    }
    if (wasUpdated) {
        updateCursor(0x00, ((row_ + height) % 16 - row_));
        isDirty_ = true;
    }
};

void PhraseView::unMuteAll() {

    UIController *controller = UIController::GetInstance();
    controller->UnMuteAll();
};

void PhraseView::toggleMute() {

    UIController *controller = UIController::GetInstance();
    controller->ToggleMute(viewData_->songX_, viewData_->songX_);
    viewMode_ = (viewMode_ != VM_MUTEON) ? VM_MUTEON : VM_NORMAL;
};

void PhraseView::switchSoloMode() {

    UIController *controller = UIController::GetInstance();
    controller->SwitchSoloMode(viewData_->songX_, viewData_->songX_,
                               (viewMode_ == VM_NORMAL));
    viewMode_ = (viewMode_ != VM_SOLOON) ? VM_SOLOON : VM_NORMAL;
    isDirty_ = true;
};

void PhraseView::OnFocus() {
    clipboard_.active_ = false;
    viewMode_ = VM_NORMAL;
    updateCursor(0, 0);
};

void PhraseView::ProcessButtonMask(unsigned short mask, bool pressed) {

    if (!pressed) {
        if (viewMode_ == VM_MUTEON) {
            if (mask & EPBM_R) {
                toggleMute();
            }
        };
        if (viewMode_ == VM_SOLOON) {
            if (mask & EPBM_R) {
                switchSoloMode();
            }
        };
        return;
    };

    if (viewMode_ == VM_NEW) {
        if (mask == EPBM_A) {

            // If note or I, we request a new instr

            if (col_ < 2) {
                InstrumentBank *bank = viewData_->project_->GetInstrumentBank();
                unsigned short next = bank->GetNext();
                if (next != NO_MORE_INSTRUMENT) {
                    unsigned char *c = phrase_->instr_ +
                                       (16 * viewData_->currentPhrase_ + row_);
                    *c = (unsigned char)next;
                    lastInstr_ = next;
                    isDirty_ = true;
                }
                mask &= (0xFFFF - EPBM_A);
            } else {
                if ((col_ == 3) &&
                    (*(phrase_->cmd1_ + (16 * viewData_->currentPhrase_ +
                                         row_))) == I_CMD_TABL) {
                    TableHolder *th = TableHolder::GetInstance();
                    unsigned short next = th->GetNext();
                    if (next != NO_MORE_TABLE) {
                        ushort *c = phrase_->param1_ +
                                    (16 * viewData_->currentPhrase_ + row_);
                        *c = next;
                        isDirty_ = true;
                        mask &= (0xFFFF - EPBM_A);
                        cmdEdit_.SetInt(next);
                    }
                }
                if ((col_ == 5) &&
                    (*(phrase_->cmd2_ + (16 * viewData_->currentPhrase_ +
                                         row_))) == I_CMD_TABL) {
                    TableHolder *th = TableHolder::GetInstance();
                    unsigned short next = th->GetNext();
                    if (next != NO_MORE_TABLE) {
                        ushort *c = phrase_->param2_ +
                                    (16 * viewData_->currentPhrase_ + row_);
                        *c = next;
                        isDirty_ = true;
                        mask &= (0xFFFF - EPBM_A);
                        cmdEdit_.SetInt(next);
                    }
                }
            };
        }
    }

    if (viewMode_ == VM_CLONE) {
        if ((mask & EPBM_A) && (mask & EPBM_L)) {
            if (col_ < 2) {
                InstrumentBank *bank = viewData_->project_->GetInstrumentBank();
                unsigned char *c =
                    phrase_->instr_ + (16 * viewData_->currentPhrase_ + row_);
                if (*c != 0xFF) {
                    unsigned short next = bank->Clone(*c);
                    if (next != NO_MORE_INSTRUMENT) {
                        *c = (unsigned char)next;
                        lastInstr_ = next;
                        isDirty_ = true;
                    } else {
                        View::SetNotification("No more instruments");
                    }
                }
            } else {
                if ((col_ == 3) &&
                    (*(phrase_->cmd1_ + (16 * viewData_->currentPhrase_ +
                                         row_))) == I_CMD_TABL) {
                    TableHolder *th = TableHolder::GetInstance();
                    int current = *(phrase_->param1_ +
                                    (16 * viewData_->currentPhrase_ + row_));
                    if (current != -1) {
                        unsigned short next = th->Clone(current);
                        if (next != NO_MORE_TABLE) {
                            ushort *c = phrase_->param1_ +
                                        (16 * viewData_->currentPhrase_ + row_);
                            *c = next;
                            isDirty_ = true;
                            cmdEdit_.SetInt(next);
                        } else {
                            View::SetNotification("No more tables");
                        }
                    }
                }
                if ((col_ == 5) &&
                    (*(phrase_->cmd2_ + (16 * viewData_->currentPhrase_ +
                                         row_))) == I_CMD_TABL) {
                    TableHolder *th = TableHolder::GetInstance();
                    unsigned short next =
                        th->Clone(*(phrase_->param2_ +
                                    (16 * viewData_->currentPhrase_ + row_)));
                    if (next != NO_MORE_TABLE) {
                        ushort *c = phrase_->param2_ +
                                    (16 * viewData_->currentPhrase_ + row_);
                        *c = next;
                        isDirty_ = true;
                        cmdEdit_.SetInt(next);
                    } else {
                        View::SetNotification("No more tables");
                    }
                }
            };
            mask &= (0xFFFF - (EPBM_A | EPBM_L));
        } else {
            viewMode_ = VM_SELECTION;
        }
    };

    if (viewMode_ == VM_SELECTION) {
        if (!clipboard_.active_) {
            clipboard_.active_ = true;
            clipboard_.col_ = col_;
            clipboard_.row_ = row_;
            saveCol_ = col_;
            saveRow_ = row_;
        }
        processSelectionButtonMask(mask);
    } else {
        viewMode_ = VM_NORMAL;
        processNormalButtonMask(mask);
    };
}

void PhraseView::processNormalButtonMask(unsigned short mask) {
    // Stop audition when pressing any button except A
    if (!(mask & EPBM_A)) {
        stopAudition();
    }
    // B Modifier

    Player *player = Player::GetInstance();

    if (mask & EPBM_B) {
        if (mask & EPBM_LEFT)
            warpToNeighbour(-1);
        if (mask & EPBM_RIGHT)
            warpToNeighbour(1);
        if (mask & EPBM_UP)
            warpInChain(-1);
        if (mask & EPBM_DOWN)
            warpInChain(1);
        if (mask & EPBM_A) {
            cutPosition();
        }
        if (mask & EPBM_L) {
            viewMode_ = VM_CLONE;
        };
        if (mask & EPBM_R)
            toggleMute();
    } else {

        // A Modifer

        if (mask & EPBM_A) {
            if ((col_ == 0) || (col_ == 1)) { // Preview when pressing A
                Player *player = Player::GetInstance();
                if (!player->IsRunning()) {
                    player->OnStartButton(PM_AUDITION, viewData_->songX_, false,
                                          viewData_->chainRow_);
                }
            }

            if (mask & EPBM_DOWN) {
                if (isCommandColumn())
                    enterCommandSelector();
                else
                    updateCursorValue(VUD_DOWN);
            }
            if (mask & EPBM_UP) {
                if (isCommandColumn())
                    enterCommandSelector();
                else
                    updateCursorValue(VUD_UP);
            }
            if (mask & EPBM_LEFT)
                updateCursorValue(VUD_LEFT);
            if (mask & EPBM_RIGHT)
                updateCursorValue(VUD_RIGHT);
            if (mask & EPBM_L)
                pasteClipboard();
            if (mask & EPBM_R)
                switchSoloMode();
            if (mask == EPBM_A) {
                pasteLast();
                if ((col_ == 1) || (col_ == 3) || (col_ == 5))
                    viewMode_ = VM_NEW;
            }

        } else {

            // R Modifier

            if (mask & EPBM_R) {
                if (mask & EPBM_LEFT) {
                    ViewType vt = VT_CHAIN;
                    ViewEvent ve(VET_SWITCH_VIEW, &vt);
                    SetChanged();
                    NotifyObservers(&ve);
                }
                if (mask & EPBM_RIGHT) {
                    unsigned char *c = phrase_->instr_ +
                                       (16 * viewData_->currentPhrase_ + row_);
                    if (*c != 0xFF) {
                        viewData_->currentInstrument_ = *c;
                    } else {
                        int nearest = findClosestInstrumentFor(row_);
                        if (nearest >= 0) {
                            viewData_->currentInstrument_ = nearest;
                        } else viewData_->currentInstrument_= lastInstr_;
                    }
                    if (viewData_->currentInstrument_ != 0xFF) {
                        ViewType vt = VT_INSTRUMENT;
                        ViewEvent ve(VET_SWITCH_VIEW, &vt);
                        SetChanged();
                        NotifyObservers(&ve);
                    }
                }
                if (mask & EPBM_DOWN) {

                    // Go to table view

                    ViewType vt = VT_TABLE;

                    FourCC *cmd = phrase_->cmd1_ +
                                  (16 * viewData_->currentPhrase_ + row_);
                    ushort *param = phrase_->param1_ +
                                    (16 * viewData_->currentPhrase_ + row_);

                    if (*cmd != I_CMD_TABL) {
                        cmd = phrase_->cmd2_ +
                              (16 * viewData_->currentPhrase_ + row_);
                        param = phrase_->param2_ +
                                (16 * viewData_->currentPhrase_ + row_);
                    }
                    if (*cmd == I_CMD_TABL) {
                        viewData_->currentTable_ = (*param) & 0x7F;
                    }
                    ViewEvent ve(VET_SWITCH_VIEW, &vt);
                    SetChanged();
                    NotifyObservers(&ve);
                }

                if (mask & EPBM_UP) {

                    // Go to groove view

                    ViewType vt = VT_GROOVE;
                    ViewEvent ve(VET_SWITCH_VIEW, &vt);
                    SetChanged();
                    NotifyObservers(&ve);
                }

                if (mask & EPBM_START) {
                    player->OnStartButton(PM_PHRASE, viewData_->songX_, true,
                                          viewData_->chainRow_);
                }
                if (mask & EPBM_L)
                    unMuteAll();

            } else {
                // L Modifier
                if (mask & EPBM_L) {

                } else {
                    // No modifier

                    if (mask & EPBM_DOWN)
                        updateCursor(0, 1);
                    if (mask & EPBM_UP)
                        updateCursor(0, -1);
                    if (mask & EPBM_LEFT)
                        updateCursor(-1, 0);
                    if (mask & EPBM_RIGHT)
                        updateCursor(1, 0);
                    if (mask & EPBM_START) {
                        player->OnStartButton(PM_PHRASE, viewData_->songX_,
                                              false, viewData_->chainRow_);
                    }
                }
            }
        }
    }
};

/*
 * For currently selected row, find nearest instrument from the top
 */
int PhraseView::findClosestInstrumentFor(int row) {
    unsigned char *instr = phrase_->instr_ + (16 * viewData_->currentPhrase_);
    if (instr[row] != 0xFF) return instr[row];
    for (int d = 1; d < 16; ++d) {
        int up = row - d;
        int down = row + d;
        if (up >= 0 && instr[up] != 0xFF) return instr[up];
        if (down < 16 && instr[down] != 0xFF) return instr[down];
    }
    return -1; // none found in phrase
}

/*
 * Reserve char cells under the phrase so the vector overlay has a clean
 * background (drawn in DrawOverlay after the char grid flush).
 */
void PhraseView::drawSampleStrip() {
    if (miniLayout_) {
        return;
    }

    GUIPoint anchor = GetAnchor();
    const int stripLeft = View::margin_ + 1;
    const int stripWidth = 38;
    const int stripTop = anchor._y + 17;
    const int stripHeight = 5;

    ClearRect(stripLeft - 1, stripTop, stripWidth + 2, stripHeight);
}

void PhraseView::ensureWaveformCache(short *pcm, int nFrames, int nCh,
                                     int sampleIdx) {
    if (waveCacheSample_ == sampleIdx && waveCacheFrames_ == nFrames) {
        return;
    }
    waveCacheSample_ = sampleIdx;
    waveCacheFrames_ = nFrames;
    for (int b = 0; b < kWaveBins; b++) {
        int start = (int)((long)b * nFrames / kWaveBins);
        int end = (int)((long)(b + 1) * nFrames / kWaveBins);
        if (end <= start) {
            end = start + 1;
        }
        if (end > nFrames) {
            end = nFrames;
        }
        int peak = 0;
        int stride = ((end - start) / 48) + 1;
        for (int f = start; f < end; f += stride) {
            int sample = pcm[f * nCh];
            if (sample < 0) {
                sample = -sample;
            }
            if (sample > peak) {
                peak = sample;
            }
        }
        // Store 0..255 amplitude for later scaling to strip height.
        int amp = (peak * 255) / 32768;
        if (amp < 1 && peak > 0) {
            amp = 1;
        }
        if (amp > 255) {
            amp = 255;
        }
        wavePeak_[b] = (unsigned char)amp;
    }
}

int PhraseView::resolvePlayingSlice(int instrId, I_Instrument *instr,
                                    int nSlices) {
    if (nSlices <= 1) {
        stickyPlayingSlice_ = -1;
        return -1;
    }

    Player *player = Player::GetInstance();
    PlayerMixer *pm = PlayerMixer::GetInstance();

    if (player->IsRunning()) {
        // Prefer the note currently sounding on a channel using this instrument.
        for (int c = 0; c < SONG_CHANNEL_COUNT; c++) {
            int note = pm->GetChannelNote(c);
            if (note == 0xFF || note >= nSlices) {
                continue;
            }
            I_Instrument *chInstr = pm->GetInstrument(c);
            if (!chInstr) {
                chInstr = pm->GetLastInstrument(c);
            }
            if (chInstr == instr) {
                stickyPlayingSlice_ = note;
                return note;
            }
        }

        // Oneshots clear notes_ quickly — fall back to the note under the
        // playhead in this phrase for a channel that is playing it.
        unsigned char *notes =
            phrase_->note_ + (16 * viewData_->currentPhrase_);
        unsigned char *instrs =
            phrase_->instr_ + (16 * viewData_->currentPhrase_);
        for (int c = 0; c < SONG_CHANNEL_COUNT; c++) {
            if (!player->IsChannelPlaying(c)) {
                continue;
            }
            if (viewData_->currentPlayPhrase_[c] != viewData_->currentPhrase_) {
                continue;
            }
            if (viewData_->playMode_ == PM_AUDITION) {
                continue;
            }
            int prow = viewData_->phrasePlayPos_[c];
            if (prow < 0 || prow > 15) {
                continue;
            }
            unsigned char n = notes[prow];
            if (n == 0xFF || n >= nSlices) {
                continue;
            }
            unsigned char id = instrs[prow];
            if (id == 0xFF) {
                id = (unsigned char)findClosestInstrumentFor(prow);
            }
            if ((int)id == instrId) {
                stickyPlayingSlice_ = n;
                return n;
            }
        }

        // Keep last slice lit between oneshot gaps so we don't thrash redraws
        // (and so the highlight doesn't flicker every tick).
        if (stickyPlayingSlice_ >= 0 && stickyPlayingSlice_ < nSlices) {
            return stickyPlayingSlice_;
        }
    } else {
        stickyPlayingSlice_ = -1;
    }

    // Editing: highlight the slice for the note on the cursor row.
    unsigned char *notes = phrase_->note_ + (16 * viewData_->currentPhrase_);
    unsigned char n = notes[row_];
    if (n != 0xFF && n < nSlices) {
        return n;
    }
    return -1;
}

/*
 * Pixel waveform + slice markers/highlight. Domain matches the engine:
 * slices divide [0 .. loopEnd] (or full sample if loop end unset).
 * Synth instruments show oscillator*ADSR with LFO as a thin white line.
 */
void PhraseView::clearWaveformStrip(int px, int py, int pw, int ph) {
    if (overlayValid_ && overlayDrawnSample_ == -3 &&
        overlayDrawnSynthKey_ == 0) {
        return;
    }
    AppWindow &aw = (AppWindow &)w_;
    GUIRect strip(px - 2, py, px + pw + 2, py + ph);
    aw.BeginBatchDraw();
    aw.FillRect(strip, CD_BACKGROUND);
    aw.EndBatchDraw(strip);
    overlayValid_ = true;
    overlayDrawnSample_ = -3;
    overlayDrawnStart_ = -1;
    overlayDrawnEnd_ = -1;
    overlayDrawnSlices_ = -1;
    overlayDrawnSlice_ = -2;
    overlayDrawnSynthKey_ = 0;
}

void PhraseView::DrawOverlay() {
    if (miniLayout_) {
        return;
    }

    if (HasModal()) {
        overlayValid_ = false;
        return;
    }

    GUIPoint anchor = GetAnchor();

    const int stripLeft = View::margin_ + 1;
    const int stripWidth = 38;
    const int stripTop = anchor._y + 17;
    const int stripHeight = 5;
    const int cw = AppWindow::CharWidth();
    const int ch = AppWindow::CharHeight();

    const int px = stripLeft * cw;
    const int py = stripTop * ch;
    const int pw = stripWidth * cw;
    const int ph = stripHeight * ch;

    int instrId = findClosestInstrumentFor(row_);
    if (instrId < 0) {
        clearWaveformStrip(px, py, pw, ph);
        return;
    }

    InstrumentBank *bank = viewData_->project_->GetInstrumentBank();
    I_Instrument *instr = bank->GetInstrument(instrId);
    if (!instr) {
        clearWaveformStrip(px, py, pw, ph);
        return;
    }

    if (instr->GetType() == IT_SAMPLE) {
        drawSampleOverlay(px, py, pw, ph, instrId, instr);
        return;
    }
    if (instr->GetType() == IT_SYNTH) {
        drawSynthOverlay(px, py, pw, ph, instr);
        return;
    }

    clearWaveformStrip(px, py, pw, ph);
}

void PhraseView::drawSampleOverlay(int px, int py, int pw, int ph, int instrId,
                                   I_Instrument *instr) {
    AppWindow &aw = (AppWindow &)w_;
    SampleInstrument *si = (SampleInstrument *)instr;
    int sampleIdx = si->GetSampleIndex();
    if (sampleIdx < 0 || sampleIdx == NO_SAMPLE) {
        clearWaveformStrip(px, py, pw, ph);
        return;
    }

    SoundSource *src = SamplePool::GetInstance()->GetSource(sampleIdx);
    if (!src) {
        clearWaveformStrip(px, py, pw, ph);
        return;
    }

    short *pcm = (short *)src->GetSampleBuffer(-1);
    int nFrames = src->GetSize(-1);
    int nCh = src->GetChannelCount(-1);
    if (!pcm || nFrames <= 0 || nCh < 1) {
        clearWaveformStrip(px, py, pw, ph);
        return;
    }

    Variable *startVar = si->FindVariable(SIP_START);
    Variable *endVar = si->FindVariable(SIP_END);
    int startFrame = startVar ? startVar->GetInt() : 0;
    int endFrame = endVar ? endVar->GetInt() : nFrames;
    if (endFrame <= 0 || endFrame > nFrames) {
        endFrame = nFrames;
    }
    if (startFrame < 0) {
        startFrame = 0;
    }
    if (startFrame >= endFrame) {
        startFrame = endFrame > 1 ? endFrame - 1 : 0;
    }

    Variable *slicesVar = si->FindVariable(SIP_SLICES);
    int nSlices = slicesVar ? slicesVar->GetInt() : 1;
    if (nSlices < 1) {
        nSlices = 1;
    }
    Variable *toEndVar = si->FindVariable(SIP_SLICETOEND);
    bool sliceToEnd = (nSlices > 1) && toEndVar && toEndVar->GetInt() != 0;

    int playingSlice = resolvePlayingSlice(instrId, instr, nSlices);

    // Flush is invoked on every player tick / expose. Re-painting ~100 rects
    // each time starves the audio fill thread on macOS — keep the previous
    // pixels when the visible strip state is unchanged.
    if (overlayValid_ && overlayDrawnSample_ == sampleIdx &&
        overlayDrawnStart_ == startFrame && overlayDrawnEnd_ == endFrame &&
        overlayDrawnSlices_ == nSlices && overlayDrawnSlice_ == playingSlice &&
        overlayDrawnSliceToEnd_ == sliceToEnd && overlayDrawnSynthKey_ == 0) {
        return;
    }

    ensureWaveformCache(pcm, nFrames, nCh, sampleIdx);

    int selX0 = px + (int)((long)startFrame * pw / nFrames);
    int selX1 = px + (int)((long)endFrame * pw / nFrames);
    if (selX0 < px)
        selX0 = px;
    if (selX1 > px + pw)
        selX1 = px + pw;
    if (selX1 <= selX0)
        selX1 = selX0 + 1;
    int selW = selX1 - selX0;
    if (selW < 1)
        selW = 1;

    GUIRect stripDirty(px - 2, py, px + pw + 2, py + ph);
    aw.BeginBatchDraw();

    // Backdrop + side rails
    aw.FillRect(stripDirty, CD_BACKGROUND);
    GUIRect leftRail(px - 2, py, px, py + ph);
    GUIRect rightRail(px + pw, py, px + pw + 2, py + ph);
    aw.FillRect(leftRail, (selX0 <= px) ? CD_BORDER : CD_SONGVIEW00);
    aw.FillRect(rightRail, (selX1 >= px + pw) ? CD_BORDER : CD_SONGVIEW00);

    const int midY = py + ph / 2;
    const int maxAmp = ph / 2 - 1;

    // Horizontal center baseline: dimmed outside [selX0, selX1], bright inside.
    if (selX0 > px) {
        GUIRect midLeft(px, midY - 1, selX0, midY + 1);
        aw.FillRect(midLeft, CD_SONGVIEW00);
    }
    GUIRect midActive(selX0, midY - 1, selX1, midY + 1);
    aw.FillRect(midActive, CD_HILITE1);
    if (selX1 < px + pw) {
        GUIRect midRight(selX1, midY - 1, px + pw, midY + 1);
        aw.FillRect(midRight, CD_SONGVIEW00);
    }

    // Active slice fill inside [selX0 .. selX1].
    int playX0 = -1;
    int playX1 = -1;
    if (playingSlice >= 0 && nSlices > 1) {
        playX0 = selX0 + (int)((long)playingSlice * selW / nSlices);
        playX1 = sliceToEnd
                     ? selX1
                     : selX0 + (int)((long)(playingSlice + 1) * selW / nSlices);
        if (playX1 <= playX0) {
            playX1 = playX0 + 1;
        }
        GUIRect band(playX0, py, playX1, py + ph);
        aw.FillRect(band, CD_HILITE2);
        GUIRect tip(playX0, py, playX1, py + 2);
        aw.FillRect(tip, CD_PLAY);
        GUIRect tip2(playX0, py + ph - 2, playX1, py + ph);
        aw.FillRect(tip2, CD_PLAY);
    }

    // Cached envelope mapped across the strip.
    for (int b = 0; b < kWaveBins; b++) {
        int x0 = px + (int)((long)b * pw / kWaveBins);
        int x1 = px + (int)((long)(b + 1) * pw / kWaveBins);
        if (x1 <= x0) {
            x1 = x0 + 1;
        }
        int amp = (wavePeak_[b] * maxAmp) / 255;
        if (amp < 1 && wavePeak_[b] > 0) {
            amp = 1;
        }
        if (amp <= 0) {
            continue;
        }
        int midBin = (x0 + x1) / 2;
        bool inSelected = (midBin >= selX0 && midBin < selX1);
        bool inPlaying =
            inSelected && (playX0 >= 0) && (midBin >= playX0 && midBin < playX1);
        ColorDefinition col = !inSelected ? CD_SONGVIEW00
                              : inPlaying ? CD_PLAY
                                          : CD_HILITE1;
        GUIRect column(x0, midY - amp, x1, midY + amp + 1);
        aw.FillRect(column, col);
    }

    // Slice markers inside [selX0 .. selX1].
    if (nSlices > 1) {
        for (int s = 1; s < nSlices; s++) {
            int mx = selX0 + (int)((long)s * selW / nSlices);
            GUIRect marker(mx, py, mx + 1, py + ph);
            aw.FillRect(marker, CD_BORDER);
        }
    }

    // White active-segment bracket matching InstrumentView.
    GUIRect startBar(selX0 - 1, py, selX0 + 1, py + ph);
    GUIRect endBar(selX1 - 1, py, selX1 + 1, py + ph);
    GUIRect bottomBar(selX0 - 1, py + ph - 2, selX1 + 1, py + ph);
    aw.FillRect(startBar, CD_NORMAL);
    aw.FillRect(endBar, CD_NORMAL);
    aw.FillRect(bottomBar, CD_NORMAL);

    aw.EndBatchDraw(stripDirty);

    overlayValid_ = true;
    overlayDrawnSample_ = sampleIdx;
    overlayDrawnStart_ = startFrame;
    overlayDrawnEnd_ = endFrame;
    overlayDrawnSlices_ = nSlices;
    overlayDrawnSlice_ = playingSlice;
    overlayDrawnSliceToEnd_ = sliceToEnd;
    overlayDrawnSynthKey_ = 0;
}

int PhraseView::synthPreviewKey(I_Instrument *instr) const {
    // Fingerprint of params that affect the strip so we can skip redraws.
    int mode = 0, wave = 0, mshape = 0, atk = 0, dec = 0, sus = 0, rel = 0;
    int rate = 0, depth = 0, sz = 0, mult = 0, warp = 0, scan = 0;
    int tmb = 0, col = 0, deg = 0, red = 0, amp = 0, lim = 0;
    Variable *v;
    if ((v = instr->FindVariable(SYP_MODE)))
        mode = v->GetInt();
    if ((v = instr->FindVariable(SYP_WAVEFORM)))
        wave = v->GetInt();
    if ((v = instr->FindVariable(SYP_MACROSHAPE)))
        mshape = v->GetInt();
    if ((v = instr->FindVariable(SYP_ATTACK)))
        atk = v->GetInt();
    if ((v = instr->FindVariable(SYP_DECAY)))
        dec = v->GetInt();
    if ((v = instr->FindVariable(SYP_SUSTAIN)))
        sus = v->GetInt();
    if ((v = instr->FindVariable(SYP_RELEASE)))
        rel = v->GetInt();
    if ((v = instr->FindVariable(SYP_LFORATE)))
        rate = v->GetInt();
    if ((v = instr->FindVariable(SYP_LFODEPTH)))
        depth = v->GetInt();
    if ((v = instr->FindVariable(SYP_SIZE)))
        sz = v->GetInt();
    if ((v = instr->FindVariable(SYP_MULT)))
        mult = v->GetInt();
    if ((v = instr->FindVariable(SYP_WARP)))
        warp = v->GetInt();
    if ((v = instr->FindVariable(SYP_SCAN)))
        scan = v->GetInt();
    if ((v = instr->FindVariable(SYP_TIMBRE)))
        tmb = v->GetInt();
    if ((v = instr->FindVariable(SYP_COLOR)))
        col = v->GetInt();
    if ((v = instr->FindVariable(SYP_DEGRADE)))
        deg = v->GetInt();
    if ((v = instr->FindVariable(SYP_REDUX)))
        red = v->GetInt();
    if ((v = instr->FindVariable(SYP_AMP)))
        amp = v->GetInt();
    if ((v = instr->FindVariable(SYP_LIM)))
        lim = v->GetInt();
    return (mode & 0x3) ^ ((wave & 0xF) << 2) ^ ((mshape & 0xF) << 6) ^
           ((atk & 0xFF) << 4) ^ ((dec & 0xFF) << 12) ^ ((sus & 0xFF) << 20) ^
           ((rel & 0xFF) << 7) ^ ((rate & 0xFF) << 15) ^
           ((depth & 0xFF) << 23) ^ ((sz & 0xFF) << 2) ^ ((mult & 0xFF) << 10) ^
           ((warp & 0xFF) << 18) ^ ((scan & 0xFF) << 24) ^ ((tmb & 0xFF) << 3) ^
           ((col & 0xFF) << 11) ^ ((deg & 0xFF) << 19) ^ ((red & 0xFF) << 21) ^
           ((amp & 0xFF) << 5) ^ ((lim & 0xF) << 28);
}

void PhraseView::drawSynthOverlay(int px, int py, int pw, int ph,
                                  I_Instrument *instr) {
    AppWindow &aw = (AppWindow &)w_;
    SynthInstrument *sy = (SynthInstrument *)instr;
    int key = synthPreviewKey(instr);

    if (overlayValid_ && overlayDrawnSynthKey_ == key &&
        overlayDrawnSample_ == -2) {
        return;
    }

    sy->FillPhrasePreview(synthWaveBin_, synthLfoBin_, kWaveBins);

    GUIRect stripDirty(px - 2, py, px + pw + 2, py + ph);
    aw.BeginBatchDraw();

    aw.FillRect(stripDirty, CD_BACKGROUND);
    GUIRect leftRail(px - 2, py, px, py + ph);
    GUIRect rightRail(px + pw, py, px + pw + 2, py + ph);
    aw.FillRect(leftRail, CD_BORDER);
    aw.FillRect(rightRail, CD_BORDER);

    const int midY = py + ph / 2;
    const int maxAmp = ph / 2 - 1;
    if (maxAmp < 1) {
        aw.EndBatchDraw(stripDirty);
        return;
    }

    // Center line
    GUIRect mid(px, midY, px + pw, midY + 1);
    aw.FillRect(mid, CD_BORDER);

    // Oscillator * ADSR as bipolar columns.
    for (int b = 0; b < kWaveBins; b++) {
        int x0 = px + (int)((long)b * pw / kWaveBins);
        int x1 = px + (int)((long)(b + 1) * pw / kWaveBins);
        if (x1 <= x0) {
            x1 = x0 + 1;
        }
        int amp = ((int)synthWaveBin_[b] * maxAmp) / 127;
        if (amp == 0 && synthWaveBin_[b] != 0) {
            amp = (synthWaveBin_[b] > 0) ? 1 : -1;
        }
        int y0 = midY;
        int y1 = midY - amp;
        if (y1 < y0) {
            int tmp = y0;
            y0 = y1;
            y1 = tmp;
        }
        y1 += 1;
        if (y0 < py)
            y0 = py;
        if (y1 > py + ph)
            y1 = py + ph;
        if (y1 <= y0) {
            continue;
        }
        GUIRect column(x0, y0, x1, y1);
        aw.FillRect(column, CD_HILITE1);
    }

    // LFO on top — thin near-white (FOREGROUND) 1px stroke.
    int prevX = -1;
    int prevY = -1;
    for (int b = 0; b < kWaveBins; b++) {
        int x = px + (int)((long)b * pw / kWaveBins) +
                (int)((long)pw / (kWaveBins * 2));
        int amp = ((int)synthLfoBin_[b] * maxAmp) / 127;
        int y = midY - amp;
        if (y < py)
            y = py;
        if (y >= py + ph)
            y = py + ph - 1;
        GUIRect dot(x, y, x + 1, y + 1);
        aw.FillRect(dot, CD_NORMAL);
        if (prevX >= 0) {
            // Connect bins with a 1px Bresenham-ish fill so the stroke reads.
            int x0 = prevX;
            int y0 = prevY;
            int x1 = x;
            int y1 = y;
            int dx = (x1 > x0) ? (x1 - x0) : (x0 - x1);
            int dy = (y1 > y0) ? (y1 - y0) : (y0 - y1);
            int sx = (x0 < x1) ? 1 : -1;
            int sy = (y0 < y1) ? 1 : -1;
            int err = dx - dy;
            while (true) {
                GUIRect p(x0, y0, x0 + 1, y0 + 1);
                aw.FillRect(p, CD_NORMAL);
                if (x0 == x1 && y0 == y1) {
                    break;
                }
                int e2 = 2 * err;
                if (e2 > -dy) {
                    err -= dy;
                    x0 += sx;
                }
                if (e2 < dx) {
                    err += dx;
                    y0 += sy;
                }
            }
        }
        prevX = x;
        prevY = y;
    }

    aw.EndBatchDraw(stripDirty);

    overlayValid_ = true;
    overlayDrawnSample_ = -2; // synth marker (not a sample index)
    overlayDrawnStart_ = -1;
    overlayDrawnEnd_ = -1;
    overlayDrawnSlices_ = -1;
    overlayDrawnSlice_ = -2;
    overlayDrawnSliceToEnd_ = false;
    overlayDrawnSynthKey_ = key;
}

void PhraseView::processSelectionButtonMask(unsigned short mask) {

    Player *player = Player::GetInstance();

    // B modifier

    if (mask & EPBM_B) {
        if (mask & EPBM_L) {
            extendSelection();
        } else if (mask & EPBM_R) {
            interpolateSelection();
        } else {
            copySelection();
        }
    } else {

        // A Modifer

        if (mask & EPBM_A) {

            if (mask & EPBM_DOWN)
                updateSelectionValue(VUD_DOWN);
            if (mask & EPBM_UP)
                updateSelectionValue(VUD_UP);
            if (mask & EPBM_LEFT)
                updateSelectionValue(VUD_LEFT);
            if (mask & EPBM_RIGHT)
                updateSelectionValue(VUD_RIGHT);

            if (mask & EPBM_L)
                cutSelection();
            if (mask & EPBM_R)
                switchSoloMode();
        } else {

            // R Modifier

            if (mask & EPBM_R) {
                if (mask & EPBM_LEFT) {
                    ViewType vt = VT_CHAIN;
                    ViewEvent ve(VET_SWITCH_VIEW, &vt);
                    SetChanged();
                    NotifyObservers(&ve);
                }
                if (mask & EPBM_RIGHT) {
                    unsigned char *c = phrase_->instr_ +
                                       (16 * viewData_->currentPhrase_ + row_);
                    if (*c != 0xFF) {
                        viewData_->currentInstrument_ = *c;
                    } else {
                        int nearest = findClosestInstrumentFor(row_);
                        if (nearest >= 0) {
                            viewData_->currentInstrument_ = nearest;
                        } else viewData_->currentInstrument_= lastInstr_;
                    }
                    ViewType vt = VT_INSTRUMENT;
                    ViewEvent ve(VET_SWITCH_VIEW, &vt);
                    SetChanged();
                    NotifyObservers(&ve);
                }
                if (mask & EPBM_START) {
                    player->OnStartButton(PM_PHRASE, viewData_->songX_, true,
                                          viewData_->chainRow_);
                }
                if (mask & EPBM_L)
                    unMuteAll();

            } else {
                // L Modifier
                if (mask & EPBM_L) {

                } else {
                    // No modifier

                    if (mask & EPBM_DOWN)
                        updateCursor(0, 1);
                    if (mask & EPBM_UP)
                        updateCursor(0, -1);
                    if (mask & EPBM_LEFT)
                        updateCursor(-1, 0);
                    if (mask & EPBM_RIGHT)
                        updateCursor(1, 0);
                    if (mask & EPBM_START) {
                        player->OnStartButton(PM_PHRASE, viewData_->songX_,
                                              false, viewData_->chainRow_);
                    }
                }
            }
        }
    }
};

void PhraseView::setTextProps(GUITextProperties &props, int row, int col,
                              bool restore) {

    bool invert = false;

    if (clipboard_.active_) {
        GUIRect selRect = getSelectionRect();
        if ((row >= selRect.Left()) && (row <= selRect.Right()) &&
            (col >= selRect.Top()) && (col <= selRect.Bottom())) {
            invert = true;
        }
    } else {
        if ((col_ == row) && (row_ == col)) {
            invert = true;
        }
    }

    if (invert) {
        if (restore) {
            SetColor(CD_NORMAL);
            props.invert_ = false;
        } else {
            SetColor(CD_HILITE2);
            props.invert_ = true;
        }
    }
};

void PhraseView::DrawView() {

    Clear();
    overlayValid_ = false; // Clear() wipes the pixel strip; redraw on next Flush
    stickyPlayingSlice_ = -1;
    View::EnableNotification();

    GUITextProperties props;
    GUIPoint pos = GetTitlePosition();

    // Draw title

    char title[20];

    SetColor(CD_NORMAL);
    sprintf(title, "Phrase %2.2x", viewData_->currentPhrase_);
    DrawString(pos._x, pos._y, title, props);

    // Compute song grid location

    GUIPoint anchor = GetAnchor();

    // Display row numbers

    char buffer[6];
    pos = anchor;
    pos._x -= 3;
    for (int j = 0; j < 16; j++) {
        ((j / altRowNumber_) % 2) ? SetColor(CD_ROW) : SetColor(CD_ROW2);
        hex2char(j, buffer);
        DrawString(pos._x, pos._y, buffer, props);
        pos._y++;
    }

    // Draw column titles
    pos = anchor;
    pos._y -= 1;
    SetColor(CD_COL_TITLE);
    DrawString(pos._x, pos._y, "Nt", props);
    DrawString(pos._x + 4, pos._y, "I", props);
    DrawString(pos._x + 8, pos._y, "Cmd1", props);
    DrawString(pos._x + 13, pos._y, "", props);
    DrawString(pos._x + 18, pos._y, "Cmd2", props);
    DrawString(pos._x + 23, pos._y, "", props);

    SetColor(CD_NORMAL);

    pos = anchor;

    // Display notes

    unsigned char *data = phrase_->note_ + (16 * viewData_->currentPhrase_);

    buffer[4] = 0;
    for (int j = 0; j < 16; j++) {
        unsigned char d = *data++;
        setTextProps(props, 0, j, false);
        (0 == j || 4 == j || 8 == j || 12 == j) ? SetColor(CD_MAJORBEAT)
                                                : SetColor(CD_NORMAL);
        if (d == 0xFF) {
            DrawString(pos._x, pos._y, "----", props);
        } else {
            note2char(d, buffer);
            DrawString(pos._x, pos._y, buffer, props);
        }
        setTextProps(props, 0, j, true);
        pos._y++;
    }

    // Draw instruments

    pos = anchor;
    pos._x += 4;

    data = phrase_->instr_ + (16 * viewData_->currentPhrase_);
    buffer[0] = 'I';
    buffer[3] = 0;

    for (int j = 0; j < 16; j++) {
        unsigned char d = *data++;
        setTextProps(props, 1, j, false);
        (0 == j || 4 == j || 8 == j || 12 == j) ? SetColor(CD_MAJORBEAT)
                                                : SetColor(CD_NORMAL);
        if (d == 0xFF) {
            SetColor(CD_NORMAL);
            DrawString(pos._x, pos._y, "I", props);
            DrawString(pos._x + 1, pos._y, "--", props);
        } else {
            hex2char(d, buffer + 1);
            DrawString(pos._x, pos._y, buffer, props);
            if (j == row_ && (col_ == 0 || col_ == 1)) {
                SetColor(CD_NORMAL);
                sprintf(buffer, "I%2.2x: ", d);
                std::string instrLine = buffer;
                setTextProps(props, 1, j, true);
                GUIPoint location = GetTitlePosition();
                location._x += 12;
                InstrumentBank *bank = viewData_->project_->GetInstrumentBank();
                I_Instrument *instr = bank->GetInstrument(d);
                instrLine += instr->GetName();
                DrawString(location._x, location._y, instrLine.c_str(), props);
            }
        }
        setTextProps(props, 1, j, true);
        pos._y++;
    }

    // Draw command 1

    pos = anchor;
    pos._x += 8;

    FourCC *f = phrase_->cmd1_ + (16 * viewData_->currentPhrase_);

    buffer[4] = 0;

    for (int j = 0; j < 16; j++) {
        FourCC command = *f++;
        fourCC2char(command, buffer);
        setTextProps(props, 2, j, false);
        (0 == j || 4 == j || 8 == j || 12 == j) ? SetColor(CD_MAJORBEAT)
                                                : SetColor(CD_NORMAL);
        DrawString(pos._x, pos._y, buffer, props);
        setTextProps(props, 2, j, true);
        pos._y++;
        if (j == row_ && col_ == 2) {
            printHelpLegend(command, props);
        }
    }

    // Draw commands params 1

    pos = anchor;
    pos._x += 13;

    ushort *param = phrase_->param1_ + (16 * viewData_->currentPhrase_);
    buffer[5] = 0;

    for (int j = 0; j < 16; j++) {
        ushort p = *param++;
        setTextProps(props, 3, j, false);
        /*		if (p==0xFFFF) {
                    DrawString(pos._x,pos._y,"----",props) ;
                } else {
        */ (0 == j || 4 == j || 8 == j || 12 == j) ? SetColor(CD_MAJORBEAT)
                                                   : SetColor(CD_NORMAL);
        hexshort2char(p, buffer);
        DrawString(pos._x, pos._y, buffer, props);
        /*		}
         */
        setTextProps(props, 3, j, true);
        pos._y++;
    }

    // Draw commands 2

    pos = anchor;
    pos._x += 18;

    f = phrase_->cmd2_ + (16 * viewData_->currentPhrase_);

    buffer[4] = 0;

    for (int j = 0; j < 16; j++) {
        FourCC command = *f++;
        fourCC2char(command, buffer);
        (0 == j || 4 == j || 8 == j || 12 == j) ? SetColor(CD_MAJORBEAT)
                                                : SetColor(CD_NORMAL);
        setTextProps(props, 4, j, false);
        DrawString(pos._x, pos._y, buffer, props);
        setTextProps(props, 4, j, true);
        pos._y++;
        if (j == row_ && col_ == 4) {
            printHelpLegend(command, props);
        }
    }

    // Draw commands params

    pos = anchor;
    pos._x += 23;

    param = phrase_->param2_ + (16 * viewData_->currentPhrase_);
    buffer[5] = 0;

    for (int j = 0; j < 16; j++) {
        ushort p = *param++;
        setTextProps(props, 5, j, false);
        /*		if (p==0xFFFF) {
                    DrawString(pos._x,pos._y,"----",props) ;
                } else {
        */ (0 == j || 4 == j || 8 == j || 12 == j) ? SetColor(CD_MAJORBEAT)
                                                   : SetColor(CD_NORMAL);
        hexshort2char(p, buffer);
        DrawString(pos._x, pos._y, buffer, props);
        /*		}
         */
        setTextProps(props, 5, j, true);
        pos._y++;
    }

    drawMap();
    drawSampleStrip();
    drawNotes();

    Player *player = Player::GetInstance();
    if (player->IsRunning()) {
        OnPlayerUpdate(PET_UPDATE);
    };

    if ((viewMode_ != VM_SELECTION) && ((col_ == 3) || (col_ == 5))) {
        cmdEditField_->SetFocus();
        cmdEditField_->Draw(w_);
    };
};

void PhraseView::OnPlayerUpdate(PlayerEventType eventType, unsigned int tick) {

    GUITextProperties props;
    // Do not ClearRect the sample strip here — that dirties char cells under the
    // pixel overlay every tick and forces expensive surface updates. The strip
    // is reserved once in DrawView.
    drawNotes();

    GUIPoint anchor = GetAnchor();
    GUIPoint pos = anchor;
    pos._x -= 1;

    SetColor(CD_NORMAL);

    pos._y = anchor._y + lastPlayingPos_;
    if (!commandSelectorModalActive_ ||
        !CommandSelectorCommon::popupContainsPoint(anchor, pos._x, pos._y)) {
        DrawString(pos._x, pos._y, " ", props);
    }

    Player *player = Player::GetInstance();

    if (eventType != PET_STOP) {

        // Clear current position if needed

        for (int i = 0; i < SONG_CHANNEL_COUNT; i++) {
            if (player->IsChannelPlaying(i)) {
                if (viewData_->currentPlayPhrase_[i] ==
                        viewData_->currentPhrase_ &&
                    viewData_->playMode_ != PM_AUDITION) {
                    pos._y = anchor._y + viewData_->phrasePlayPos_[i];
                    if (!commandSelectorModalActive_ ||
                        !CommandSelectorCommon::popupContainsPoint(
                            anchor, pos._x, pos._y)) {
                        if (!player->IsChannelMuted(i)) {
                            SetColor(CD_PLAY);
                            DrawString(pos._x, pos._y, ">", props);
                        } else {
                            SetColor(CD_MUTE);
                            DrawString(pos._x, pos._y, "-", props);
                        }
                    }
                    SetColor(CD_NORMAL);
                    lastPlayingPos_ = viewData_->phrasePlayPos_[i];
                    break;
                }
            }
        }

        // clear any live indicator
        pos._y = anchor._y;

        // Loop on all channels to see if one has queued current chain
        if (player->GetSequencerMode() == SM_LIVE) {

            for (int i = 0; i < SONG_CHANNEL_COUNT; i++) {
                // is anything queued ?
                if (player->GetQueueingMode(i) != QM_NONE) {
                    // find the chain queued in channel
                    unsigned char songPos = player->GetQueuePosition(i);
                    unsigned char *chain =
                        viewData_->song_->data_ + i + 8 * songPos;
                    if (*chain == viewData_->currentChain_) {
                        char *indicator = player->GetLiveIndicator(i);
                        if (!commandSelectorModalActive_ ||
                            !CommandSelectorCommon::popupContainsPoint(
                                anchor, pos._x, pos._y)) {
                            DrawString(pos._x, pos._y, indicator, props);
                        }
                        break;
                    }
                }
            }
        }
    }

    pos = anchor;
    pos._x += 200;

    /*	if (player->Clipped()) {
               w_.DrawString("clip",pos,props);
        } else {
               w_.DrawString("----",pos,props);
        }
    */
};

void PhraseView::printHelpLegend(FourCC command, GUITextProperties props) {
    SetColor(CD_NORMAL);
    std::string *cmdStr = getHelpLegend(command);
    DrawString(10, 0, cmdStr[0].c_str(), props);
    DrawString(10, 1, cmdStr[1].c_str(), props);
    DrawString(10, 2, cmdStr[2].c_str(), props);
}
