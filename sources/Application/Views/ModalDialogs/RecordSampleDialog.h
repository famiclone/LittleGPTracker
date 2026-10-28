#ifndef _RECORD_SAMPLE_DIALOG_H_
#define _RECORD_SAMPLE_DIALOG_H_

#include "Application/Audio/SDLAudioCapture.h"
#include "Application/Views/BaseClasses/ModalView.h"

class RecordSampleDialog : public ModalView {
  public:
    RecordSampleDialog(View &view);
    virtual ~RecordSampleDialog();

    virtual void DrawView();
    virtual void OnPlayerUpdate(PlayerEventType, unsigned int currentTick);
    virtual void OnFocus();
    virtual void AnimationUpdate();
    virtual void ProcessButtonMask(unsigned short mask, bool pressed);

  private:
    enum State { IDLE, RECORDING, STOPPED };
    enum Button { BTN_REC = 0, BTN_SAVE = 1, BTN_EXIT = 2 };

    void startRecording();
    void stopRecording();
    bool saveRecording();

    SDLAudioCapture capture_;
    State state_;
    int selected_;
    int toInstr_;
    int maxSeconds_;
    static const int kMaxSeconds = 20;
};

#endif
