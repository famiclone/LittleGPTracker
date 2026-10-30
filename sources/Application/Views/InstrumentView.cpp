#include "InstrumentView.h"
#include "Application/AppWindow.h"
#include "Application/Instruments/MidiInstrument.h"
#include "Application/Instruments/SampleInstrument.h"
#include "Application/Instruments/SamplePool.h"
#include "Application/Instruments/SynthInstrument.h"
#include "Application/Model/Config.h"
#include "Application/Player/PlayerMixer.h"
#include "BaseClasses/ModalView.h"
#include "BaseClasses/UIBigHexVarField.h"
#include "BaseClasses/UIIntVarOffField.h"
#include "BaseClasses/UINoteVarField.h"
#include "BaseClasses/UIStaticField.h"
#include "Foundation/Variables/Variable.h"
#include "ModalDialogs/ImportSampleDialog.h"
#include "ModalDialogs/MessageBox.h"
#include "ModalDialogs/NewProjectDialog.h"
#include "System/Console/Trace.h"
#include "System/FileSystem/FileSystem.h"
#include "System/System/System.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *instrumentTypeNames[] = {(char *)"SAMPLER", (char *)"SYNTH",
                                      (char *)"SHAPES"};

static const char *sampleFilterLabels[] = {"ORIG", "BASSY", "SCREAM"};
static const char *sampleLoopLabels[] = {"NONE", "LOOP", "PINGPONG", "OSC",
                                         "SYNC", "GRAIN"};
static const char *sampleInterpLabels[] = {"LINEAR", "NONE"};
static const char *onOffUpperLabels[] = {"OFF", "ON"};

// Modal dialog for picking a saved .ins file from root:instruments
class LoadInstrumentModal : public ModalView {
  public:
    LoadInstrumentModal(View &view)
        : ModalView(view), selected_(0), topIndex_(0) {}
    virtual ~LoadInstrumentModal() {}

    virtual void OnFocus() {
        InstrumentView::ListInstrumentFiles(files_);
        selected_ = 0;
        topIndex_ = 0;
    }

    virtual void OnPlayerUpdate(PlayerEventType, unsigned int) {}

    virtual void DrawView() {
        const int w = 24;
        const int visRows = 8;
        SetWindow(w, visRows + 3);

        GUITextProperties props;
        SetColor(CD_HILITE2);
        DrawString(1, 0, "LOAD INSTRUMENT (.INS)", props);
        SetColor(CD_NORMAL);

        if (files_.empty()) {
            DrawString(1, 2, "(no .ins files)", props);
            DrawString(1, visRows + 2, "B=cancel", props);
            return;
        }

        for (int row = 0; row < visRows; row++) {
            int idx = topIndex_ + row;
            if (idx >= (int)files_.size())
                break;
            props.invert_ = (idx == selected_);
            SetColor(props.invert_ ? CD_HILITE2 : CD_NORMAL);
            DrawString(1, 2 + row, files_[idx].c_str(), props);
        }
        props.invert_ = false;
        SetColor(CD_NORMAL);
        DrawString(1, visRows + 2, "A=load  B=cancel", props);
    }

    virtual void ProcessButtonMask(unsigned short mask, bool pressed) {
        if (!pressed)
            return;
        if (mask == EPBM_B) {
            EndModal(0);
            return;
        }
        if (files_.empty())
            return;
        if (mask == EPBM_A) {
            EndModal(1);
            return;
        }
        const int visRows = 8;
        if (mask == EPBM_UP) {
            if (selected_ > 0)
                selected_--;
            if (selected_ < topIndex_)
                topIndex_ = selected_;
            isDirty_ = true;
        }
        if (mask == EPBM_DOWN) {
            if (selected_ < (int)files_.size() - 1)
                selected_++;
            if (selected_ >= topIndex_ + visRows)
                topIndex_ = selected_ - visRows + 1;
            isDirty_ = true;
        }
    }

    const char *GetSelection() const {
        if (files_.empty() || selected_ < 0 ||
            selected_ >= (int)files_.size())
            return 0;
        return files_[selected_].c_str();
    }

  private:
    std::vector<std::string> files_;
    int selected_;
    int topIndex_;
};

void ImportSampleDialogCallback(View &v, ModalView &dialog) {
    ((InstrumentView &)v).OnFocus();
}

static void NameInstrumentCallback(View &v, ModalView &dialog) {
    NewProjectDialog &npd = (NewProjectDialog &)dialog;
    if (dialog.GetReturnCode() > 0) {
        std::string name = npd.GetName();
        ((InstrumentView &)v).OnSetInstrumentName(name.c_str());
    }
}

static void SaveInstrumentCallback(View &v, ModalView &dialog) {
    NewProjectDialog &npd = (NewProjectDialog &)dialog;
    if (dialog.GetReturnCode() > 0) {
        std::string name = npd.GetName();
        ((InstrumentView &)v).OnSaveInstrumentFile(name.c_str());
    }
}

static void LoadInstrumentCallback(View &v, ModalView &dialog) {
    LoadInstrumentModal &lim = (LoadInstrumentModal &)dialog;
    if (dialog.GetReturnCode() > 0) {
        const char *name = lim.GetSelection();
        if (name) {
            ((InstrumentView &)v).OnLoadInstrumentFile(name);
        }
    }
}

bool InstrumentView::EnsureInstrumentsDir() {
    Path dir("root:instruments");
    std::string resolved = dir.GetPath();
    Path resolvedPath(resolved);
    if (resolvedPath.Exists() && resolvedPath.IsDirectory())
        return true;
    Result r = FileSystem::GetInstance()->MakeDir(resolved.c_str());
    return !r.Failed();
}

void InstrumentView::ListInstrumentFiles(std::vector<std::string> &out) {
    out.clear();
    Path dir("root:instruments");
    std::string resolved = dir.GetPath();
    Path resolvedPath(resolved);
    if (!resolvedPath.Exists() || !resolvedPath.IsDirectory())
        return;
    I_Dir *d = FileSystem::GetInstance()->Open(resolved.c_str());
    if (!d)
        return;
    d->GetContent("*.ins");
    d->Sort();
    IteratorPtr<Path> it(d->GetIterator());
    for (it->Begin(); !it->IsDone(); it->Next()) {
        Path &p = it->CurrentItem();
        if (!p.IsFile())
            continue;
        std::string n = p.GetName();
        if (n.size() > 4 && n.compare(n.size() - 4, 4, ".ins") == 0)
            n = n.substr(0, n.size() - 4);
        out.push_back(n);
    }
    delete d;
}

InstrumentView::InstrumentView(GUIWindow &w, ViewData *data)
    : FieldView(w, data), paramBaseY_(7), nameField_(0), playField_(0),
      loadField_(0), saveField_(0), startMarkerField_(0), endMarkerField_(0),
      isAuditioning_(false), auditionChannel_(-1),
      inProcessButton_(false), pendingInstrumentChange_(false),
      barItemCount_(0), waveCacheSample_(-1), waveCacheFrames_(-1),
      overlayValid_(false), overlayDrawnSample_(-1), overlayDrawnStart_(-1),
      overlayDrawnEnd_(-1), overlayDrawnSlices_(-1),
      overlayDrawnFocusMarker_(0), overlayDrawnSynthKey_(0),
      overlayDrawnBarsKey_(0) {

    project_ = data->project_;
    lastFocusID_ = 0;
    current_ = 0;
    strcpy(nameLabelBuf_, "------------");
    strcpy(playLabelBuf_, "PLAY");
    typeVar_ = new Variable("type", IVP_TYPE, instrumentTypeNames, 3, 0);
    startMarkerVar_ = new Variable("startMarker", IVP_START_MARKER, 0);
    endMarkerVar_ = new Variable("endMarker", IVP_END_MARKER, 0);
    onInstrumentChange();
}

InstrumentView::~InstrumentView() {
    stopInstrumentAudition();
    delete typeVar_;
    delete startMarkerVar_;
    delete endMarkerVar_;
}

GUIPoint InstrumentView::paramAnchor() const {
    return GUIPoint(1, paramBaseY_);
}

InstrumentType InstrumentView::getInstrumentType() {
    int i = viewData_->currentInstrument_;
    InstrumentBank *bank = viewData_->project_->GetInstrumentBank();
    I_Instrument *instrument = bank->GetInstrument(i);
    return instrument->GetType();
}

void InstrumentView::addBarItem(int charX, int charY, int charW, FourCC varId,
                                int maxVal, bool centered) {
    if (barItemCount_ >= kMaxBarItems) {
        return;
    }
    BarItem &b = barItems_[barItemCount_++];
    b.charX_ = charX;
    b.charY_ = charY;
    b.charW_ = charW;
    b.varId_ = varId;
    b.maxVal_ = (maxVal > 0) ? maxVal : 255;
    b.centered_ = centered;
}

bool InstrumentView::isActionField(const UIField *field) const {
    return (field == nameField_ || field == playField_ ||
            field == loadField_ || field == saveField_);
}

void InstrumentView::refreshPlayFieldText() {
    if (isAuditioning_ && auditionChannel_ >= 0 &&
        PlayerMixer::GetInstance()->GetInstrument(auditionChannel_) ==
            current_) {
        strcpy(playLabelBuf_, "STOP");
    } else {
        isAuditioning_ = false;
        auditionChannel_ = -1;
        strcpy(playLabelBuf_, "PLAY");
    }
    if (playField_) {
        playField_->SetName(playLabelBuf_);
    }
}

