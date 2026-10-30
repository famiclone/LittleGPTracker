#include "SDLEventManager.h"
#include "Application/Application.h"
#include "Application/Model/Config.h"
#include "System/Console/Trace.h"
#include "UIFramework/BasicDatas/GUIEvent.h"
#include "SDLGUIWindowImp.h"
#ifdef PLATFORM_TG5040
#include "Adapters/LINUX/System/LinuxPowerSleep.h"
#endif
bool SDLEventManager::finished_=false ;
bool SDLEventManager::dumpEvent_=false ;

SDLEventManager::SDLEventManager() 
{
}

SDLEventManager::~SDLEventManager() 
{
}

void SDLEventManager::EnsureJoyControllers(int slot) {
	if (slot < 0 || slot >= MAX_JOY_COUNT) {
		return;
	}
	char sourceName[128];
	if (!buttonCS_[slot]) {
		sprintf(sourceName, "buttonJoy%d", slot);
		buttonCS_[slot] = new ButtonControllerSource(sourceName);
	}
	if (!joystickCS_[slot]) {
		sprintf(sourceName, "axisJoy%d", slot);
		joystickCS_[slot] = new JoystickControllerSource(sourceName);
	}
	if (!hatCS_[slot]) {
		sprintf(sourceName, "hatJoy%d", slot);
		hatCS_[slot] = new HatControllerSource(sourceName);
	}
}

int SDLEventManager::FindJoySlot(SDL_JoystickID instanceId) const {
	for (int i = 0; i < MAX_JOY_COUNT; i++) {
		if (joystick_[i] &&
		    SDL_JoystickInstanceID(joystick_[i]) == instanceId) {
			return i;
		}
	}
	return -1;
}

void SDLEventManager::OnWakeFromSleep() {
	Trace::Log("EVENT", "wake from sleep: reinit input");
	for (int i = 0; i < MAX_JOY_COUNT; i++) {
		if (!joystick_[i]) {
			continue;
		}
		if (SDL_JoystickGetAttached(joystick_[i])) {
			continue;
		}
		Trace::Log("EVENT", "joystick slot %d detached, closing", i);
		SDL_JoystickClose(joystick_[i]);
		joystick_[i] = 0;
	}

	// Prefer reopening by device index into free/detached slots. Instance
	// IDs change across close/open, so event handlers use FindJoySlot.
	int joyCount = SDL_NumJoysticks();
	joyCount = (joyCount > MAX_JOY_COUNT) ? MAX_JOY_COUNT : joyCount;
	for (int i = 0; i < joyCount; i++) {
		if (joystick_[i] && SDL_JoystickGetAttached(joystick_[i])) {
			continue;
		}
		if (joystick_[i]) {
			SDL_JoystickClose(joystick_[i]);
			joystick_[i] = 0;
		}
		joystick_[i] = SDL_JoystickOpen(i);
		EnsureJoyControllers(i);
		Trace::Log("EVENT", "joystick[%d]=%x (id=%d)", i, joystick_[i],
		           joystick_[i] ? SDL_JoystickInstanceID(joystick_[i]) : -1);
	}

	GUIWindow *appWindow = Application::GetInstance()->GetWindow();
	if (appWindow) {
		SDLGUIWindowImp *sdlWindow =
		    (SDLGUIWindowImp *)appWindow->GetImpWindow();
		if (sdlWindow) {
			sdlWindow->Invalidate();
		}
	}
}

bool SDLEventManager::Init() 
{
	EventManager::Init() ;
	
	if ( SDL_Init(SDL_INIT_VIDEO|SDL_INIT_JOYSTICK|SDL_INIT_TIMER) < 0 )
  {
		return false;
	}
  
	SDL_ShowCursor(SDL_DISABLE);
	
	atexit(SDL_Quit) ;
	
  SDL_InitSubSystem(SDL_INIT_JOYSTICK);

	int joyCount=SDL_NumJoysticks() ;
	joyCount=(joyCount>MAX_JOY_COUNT)?MAX_JOY_COUNT:joyCount ;

	keyboardCS_=new KeyboardControllerSource("keyboard") ;
	const char *dumpIt=Config::GetInstance()->GetValue("DUMPEVENT") ;
	if ((dumpIt)&&(!strcmp(dumpIt,"YES")))
  {
		dumpEvent_=true ;
	}

	for (int i=0;i<MAX_JOY_COUNT;i++) 
  {
		joystick_[i]=0 ;
		buttonCS_[i]=0 ;
		joystickCS_[i]=0 ;
		hatCS_[i]=0 ;
	}
    
	for (int i=0;i<joyCount;i++) 
  {
		joystick_[i]=SDL_JoystickOpen(i) ;
    Trace::Log("EVENT","joystick[%d]=%x",i,joystick_[i]) ;
		Trace::Log("EVENT","Number of axis:%d",SDL_JoystickNumAxes(joystick_[i])) ;
		Trace::Log("EVENT","Number of buttons:%d",SDL_JoystickNumButtons(joystick_[i])) ;
		Trace::Log("EVENT","Number of hats:%d",SDL_JoystickNumHats(joystick_[i])) ;
		EnsureJoyControllers(i);
	}
  
	return true ;
} 

