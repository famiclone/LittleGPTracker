#ifndef _PROJECT_VIEW_H_
#define _PROJECT_VIEW_H_

#include "BaseClasses/FieldView.h"
#include "Foundation/Observable.h"
#include "ViewData.h"
#include <string>

class UIActionField;

class ProjectView: public FieldView,public I_Observer {
public:
	ProjectView(GUIWindow &w,ViewData *data) ;
	virtual ~ProjectView() ;

	virtual void ProcessButtonMask(unsigned short mask,bool pressed) ;
	virtual void DrawView() ;
	virtual void OnPlayerUpdate(PlayerEventType,unsigned int) {} ;
	virtual void OnFocus() {} ;

	// Observer for action callback

	void Update(Observable &,I_ObservableData *) ;

	void OnLoadProject() ;
	void OnSaveAsProject(char * data) ;
	void OnRenameProject(char *data) ;
	void OnPurgeInstruments(bool removeFromDisk) ;
	void OnQuit() ;

protected:
private:
  void refreshNameField() ;
  std::string currentShortName() ;

  Project *project_;
  // Debug
  unsigned long lastTick_;
  unsigned long lastClock_;
  UIField *tempoField_;
  UIActionField *nameField_;
} ;
#endif