void InstrumentView::stopInstrumentAudition() {
    if (!isAuditioning_ && auditionChannel_ < 0) {
        return;
    }
    int ch = (auditionChannel_ >= 0 && auditionChannel_ < SONG_CHANNEL_COUNT)
                 ? auditionChannel_
                 : 0;
    PlayerMixer *pm = PlayerMixer::GetInstance();
    pm->Lock();
    if (pm->GetInstrument(ch) == current_) {
        pm->StopInstrument(ch);
    }
    pm->Unlock();
    isAuditioning_ = false;
    auditionChannel_ = -1;
    refreshPlayFieldText();
    isDirty_ = true;
}

void InstrumentView::toggleInstrumentAudition(bool forceRetrigger) {
    if (!current_) {
        return;
    }
    PlayerMixer *pm = PlayerMixer::GetInstance();
    int ch = viewData_->songX_;
    if (ch < 0 || ch >= SONG_CHANNEL_COUNT) {
        ch = 0;
    }
    bool activeOnCh = (isAuditioning_ && auditionChannel_ >= 0 &&
                       pm->GetInstrument(auditionChannel_) == current_);
    if (activeOnCh && !forceRetrigger) {
        stopInstrumentAudition();
        SetNotification("Audition: STOP");
        return;
    }
    if (isAuditioning_ && auditionChannel_ >= 0 && auditionChannel_ != ch) {
        stopInstrumentAudition();
    }
    int note = 60;
    Variable *rv = current_->FindVariable(
        (current_->GetType() == IT_SYNTH) ? SYP_ROOTNOTE : SIP_ROOTNOTE);
    if (rv) {
        note = rv->GetInt();
        if (note < 0 || note > 127) {
            note = 60;
        }
    }
    pm->Lock();
    pm->StopInstrument(ch);
    pm->StartInstrument(ch, current_, (unsigned char)note, true);
    pm->Unlock();
    isAuditioning_ = true;
    auditionChannel_ = ch;
    refreshPlayFieldText();
    SetNotification("Audition: PLAY");
    isDirty_ = true;
}

void InstrumentView::refreshNameFieldText() {
    if (!current_) {
        strcpy(nameLabelBuf_, "------------");
    } else {
        const char *un = current_->GetUserName();
        if (un && un[0]) {
            int len = (int)strlen(un);
            if (len > 12)
                len = 12;
            memcpy(nameLabelBuf_, un, len);
            for (int k = len; k < 12; k++) {
                nameLabelBuf_[k] = '-';
            }
            nameLabelBuf_[12] = '\0';
        } else {
            strcpy(nameLabelBuf_, "------------");
        }
    }
    if (nameField_) {
        nameField_->SetName(nameLabelBuf_);
    }
}

void InstrumentView::OnSetInstrumentName(const char *name) {
    if (!current_)
        return;
    current_->SetUserName(name);
    refreshNameFieldText();
    overlayValid_ = false;
    isDirty_ = true;
}

void InstrumentView::OnSaveInstrumentFile(const char *name) {
    if (!current_ || !name || !name[0])
        return;
    if (!EnsureInstrumentsDir()) {
        SetNotification("Cannot create instruments/");
        return;
    }
    if (current_->GetUserName()[0] == '\0') {
        current_->SetUserName(name);
        refreshNameFieldText();
    }

    Path filePath =
        Path("root:instruments").Descend(std::string(name) + ".ins");
    I_File *f = FileSystem::GetInstance()->Open(filePath.GetPath().c_str(), "w");
    if (!f) {
        SetNotification("Save .ins failed");
        return;
    }

    std::string content = "# Swine Tracker Instrument (.ins)\n";
    InstrumentType it = current_->GetType();
    const char *typeStr = "SAMPLER";
    if (it == IT_SYNTH) {
        SynthInstrument *sy = (SynthInstrument *)current_;
        typeStr = (sy->GetEngineMode() == SYM_SHAPES) ? "SHAPES" : "SYNTH";
    } else if (it == IT_MIDI) {
        typeStr = "MIDI";
    }
    content += std::string("type=") + typeStr + "\n";
    content += std::string("name=") + current_->GetUserName() + "\n";

    IteratorPtr<Variable> vit(current_->GetIterator());
    for (vit->Begin(); !vit->IsDone(); vit->Next()) {
        Variable &v = vit->CurrentItem();
        content += std::string(v.GetName()) + "=" + v.GetString() + "\n";
    }

    f->Write(content.c_str(), 1, (int)content.size());
    f->Close();
    delete f;

    std::string msg = std::string("Saved ") + name + ".ins";
    SetNotification(msg.c_str());
    overlayValid_ = false;
    isDirty_ = true;
}

void InstrumentView::OnLoadInstrumentFile(const char *name) {
    if (!name || !name[0])
        return;
    Path filePath =
        Path("root:instruments").Descend(std::string(name) + ".ins");
    I_File *f = FileSystem::GetInstance()->Open(filePath.GetPath().c_str(), "r");
    if (!f) {
        SetNotification("Load .ins failed");
        return;
    }

    char buf[4096];
    memset(buf, 0, sizeof(buf));
    int nRead = f->Read(buf, 1, sizeof(buf) - 1);
    f->Close();
    delete f;
    if (nRead <= 0) {
        SetNotification("Empty .ins file");
        return;
    }

    int instIdx = viewData_->currentInstrument_;
    InstrumentBank *bank = viewData_->project_->GetInstrumentBank();

    InstrumentType wantType = getInstrumentType();
    int wantSynthMode = -1;
    std::string loadedUserName = name;

    char *savePtr = 0;
    char *line = strtok_r(buf, "\r\n", &savePtr);
    std::vector<std::pair<std::string, std::string> > kvPairs;
    while (line) {
        while (*line == ' ' || *line == '\t')
            line++;
        if (*line != '\0' && *line != '#') {
            char *eq = strchr(line, '=');
            if (eq) {
                std::string k(line, eq - line);
                std::string val(eq + 1);
                if (k == "type") {
                    if (val == "SHAPES" || val == "shapes" ||
                        val == "MACROSYN") {
                        wantType = IT_SYNTH;
                        wantSynthMode = SYM_SHAPES;
                    } else if (val == "SYNTH" || val == "WAVSYNTH" ||
                               val == "Synth" || val == "synth") {
                        wantType = IT_SYNTH;
                        wantSynthMode = SYM_SYNTH;
                    } else if (val == "SAMPLER" || val == "Sample" ||
                               val == "sample") {
                        wantType = IT_SAMPLE;
                    }
                } else if (k == "name") {
                    if (!val.empty()) {
                        loadedUserName = val;
                    }
                } else {
                    kvPairs.push_back(std::make_pair(k, val));
                }
            }
        }
        line = strtok_r(0, "\r\n", &savePtr);
    }

    if (instIdx < MAX_SAMPLEINSTRUMENT_COUNT &&
        (wantType == IT_SAMPLE || wantType == IT_SYNTH) &&
        getInstrumentType() != wantType) {
        if (current_) {
            current_->RemoveObserver(*this);
            current_ = 0;
        }
        bank->SetInstrumentType(instIdx, wantType);
    }

    I_Instrument *instr = bank->GetInstrument(instIdx);
    if (instr) {
        instr->SetUserName(loadedUserName.c_str());
        for (size_t p = 0; p < kvPairs.size(); p++) {
            const std::string &k = kvPairs[p].first;
            const std::string &val = kvPairs[p].second;
            IteratorPtr<Variable> vit(instr->GetIterator());
            for (vit->Begin(); !vit->IsDone(); vit->Next()) {
                Variable &v = vit->CurrentItem();
                if (k == v.GetName()) {
                    v.SetString(val.c_str(), false);
                    break;
                }
            }
        }
        if (instr->GetType() == IT_SYNTH && wantSynthMode >= 0) {
            ((SynthInstrument *)instr)->SetEngineMode(wantSynthMode);
        }
        instr->Init();
    }

    onInstrumentChange();
    ((AppWindow &)w_).Clear(true);
    std::string msg = std::string("Loaded ") + name + ".ins";
    SetNotification(msg.c_str());
    isDirty_ = true;
}

void InstrumentView::onInstrumentChange() {

    ClearFocus();
    nameField_ = 0;
    playField_ = 0;
    loadField_ = 0;
    saveField_ = 0;
    startMarkerField_ = 0;
    endMarkerField_ = 0;
    barItemCount_ = 0;
    overlayValid_ = false;

    I_Instrument *old = current_;

    int i = viewData_->currentInstrument_;
    InstrumentBank *bank = viewData_->project_->GetInstrumentBank();
    current_ = bank->GetInstrument(i);

    if (old && old != current_) {
        stopInstrumentAudition();
        old->RemoveObserver(*this);
    }
    T_SimpleList<UIField>::Empty();

    InstrumentType it = getInstrumentType();

    if (it == IT_SYNTH && current_) {
        int m = ((SynthInstrument *)current_)->GetEngineMode();
        typeVar_->SetInt((m == SYM_SHAPES) ? 2 : 1, false);
    } else if (it == IT_SAMPLE) {
        typeVar_->SetInt(0, false);
    }

    switch (it) {
    case IT_MIDI:
        fillMidiParameters();
        break;
    case IT_SAMPLE:
        fillSampleParameters();
        break;
    case IT_SYNTH:
        fillSynthParameters();
        break;
    case IT_LAST:
        break;
    }

    SetFocus(T_SimpleList<UIField>::GetFirst());
    IteratorPtr<UIField> it2(T_SimpleList<UIField>::GetIterator());
    for (it2->Begin(); !it2->IsDone(); it2->Next()) {
        UIField &raw = it2->CurrentItem();
        if (raw.IsStatic() || isActionField(&raw)) {
            continue;
        }
        UIIntVarField &field = (UIIntVarField &)raw;
        if (field.GetVariableID() == lastFocusID_) {
            SetFocus(&field);
            break;
        }
    }
    if (current_ && current_ != old) {
        current_->AddObserver(*this);
    }
}