int SDLEventManager::MainLoop() 
{
	GUIWindow *appWindow=Application::GetInstance()->GetWindow() ;
	SDLGUIWindowImp *sdlWindow=(SDLGUIWindowImp *)appWindow->GetImpWindow() ;
	while (!finished_)
	{
		SDL_Event event;
		if (SDL_WaitEvent(&event)) 
    {
			switch (event.type) {
				case SDL_KEYDOWN:
					if (dumpEvent_) 
          {
                        Trace::Log("EVENT","key(%s:%d):%d",SDL_GetScancodeName(event.key.keysym.scancode),event.key.keysym.scancode,1) ;
					}
                    keyboardCS_->SetKey((int)event.key.keysym.scancode,true) ;
					break ;

				case SDL_KEYUP:
					if (dumpEvent_) 
          {
                        Trace::Log("EVENT","key(%s:%d):%d",SDL_GetScancodeName(event.key.keysym.scancode),event.key.keysym.scancode,0) ;
					}
                    keyboardCS_->SetKey((int)event.key.keysym.scancode,false) ;
					break ;


				case SDL_JOYBUTTONDOWN:
				case SDL_JOYBUTTONUP: {
					int slot = FindJoySlot(event.jbutton.which);
					if (slot < 0 || !buttonCS_[slot]) {
						break;
					}
					if (dumpEvent_ && event.type == SDL_JOYBUTTONUP) {
						Trace::Log("EVENT","but(%d):%d",slot,event.jbutton.button) ;
					}
					buttonCS_[slot]->SetButton(event.jbutton.button,
					                           event.type == SDL_JOYBUTTONDOWN);
					break;
				}
				case SDL_JOYAXISMOTION: {
					int slot = FindJoySlot(event.jaxis.which);
					if (slot < 0 || !joystickCS_[slot]) {
						break;
					}
					if (dumpEvent_) {
						Trace::Log("EVENT","joy(%d)::%d=%d",slot,event.jaxis.axis,event.jaxis.value) ;
					}
					joystickCS_[slot]->SetAxis(event.jaxis.axis,float(event.jaxis.value)/32767.0f) ;
					break;
				}
				case SDL_JOYHATMOTION: {
					int slot = FindJoySlot(event.jhat.which);
					if (slot < 0 || !hatCS_[slot]) {
						break;
					}
					if (dumpEvent_)
          {
						for (int i=0;i<4;i++)
            {
							int mask = 1<<i ;
							if (event.jhat.value&mask)
              {
								Trace::Log("EVENT","hat(%d)::%d::%d",slot,event.jhat.hat,i) ;
							}
						}
					}
					hatCS_[slot]->SetHat(event.jhat.hat,event.jhat.value) ;
					break;
				}
				case SDL_JOYBALLMOTION:
					if (dumpEvent_)
          {
						Trace::Log("EVENT","ball(%d)::%d=(%d,%d)",event.jball.which,event.jball.ball,event.jball.xrel,event.jball.yrel) ;
					}
					break ;
#ifdef PLATFORM_TG5040
				case SDL_JOYDEVICEREMOVED:
				case SDL_JOYDEVICEADDED:
					OnWakeFromSleep();
					break;
#endif
		}

			switch (event.type) 
			{

				case SDL_QUIT:
					sdlWindow->ProcessQuit() ;
					break ;
                case SDL_WINDOWEVENT:
                    switch (event.window.event)
                    {
                        case SDL_WINDOWEVENT_EXPOSED:
                        case SDL_WINDOWEVENT_RESIZED:
                        case SDL_WINDOWEVENT_SIZE_CHANGED:
                            sdlWindow->ProcessExpose() ;
                            break;
                    }
					break ;
				case SDL_USEREVENT:
#ifdef PLATFORM_TG5040
					if (event.user.code == LGPT_SDL_USER_WAKE) {
						OnWakeFromSleep();
						break;
					}
#endif
					sdlWindow->ProcessUserEvent(event) ;
					break ;
			}
		}
	}
	return 0 ;
} ;



void SDLEventManager::PostQuitMessage()
{
  Trace::Log("EVENT","SDEM:PostQuitMessage()") ;
	finished_=true  ;
} ; 


int SDLEventManager::GetKeyCode(const char *key)
{
    return SDL_GetScancodeFromName(key);
}
