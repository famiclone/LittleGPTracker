#include "InstrumentView.h"
#include "Application/AppWindow.h"
#include "Application/Instruments/MidiInstrument.h"
#include "Application/Instruments/SampleInstrument.h"
#include "Application/Instruments/SamplePool.h"
#include "Application/Instruments/SynthInstrument.h"
#include "Application/Model/Config.h"
#include "BaseClasses/UIBigHexVarField.h"
#include "BaseClasses/UIIntVarOffField.h"
#include "BaseClasses/UINoteVarField.h"
#include "BaseClasses/UIStaticField.h"
#include "Foundation/Variables/Variable.h"
#include "ModalDialogs/ImportSampleDialog.h"
#include "ModalDialogs/MessageBox.h"
#include "System/System/System.h"

static char *instrumentTypeNames[] = {"sample", "synth"};
static const char *synthWaveNames[] = {"saw", "triangle", "sine", "square",
                                       "noise"};

void ImportSampleDialogCallback(View &v, ModalView &dialog) {
    ((InstrumentView &)v).OnFocus();
}

InstrumentView::InstrumentView(GUIWindow &w, ViewData *data)
    : FieldView(w, data), paramBaseY_(7), waveField_(0), waveCacheSample_(-1),
      waveCacheDomain_(-1), waveCacheSlices_(-1), overlayValid_(false),
      overlayDrawnSample_(-1), overlayDrawnDomain_(-1), overlayDrawnSlices_(-1),
      overlayDrawnSynthKey_(0) {

    project_ = data->project_;
    lastFocusID_ = 0;
    current_ = 0;
    // Plain Variable (not Watched): type switch must run after ProcessArrow
    // returns, otherwise Empty() deletes the field still on the stack.
    typeVar_ = new Variable("type", IVP_TYPE, instrumentTypeNames, 2, 0);
    onInstrumentChange();
}

InstrumentView::~InstrumentView() { delete typeVar_; }

GUIPoint InstrumentView::paramAnchor() const {
    // Leave title + waveform strip at the top (matches reference layout).
    return GUIPoint(1, paramBaseY_);
}

InstrumentType InstrumentView::getInstrumentType() {
    int i = viewData_->currentInstrument_;
    InstrumentBank *bank = viewData_->project_->GetInstrumentBank();
    I_Instrument *instrument = bank->GetInstrument(i);
    return instrument->GetType();
};

void InstrumentView::onInstrumentChange() {

    ClearFocus();
    waveField_ = 0;
    overlayValid_ = false;

    I_Instrument *old = current_;

    int i = viewData_->currentInstrument_;
    InstrumentBank *bank = viewData_->project_->GetInstrumentBank();
    current_ = bank->GetInstrument(i);

    if (old && old != current_) {
        old->RemoveObserver(*this);
    };
    T_SimpleList<UIField>::Empty();

    InstrumentType it = getInstrumentType();

    // Keep type field in sync with the live instrument.
    if (it == IT_SYNTH) {
        typeVar_->SetInt(1, false);
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
    };

    SetFocus(T_SimpleList<UIField>::GetFirst());
    IteratorPtr<UIField> it2(T_SimpleList<UIField>::GetIterator());
    for (it2->Begin(); !it2->IsDone(); it2->Next()) {
        UIField &raw = it2->CurrentItem();
        if (raw.IsStatic()) {
            continue;
        }
        UIIntVarField &field = (UIIntVarField &)raw;
        if (field.GetVariableID() == lastFocusID_) {
            SetFocus(&field);
            break;
        }
    };
    refreshWaveformFieldPosition();
    if (current_ && current_ != old) {
        current_->AddObserver(*this);
    }
};

