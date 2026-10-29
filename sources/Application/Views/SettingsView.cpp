#include "SettingsView.h"
#include "Application/Mixer/MixerService.h"
#include "BaseClasses/UIActionField.h"
#include "BaseClasses/UIIntVarField.h"
#include "Services/Midi/MidiService.h"

#define ACTION_BACK MAKE_FOURCC('B', 'A', 'C', 'K')
#define ACTION_APPEARANCE MAKE_FOURCC('A', 'P', 'P', 'R')

SettingsView::SettingsView(GUIWindow &w, ViewData *data) : FieldView(w, data) {
    project_ = data->project_;

    GUIPoint position = GetAnchor();

    Variable *v = project_->FindVariable(VAR_MIDIDEVICE);
    NAssert(v);
    UIIntVarField *field = new UIIntVarField(
        position, *v, "MIDI: %s", 0, MidiService::GetInstance()->Size(), 1, 1);
    T_SimpleList<UIField>::Insert(field);

    position._y += 2;
    v = project_->FindVariable(VAR_RENDER);
    NAssert(v);
    field = new UIIntVarField(position, *v, "Render: %s", 0,
                              project_->MAX_RENDER_MODE - 1, 1, 2);
    T_SimpleList<UIField>::Insert(field);

    position._y += 2;
    UIActionField *appearance =
        new UIActionField("Appearance", ACTION_APPEARANCE, position);
    appearance->AddObserver(*this);
    T_SimpleList<UIField>::Insert(appearance);

    position._y += 2;
    UIActionField *back = new UIActionField("Back", ACTION_BACK, position);
    back->AddObserver(*this);
    T_SimpleList<UIField>::Insert(back);
}

SettingsView::~SettingsView() {}

void SettingsView::goBackToProject() {
    ViewType vt = VT_PROJECT;
    ViewEvent ve(VET_SWITCH_VIEW, &vt);
    SetChanged();
    NotifyObservers(&ve);
}

void SettingsView::ProcessButtonMask(unsigned short mask, bool pressed) {
    if (!pressed) {
        return;
    }

    FieldView::ProcessButtonMask(mask);

    if (mask & EPBM_R) {
        if ((mask & EPBM_LEFT) || (mask & EPBM_UP)) {
            goBackToProject();
        } else if (mask & EPBM_RIGHT) {
            ViewType vt = VT_APPEARANCE;
            ViewEvent ve(VET_SWITCH_VIEW, &vt);
            SetChanged();
            NotifyObservers(&ve);
        }
    } else if (mask & EPBM_START) {
        Player *player = Player::GetInstance();
        int renderMode = viewData_->renderMode_;
        if (renderMode > 0 && !player->IsRunning()) {
            viewData_->isRendering_ = true;
            View::SetNotification("Rendering started!");
        } else if (viewData_->isRendering_ && player->IsRunning()) {
            viewData_->isRendering_ = false;
            View::SetNotification("Rendering done!");
        }
        player->OnStartButton(PM_SONG, viewData_->songX_, false,
                              viewData_->songX_);
    }
}

void SettingsView::DrawView() {
    Clear();

    GUITextProperties props;
    GUIPoint pos = GetTitlePosition();
    SetColor(CD_NORMAL);
    DrawString(pos._x, pos._y, "Settings", props);

    FieldView::Redraw();
    drawMap();

    int currentMode = project_->GetRenderMode();
    if ((viewData_->renderMode_ != currentMode) &&
        !MixerService::GetInstance()->IsRendering()) {
        if (currentMode > 0 && viewData_->renderMode_ == 0) {
            View::SetNotification("Rendering on, press start");
        } else if (currentMode == 0 && viewData_->renderMode_ > 0) {
            View::SetNotification("Rendering off");
        }
        viewData_->renderMode_ = currentMode;
        MixerService::GetInstance()->SetRenderMode(currentMode);
    }

    View::EnableNotification();
}

void SettingsView::Update(Observable &, I_ObservableData *data) {
    if (!hasFocus_) {
        return;
    }

#ifdef _64BIT
    int fourcc = *((int *)data);
#else
    int fourcc = (unsigned int)data;
#endif

    UIField *focus = GetFocus();
    if (focus) {
        focus->ClearFocus();
        focus->Draw(w_);
        w_.Flush();
        focus->SetFocus();
    }

    switch (fourcc) {
    case ACTION_BACK:
        goBackToProject();
        break;
    case ACTION_APPEARANCE: {
        ViewType vt = VT_APPEARANCE;
        ViewEvent ve(VET_SWITCH_VIEW, &vt);
        SetChanged();
        NotifyObservers(&ve);
        break;
    }
    default:
        break;
    }

    if (focus) {
        focus->Draw(w_);
    }
    isDirty_ = true;
}
