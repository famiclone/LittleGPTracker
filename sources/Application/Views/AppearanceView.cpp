#include "AppearanceView.h"
#include "Application/AppWindow.h"
#include "Application/Model/ColorScheme.h"
#include "Application/Views/ModalDialogs/NewProjectDialog.h"
#include "Application/Views/ModalDialogs/SelectSchemeDialog.h"
#include "BaseClasses/UIActionField.h"
#include "BaseClasses/UIIntVarField.h"
#include <stdlib.h>
#include <string.h>

#define ACTION_BACK MAKE_FOURCC('B', 'A', 'C', 'K')
#define ACTION_SAVE MAKE_FOURCC('S', 'A', 'V', 'E')
#define ACTION_LOAD MAKE_FOURCC('L', 'O', 'A', 'D')

static void SaveSchemeNamedCallback(View &v, ModalView &dialog) {
    NewProjectDialog &npd = (NewProjectDialog &)dialog;
    if (dialog.GetReturnCode() > 0) {
        std::string name = npd.GetName();
        ((AppearanceView &)v).OnSaveScheme(name.c_str());
    }
}

static void LoadSchemeCallback(View &v, ModalView &dialog) {
    SelectSchemeDialog &ssd = (SelectSchemeDialog &)dialog;
    if (dialog.GetReturnCode() > 0) {
        const char *name = ssd.GetSelection();
        if (name && ColorScheme::Load(name)) {
            v.SetNotification("Scheme loaded");
            ((AppearanceView &)v).OnFocus();
        } else {
            v.SetNotification("Load failed");
        }
    }
}

AppearanceView::AppearanceView(GUIWindow &w, ViewData *data)
    : FieldView(w, data), colorSelect_(0), rVar_(0), gVar_(0), bVar_(0),
      colorLabels_(0), colorLabelCount_(0), lastColorIndex_(-1), lastR_(-1),
      lastG_(-1), lastB_(-1) {

    colorLabelCount_ = ColorScheme::EntryCount();
    colorLabels_ = (char **)malloc(sizeof(char *) * colorLabelCount_);
    for (int i = 0; i < colorLabelCount_; i++) {
        const char *label = ColorScheme::Entries()[i].label;
        colorLabels_[i] = (char *)malloc(strlen(label) + 1);
        strcpy(colorLabels_[i], label);
    }

    colorSelect_ =
        new WatchedVariable("color", 0, colorLabels_, colorLabelCount_, 0);
    colorSelect_->AddObserver(*this);

    rVar_ = new Variable("r", 0, 0);
    gVar_ = new Variable("g", 0, 0);
    bVar_ = new Variable("b", 0, 0);

    GUIPoint position = GetAnchor();

    UIIntVarField *field = new UIIntVarField(
        position, *colorSelect_, "Color: %s", 0, colorLabelCount_ - 1, 1, 1);
    T_SimpleList<UIField>::Insert(field);

    position._y += 2;
    field = new UIIntVarField(position, *rVar_, "R: %2.2X [%d]", 0, 255, 1, 16);
    T_SimpleList<UIField>::Insert(field);

    position._y += 1;
    field = new UIIntVarField(position, *gVar_, "G: %2.2X [%d]", 0, 255, 1, 16);
    T_SimpleList<UIField>::Insert(field);

    position._y += 1;
    field = new UIIntVarField(position, *bVar_, "B: %2.2X [%d]", 0, 255, 1, 16);
    T_SimpleList<UIField>::Insert(field);

    // leave one row for the color swatch drawn in DrawView
    position._y += 3;
    UIActionField *a =
        new UIActionField("Save Scheme", ACTION_SAVE, position);
    a->AddObserver(*this);
    T_SimpleList<UIField>::Insert(a);

    position._y += 1;
    a = new UIActionField("Load Scheme", ACTION_LOAD, position);
    a->AddObserver(*this);
    T_SimpleList<UIField>::Insert(a);

    position._y += 2;
    a = new UIActionField("Back", ACTION_BACK, position);
    a->AddObserver(*this);
    T_SimpleList<UIField>::Insert(a);

    loadColorIntoFields();
}

