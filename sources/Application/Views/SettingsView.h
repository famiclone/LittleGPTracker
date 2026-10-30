#ifndef _SETTINGS_VIEW_H_
#define _SETTINGS_VIEW_H_

#include "BaseClasses/FieldView.h"
#include "Foundation/Observable.h"
#include "ViewData.h"

class SettingsView : public FieldView, public I_Observer {
  public:
    SettingsView(GUIWindow &w, ViewData *data);
    virtual ~SettingsView();

    virtual void ProcessButtonMask(unsigned short mask, bool pressed);
    virtual void DrawView();
    virtual void OnPlayerUpdate(PlayerEventType, unsigned int) {}
    virtual void OnFocus() {}

    void Update(Observable &, I_ObservableData *);

  private:
    void goBackToProject();
    Project *project_;
};

#endif
