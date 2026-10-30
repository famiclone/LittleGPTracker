#ifndef _APPEARANCE_VIEW_H_
#define _APPEARANCE_VIEW_H_

#include "BaseClasses/FieldView.h"
#include "Foundation/Observable.h"
#include "Foundation/Variables/Variable.h"
#include "Foundation/Variables/WatchedVariable.h"
#include "ViewData.h"

class AppearanceView : public FieldView, public I_Observer {
  public:
    AppearanceView(GUIWindow &w, ViewData *data);
    virtual ~AppearanceView();

    virtual void ProcessButtonMask(unsigned short mask, bool pressed);
    virtual void DrawView();
    virtual void OnPlayerUpdate(PlayerEventType, unsigned int) {}
    virtual void OnFocus();

    void Update(Observable &, I_ObservableData *);
    void OnSaveScheme(const char *name);

  private:
    void goBackToSettings();
    void loadColorIntoFields();
    void applyFieldsToColor();

    WatchedVariable *colorSelect_;
    Variable *rVar_;
    Variable *gVar_;
    Variable *bVar_;

    char **colorLabels_;
    int colorLabelCount_;
    int lastColorIndex_;
    int lastR_, lastG_, lastB_;
};

#endif