AppearanceView::~AppearanceView() {
    delete colorSelect_;
    delete rVar_;
    delete gVar_;
    delete bVar_;
    if (colorLabels_) {
        for (int i = 0; i < colorLabelCount_; i++)
            free(colorLabels_[i]);
        free(colorLabels_);
    }
}

void AppearanceView::OnSaveScheme(const char *name) {
    if (ColorScheme::Save(name)) {
        SetNotification("Scheme saved");
    } else {
        SetNotification("Save failed");
    }
}

void AppearanceView::goBackToSettings() {
    ViewType vt = VT_SETTINGS;
    ViewEvent ve(VET_SWITCH_VIEW, &vt);
    SetChanged();
    NotifyObservers(&ve);
}

void AppearanceView::loadColorIntoFields() {
    int idx = colorSelect_->GetInt();
    if (idx < 0 || idx >= ColorScheme::EntryCount())
        return;
    GUIColor c =
        AppWindow::GetPaletteColor(ColorScheme::Entries()[idx].cd);
    rVar_->SetInt((int)(c._r & 0xFF), false);
    gVar_->SetInt((int)(c._g & 0xFF), false);
    bVar_->SetInt((int)(c._b & 0xFF), false);
    lastColorIndex_ = idx;
    lastR_ = rVar_->GetInt();
    lastG_ = gVar_->GetInt();
    lastB_ = bVar_->GetInt();
}

void AppearanceView::applyFieldsToColor() {
    int idx = colorSelect_->GetInt();
    if (idx < 0 || idx >= ColorScheme::EntryCount())
        return;
    int r = rVar_->GetInt();
    int g = gVar_->GetInt();
    int b = bVar_->GetInt();
    if (idx == lastColorIndex_ && r == lastR_ && g == lastG_ && b == lastB_)
        return;
    AppWindow::SetPaletteColor(ColorScheme::Entries()[idx].cd,
                               GUIColor(r, g, b));
    lastColorIndex_ = idx;
    lastR_ = r;
    lastG_ = g;
    lastB_ = b;
    AppWindow *w = AppWindow::GetInstance();
    if (w)
        w->InvalidatePalette();
}

void AppearanceView::OnFocus() { loadColorIntoFields(); }

void AppearanceView::ProcessButtonMask(unsigned short mask, bool pressed) {
    if (!pressed)
        return;

    FieldView::ProcessButtonMask(mask);
    applyFieldsToColor();

    if (mask & EPBM_R) {
        if ((mask & EPBM_LEFT) || (mask & EPBM_UP)) {
            goBackToSettings();
        }
    }
}

void AppearanceView::DrawView() {
    applyFieldsToColor();

    Clear();

    GUITextProperties props;
    GUIPoint pos = GetTitlePosition();
    SetColor(CD_NORMAL);
    DrawString(pos._x, pos._y, "Appearance", props);

    FieldView::Redraw();

    GUIPoint anchor = GetAnchor();
    // Swatch sits in the blank row under B (anchor._y + 5)
    GUIRect swatch(anchor._x * AppWindow::CharWidth(),
                   (anchor._y + 5) * AppWindow::CharHeight(),
                   (anchor._x + 12) * AppWindow::CharWidth(),
                   (anchor._y + 6) * AppWindow::CharHeight());
    int idx = colorSelect_->GetInt();
    if (idx >= 0 && idx < ColorScheme::EntryCount()) {
        ((AppWindow &)w_)
            .FillRect(swatch, ColorScheme::Entries()[idx].cd);
    }

    drawMap();
    View::EnableNotification();
}

void AppearanceView::Update(Observable &, I_ObservableData *data) {
    if (!hasFocus_) {
        return;
    }

    // WatchedVariable notifies with null data when color selection changes
    if (!data) {
        loadColorIntoFields();
        isDirty_ = true;
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
        goBackToSettings();
        break;
    case ACTION_SAVE: {
        ColorScheme::EnsureSchemesDir();
        NewProjectDialog *dlg =
            new NewProjectDialog(*this, "root:schemes", 0, false);
        DoModal(dlg, SaveSchemeNamedCallback);
        break;
    }
    case ACTION_LOAD: {
        SelectSchemeDialog *dlg = new SelectSchemeDialog(*this);
        DoModal(dlg, LoadSchemeCallback);
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