void InstrumentView::applyTypeChangeIfNeeded() {
    int i = viewData_->currentInstrument_;
    if (i >= MAX_SAMPLEINSTRUMENT_COUNT) {
        return;
    }
    InstrumentType current = getInstrumentType();
    if (current != IT_SAMPLE && current != IT_SYNTH) {
        return;
    }
    int tv = typeVar_->GetInt();
    InstrumentType want = (tv == 0) ? IT_SAMPLE : IT_SYNTH;
    int wantSynthMode = (tv == 2) ? SYM_SHAPES : SYM_SYNTH;

    InstrumentBank *bank = viewData_->project_->GetInstrumentBank();
    if (current == want) {
        if (current == IT_SYNTH && current_) {
            SynthInstrument *sy = (SynthInstrument *)current_;
            if (sy->GetEngineMode() != wantSynthMode) {
                stopInstrumentAudition();
                sy->SetEngineMode(wantSynthMode);
                onInstrumentChange();
                ((AppWindow &)w_).Clear(true);
                isDirty_ = true;
            }
        }
        return;
    }

    stopInstrumentAudition();
    std::string savedName = current_ ? current_->GetUserName() : "";
    if (current_) {
        current_->RemoveObserver(*this);
        current_ = 0;
    }
    bank->SetInstrumentType(i, want);
    I_Instrument *newInstr = bank->GetInstrument(i);
    if (newInstr) {
        if (!savedName.empty()) {
            newInstr->SetUserName(savedName.c_str());
        }
        if (want == IT_SYNTH) {
            ((SynthInstrument *)newInstr)->SetEngineMode(wantSynthMode);
        }
    }
    onInstrumentChange();
    ((AppWindow &)w_).Clear(true);
    isDirty_ = true;
}

void InstrumentView::applySlicesUiIfNeeded() {
    if (getInstrumentType() != IT_SAMPLE || !current_) {
        return;
    }
    Variable *slices = current_->FindVariable(SIP_SLICES);
    if (!slices) {
        return;
    }
    bool want = slices->GetInt() > 1;
    bool have = false;
    IteratorPtr<UIField> it(T_SimpleList<UIField>::GetIterator());
    for (it->Begin(); !it->IsDone(); it->Next()) {
        UIField &raw = it->CurrentItem();
        if (raw.IsStatic() || isActionField(&raw)) {
            continue;
        }
        if (((UIIntVarField &)raw).GetVariableID() == SIP_SLICETOEND) {
            have = true;
            break;
        }
    }
    if (want != have) {
        onInstrumentChange();
        isDirty_ = true;
    }
}

void InstrumentView::syncSampleRangeFromMarkers() {
    if (getInstrumentType() != IT_SAMPLE || !current_) {
        return;
    }
    SampleInstrument *si = (SampleInstrument *)current_;
    int nFrames = si->GetSampleSize();
    if (nFrames <= 0) {
        return;
    }
    Variable *startVar = si->FindVariable(SIP_START);
    Variable *loopStartVar = si->FindVariable(SIP_LOOPSTART);
    Variable *endVar = si->FindVariable(SIP_END);
    if (!startVar || !loopStartVar || !endVar) {
        return;
    }

    int curStart = startVar->GetInt();
    int curEnd = endVar->GetInt();
    if (curEnd <= 0 || curEnd > nFrames) {
        curEnd = nFrames;
    }
    if (curStart < 0) {
        curStart = 0;
    }
    if (curStart >= curEnd) {
        curStart = curEnd > 1 ? curEnd - 1 : 0;
    }

    int wantStart = startMarkerVar_->GetInt();
    int wantEnd = endMarkerVar_->GetInt();

    UIField *focus = GetFocus();
    if (focus == startMarkerField_) {
        if (wantStart < 0) {
            wantStart = 0;
        }
        if (wantStart >= curEnd) {
            wantStart = curEnd > 1 ? curEnd - 1 : 0;
        }
        startMarkerVar_->SetInt(wantStart, false);
        if (wantStart != curStart || loopStartVar->GetInt() != wantStart) {
            startVar->SetInt(wantStart, false);
            loopStartVar->SetInt(wantStart, false);
            overlayValid_ = false;
            isDirty_ = true;
        }
    } else if (focus == endMarkerField_) {
        if (wantEnd <= curStart) {
            wantEnd = curStart + 1;
        }
        if (wantEnd > nFrames) {
            wantEnd = nFrames;
        }
        endMarkerVar_->SetInt(wantEnd, false);
        if (wantEnd != curEnd) {
            endVar->SetInt(wantEnd, false);
            overlayValid_ = false;
            isDirty_ = true;
        }
    } else {
        startMarkerVar_->SetInt(curStart, false);
        endMarkerVar_->SetInt(curEnd, false);
    }
}

void InstrumentView::fillHeaderParameters() {
    // Row 7: TYPE   SAMPLER|SYNTH|SHAPES   PLAY LOAD SAVE
    GUIPoint pos(1, paramBaseY_);
    T_SimpleList<UIField>::Insert(new UIStaticField(pos, "TYPE"));

    pos._x = 11;
    UIIntVarField *tf = new UIIntVarField(pos, *typeVar_, "%s", 0, 2, 1, 1);
    T_SimpleList<UIField>::Insert(tf);

    refreshPlayFieldText();
    pos._x = 21;
    playField_ = new UIActionField(playLabelBuf_, IV_ACTION_PLAY, pos);
    playField_->AddObserver(*this);
    T_SimpleList<UIField>::Insert(playField_);

    pos._x = 27;
    loadField_ = new UIActionField("LOAD", IV_ACTION_LOAD, pos);
    loadField_->AddObserver(*this);
    T_SimpleList<UIField>::Insert(loadField_);

    pos._x = 33;
    saveField_ = new UIActionField("SAVE", IV_ACTION_SAVE, pos);
    saveField_->AddObserver(*this);
    T_SimpleList<UIField>::Insert(saveField_);

    // Row 8: NAME   ------------
    pos._x = 1;
    pos._y = paramBaseY_ + 1;
    T_SimpleList<UIField>::Insert(new UIStaticField(pos, "NAME"));

    refreshNameFieldText();
    pos._x = 11;
    nameField_ = new UIActionField(nameLabelBuf_, IV_ACTION_NAME, pos);
    nameField_->AddObserver(*this);
    T_SimpleList<UIField>::Insert(nameField_);

    // Row 9: TRANSP. ON   TBL. --   AUTO OFF
    if (current_) {
        pos._x = 1;
        pos._y = paramBaseY_ + 2;
        T_SimpleList<UIField>::Insert(new UIStaticField(pos, "TRANSP."));

        Variable *trsp = current_->FindVariable(
            (current_->GetType() == IT_SYNTH) ? SYP_TRANSPOSE : SIP_TRANSPOSE);
        if (trsp) {
            pos._x = 11;
            T_SimpleList<UIField>::Insert(
                new UIIntVarField(pos, *trsp, "%s", 0, 1, 1, 1));
        }

        pos._x = 16;
        T_SimpleList<UIField>::Insert(new UIStaticField(pos, "TBL."));
        Variable *tbl = current_->FindVariable(
            (current_->GetType() == IT_SYNTH) ? SYP_TABLE : SIP_TABLE);
        if (tbl) {
            pos._x = 21;
            T_SimpleList<UIField>::Insert(
                new UIIntVarOffField(pos, *tbl, "%2.2X", 0x00, 0x7F, 1, 0x10));
        }

        pos._x = 26;
        T_SimpleList<UIField>::Insert(new UIStaticField(pos, "AUTO"));
        Variable *tbla = current_->FindVariable(
            (current_->GetType() == IT_SYNTH) ? SYP_TABLEAUTO : SIP_TABLEAUTO);
        if (tbla) {
            pos._x = 31;
            T_SimpleList<UIField>::Insert(
                new UIIntVarField(pos, *tbla, "%s", 0, 1, 1, 1));
        }
    }
}