void InstrumentView::applyTypeChangeIfNeeded() {
    int i = viewData_->currentInstrument_;
    if (i >= MAX_SAMPLEINSTRUMENT_COUNT) {
        return;
    }
    InstrumentType current = getInstrumentType();
    if (current != IT_SAMPLE && current != IT_SYNTH) {
        return;
    }
    InstrumentType want = (typeVar_->GetInt() == 1) ? IT_SYNTH : IT_SAMPLE;
    if (current == want) {
        return;
    }
    InstrumentBank *bank = viewData_->project_->GetInstrumentBank();
    if (current_) {
        current_->RemoveObserver(*this);
        current_ = 0;
    }
    bank->SetInstrumentType(i, want);
    onInstrumentChange();
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
        if (raw.IsStatic()) {
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

void InstrumentView::addTypeField() {
    // Title-bar tabs: "synth" / "sample" on the right of Instrument XX.
    GUIPoint position(26, 0);
    UIIntVarField *f1 =
        new UIIntVarField(position, *typeVar_, "%s", 0, 1, 1, 1);
    T_SimpleList<UIField>::Insert(f1);
}

void InstrumentView::refreshWaveformFieldPosition() {
    if (!waveField_ || !current_ || getInstrumentType() != IT_SYNTH) {
        return;
    }
    Variable *v = current_->FindVariable(SYP_WAVEFORM);
    if (!v) {
        return;
    }
    int wave = v->GetInt();
    if (wave < 0) {
        wave = 0;
    }
    if (wave >= SYW_LAST) {
        wave = SYW_LAST - 1;
    }
    GUIPoint p(22, paramBaseY_ + wave);
    waveField_->SetPosition(p);
}

void InstrumentView::fillSampleParameters() {

    int i = viewData_->currentInstrument_;
    InstrumentBank *bank = viewData_->project_->GetInstrumentBank();
    I_Instrument *instr = bank->GetInstrument(i);
    SampleInstrument *instrument = (SampleInstrument *)instr;
    GUIPoint position = paramAnchor();

    addTypeField();

    Variable *v = instrument->FindVariable(SIP_SAMPLE);
    SamplePool *sp = SamplePool::GetInstance();
    int sampleMax = sp->GetNameListSize() - 1;
    if (sampleMax < 0) {
        sampleMax = 0;
    }
    UIIntVarField *f1 =
        new UIIntVarField(position, *v, "sample: %s", 0, sampleMax, 1, 0x10);
    T_SimpleList<UIField>::Insert(f1);

    position._y += 1;
#ifdef FFMPEG_ENABLED
    v = instrument->FindVariable(SIP_PRINTFX);
    f1 = new UIIntVarField(position, *v, "%s", 0, 3, 1, 2);
    T_SimpleList<UIField>::Insert(f1);

    position._x += 7;
    v = instrument->FindVariable(SIP_IR_WET);
    f1 = new UIIntVarField(position, *v, "wet: %d%%", 0, 100, 1, 10);
    T_SimpleList<UIField>::Insert(f1);
    position._x -= 7;

    position._y += 1;
    v = instrument->FindVariable(SIP_IR_PAD);
    f1 = new UIIntVarField(position, *v, "pad: %dms", 0, 5000, 5, 100);
    T_SimpleList<UIField>::Insert(f1);
#endif

    position._y += 1;
    v = instrument->FindVariable(SIP_CRUSH);
    f1 = new UIIntVarField(position, *v, "crush: %d", 1, 0x10, 1, 4);
    T_SimpleList<UIField>::Insert(f1);

    position._y += 1;
    v = instrument->FindVariable(SIP_CRUSHVOL);
    f1 = new UIIntVarField(position, *v, "drive: %2.2X", 0, 0xFF, 1, 0x10);
    T_SimpleList<UIField>::Insert(f1);

    position._y += 1;
    v = instrument->FindVariable(SIP_DOWNSMPL);
    f1 = new UIIntVarField(position, *v, "downsample: %d", 0, 8, 1, 4);
    T_SimpleList<UIField>::Insert(f1);

    position._y += 1;
    v = instrument->FindVariable(SIP_VOLUME);
    f1 = new UIIntVarField(position, *v, "volume: %d [%2.2X]", 0, 255, 1, 10);
    T_SimpleList<UIField>::Insert(f1);

    position._y += 1;
    v = instrument->FindVariable(SIP_PAN);
    f1 = new UIIntVarField(position, *v, "pan: %2.2X", 0, 0xFE, 1, 0x10);
    T_SimpleList<UIField>::Insert(f1);

    position._y += 1;
    v = instrument->FindVariable(SIP_ROOTNOTE);
    f1 = new UINoteVarField(position, *v, "root note: %s", 0, 0x7F, 1, 0x0C);
    T_SimpleList<UIField>::Insert(f1);

    position._y += 1;
    v = instrument->FindVariable(SIP_FINETUNE);
    f1 = new UIIntVarField(position, *v, "detune: %2.2X", 0, 255, 1, 0x10);
    T_SimpleList<UIField>::Insert(f1);

    position._y += 1;
    UIStaticField *sf = new UIStaticField(position, "flt cut/res:");
    T_SimpleList<UIField>::Insert(sf);

    position._x += 13;
    v = instrument->FindVariable(SIP_FILTCUTOFF);
    f1 = new UIIntVarField(position, *v, "%2.2X", 0, 0xFF, 1, 0x10);
    T_SimpleList<UIField>::Insert(f1);

    position._x += 3;
    v = instrument->FindVariable(SIP_FILTRESO);
    f1 = new UIIntVarField(position, *v, "%2.2X", 0, 0xFF, 1, 0x10);
    T_SimpleList<UIField>::Insert(f1);
    position._x -= 16;

    position._y += 1;
    v = instrument->FindVariable(SIP_FILTMIX);
    f1 = new UIIntVarField(position, *v, "type: %2.2X", 0, 0xFF, 1, 0x10);
    T_SimpleList<UIField>::Insert(f1);

    position._y += 1;
    v = instrument->FindVariable(SIP_FILTMODE);
    f1 = new UIIntVarField(position, *v, "Mode: %s", 0, 2, 1, 1);
    T_SimpleList<UIField>::Insert(f1);

    position._y += 1;
    v = instrument->FindVariable(SIP_ATTENUATE);
    f1 = new UIIntVarField(position, *v, "attenuate: %d [%2.2X]", 1, 0xFF, 1,
                           0x10);
    T_SimpleList<UIField>::Insert(f1);

    position._y += 1;
    sf = new UIStaticField(position, "fb tune/mix: ");
    T_SimpleList<UIField>::Insert(sf);

    v = instrument->FindVariable(SIP_FBTUNE);
    position._x += 13;
    f1 = new UIIntVarField(position, *v, "%2.2X", 0, 0xFF, 1, 0x10);
    T_SimpleList<UIField>::Insert(f1);

    position._x += 3;
    v = instrument->FindVariable(SIP_FBMIX);
    f1 = new UIIntVarField(position, *v, "%2.2X", 0, 0xFF, 1, 0X10);
    T_SimpleList<UIField>::Insert(f1);

    position._x -= 16;

    position._y += 1;
    v = instrument->FindVariable(SIP_INTERPOLATION);
    f1 = new UIIntVarField(position, *v, "interpolation: %s", 0, 1, 1, 1);
    T_SimpleList<UIField>::Insert(f1);

    position._y += 1;
    v = instrument->FindVariable(SIP_LOOPMODE);
    f1 = new UIIntVarField(position, *v, "loop mode: %s", 0, SILM_LAST - 1, 1,
                           1);
    T_SimpleList<UIField>::Insert(f1);

    position._y += 1;
    v = instrument->FindVariable(SIP_SLICES);
    f1 = new UIIntVarField(position, *v, "slices: %d", 1, 0xFF, 1, 0x10);
    T_SimpleList<UIField>::Insert(f1);

    if (v->GetInt() > 1) {
        position._y += 1;
        v = instrument->FindVariable(SIP_SLICETOEND);
        f1 = new UIIntVarField(position, *v, "to end: %s", 0, 1, 1, 1);
        T_SimpleList<UIField>::Insert(f1);
    }

    int sampleEnd = instrument->GetSampleSize() - 1;
    if (sampleEnd < 0) {
        sampleEnd = 0;
    }

    position._y += 1;
    v = instrument->FindVariable(SIP_START);
    f1 = new UIBigHexVarField(position, *v, 7, "start: %7.7X", 0, sampleEnd, 16);
    T_SimpleList<UIField>::Insert(f1);

    position._y += 1;
    v = instrument->FindVariable(SIP_LOOPSTART);
    f1 = new UIBigHexVarField(position, *v, 7, "loop start: %7.7X", 0, sampleEnd,
                              16);
    T_SimpleList<UIField>::Insert(f1);

    position._y += 1;
    v = instrument->FindVariable(SIP_END);
    f1 = new UIBigHexVarField(position, *v, 7, "loop end: %7.7X", 0, sampleEnd,
                              16);
    T_SimpleList<UIField>::Insert(f1);

    v = instrument->FindVariable(SIP_TABLEAUTO);
    position._y += 1;
    UIIntVarField *f2 =
        new UIIntVarField(position, *v, "automation: %s", 0, 1, 1, 1);
    T_SimpleList<UIField>::Insert(f2);

    position._y += 1;
    v = instrument->FindVariable(SIP_TABLE);
    f1 = new UIIntVarOffField(position, *v, "table: %2.2X", 0x00, 0x7F, 1, 0x10);
    T_SimpleList<UIField>::Insert(f1);
};

void InstrumentView::fillMidiParameters() {

    int i = viewData_->currentInstrument_;
    InstrumentBank *bank = viewData_->project_->GetInstrumentBank();
    I_Instrument *instr = bank->GetInstrument(i);
    MidiInstrument *instrument = (MidiInstrument *)instr;
    GUIPoint position = paramAnchor();

    Variable *v = instrument->FindVariable(MIP_CHANNEL);
    UIIntVarField *f1 =
        new UIIntVarField(position, *v, "channel: %2.2d", 0, 0x0F, 1, 0x04, 1);
    T_SimpleList<UIField>::Insert(f1);
    f1->SetFocus();

    position._y += 1;
    v = instrument->FindVariable(MIP_VOLUME);
    f1 = new UIIntVarField(position, *v, "volume: %2.2X", 0, 0xFF, 1, 0x10);
    T_SimpleList<UIField>::Insert(f1);

    position._y += 1;
    v = instrument->FindVariable(MIP_NOTELENGTH);
    f1 = new UIIntVarField(position, *v, "length: %2.2X", 0, 0xFF, 1, 0x10);
    T_SimpleList<UIField>::Insert(f1);

    v = instrument->FindVariable(MIP_TABLEAUTO);
    position._y += 2;
    UIIntVarField *f2 =
        new UIIntVarField(position, *v, "automation: %s", 0, 1, 1, 1);
    T_SimpleList<UIField>::Insert(f2);

    position._y += 1;
    v = instrument->FindVariable(MIP_TABLE);
    f1 = new UIIntVarOffField(position, *v, "table: %2.2X", 0, 0x7F, 1, 0x10);
    T_SimpleList<UIField>::Insert(f1);
};

void InstrumentView::fillSynthParameters() {

    int i = viewData_->currentInstrument_;
    InstrumentBank *bank = viewData_->project_->GetInstrumentBank();
    I_Instrument *instr = bank->GetInstrument(i);
    SynthInstrument *instrument = (SynthInstrument *)instr;
    GUIPoint position = paramAnchor();

    addTypeField();

    // Left column: ADSR + LFO
    Variable *v = instrument->FindVariable(SYP_ATTACK);
    UIIntVarField *f1 =
        new UIIntVarField(position, *v, "attack: %2.2X", 0, 0xFF, 1, 0x10);
    T_SimpleList<UIField>::Insert(f1);

    position._y += 1;
    v = instrument->FindVariable(SYP_DECAY);
    f1 = new UIIntVarField(position, *v, "decay: %2.2X", 0, 0xFF, 1, 0x10);
    T_SimpleList<UIField>::Insert(f1);

    position._y += 1;
    v = instrument->FindVariable(SYP_SUSTAIN);
    f1 = new UIIntVarField(position, *v, "sustain: %2.2X", 0, 0xFF, 1, 0x10);
    T_SimpleList<UIField>::Insert(f1);

    position._y += 1;
    v = instrument->FindVariable(SYP_RELEASE);
    f1 = new UIIntVarField(position, *v, "release: %2.2X", 0, 0xFF, 1, 0x10);
    T_SimpleList<UIField>::Insert(f1);

    position._y += 2;
    v = instrument->FindVariable(SYP_LFORATE);
    f1 = new UIIntVarField(position, *v, "lfo rate: %2.2X", 0, 0xFF, 1, 0x10);
    T_SimpleList<UIField>::Insert(f1);

    position._y += 1;
    v = instrument->FindVariable(SYP_LFODEPTH);
    f1 = new UIIntVarField(position, *v, "lfo depth: %2.2X", 0, 0xFF, 1, 0x10);
    T_SimpleList<UIField>::Insert(f1);

    // Right column: waveform list (field tracks selected row).
    v = instrument->FindVariable(SYP_WAVEFORM);
    int wave = v->GetInt();
    if (wave < 0) {
        wave = 0;
    }
    if (wave >= SYW_LAST) {
        wave = SYW_LAST - 1;
    }
    GUIPoint wavePos(22, paramBaseY_ + wave);
    waveField_ =
        new UIIntVarField(wavePos, *v, "%s", 0, SYW_LAST - 1, 1, 1);
    T_SimpleList<UIField>::Insert(waveField_);

    // Bottom: volume / pan / root / detune / automation / table
    position._y = paramBaseY_ + 8;
    position._x = 1;
    v = instrument->FindVariable(SYP_VOLUME);
    f1 = new UIIntVarField(position, *v, "volume: %d [%2.2X]", 0, 255, 1, 10);
    T_SimpleList<UIField>::Insert(f1);

    position._y += 1;
    v = instrument->FindVariable(SYP_PAN);
    f1 = new UIIntVarField(position, *v, "pan: %2.2X", 0, 0xFE, 1, 0x10);
    T_SimpleList<UIField>::Insert(f1);

    position._y += 1;
    v = instrument->FindVariable(SYP_ROOTNOTE);
    f1 = new UINoteVarField(position, *v, "root note: %s", 0, 0x7F, 1, 0x0C);
    T_SimpleList<UIField>::Insert(f1);

    position._y += 1;
    v = instrument->FindVariable(SYP_FINETUNE);
    f1 = new UIIntVarField(position, *v, "detune: %2.2X", 0, 255, 1, 0x10);
    T_SimpleList<UIField>::Insert(f1);

    v = instrument->FindVariable(SYP_TABLEAUTO);
    position._y += 2;
    UIIntVarField *f2 =
        new UIIntVarField(position, *v, "automation: %s", 0, 1, 1, 1);
    T_SimpleList<UIField>::Insert(f2);

    position._y += 1;
    v = instrument->FindVariable(SYP_TABLE);
    f1 = new UIIntVarOffField(position, *v, "table: %2.2X", 0x00, 0x7F, 1, 0x10);
    T_SimpleList<UIField>::Insert(f1);
};

void InstrumentView::warpToNext(int offset) {
    int instrument = viewData_->currentInstrument_ + offset;
    if (instrument >= MAX_INSTRUMENT_COUNT) {
        instrument = instrument - MAX_INSTRUMENT_COUNT;
    };
    if (instrument < 0) {
        instrument = MAX_INSTRUMENT_COUNT + instrument;
    };
    viewData_->currentInstrument_ = instrument;
    onInstrumentChange();
    isDirty_ = true;
};

void InstrumentView::ProcessButtonMask(unsigned short mask, bool pressed) {

    if (!pressed)
        return;

    isDirty_ = false;

    if (viewMode_ == VM_NEW) {
        if (mask == EPBM_A) {
            UIIntVarField *field = (UIIntVarField *)GetFocus();
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
        if ((mask & EPBM_A) && (mask & EPBM_L)) {
            UIIntVarField *field = (UIIntVarField *)GetFocus();
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
    };

    if (viewMode_ == VM_SELECTION) {
    } else {
        viewMode_ = VM_NORMAL;
    }

    FieldView::ProcessButtonMask(mask);
    refreshWaveformFieldPosition();

    // Re-type / rebuild only after the field ProcessArrow has returned;
    // rebuilding inside a WatchedVariable notify would delete the active UIField.
    applyTypeChangeIfNeeded();
    applySlicesUiIfNeeded();

    Player *player = Player::GetInstance();
    // B Modifier

    if (mask & EPBM_B) {
        if (mask & EPBM_LEFT)
            warpToNext(-1);
        if (mask & EPBM_RIGHT)
            warpToNext(+1);
        if (mask & EPBM_DOWN)
            warpToNext(-16);
        if (mask & EPBM_UP)
            warpToNext(+16);
        if (mask & EPBM_A) { // Allow cut instrument
            if (getInstrumentType() == IT_SAMPLE) {
                UIIntVarField *focus = (UIIntVarField *)GetFocus();
                if (focus && focus->GetVariableID() == SIP_SAMPLE) {
                    int i = viewData_->currentInstrument_;
                    InstrumentBank *bank =
                        viewData_->project_->GetInstrumentBank();
                    I_Instrument *instr = bank->GetInstrument(i);
                    instr->Purge();
                    isDirty_ = true;
                }
            }

            // Check if on table
            if (GetFocus() == T_SimpleList<UIField>::GetLast()) {
                int i = viewData_->currentInstrument_;
                InstrumentBank *bank =
                    viewData_->project_->GetInstrumentBank();
                I_Instrument *instr = bank->GetInstrument(i);
                Variable *v = instr->FindVariable(SIP_TABLE);
                if (!v) {
                    v = instr->FindVariable(SYP_TABLE);
                }
                if (v) {
                    v->SetInt(-1);
                    isDirty_ = true;
                }
            };
        }
        if (mask & EPBM_L) {
            viewMode_ = VM_CLONE;
        };
    } else {

        // A modifier

        if (mask == EPBM_A) {
            FourCC varID = ((UIIntVarField *)GetFocus())->GetVariableID();
            if ((varID == SIP_TABLE) || (varID == MIP_TABLE) ||
                (varID == SYP_TABLE) || (varID == SIP_SAMPLE) ||
                (varID == SIP_PRINTFX)) {
                viewMode_ = VM_NEW;
            }
        } else {

            // R Modifier

            if (mask & EPBM_R) {
                if (mask & EPBM_LEFT) {
                    ViewType vt = VT_PHRASE;
                    ViewEvent ve(VET_SWITCH_VIEW, &vt);
                    SetChanged();
                    NotifyObservers(&ve);
                }

                if (mask & EPBM_DOWN) {

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
                    player->OnStartButton(PM_PHRASE, viewData_->songX_, true,
                                          viewData_->chainRow_);
                }
            } else {
                // No modifier
                if (mask & EPBM_START) {
                    player->OnStartButton(PM_PHRASE, viewData_->songX_, false,
                                          viewData_->chainRow_);
                }
            }
        }
    }

    UIIntVarField *field = (UIIntVarField *)GetFocus();
    if (field) {
        lastFocusID_ = field->GetVariableID();
    }
};

void InstrumentView::drawTypeTabs() {
    GUITextProperties props;
    InstrumentType it = getInstrumentType();

    // Overwrite the raw "%s" type field with both tab labels.
    props.invert_ = (it == IT_SYNTH);
    SetColor(props.invert_ ? CD_HILITE2 : CD_NORMAL);
    DrawString(26, 0, "synth ", props);

    props.invert_ = (it == IT_SAMPLE);
    SetColor(props.invert_ ? CD_HILITE2 : CD_NORMAL);
    DrawString(32, 0, "sample", props);

    SetColor(CD_NORMAL);
}

void InstrumentView::drawWaveformPicker() {
    if (getInstrumentType() != IT_SYNTH || !current_) {
        return;
    }
    Variable *v = current_->FindVariable(SYP_WAVEFORM);
    if (!v) {
        return;
    }
    int wave = v->GetInt();
    GUITextProperties props;
    for (int i = 0; i < SYW_LAST; i++) {
        // Selected row is drawn by the focused UIIntVarField; paint the rest.
        if (i == wave) {
            continue;
        }
        props.invert_ = false;
        SetColor(CD_NORMAL);
        DrawString(22, paramBaseY_ + i, synthWaveNames[i], props);
    }
}

void InstrumentView::ensureWaveformCache(short *pcm, int domain, int nCh,
                                         int sampleIdx, int nSlices) {
    if (waveCacheSample_ == sampleIdx && waveCacheDomain_ == domain &&
        waveCacheSlices_ == nSlices) {
        return;
    }
    waveCacheSample_ = sampleIdx;
    waveCacheDomain_ = domain;
    waveCacheSlices_ = nSlices;
    for (int b = 0; b < kWaveBins; b++) {
        int start = (int)((long)b * domain / kWaveBins);
        int end = (int)((long)(b + 1) * domain / kWaveBins);
        if (end <= start) {
            end = start + 1;
        }
        if (end > domain) {
            end = domain;
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
    int wave = 0, atk = 0, dec = 0, sus = 0, rel = 0, rate = 0, depth = 0;
    Variable *v;
    if ((v = current_->FindVariable(SYP_WAVEFORM)))
        wave = v->GetInt();
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
    return (wave & 0x7) | ((atk & 0xFF) << 3) | ((dec & 0xFF) << 11) |
           ((sus & 0xFF) << 19) | ((rel & 0x1F) << 27) | ((rate & 0xFF) << 8) |
           ((depth & 0xFF) << 16);
}

void InstrumentView::clearWaveformStrip(int px, int py, int pw, int ph) {
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
    overlayDrawnDomain_ = -1;
    overlayDrawnSlices_ = -1;
    overlayDrawnSynthKey_ = 0;
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

    int domain = si->GetLoopEnd();
    if (domain <= 0 || domain > nFrames) {
        domain = nFrames;
    }

    Variable *slicesVar = si->FindVariable(SIP_SLICES);
    int nSlices = slicesVar ? slicesVar->GetInt() : 1;
    if (nSlices < 1) {
        nSlices = 1;
    }

    if (overlayValid_ && overlayDrawnSample_ == sampleIdx &&
        overlayDrawnDomain_ == domain && overlayDrawnSlices_ == nSlices &&
        overlayDrawnSynthKey_ == 0) {
        return;
    }

    ensureWaveformCache(pcm, domain, nCh, sampleIdx, nSlices);

    GUIRect stripDirty(px - 2, py, px + pw + 2, py + ph);
    aw.BeginBatchDraw();

    aw.FillRect(stripDirty, CD_BACKGROUND);
    GUIRect leftRail(px - 2, py, px, py + ph);
    GUIRect rightRail(px + pw, py, px + pw + 2, py + ph);
    aw.FillRect(leftRail, CD_BORDER);
    aw.FillRect(rightRail, CD_BORDER);

    const int midY = py + ph / 2;
    const int maxAmp = ph / 2 - 1;
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
        GUIRect column(x0, midY - amp, x1, midY + amp + 1);
        aw.FillRect(column, CD_HILITE1);
    }

    if (nSlices > 1) {
        for (int s = 1; s < nSlices; s++) {
            int mx = px + (int)((long)s * pw / nSlices);
            GUIRect marker(mx, py, mx + 1, py + ph);
            aw.FillRect(marker, CD_BORDER);
        }
    }

    // Loop region markers (triangles ≈ tip bars at start/end).
    Variable *loopStart = si->FindVariable(SIP_LOOPSTART);
    Variable *loopEnd = si->FindVariable(SIP_END);
    if (loopStart && loopEnd && domain > 0) {
        int ls = loopStart->GetInt();
        int le = loopEnd->GetInt();
        if (ls < 0)
            ls = 0;
        if (le > domain)
            le = domain;
        if (le > ls) {
            int x0 = px + (int)((long)ls * pw / domain);
            int x1 = px + (int)((long)le * pw / domain);
            GUIRect tip0(x0, py, x0 + 2, py + 2);
            GUIRect tip1(x1 - 2, py, x1, py + 2);
            aw.FillRect(tip0, CD_PLAY);
            aw.FillRect(tip1, CD_PLAY);
            GUIRect box(x0, py, x1, py + ph);
            // Light outline via top/bottom edges
            GUIRect top(x0, py, x1, py + 1);
            GUIRect bot(x0, py + ph - 1, x1, py + ph);
            aw.FillRect(top, CD_NORMAL);
            aw.FillRect(bot, CD_NORMAL);
        }
    }

    aw.EndBatchDraw(stripDirty);

    overlayValid_ = true;
    overlayDrawnSample_ = sampleIdx;
    overlayDrawnDomain_ = domain;
    overlayDrawnSlices_ = nSlices;
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

    overlayValid_ = true;
    overlayDrawnSample_ = -2;
    overlayDrawnDomain_ = -1;
    overlayDrawnSlices_ = -1;
    overlayDrawnSynthKey_ = key;
}

void InstrumentView::DrawOverlay() {
    if (View::miniLayout_) {
        return;
    }

    // Do not draw pixel overlay (or FillRect over the dialog) while a modal
    // is active; DoModal/EndModal invalidate the char cache so Flush repaints
    // the background and dialog cleanly.
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

    if (it == IT_SAMPLE) {
        drawSampleOverlay(px, py, pw, ph);
    } else {
        drawSynthOverlay(px, py, pw, ph);
    }
}

void InstrumentView::DrawView() {

    Clear();
    View::EnableNotification();
    overlayValid_ = false;

    GUITextProperties props;
    GUIPoint pos = GetTitlePosition();

    char title[20];
    SetColor(CD_NORMAL);
    sprintf(title, "Instrument %2.2X", viewData_->currentInstrument_);
    DrawString(pos._x, pos._y, title, props);

    // Reserve blank rows under the title for the pixel waveform strip.
    if (!View::miniLayout_) {
        SetColor(CD_BACKGROUND);
        props.invert_ = false;
        char blank[40];
        memset(blank, ' ', 38);
        blank[38] = 0;
        for (int r = 1; r <= kWaveStripRows; r++) {
            DrawString(1, r, blank, props);
        }
        SetColor(CD_NORMAL);
    }

    FieldView::Redraw();
    drawTypeTabs();
    drawWaveformPicker();
};

void InstrumentView::OnFocus() { onInstrumentChange(); }

void InstrumentView::Update(Observable &o, I_ObservableData *d) {
    (void)o;
    (void)d;
    onInstrumentChange();
}
