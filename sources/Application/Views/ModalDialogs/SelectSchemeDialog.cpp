#include "SelectSchemeDialog.h"
#include "Application/Model/ColorScheme.h"

#define DIALOG_WIDTH 22
#define VISIBLE_ROWS 8

SelectSchemeDialog::SelectSchemeDialog(View &view)
    : ModalView(view), selected_(0), topIndex_(0) {}

SelectSchemeDialog::~SelectSchemeDialog() {}

void SelectSchemeDialog::OnFocus() {
    ColorScheme::ListSchemes(schemes_);
    selected_ = 0;
    topIndex_ = 0;
}

void SelectSchemeDialog::DrawView() {
    SetWindow(DIALOG_WIDTH, VISIBLE_ROWS + 3);

    GUITextProperties props;
    SetColor(CD_NORMAL);
    DrawString(1, 0, "Load scheme", props);

    if (schemes_.empty()) {
        DrawString(1, 2, "(no schemes)", props);
        DrawString(1, VISIBLE_ROWS + 2, "B=cancel", props);
        return;
    }

    for (int row = 0; row < VISIBLE_ROWS; row++) {
        int idx = topIndex_ + row;
        if (idx >= (int)schemes_.size())
            break;
        props.invert_ = (idx == selected_);
        DrawString(1, 2 + row, schemes_[idx].c_str(), props);
    }
    props.invert_ = false;
    DrawString(1, VISIBLE_ROWS + 2, "A=load B=cancel", props);
}

void SelectSchemeDialog::ProcessButtonMask(unsigned short mask, bool pressed) {
    if (!pressed)
        return;

    if (mask == EPBM_B) {
        EndModal(0);
        return;
    }

    if (schemes_.empty())
        return;

    if (mask == EPBM_A) {
        EndModal(1);
        return;
    }
    if (mask == EPBM_UP) {
        if (selected_ > 0)
            selected_--;
        if (selected_ < topIndex_)
            topIndex_ = selected_;
        isDirty_ = true;
    }
    if (mask == EPBM_DOWN) {
        if (selected_ < (int)schemes_.size() - 1)
            selected_++;
        if (selected_ >= topIndex_ + VISIBLE_ROWS)
            topIndex_ = selected_ - VISIBLE_ROWS + 1;
        isDirty_ = true;
    }
}

const char *SelectSchemeDialog::GetSelection() const {
    if (schemes_.empty() || selected_ < 0 ||
        selected_ >= (int)schemes_.size())
        return 0;
    return schemes_[selected_].c_str();
}