void InstrumentView::fillSynthParameters() {
    int i = viewData_->currentInstrument_;
    InstrumentBank *bank = viewData_->project_->GetInstrumentBank();
    SynthInstrument *instrument = (SynthInstrument *)bank->GetInstrument(i);
    bool isShapes = (instrument->GetEngineMode() == SYM_SHAPES);
    GUIPoint pos(1, paramBaseY_);

    fillHeaderParameters();

    // Row 11: SHAPE     01 <LABEL>
    int yShape = paramBaseY_ + 4;
    pos = GUIPoint(1, yShape);
    T_SimpleList<UIField>::Insert(new UIStaticField(pos, "SHAPE"));
    Variable *v = instrument->FindVariable(isShapes ? SYP_MACROSHAPE
                                                    : SYP_WAVEFORM);
    pos = GUIPoint(11, yShape);
    T_SimpleList<UIField>::Insert(new UIIntVarField(
        pos, *v, "%2.2X", 0, (isShapes ? SMS_LAST : SYW_LAST) - 1, 1, 2));

    int yGrid = paramBaseY_ + 6;
    struct RowSpec {
        const char *label;
        FourCC id;
        int maxVal;
        int coarse;
        bool hasBar;
        bool centered;
    };

    static const RowSpec leftRowsSynth[] = {
        {"SIZE", SYP_SIZE, 0xFF, 0x10, true, false},
        {"MULT", SYP_MULT, 0xFF, 0x10, true, false},
        {"WARP", SYP_WARP, 0xFF, 0x10, true, false},
        {"SCAN", SYP_SCAN, 0xFF, 0x10, true, false},
        {"FILTER", SYP_FILTMODE, SYF_LAST - 1, 1, false, false},
        {"CUTOFF", SYP_FILTCUT, 0xFF, 0x10, true, false},
        {"RES", SYP_FILTRES, 0xFF, 0x10, true, false},
    };

    static const RowSpec leftRowsShapes[] = {
        {"TIMBRE", SYP_TIMBRE, 0xFF, 0x10, true, false},
        {"COLOR", SYP_COLOR, 0xFF, 0x10, true, false},
        {"DEGRADE", SYP_DEGRADE, 0xFF, 0x10, true, false},
        {"REDUX", SYP_REDUX, 0xFF, 0x10, true, false},
        {"FILTER", SYP_FILTMODE, SYF_LAST - 1, 1, false, false},
        {"CUTOFF", SYP_FILTCUT, 0xFF, 0x10, true, false},
        {"RES", SYP_FILTRES, 0xFF, 0x10, true, false},
    };

    const RowSpec *leftRows = isShapes ? leftRowsShapes : leftRowsSynth;

    for (int r = 0; r < 7; r++) {
        int ry = yGrid + r;
        pos = GUIPoint(1, ry);
        T_SimpleList<UIField>::Insert(new UIStaticField(pos, leftRows[r].label));
        v = instrument->FindVariable(leftRows[r].id);
        if (v) {
            pos = GUIPoint(11, ry);
            const char *fmt = leftRows[r].hasBar ? "%2.2X" : "%s";
            T_SimpleList<UIField>::Insert(
                new UIIntVarField(pos, *v, fmt, 0, leftRows[r].maxVal, 1,
                                  leftRows[r].coarse));
            if (leftRows[r].hasBar) {
                addBarItem(13, ry, 6, leftRows[r].id, leftRows[r].maxVal,
                           leftRows[r].centered);
            }
        }
    }

    // Right column (rows 13..20): AMP, LIM, PAN, DRY, CHO, DEL, REV, TAPE
    static const RowSpec rightRows[] = {
        {"AMP", SYP_AMP, 0xFF, 0x10, true, false},
        {"LIM", SYP_LIM, SYL_LAST - 1, 1, false, false},
        {"PAN", SYP_PAN, 0xFE, 0x10, true, true},
        {"DRY", SYP_VOLUME, 0xFF, 0x10, true, false},
        {"CHO", SYP_CHORUS, 0xFF, 0x10, true, false},
        {"DEL", SYP_DELSEND, 0xFF, 0x10, true, false},
        {"REV", SYP_REVSEND, 0xFF, 0x10, true, false},
        {"TAPE", SYP_TAPESEND, 0xFF, 0x10, true, false},
    };

    for (int r = 0; r < 8; r++) {
        int ry = yGrid + r;
        pos = GUIPoint(21, ry);
        T_SimpleList<UIField>::Insert(
            new UIStaticField(pos, rightRows[r].label));
        v = instrument->FindVariable(rightRows[r].id);
        if (v) {
            pos = GUIPoint(27, ry);
            const char *fmt = rightRows[r].hasBar ? "%2.2X" : "%s";
            T_SimpleList<UIField>::Insert(
                new UIIntVarField(pos, *v, fmt, 0, rightRows[r].maxVal, 1,
                                  rightRows[r].coarse));
            if (rightRows[r].hasBar) {
                addBarItem(29, ry, 8, rightRows[r].id, rightRows[r].maxVal,
                           rightRows[r].centered);
            }
        }
    }

    // Envelope & LFO/Pitch block (rows 21..24)
    int yEnv = yGrid + 8;
    static const RowSpec envLeft[] = {
        {"ATK", SYP_ATTACK, 0xFF, 0x10, true, false},
        {"DEC", SYP_DECAY, 0xFF, 0x10, true, false},
        {"SUS", SYP_SUSTAIN, 0xFF, 0x10, true, false},
        {"REL", SYP_RELEASE, 0xFF, 0x10, true, false},
    };
    for (int r = 0; r < 4; r++) {
        int ry = yEnv + r;
        pos = GUIPoint(1, ry);
        T_SimpleList<UIField>::Insert(new UIStaticField(pos, envLeft[r].label));
        v = instrument->FindVariable(envLeft[r].id);
        if (v) {
            pos = GUIPoint(11, ry);
            T_SimpleList<UIField>::Insert(
                new UIIntVarField(pos, *v, "%2.2X", 0, envLeft[r].maxVal, 1,
                                  envLeft[r].coarse));
            addBarItem(13, ry, 6, envLeft[r].id, envLeft[r].maxVal, false);
        }
    }

    pos = GUIPoint(21, yEnv);
    T_SimpleList<UIField>::Insert(new UIStaticField(pos, "ROOT"));
    v = instrument->FindVariable(SYP_ROOTNOTE);
    if (v) {
        pos = GUIPoint(27, yEnv);
        T_SimpleList<UIField>::Insert(
            new UINoteVarField(pos, *v, "%s", 0, 0x7F, 1, 0x0C));
    }

    pos = GUIPoint(21, yEnv + 1);
    T_SimpleList<UIField>::Insert(new UIStaticField(pos, "FINE"));
    v = instrument->FindVariable(SYP_FINETUNE);
    if (v) {
        pos = GUIPoint(27, yEnv + 1);
        T_SimpleList<UIField>::Insert(
            new UIIntVarField(pos, *v, "%2.2X", 0, 0xFF, 1, 0x10));
        addBarItem(29, yEnv + 1, 8, SYP_FINETUNE, 0xFF, true);
    }

    pos = GUIPoint(21, yEnv + 2);
    T_SimpleList<UIField>::Insert(new UIStaticField(pos, "LFO R"));
    v = instrument->FindVariable(SYP_LFORATE);
    if (v) {
        pos = GUIPoint(27, yEnv + 2);
        T_SimpleList<UIField>::Insert(
            new UIIntVarField(pos, *v, "%2.2X", 0, 0xFF, 1, 0x10));
        addBarItem(29, yEnv + 2, 8, SYP_LFORATE, 0xFF, false);
    }

    pos = GUIPoint(21, yEnv + 3);
    T_SimpleList<UIField>::Insert(new UIStaticField(pos, "LFO D"));
    v = instrument->FindVariable(SYP_LFODEPTH);
    if (v) {
        pos = GUIPoint(27, yEnv + 3);
        T_SimpleList<UIField>::Insert(
            new UIIntVarField(pos, *v, "%2.2X", 0, 0xFF, 1, 0x10));
        addBarItem(29, yEnv + 3, 8, SYP_LFODEPTH, 0xFF, false);
    }
}

