#ifndef _NEW_PROJECT_DIALOG_H_
#define _NEW_PROJECT_DIALOG_H_

#include "Application/Utils/KeyboardLayout.h"
#include "Application/Views/BaseClasses/ModalView.h"
#include <string>

#define MAX_NAME_LENGTH 12
#define BUTTONS_LENGTH 3

class NewProjectDialog:public ModalView {
public:
  // editShortName: optional leaf without prefix (rename / edit mode)
  // useLgptPrefix: project folders use "lgpt_"; schemes use bare names
  NewProjectDialog(View &view, Path currentPath = "root:",
                   const char *editShortName = 0, bool useLgptPrefix = true);
  virtual ~NewProjectDialog();

  virtual void DrawView();
  virtual void OnPlayerUpdate(PlayerEventType, unsigned int currentTick);
  virtual void OnFocus();
  virtual void ProcessButtonMask(unsigned short mask, bool pressed);

  std::string GetName();

private:
  Path currentPath_;
  int selected_;
  int lastChar_;
  char name_[MAX_NAME_LENGTH + 1];
  char initialShort_[MAX_NAME_LENGTH + 1];
  std::string allowedExistingName_;
  bool useLgptPrefix_;
  int currentChar_;
  bool keyboardMode_;
  int keyboardRow_;
  int keyboardCol_ ;
  void moveCursor(int direction);
  std::string entryNameForCheck(const std::string &fullName);
};
#endif
