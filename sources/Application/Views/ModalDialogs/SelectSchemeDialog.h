#ifndef _SELECT_SCHEME_DIALOG_H_
#define _SELECT_SCHEME_DIALOG_H_

#include "Application/Views/BaseClasses/ModalView.h"
#include <string>
#include <vector>

class SelectSchemeDialog : public ModalView {
  public:
    SelectSchemeDialog(View &view);
    virtual ~SelectSchemeDialog();

    virtual void DrawView();
    virtual void OnPlayerUpdate(PlayerEventType, unsigned int) {}
    virtual void OnFocus();
    virtual void ProcessButtonMask(unsigned short mask, bool pressed);

    const char *GetSelection() const;

  private:
    std::vector<std::string> schemes_;
    int selected_;
    int topIndex_;
};

#endif