void InstrumentView::fillSampleParameters() {
    int i = viewData_->currentInstrument_;
    InstrumentBank *bank = viewData_->project_->GetInstrumentBank();
    SampleInstrument *instrument = (SampleInstrument *)bank->GetInstrument(i);
    GUIPoint pos(1, paramBaseY_);

    int sampleSize = instrument->GetSampleSize();
    if (sampleSize < 1) {
        sampleSize = 1;
    }
    Variable *startVar = instrument->FindVariable(SIP_START);
    Variable *loopStartVar = instrument->FindVariable(SIP_LOOPSTART);
    Variable *endVar = instrument->FindVariable(SIP_END);
    int curStart = startVar ? startVar->GetInt() : 0;
    int curEnd = endVar ? endVar->GetInt() : sampleSize;
    if (curEnd <= 0 || curEnd > sampleSize) {
        curEnd = sampleSize;
    }
    if (curStart < 0) {
        curStart = 0;
    }
    if (curStart >= curEnd) {
        curStart = curEnd > 1 ? curEnd - 1 : 0;
    }
    if (loopStartVar && loopStartVar->GetInt() == 0 && curStart > 0) {
        loopStartVar->SetInt(curStart, false);
    }
    startMarkerVar_->SetInt(curStart, false);
    endMarkerVar_->SetInt(curEnd, false);

    int fineStep = sampleSize / 128;
    if (fineStep < 1) {
        fineStep = 1;
    }
    int coarseStep = sampleSize / 16;
    if (coarseStep < 1) {
        coarseStep = 1;
    }

    GUIPoint startHandlePos(1, paramBaseY_ - 1);
    startMarkerField_ = new UIIntVarField(
        startHandlePos, *startMarkerVar_, "", 0, sampleSize - 1, fineStep,
        coarseStep);
    T_SimpleList<UIField>::Insert(startMarkerField_);

    GUIPoint endHandlePos(20, paramBaseY_ - 1);
    endMarkerField_ = new UIIntVarField(endHandlePos, *endMarkerVar_, "", 1,
                                        sampleSize, fineStep, coarseStep);
    T_SimpleList<UIField>::Insert(endMarkerField_);

    fillHeaderParameters();

    int ySample = paramBaseY_ + 4;
    pos = GUIPoint(1, ySample);
    T_SimpleList<UIField>::Insert(new UIStaticField(pos, "SAMPLE"));
    Variable *v = instrument->FindVariable(SIP_SAMPLE);
    SamplePool *sp = SamplePool::GetInstance();
    int sampleMax = sp->GetNameListSize() - 1;
    if (sampleMax < 0) {
        sampleMax = 0;
    }
    pos = GUIPoint(11, ySample);
    T_SimpleList<UIField>::Insert(
        new UIIntVarField(pos, *v, "%s", 0, sampleMax, 1, 0x10));

#ifdef FFMPEG_ENABLED
    int yFx = ySample + 1;
    pos = GUIPoint(1, yFx);
    T_SimpleList<UIField>::Insert(new UIStaticField(pos, "IR FX"));
    v = instrument->FindVariable(SIP_PRINTFX);
    pos = GUIPoint(11, yFx);
    T_SimpleList<UIField>::Insert(
        new UIIntVarField(pos, *v, "%s", 0, 3, 1, 2));

    v = instrument->FindVariable(SIP_IR_WET);
    pos = GUIPoint(18, yFx);
    T_SimpleList<UIField>::Insert(
        new UIIntVarField(pos, *v, "WET:%d%%", 0, 100, 1, 10));

    v = instrument->FindVariable(SIP_IR_PAD);
    pos = GUIPoint(28, yFx);
    T_SimpleList<UIField>::Insert(
        new UIIntVarField(pos, *v, "PAD:%dms", 0, 5000, 5, 100));
#endif

    int yGrid = paramBaseY_ + 7;
    struct SampleRowSpec {
        const char *label;
        FourCC id;
        int minVal;
        int maxVal;
        int coarse;
        bool hasBar;
        bool centered;
    };

    static const SampleRowSpec leftSampleRows[] = {
        {"CRUSH", SIP_CRUSH, 1, 0x10, 4, true, false},
        {"DRIVE", SIP_CRUSHVOL, 0, 0xFF, 0x10, true, false},
        {"DOWNSMP", SIP_DOWNSMPL, 0, 8, 2, true, false},
        {"FILTER", SIP_FILTMODE, 0, 4, 1, false, false},
        {"CUTOFF", SIP_FILTCUTOFF, 0, 0xFF, 0x10, true, false},
        {"RES", SIP_FILTRESO, 0, 0xFF, 0x10, true, false},
        {"AMP", SIP_AMP, 0, 0xFF, 0x10, true, false},
        {"ATTEN", SIP_ATTENUATE, 1, 0xFF, 0x10, true, false},
        {"FB TUNE", SIP_FBTUNE, 0, 0xFF, 0x10, true, false},
        {"FB MIX", SIP_FBMIX, 0, 0xFF, 0x10, true, false},
        {"STRETCH", SIP_TIMESTRETCH, 0, 0xFE, 0x10, true, true},
    };

    for (int r = 0; r < 11; r++) {
        int ry = yGrid + r;
        pos = GUIPoint(1, ry);
        T_SimpleList<UIField>::Insert(
            new UIStaticField(pos, leftSampleRows[r].label));
        v = instrument->FindVariable(leftSampleRows[r].id);
        if (v) {
            pos = GUIPoint(11, ry);
            const char *fmt = leftSampleRows[r].hasBar ? "%2.2X" : "%s";
            T_SimpleList<UIField>::Insert(new UIIntVarField(
                pos, *v, fmt, leftSampleRows[r].minVal,
                leftSampleRows[r].maxVal, 1, leftSampleRows[r].coarse));
            if (leftSampleRows[r].hasBar) {
                addBarItem(13, ry, 6, leftSampleRows[r].id,
                           leftSampleRows[r].maxVal,
                           leftSampleRows[r].centered);
            }
        }
    }

    pos = GUIPoint(21, yGrid + 0);
    T_SimpleList<UIField>::Insert(new UIStaticField(pos, "DRY"));
    v = instrument->FindVariable(SIP_VOLUME);
    pos = GUIPoint(27, yGrid + 0);
    T_SimpleList<UIField>::Insert(
        new UIIntVarField(pos, *v, "%2.2X", 0, 0xFF, 1, 0x10));
    addBarItem(29, yGrid + 0, 8, SIP_VOLUME, 0xFF, false);

    pos = GUIPoint(21, yGrid + 1);
    T_SimpleList<UIField>::Insert(new UIStaticField(pos, "PAN"));
    v = instrument->FindVariable(SIP_PAN);
    pos = GUIPoint(27, yGrid + 1);
    T_SimpleList<UIField>::Insert(
        new UIIntVarField(pos, *v, "%2.2X", 0, 0xFE, 1, 0x10));
    addBarItem(29, yGrid + 1, 8, SIP_PAN, 0xFE, true);

    pos = GUIPoint(21, yGrid + 2);
    T_SimpleList<UIField>::Insert(new UIStaticField(pos, "ROOT"));
    v = instrument->FindVariable(SIP_ROOTNOTE);
    pos = GUIPoint(27, yGrid + 2);
    T_SimpleList<UIField>::Insert(
        new UINoteVarField(pos, *v, "%s", 0, 0x7F, 1, 0x0C));

    pos = GUIPoint(21, yGrid + 3);
    T_SimpleList<UIField>::Insert(new UIStaticField(pos, "FINE"));
    v = instrument->FindVariable(SIP_FINETUNE);
    pos = GUIPoint(27, yGrid + 3);
    T_SimpleList<UIField>::Insert(
        new UIIntVarField(pos, *v, "%2.2X", 0, 0xFF, 1, 0x10));
    addBarItem(29, yGrid + 3, 8, SIP_FINETUNE, 0xFF, true);

    pos = GUIPoint(21, yGrid + 4);
    T_SimpleList<UIField>::Insert(new UIStaticField(pos, "INTERP"));
    v = instrument->FindVariable(SIP_INTERPOLATION);
    pos = GUIPoint(27, yGrid + 4);
    T_SimpleList<UIField>::Insert(
        new UIIntVarField(pos, *v, "%s", 0, 1, 1, 1));

    pos = GUIPoint(21, yGrid + 5);
    T_SimpleList<UIField>::Insert(new UIStaticField(pos, "LOOP"));
    v = instrument->FindVariable(SIP_LOOPMODE);
    pos = GUIPoint(27, yGrid + 5);
    T_SimpleList<UIField>::Insert(
        new UIIntVarField(pos, *v, "%s", 0, SILM_LAST - 1, 1, 1));

    pos = GUIPoint(21, yGrid + 6);
    T_SimpleList<UIField>::Insert(new UIStaticField(pos, "SLICES"));
    v = instrument->FindVariable(SIP_SLICES);
    pos = GUIPoint(27, yGrid + 6);
    T_SimpleList<UIField>::Insert(
        new UIIntVarField(pos, *v, "%2.2X", 1, 0xFF, 1, 0x10));
    if (v->GetInt() > 1) {
        Variable *ste = instrument->FindVariable(SIP_SLICETOEND);
        if (ste) {
            pos = GUIPoint(30, yGrid + 6);
            T_SimpleList<UIField>::Insert(
                new UIIntVarField(pos, *ste, "END:%s", 0, 1, 1, 1));
        }
    } else {
        addBarItem(29, yGrid + 6, 8, SIP_SLICES, 0x40, false);
    }

    pos = GUIPoint(21, yGrid + 7);
    T_SimpleList<UIField>::Insert(new UIStaticField(pos, "REVRS"));
    v = instrument->FindVariable(SIP_REVERSE);
    if (v) {
        pos = GUIPoint(27, yGrid + 7);
        T_SimpleList<UIField>::Insert(
            new UIIntVarField(pos, *v, "%s", 0, 1, 1, 1));
    }

    pos = GUIPoint(21, yGrid + 8);
    T_SimpleList<UIField>::Insert(new UIStaticField(pos, "DEL"));
    v = instrument->FindVariable(SIP_DELSEND);
    if (v) {
        pos = GUIPoint(27, yGrid + 8);
        T_SimpleList<UIField>::Insert(
            new UIIntVarField(pos, *v, "%2.2X", 0, 0xFF, 1, 0x10));
        addBarItem(29, yGrid + 8, 8, SIP_DELSEND, 0xFF, false);
    }

    pos = GUIPoint(21, yGrid + 9);
    T_SimpleList<UIField>::Insert(new UIStaticField(pos, "REV"));
    v = instrument->FindVariable(SIP_REVSEND);
    if (v) {
        pos = GUIPoint(27, yGrid + 9);
        T_SimpleList<UIField>::Insert(
            new UIIntVarField(pos, *v, "%2.2X", 0, 0xFF, 1, 0x10));
        addBarItem(29, yGrid + 9, 8, SIP_REVSEND, 0xFF, false);
    }

    pos = GUIPoint(21, yGrid + 10);
    T_SimpleList<UIField>::Insert(new UIStaticField(pos, "TAPE"));
    v = instrument->FindVariable(SIP_TAPESEND);
    if (v) {
        pos = GUIPoint(27, yGrid + 10);
        T_SimpleList<UIField>::Insert(
            new UIIntVarField(pos, *v, "%2.2X", 0, 0xFF, 1, 0x10));
        addBarItem(29, yGrid + 10, 8, SIP_TAPESEND, 0xFF, false);
    }
}

void InstrumentView::fillMidiParameters() {
    int i = viewData_->currentInstrument_;
    InstrumentBank *bank = viewData_->project_->GetInstrumentBank();
    MidiInstrument *instrument = (MidiInstrument *)bank->GetInstrument(i);
    GUIPoint position = paramAnchor();

    Variable *v = instrument->FindVariable(MIP_CHANNEL);
    UIIntVarField *f1 =
        new UIIntVarField(position, *v, "CHANNEL   %2.2d", 0, 0x0F, 1, 0x04, 1);
    T_SimpleList<UIField>::Insert(f1);
    f1->SetFocus();

    position._y += 1;
    v = instrument->FindVariable(MIP_VOLUME);
    f1 = new UIIntVarField(position, *v, "VOLUME    %2.2X", 0, 0xFF, 1, 0x10);
    T_SimpleList<UIField>::Insert(f1);

    position._y += 1;
    v = instrument->FindVariable(MIP_NOTELENGTH);
    f1 = new UIIntVarField(position, *v, "LENGTH    %2.2X", 0, 0xFF, 1, 0x10);
    T_SimpleList<UIField>::Insert(f1);

    v = instrument->FindVariable(MIP_TABLEAUTO);
    position._y += 2;
    UIIntVarField *f2 =
        new UIIntVarField(position, *v, "AUTO      %s", 0, 1, 1, 1);
    T_SimpleList<UIField>::Insert(f2);

    position._y += 1;
    v = instrument->FindVariable(MIP_TABLE);
    f1 = new UIIntVarOffField(position, *v, "TABLE     %2.2X", 0, 0x7F, 1, 0x10);
    T_SimpleList<UIField>::Insert(f1);
}

void InstrumentView::warpToNext(int offset) {
    int instrument = viewData_->currentInstrument_ + offset;
    if (instrument >= MAX_INSTRUMENT_COUNT) {
        instrument = instrument - MAX_INSTRUMENT_COUNT;
    }
    if (instrument < 0) {
        instrument = MAX_INSTRUMENT_COUNT + instrument;
    }
    viewData_->currentInstrument_ = instrument;
    onInstrumentChange();
    ((AppWindow &)w_).Clear(true);
    isDirty_ = true;
}

void InstrumentView::ProcessButtonMask(unsigned short mask, bool pressed) {

    if (!pressed)
        return;

    isDirty_ = false;

    // Extra button shortcut for previewing instrument sound from any row:
    // SELECT toggles audition (A + SELECT retriggers from the start;
    // L + START also toggles audition).
    if (mask & EPBM_SELECT) {
        toggleInstrumentAudition((mask & EPBM_A) != 0);
        overlayValid_ = false;
        isDirty_ = true;
        return;
    }
    if ((mask & EPBM_L) && (mask & EPBM_START)) {
        toggleInstrumentAudition(false);
        overlayValid_ = false;
        isDirty_ = true;
        return;
    }

    UIField *preFocus = GetFocus();
    bool onActionField = isActionField(preFocus);

    if (viewMode_ == VM_NEW) {
        if (mask == EPBM_A && preFocus && !preFocus->IsStatic() &&
            !onActionField) {
            UIIntVarField *field = (UIIntVarField *)preFocus;
            Variable &v = field->GetVariable();
            switch (v.GetID()) {
            case SIP_SAMPLE: {
                Path sampleLib(SamplePool::GetInstance()->GetSampleLib());
                if (FileSystem::GetInstance()->GetFileType(
                        sampleLib.GetPath().c_str()) != FT_DIR) {
                    MessageBox *mb =
                        new MessageBox(*this, "Can't access the samplelib",
                                       MBBF_OK);
                    DoModal(mb);
                } else {
                    ImportSampleDialog *isd = new ImportSampleDialog(*this);
                    DoModal(isd, ImportSampleDialogCallback);
                }
                break;
            }
            case SIP_TABLE: {
                int next = TableHolder::GetInstance()->GetNext();
                if (next != NO_MORE_TABLE) {
                    v.SetInt(next);
                    isDirty_ = true;
                }
                break;
            }
            case SIP_PRINTFX: {
                FxPrinter printer(viewData_);
                isDirty_ = printer.Run();
                View::SetNotification(printer.GetNotification());
                break;
            }
            default:
                break;
            }
            mask &= (0xFFFF - EPBM_A);
        }
    }

    if (viewMode_ == VM_CLONE) {
        if ((mask & EPBM_A) && (mask & EPBM_L) && preFocus &&
            !preFocus->IsStatic() && !onActionField) {
            UIIntVarField *field = (UIIntVarField *)preFocus;
            mask &= (0xFFFF - EPBM_A);
            Variable &v = field->GetVariable();
            int current = v.GetInt();
            if (current == -1)
                return;

            int next = TableHolder::GetInstance()->Clone(current);
            if (next != NO_MORE_TABLE) {
                v.SetInt(next);
                isDirty_ = true;
            }
        }
        mask &= (0xFFFF - (EPBM_A | EPBM_L));
    }

    if (viewMode_ != VM_SELECTION) {
        viewMode_ = VM_NORMAL;
    }

    inProcessButton_ = true;
    FieldView::ProcessButtonMask(mask);
    inProcessButton_ = false;
    if (pendingInstrumentChange_) {
        pendingInstrumentChange_ = false;
        onInstrumentChange();
        isDirty_ = true;
    }

    applyTypeChangeIfNeeded();
    applySlicesUiIfNeeded();
    syncSampleRangeFromMarkers();

    Player *player = Player::GetInstance();

    if (mask & EPBM_B) {
        if (mask & EPBM_LEFT)
            warpToNext(-1);
        if (mask & EPBM_RIGHT)
            warpToNext(+1);
        if (mask & EPBM_DOWN)
            warpToNext(-16);
        if (mask & EPBM_UP)
            warpToNext(+16);
        if (mask & EPBM_A) {
            UIField *focus = GetFocus();
            if (focus == nameField_ && current_) {
                current_->SetUserName("");
                refreshNameFieldText();
                isDirty_ = true;
            } else if (focus && !focus->IsStatic() && !isActionField(focus)) {
                UIIntVarField *vf = (UIIntVarField *)focus;
                FourCC fid = vf->GetVariableID();
                if (getInstrumentType() == IT_SAMPLE && fid == SIP_SAMPLE) {
                    int i = viewData_->currentInstrument_;
                    InstrumentBank *bank =
                        viewData_->project_->GetInstrumentBank();
                    I_Instrument *instr = bank->GetInstrument(i);
                    instr->Purge();
                    onInstrumentChange();
                    isDirty_ = true;
                } else if (fid == SIP_TABLE || fid == SYP_TABLE ||
                           fid == MIP_TABLE) {
                    vf->GetVariable().SetInt(-1);
                    isDirty_ = true;
                } else if (fid == SIP_TIMESTRETCH || fid == SIP_PAN ||
                           fid == SIP_FINETUNE || fid == SYP_PAN ||
                           fid == SYP_FINETUNE) {
                    vf->GetVariable().SetInt(0x7F);
                    isDirty_ = true;
                }
            }
        }
        if (mask & EPBM_L) {
            viewMode_ = VM_CLONE;
        }
    } else {
        if (mask == EPBM_A) {
            UIField *focus = GetFocus();
            if (focus && !focus->IsStatic() && !isActionField(focus)) {
                FourCC varID = ((UIIntVarField *)focus)->GetVariableID();
                if ((varID == SIP_TABLE) || (varID == MIP_TABLE) ||
                    (varID == SYP_TABLE) || (varID == SIP_SAMPLE) ||
                    (varID == SIP_PRINTFX)) {
                    viewMode_ = VM_NEW;
                }
            }
        } else if (mask & EPBM_R) {
            if (mask & EPBM_LEFT) {
                stopInstrumentAudition();
                ViewType vt = VT_PHRASE;
                ViewEvent ve(VET_SWITCH_VIEW, &vt);
                SetChanged();
                NotifyObservers(&ve);
            }
            if (mask & EPBM_DOWN) {
                stopInstrumentAudition();
                ViewType vt = VT_TABLE2;
                int i = viewData_->currentInstrument_;
                InstrumentBank *bank =
                    viewData_->project_->GetInstrumentBank();
                I_Instrument *instr = bank->GetInstrument(i);
                int table = instr->GetTable();
                if (table != VAR_OFF) {
                    viewData_->currentTable_ = table;
                }
                ViewEvent ve(VET_SWITCH_VIEW, &vt);
                SetChanged();
                NotifyObservers(&ve);
            }
            if (mask & EPBM_START) {
                stopInstrumentAudition();
                player->OnStartButton(PM_PHRASE, viewData_->songX_, true,
                                      viewData_->chainRow_);
            }
        } else {
            if (mask & EPBM_START) {
                stopInstrumentAudition();
                player->OnStartButton(PM_PHRASE, viewData_->songX_, false,
                                      viewData_->chainRow_);
            }
        }
    }

    UIField *field = GetFocus();
    if (field && !field->IsStatic() && !isActionField(field)) {
        lastFocusID_ = ((UIIntVarField *)field)->GetVariableID();
    }
    overlayValid_ = false;
    isDirty_ = true;
}

void InstrumentView::drawModeLabels() {
    if (!current_)
        return;
    GUITextProperties props;
    props.invert_ = false;
    SetColor(CD_HILITE2);

    InstrumentType it = getInstrumentType();
    if (it == IT_SYNTH) {
        SynthInstrument *sy = (SynthInstrument *)current_;
        bool isShapes = (sy->GetEngineMode() == SYM_SHAPES);
        int yShape = paramBaseY_ + 4;
        if (isShapes) {
            Variable *v = current_->FindVariable(SYP_MACROSHAPE);
            if (v) {
                DrawString(14, yShape,
                           SynthInstrument::GetMacroShapeLabel(v->GetInt()),
                           props);
            }
        } else {
            Variable *v = current_->FindVariable(SYP_WAVEFORM);
            if (v) {
                DrawString(14, yShape,
                           SynthInstrument::GetShapeLabel(v->GetInt()), props);
            }
        }
    }
    SetColor(CD_NORMAL);
}

int InstrumentView::paramBarsKey() const {
    if (!current_)
        return 0;
    int key = (int)current_->GetType() * 31;
    if (current_->GetType() == IT_SYNTH) {
        key = (key * 17) + ((SynthInstrument *)current_)->GetEngineMode();
    }
    UIField *focus = ((InstrumentView *)this)->GetFocus();
    FourCC focusId = 0;
    if (focus && !focus->IsStatic() && !isActionField(focus)) {
        focusId = ((UIIntVarField *)focus)->GetVariableID();
    }
    key = (key * 131) ^ (int)focusId;
    for (int i = 0; i < barItemCount_; i++) {
        Variable *v = current_->FindVariable(barItems_[i].varId_);
        int val = v ? v->GetInt() : 0;
        key = (key * 37) + val + i;
    }
    return key;
}

void InstrumentView::drawParamBarsOverlay() {
    if (!current_ || barItemCount_ <= 0)
        return;
    AppWindow &aw = (AppWindow &)w_;
    const int cw = AppWindow::CharWidth();
    const int ch = AppWindow::CharHeight();

    UIField *focus = GetFocus();
    FourCC focusId = 0;
    if (focus && !focus->IsStatic() && !isActionField(focus)) {
        focusId = ((UIIntVarField *)focus)->GetVariableID();
    }

    GUIRect dirtyRect(9 * cw, (paramBaseY_ + 5) * ch, 39 * cw, 26 * ch);
    aw.BeginBatchDraw();

    for (int i = 0; i < barItemCount_; i++) {
        const BarItem &item = barItems_[i];
        Variable *v = current_->FindVariable(item.varId_);
        if (!v)
            continue;
        int val = v->GetInt();
        if (val < 0)
            val = 0;
        if (val > item.maxVal_)
            val = item.maxVal_;

        int bx0 = item.charX_ * cw + 2;
        int bx1 = (item.charX_ + item.charW_) * cw - 2;
        int bw = bx1 - bx0;
        if (bw < 4)
            continue;
        int by0 = item.charY_ * ch + 2;
        int by1 = (item.charY_ + 1) * ch - 2;
        int valX0 = (item.charX_ - 2) * cw - 3;

        bool isFocused = (focusId != 0 && focusId == item.varId_);

        // Clear both the left-bracket gutter and the right bar/bracket slot
        GUIRect leftBrkBg(valX0 - 1, item.charY_ * ch, (item.charX_ - 2) * cw,
                          (item.charY_ + 1) * ch);
        aw.FillRect(leftBrkBg, CD_BACKGROUND);

        GUIRect slotBg(bx0 - 2, item.charY_ * ch, bx1 + 4,
                       (item.charY_ + 1) * ch);
        aw.FillRect(slotBg, CD_BACKGROUND);

        if (isFocused) {
            GUIRect track(bx0, by0, bx1, by1);
            aw.FillRect(track, CD_SONGVIEW00);
        }

        if (item.centered_) {
            int midX = bx0 + bw / 2;
            int centerVal = (item.maxVal_ + 1) / 2;
            if (val != centerVal) {
                int dx = ((val - centerVal) * (bw / 2)) / centerVal;
                int xStart = (dx < 0) ? (midX + dx) : midX;
                int xEnd = (dx < 0) ? midX : (midX + dx);
                if (xEnd <= xStart)
                    xEnd = xStart + 1;
                GUIRect fill(xStart, by0, xEnd, by1);
                aw.FillRect(fill, isFocused ? CD_HILITE1 : CD_NORMAL);
            }
            GUIRect centerTick(midX, by0, midX + 2, by1);
            aw.FillRect(centerTick, CD_NORMAL);
        } else {
            GUIRect leftTick(bx0, by0, bx0 + 2, by1);
            aw.FillRect(leftTick, CD_NORMAL);

            int fillW = (val * bw) / item.maxVal_;
            if (fillW > 0) {
                GUIRect fill(bx0, by0, bx0 + fillW, by1);
                aw.FillRect(fill, isFocused ? CD_HILITE1 : CD_NORMAL);
            }
        }

        if (isFocused) {
            int brkX1 = bx1 + 2;
            int topY = item.charY_ * ch;
            int botY = (item.charY_ + 1) * ch - 1;
            GUIRect tlH(valX0, topY, valX0 + 3, topY + 1);
            GUIRect tlV(valX0, topY, valX0 + 1, topY + 3);
            GUIRect blH(valX0, botY, valX0 + 3, botY + 1);
            GUIRect blV(valX0, botY - 2, valX0 + 1, botY + 1);
            GUIRect trH(brkX1 - 2, topY, brkX1 + 1, topY + 1);
            GUIRect trV(brkX1, topY, brkX1 + 1, topY + 3);
            GUIRect brH(brkX1 - 2, botY, brkX1 + 1, botY + 1);
            GUIRect brV(brkX1, botY - 2, brkX1 + 1, botY + 1);
            aw.FillRect(tlH, CD_NORMAL);
            aw.FillRect(tlV, CD_NORMAL);
            aw.FillRect(blH, CD_NORMAL);
            aw.FillRect(blV, CD_NORMAL);
            aw.FillRect(trH, CD_NORMAL);
            aw.FillRect(trV, CD_NORMAL);
            aw.FillRect(brH, CD_NORMAL);
            aw.FillRect(brV, CD_NORMAL);
        }
    }

    aw.EndBatchDraw(dirtyRect);
}

void InstrumentView::ensureWaveformCache(short *pcm, int nFrames, int nCh,
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

int InstrumentView::synthPreviewKey() const {
    if (!current_) {
        return 0;
    }
    int mode = 0, wave = 0, mshape = 0, atk = 0, dec = 0, sus = 0, rel = 0;
    int rate = 0, depth = 0, sz = 0, mult = 0, warp = 0, scan = 0;
    int tmb = 0, col = 0, deg = 0, red = 0, amp = 0, lim = 0;
    Variable *v;
    if ((v = current_->FindVariable(SYP_MODE)))
        mode = v->GetInt();
    if ((v = current_->FindVariable(SYP_WAVEFORM)))
        wave = v->GetInt();
    if ((v = current_->FindVariable(SYP_MACROSHAPE)))
        mshape = v->GetInt();
    if ((v = current_->FindVariable(SYP_ATTACK)))
        atk = v->GetInt();
    if ((v = current_->FindVariable(SYP_DECAY)))
        dec = v->GetInt();
    if ((v = current_->FindVariable(SYP_SUSTAIN)))
        sus = v->GetInt();
    if ((v = current_->FindVariable(SYP_RELEASE)))
        rel = v->GetInt();
    if ((v = current_->FindVariable(SYP_LFORATE)))
        rate = v->GetInt();
    if ((v = current_->FindVariable(SYP_LFODEPTH)))
        depth = v->GetInt();
    if ((v = current_->FindVariable(SYP_SIZE)))
        sz = v->GetInt();
    if ((v = current_->FindVariable(SYP_MULT)))
        mult = v->GetInt();
    if ((v = current_->FindVariable(SYP_WARP)))
        warp = v->GetInt();
    if ((v = current_->FindVariable(SYP_SCAN)))
        scan = v->GetInt();
    if ((v = current_->FindVariable(SYP_TIMBRE)))
        tmb = v->GetInt();
    if ((v = current_->FindVariable(SYP_COLOR)))
        col = v->GetInt();
    if ((v = current_->FindVariable(SYP_DEGRADE)))
        deg = v->GetInt();
    if ((v = current_->FindVariable(SYP_REDUX)))
        red = v->GetInt();
    if ((v = current_->FindVariable(SYP_AMP)))
        amp = v->GetInt();
    if ((v = current_->FindVariable(SYP_LIM)))
        lim = v->GetInt();
    return (mode & 0x3) ^ ((wave & 0xF) << 2) ^ ((mshape & 0xF) << 6) ^
           ((atk & 0xFF) << 4) ^ ((dec & 0xFF) << 12) ^ ((sus & 0xFF) << 20) ^
           ((rel & 0xFF) << 7) ^ ((rate & 0xFF) << 15) ^
           ((depth & 0xFF) << 23) ^ ((sz & 0xFF) << 2) ^ ((mult & 0xFF) << 10) ^
           ((warp & 0xFF) << 18) ^ ((scan & 0xFF) << 24) ^ ((tmb & 0xFF) << 3) ^
           ((col & 0xFF) << 11) ^ ((deg & 0xFF) << 19) ^ ((red & 0xFF) << 21) ^
           ((amp & 0xFF) << 5) ^ ((lim & 0xF) << 28);
}

void InstrumentView::clearWaveformStrip(int px, int py, int pw, int ph) {
    if (overlayValid_ && overlayDrawnSample_ == -3 &&
        overlayDrawnSynthKey_ == 0) {
        return;
    }
    AppWindow &aw = (AppWindow &)w_;
    GUIRect strip(px - 6, py, px + pw + 6, py + ph + 6);
    aw.BeginBatchDraw();
    aw.FillRect(strip, CD_BACKGROUND);
    aw.EndBatchDraw(strip);
    overlayValid_ = true;
    overlayDrawnSample_ = -3;
    overlayDrawnStart_ = -1;
    overlayDrawnEnd_ = -1;
    overlayDrawnSlices_ = -1;
    overlayDrawnFocusMarker_ = 0;
    overlayDrawnSynthKey_ = 0;
}

static void drawTriangleHandle(AppWindow &aw, int cx, int topY, bool focused) {
    ColorDefinition fillCol = focused ? CD_NORMAL : CD_BORDER;
    for (int row = 0; row < 5; row++) {
        int half = row + 1;
        GUIRect r(cx - half, topY + row, cx + half, topY + row + 1);
        aw.FillRect(r, fillCol);
    }
    if (focused) {
        for (int row = 2; row < 4; row++) {
            int half = row - 1;
            GUIRect inner(cx - half, topY + row, cx + half, topY + row + 1);
            aw.FillRect(inner, CD_BORDER);
        }
    }
}

void InstrumentView::drawSampleOverlay(int px, int py, int pw, int ph) {
    if (!current_ || current_->GetType() != IT_SAMPLE) {
        clearWaveformStrip(px, py, pw, ph);
        return;
    }
    AppWindow &aw = (AppWindow &)w_;
    SampleInstrument *si = (SampleInstrument *)current_;
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

    UIField *focus = GetFocus();
    int focusMarker = (focus == startMarkerField_) ? 1
                      : (focus == endMarkerField_) ? 2
                                                   : 0;

    ensureWaveformCache(pcm, nFrames, nCh, sampleIdx);

    int selX0 = px + (int)((long)startFrame * pw / nFrames);
    int selX1 = px + (int)((long)endFrame * pw / nFrames);
    if (selX0 < px)
        selX0 = px;
    if (selX1 > px + pw)
        selX1 = px + pw;
    if (selX1 <= selX0)
        selX1 = selX0 + 1;

    GUIRect stripDirty(px - 6, py, px + pw + 6, py + ph + 6);
    aw.BeginBatchDraw();

    aw.FillRect(stripDirty, CD_BACKGROUND);

    GUIRect leftRail(px - 2, py, px, py + ph);
    GUIRect rightRail(px + pw, py, px + pw + 2, py + ph);
    aw.FillRect(leftRail, (selX0 <= px) ? CD_BORDER : CD_SONGVIEW00);
    aw.FillRect(rightRail, (selX1 >= px + pw) ? CD_BORDER : CD_SONGVIEW00);

    const int midY = py + ph / 2;
    const int maxAmp = ph / 2 - 1;

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
        ColorDefinition col =
            (midBin >= selX0 && midBin < selX1) ? CD_HILITE1 : CD_SONGVIEW00;
        GUIRect column(x0, midY - amp, x1, midY + amp + 1);
        aw.FillRect(column, col);
    }

    if (nSlices > 1) {
        int selW = selX1 - selX0;
        if (selW < 1) {
            selW = 1;
        }
        for (int s = 1; s < nSlices; s++) {
            int mx = selX0 + (int)((long)s * selW / nSlices);
            GUIRect marker(mx, py, mx + 1, py + ph);
            aw.FillRect(marker, CD_BORDER);
        }
    }

    GUIRect startBar(selX0 - 1, py, selX0 + 1, py + ph);
    GUIRect endBar(selX1 - 1, py, selX1 + 1, py + ph);
    GUIRect bottomBar(selX0 - 1, py + ph - 2, selX1 + 1, py + ph);
    aw.FillRect(startBar, CD_NORMAL);
    aw.FillRect(endBar, CD_NORMAL);
    aw.FillRect(bottomBar, CD_NORMAL);

    drawTriangleHandle(aw, selX0, py + ph - 1, focusMarker == 1);
    drawTriangleHandle(aw, selX1, py + ph - 1, focusMarker == 2);

    aw.EndBatchDraw(stripDirty);

    overlayDrawnSample_ = sampleIdx;
    overlayDrawnStart_ = startFrame;
    overlayDrawnEnd_ = endFrame;
    overlayDrawnSlices_ = nSlices;
    overlayDrawnFocusMarker_ = focusMarker;
    overlayDrawnSynthKey_ = 0;
}

void InstrumentView::drawSynthOverlay(int px, int py, int pw, int ph) {
    if (!current_ || current_->GetType() != IT_SYNTH) {
        overlayValid_ = false;
        return;
    }
    AppWindow &aw = (AppWindow &)w_;
    SynthInstrument *sy = (SynthInstrument *)current_;
    int key = synthPreviewKey();

    sy->FillPhrasePreview(synthWaveBin_, synthLfoBin_, kWaveBins);

    GUIRect stripDirty(px - 6, py, px + pw + 6, py + ph + 6);
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

    GUIRect mid(px, midY, px + pw, midY + 1);
    aw.FillRect(mid, CD_BORDER);

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

    overlayDrawnSample_ = -2;
    overlayDrawnStart_ = -1;
    overlayDrawnEnd_ = -1;
    overlayDrawnSlices_ = -1;
    overlayDrawnFocusMarker_ = 0;
    overlayDrawnSynthKey_ = key;
}

void InstrumentView::DrawOverlay() {
    if (View::miniLayout_) {
        return;
    }

    if (HasModal()) {
        overlayValid_ = false;
        return;
    }

    const int cw = AppWindow::CharWidth();
    const int ch = AppWindow::CharHeight();
    const int px = 1 * cw;
    const int py = 1 * ch;
    const int pw = 38 * cw;
    const int ph = kWaveStripRows * ch;

    InstrumentType it = getInstrumentType();
    if (it != IT_SAMPLE && it != IT_SYNTH) {
        clearWaveformStrip(px, py, pw, ph);
        return;
    }

    int bKey = paramBarsKey();
    int sKey = (it == IT_SYNTH) ? synthPreviewKey() : 0;

    if (overlayValid_ && overlayDrawnBarsKey_ == bKey) {
        if (it == IT_SYNTH && overlayDrawnSample_ == -2 &&
            overlayDrawnSynthKey_ == sKey) {
            return;
        }
        if (it == IT_SAMPLE && current_) {
            SampleInstrument *si = (SampleInstrument *)current_;
            int sampleIdx = si->GetSampleIndex();
            Variable *sv = si->FindVariable(SIP_START);
            Variable *ev = si->FindVariable(SIP_END);
            Variable *slv = si->FindVariable(SIP_SLICES);
            UIField *focus = GetFocus();
            int fm = (focus == startMarkerField_) ? 1
                     : (focus == endMarkerField_) ? 2
                                                  : 0;
            if (overlayDrawnSample_ == sampleIdx &&
                overlayDrawnStart_ == (sv ? sv->GetInt() : 0) &&
                overlayDrawnEnd_ == (ev ? ev->GetInt() : 0) &&
                overlayDrawnSlices_ == (slv ? slv->GetInt() : 1) &&
                overlayDrawnFocusMarker_ == fm) {
                return;
            }
        }
    }

    if (it == IT_SAMPLE) {
        drawSampleOverlay(px, py, pw, ph);
    } else {
        drawSynthOverlay(px, py, pw, ph);
    }

    drawParamBarsOverlay();
    overlayDrawnBarsKey_ = bKey;
    overlayValid_ = true;
}

void InstrumentView::DrawView() {

    Clear();
    View::EnableNotification();
    overlayValid_ = false;

    GUITextProperties props;
    char title[24];
    SetColor(CD_HILITE2);
    sprintf(title, "INST. %2.2X", viewData_->currentInstrument_);
    DrawString(1, 0, title, props);
    SetColor(CD_NORMAL);

    if (!View::miniLayout_) {
        SetColor(CD_BACKGROUND);
        props.invert_ = false;
        char blank[40];
        memset(blank, ' ', 38);
        blank[38] = 0;
        for (int r = 1; r < paramBaseY_; r++) {
            DrawString(1, r, blank, props);
        }
        SetColor(CD_NORMAL);
    }

    refreshNameFieldText();
    refreshPlayFieldText();
    FieldView::Redraw();
    drawModeLabels();
    drawMap();
}

void InstrumentView::OnFocus() { onInstrumentChange(); }

void InstrumentView::Update(Observable &o, I_ObservableData *d) {
    (void)o;
    if (d) {
#ifdef _64BIT
        int fourcc = *((int *)d);
#else
        int fourcc = (unsigned int)d;
#endif
        switch (fourcc) {
        case IV_ACTION_PLAY: {
            toggleInstrumentAudition(false);
            return;
        }
        case IV_ACTION_NAME: {
            const char *curName = current_ ? current_->GetUserName() : "";
            NewProjectDialog *dlg =
                new NewProjectDialog(*this, "root:instruments", curName, false);
            DoModal(dlg, NameInstrumentCallback);
            return;
        }
        case IV_ACTION_SAVE: {
            EnsureInstrumentsDir();
            char defName[16];
            const char *curName = current_ ? current_->GetUserName() : "";
            if (curName && curName[0]) {
                strncpy(defName, curName, 12);
                defName[12] = '\0';
            } else {
                sprintf(defName, "INST_%2.2X", viewData_->currentInstrument_);
            }
            NewProjectDialog *dlg =
                new NewProjectDialog(*this, "root:instruments", defName, false);
            DoModal(dlg, SaveInstrumentCallback);
            return;
        }
        case IV_ACTION_LOAD: {
            stopInstrumentAudition();
            EnsureInstrumentsDir();
            LoadInstrumentModal *dlg = new LoadInstrumentModal(*this);
            DoModal(dlg, LoadInstrumentCallback);
            return;
        }
        default:
            break;
        }
    }

    if (inProcessButton_) {
        pendingInstrumentChange_ = true;
        return;
    }
    onInstrumentChange();
}
