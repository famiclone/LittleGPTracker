#include "View.h"
#include "System/Console/Trace.h"
#include "Application/Player/Player.h"
#include "Application/Mixer/MixerService.h"
#include "Application/Model/Mixer.h"
#include "Application/Utils/char.h"
#include "Application/AppWindow.h"
#include "Application/Model/Config.h"
#include "UIFramework/BasicDatas/FontConfig.h"
#include "ModalView.h"

bool View::initPrivate_=false ;

int View::margin_=0 ;
int View::songRowCount_; //=21 sets screen height among other things
bool View::miniLayout_=false ;
int View::altRowNumber_ = 4;

View::View(GUIWindow &w,ViewData *viewData):
	w_(w),
	modalView_(0),
	modalViewCallback_(0),
	hasFocus_(false)
{
  if (!initPrivate_) 
  {
	   GUIRect rect=w.GetRect() ;
     miniLayout_=(rect.Width()<320);
	   View::margin_=0 ;
		songRowCount_ = miniLayout_ ? 16:22; // 22 is row display count among other things

		const char *altRowStr = Config::GetInstance()->GetValue("ALTROWNUMBER");
		if (altRowStr) {
			altRowNumber_ = atoi(altRowStr);
		}

     initPrivate_=true ;
  }
	mask_=0 ;
	viewMode_=VM_NORMAL ;
	locked_=false ;
	viewData_=viewData;
	NOTIFICATION_TIMEOUT = 1000;
	displayNotification_ = "";
    // Initialize VU meter inertia tracking
    for (int i = 0; i < 8; i++) {
        prevLeftVU_[i] = 0;
        prevRightVU_[i] = 0;
    }
    for (int i = 0; i < 9; i++) {
        trackVuLevel_[i] = 0.0f;
    }
} ;

GUIPoint View::GetAnchor() {
	int width=40 ;
	int height=30 ;
	return GUIPoint((width-SONG_CHANNEL_COUNT*3)/2+2,(height-View::songRowCount_)/2) ;
}

GUIPoint View::GetTitlePosition() {
#ifndef PLATFORM_CAANOO
	return GUIPoint(0,0) ;
#else
	return GUIPoint(0,1) ;
#endif
} ;

bool View::Lock() {
	if (locked_) return false ;
	locked_=true ;
	return true ;
} ;

void View::WaitForObject() {
	while (locked_) {} ;
}

void View::Unlock() {
	locked_=false ;
}

void View::drawMap() {
    if (!miniLayout_) {
        GUIPoint anchor=GetAnchor() ;
		GUIPoint pos(View::margin_,anchor._y);
    	GUITextProperties props ;

		Player *player = Player::GetInstance();
		bool isLive = (player && player->GetSequencerMode() == SM_LIVE);
		const char *songChar = isLive ? "L" : "S";

		//draw entire map
		SetColor(CD_HILITE1) ;
    	char buffer[5] ;
		props.invert_=true ;
		//row1
		sprintf(buffer,"P G ");
        DrawString(pos._x,pos._y,buffer,props) ;
		pos._y++ ;		
		//row2
		sprintf(buffer, "%sCPI", songChar);
        DrawString(pos._x,pos._y,buffer,props) ;
		pos._y++ ;		
		//row3
        sprintf(buffer, " MTT");
        DrawString(pos._x,pos._y,buffer,props) ;

		//draw current screen on map
		SetColor(CD_HILITE2) ;
		pos._y = anchor._y;
		switch(viewType_)
		{
		case VT_CHAIN:
			pos._x+=1;
			pos._y+=1;
	        DrawString(pos._x,pos._y,"C",props) ;
			break;
		case VT_PHRASE:
			pos._x+=2;
			pos._y+=1;
	        DrawString(pos._x,pos._y,"P",props) ;
			break;
		case VT_PROJECT:
		case VT_SETTINGS:   // stays in Project's gearbox cell
		case VT_APPEARANCE: // nested under Settings
	        DrawString(pos._x,pos._y,"P",props) ;
			break;
		case VT_INSTRUMENT:
			pos._x+=3;
			pos._y+=1;
	        DrawString(pos._x,pos._y,"I",props) ;
			break;
		case VT_TABLE: //under phrase
			pos._x+=2;
			pos._y+=2;
	        DrawString(pos._x,pos._y,"T",props) ;
			break;
		case VT_TABLE2: //under instrument
			pos._x+=3;
			pos._y+=2;
	        DrawString(pos._x,pos._y,"T",props) ;
			break;
		case VT_GROOVE:
			pos._x+=2;
	        DrawString(pos._x,pos._y,"G",props) ;
			break;
        case VT_MIXER:
            pos._x+=1;
			pos._y+=2;
	        DrawString(pos._x,pos._y,"M",props) ;
			break;
		default: //VT_SONG
			pos._y+=1;
	        DrawString(pos._x,pos._y,songChar,props) ;
			break;
		}

	}//!minilayout
}

void View::drawNotes() {
    // Bottom area is rendered as pixel-rectangle per-track VU meters
    // in drawTrackVuOverlay() / DrawOverlay().
}

static void drawTransparentGlyph(AppWindow &aw, char c, int px, int py,
                                 int fillTopY) {
    unsigned int fontID = (unsigned char)c;
    if (fontID >= FONT_COUNT || c == ' ' || c == '\0') {
        return;
    }
    const unsigned char *src = font + fontID * 8;
    for (int y = 0; y < 8; y++) {
        int scanY = py + y;
        ColorDefinition col =
            (scanY >= fillTopY) ? CD_BACKGROUND : CD_NORMAL;
        int x = 0;
        while (x < 8) {
            if (src[x] == 0) {
                int xStart = x;
                while (x < 8 && src[x] == 0) {
                    x++;
                }
                GUIRect span(px + xStart, scanY, px + x, scanY + 1);
                aw.FillRect(span, col);
            } else {
                x++;
            }
        }
        if (y < 7) {
            src += FONT_WIDTH;
        }
    }
}

static int collectChannelActiveFxCodes(Mixer *mixer, int ch,
                                       const char *outCodes[3]) {
    if (!mixer || ch < 0 || ch >= MIXER_CHANNEL_COUNT) {
        return 0;
    }
    int count = 0;
    int addedCount = mixer->GetAddedFxCount();
    for (int k = 0; k < addedCount && count < 3; k++) {
        unsigned char fxId = mixer->GetAddedFxAt(k);
        switch (fxId) {
        case MIXER_FX_HPF:
            if (mixer->GetChannelHPF(ch) != 0) {
                outCodes[count++] = "HP";
            }
            break;
        case MIXER_FX_LPF:
            if (mixer->GetChannelLPF(ch) != 0) {
                outCodes[count++] = "LP";
            }
            break;
        case MIXER_FX_DLY:
            if (mixer->GetChannelDelaySteps(ch) != 0) {
                outCodes[count++] = "DL";
            }
            break;
        case MIXER_FX_REV:
            if (mixer->GetChannelReverbSize(ch) != 0) {
                outCodes[count++] = "RV";
            }
            break;
        case MIXER_FX_TPE:
            if (mixer->GetChannelTapeAmt(ch) != 0) {
                outCodes[count++] = "TP";
            }
            break;
        case MIXER_FX_SCH:
            if (mixer->GetChannelSidechainSrc(ch) < SONG_CHANNEL_COUNT &&
                mixer->GetChannelSidechainAmt(ch) > 0) {
                outCodes[count++] = "SC";
            }
            break;
        default:
            break;
        }
    }
    return count;
}

void View::drawTrackVuOverlay(bool includeMaster) {
    if (miniLayout_ || HasModal()) {
        return;
    }

    AppWindow &aw = (AppWindow &)w_;
    const int cw = AppWindow::CharWidth();
    const int ch = AppWindow::CharHeight();
    GUIPoint anchor = GetAnchor();
    Mixer *mixer = Mixer::GetInstance();

    int topRow = anchor._y + 23;
    int botRow = anchor._y + 26;
    if (includeMaster) {
        int addedCount = mixer ? mixer->GetAddedFxCount() : 0;
        int addFxRowY =
            anchor._y + 3 + addedCount + ((addedCount > 0 && addedCount < 5) ? 1 : 0);
        topRow = addFxRowY + 2;
        if (topRow > anchor._y + 22) {
            topRow = anchor._y + 22;
        }
        botRow = 30;
    }

    int by0 = topRow * ch;
    int by1 = botRow * ch;
    int barH = by1 - by0;
    if (barH <= 0) {
        return;
    }

    int numBars = includeMaster ? 9 : 8;
    Player *player = Player::GetInstance();
    bool isRunning = (player && player->IsRunning());
    MixerService *ms = MixerService::GetInstance();

    int stripX0 = anchor._x * cw;
    int lastColX = anchor._x + (numBars - 1) * 3 +
                   ((numBars - 1 == MASTER_CHANNEL_INDEX) ? 1 : 0);
    int stripX1 = (lastColX + 2) * cw;
    GUIRect dirtyStrip(stripX0, by0, stripX1, by1);

    aw.BeginBatchDraw();

    int warnThresholdPx = (barH * 3) / 4;
    if (warnThresholdPx < 1) {
        warnThresholdPx = 1;
    }

    for (int i = 0; i < numBars; i++) {
        float peak = 0.0f;
        if (isRunning && !player->IsChannelMuted(i) && ms) {
            if (i < 8) {
                int busIdx = mixer ? mixer->GetBus(i) : i;
                MixBus *bus = ms->GetMixBus(busIdx);
                if (bus) {
                    uint32_t level = bus->GetPeakLevel();
                    float pkL = (float)((level >> 16) & 0xFFFF) / 32767.0f;
                    float pkR = (float)(level & 0xFFFF) / 32767.0f;
                    peak = (pkL > pkR) ? pkL : pkR;
                }
            } else {
                uint32_t masterLevel = ms->GetMasterPeakLevel();
                float pkL = (float)((masterLevel >> 16) & 0xFFFF) / 32767.0f;
                float pkR = (float)(masterLevel & 0xFFFF) / 32767.0f;
                peak = (pkL > pkR) ? pkL : pkR;
            }
        }
        if (peak > 1.0f) {
            peak = 1.0f;
        }

        if (peak > trackVuLevel_[i]) {
            trackVuLevel_[i] = peak;
        } else {
            trackVuLevel_[i] = trackVuLevel_[i] * 0.85f + peak * 0.15f;
            if (trackVuLevel_[i] < 0.01f) {
                trackVuLevel_[i] = 0.0f;
            }
        }

        int colX = anchor._x + i * 3 + ((i == MASTER_CHANNEL_INDEX) ? 1 : 0);
        int bx0 = colX * cw;
        int bx1 = (colX + 2) * cw;

        // Draw background container rectangle
        GUIRect bgRect(bx0, by0, bx1, by1);
        aw.FillRect(bgRect, CD_HILITE1);

        int fillPx = (int)(trackVuLevel_[i] * (float)barH + 0.5f);
        if (fillPx > barH) {
            fillPx = barH;
        }
        if (fillPx > 0) {
            int bodyPx =
                (fillPx > warnThresholdPx) ? warnThresholdPx : fillPx;
            if (bodyPx > 0) {
                GUIRect bodyRect(bx0, by1 - bodyPx, bx1, by1);
                aw.FillRect(bodyRect, CD_COL_TITLE);
            }
            if (fillPx > warnThresholdPx) {
                GUIRect capRect(bx0, by1 - fillPx, bx1, by1 - warnThresholdPx);
                aw.FillRect(capRect, CD_HILITE2);
            }
        }

        // On non-mixer views (e.g. SongView), draw active FX codes on top of the 3-row VU bar
        if (!includeMaster) {
            const char *fxCodes[3];
            int fxCount = collectChannelActiveFxCodes(mixer, i, fxCodes);
            int fillTopY = by1 - fillPx;
            for (int r = 0; r < fxCount && r < 3; r++) {
                int gy = by0 + r * ch;
                drawTransparentGlyph(aw, fxCodes[r][0], bx0, gy, fillTopY);
                drawTransparentGlyph(aw, fxCodes[r][1], bx0 + cw, gy, fillTopY);
            }
        }
    }

    aw.EndBatchDraw(dirtyStrip);
}

void View::DrawOverlay() {
    if (miniLayout_ || HasModal()) {
        return;
    }
    switch (viewType_) {
    case VT_SONG:
    case VT_CHAIN:
    case VT_PHRASE:
    case VT_TABLE:
    case VT_TABLE2:
    case VT_GROOVE:
        drawTrackVuOverlay(false);
        break;
    case VT_MIXER:
        drawTrackVuOverlay(true);
        break;
    default:
        break;
    }
}

void View::DoModal(ModalView *view,ModalViewCallback cb) {
	modalView_=view ;
	modalView_->OnFocus() ;
	modalViewCallback_=cb ;
	isDirty_=true ;
	// Invalidate the char-screen cache so the next Flush repaints every
	// cell (otherwise unchanged spaces leave stale pixel overlays).
	((AppWindow &)w_).Clear(true) ;
	((AppWindow &)w_).SetDirty() ;
} ;

void View::AnimationUpdate() {
	// Keep modal UIs alive when the audio/player tick is paused (e.g. mic
	// capture suspends SDL playback, so OnPlayerUpdate never fires).
	if (modalView_) {
		modalView_->AnimationUpdate();
	}
}

void View::Redraw() {
	DrawView() ;
	if (modalView_) {
		modalView_->Redraw() ;
	}
	isDirty_=false ;
} ;

void View::SetDirty(bool isDirty) {
	isDirty_=true ;
} ;

void View::ProcessButton(unsigned short mask, bool pressed) {
	isDirty_=false ;
	if (modalView_) {
		modalView_->ProcessButton(mask,pressed);
		if (modalView_->isDirty_) {
			isDirty_=true ;
		}
		if (modalView_->IsFinished()) {
			ModalView *finishedModal = modalView_ ;
			ModalViewCallback cb = modalViewCallback_ ;
			modalView_ = 0 ;
			modalViewCallback_ = 0 ;
			// process callback sending the modal dialog
			if (cb) {
				cb(*this,*finishedModal) ;
			}
			SAFE_DELETE(finishedModal) ;
			((AppWindow &)w_).Clear(true) ;
			isDirty_=true ;
		}
	} else {
		ProcessButtonMask(mask,pressed);
	}
	if (isDirty_) ((AppWindow &)w_).SetDirty() ;
} ;

void View::Clear() {
	((AppWindow &)w_).Clear() ;
}

void View::SetColor(ColorDefinition cd) {
	((AppWindow &)w_).SetColor(cd) ;
} ;

void View::ClearRect(int x,int y,int w,int h) {
	GUIRect rect(x,y,(x+w),(y+h)) ;
	w_.ClearRect(rect) ;
} ;

void View::DrawString(int x,int y,const char *txt,GUITextProperties &props) {
	GUIPoint pos(x,y) ;
	w_.DrawString(txt,pos,props) ;
} ;

/*
	Displays the saved notification for 1 second
*/
void View::EnableNotification() {
	if ((SDL_GetTicks() - notificationTime_) <= NOTIFICATION_TIMEOUT) {
		SetColor(CD_NORMAL);
		GUITextProperties props;
        int xOffset = 4;
        // support single-line or two-line notifications separated by '\n'
        size_t nl = displayNotification_.find('\n');
		if (nl == std::string::npos) {
			DrawString(xOffset, notiDistY_, displayNotification_.c_str(), props);
		} else {
			std::string line1 = displayNotification_.substr(0, nl);
			std::string line2 = displayNotification_.substr(nl + 1);
			DrawString(xOffset, notiDistY_, line1.c_str(), props);
			DrawString(xOffset, notiDistY_ + 1, line2.c_str(), props);
		}
    } else {
		displayNotification_ = "";
	}
}

/*
    Set displayed notification
    Saves the current time
    Optionally set display y offset if not in a project (default == 2)
    Allows negative offsets, use with care!
*/
void View::SetNotification(const char *notification, int offset) {
    notificationTime_ = SDL_GetTicks();
    displayNotification_ = notification;
    notiDistY_ = offset;
    isDirty_ = true;
}

void View::drawVUMeter(uint8_t leftBars, uint8_t rightBars, GUIPoint pos,
                       GUITextProperties props, int vuIndex, bool forceRedraw) {
    // Clamp bars to VU meter height
    leftBars = (leftBars > VU_METER_HEIGHT) ? VU_METER_HEIGHT : leftBars;
    rightBars = (rightBars > VU_METER_HEIGHT) ? VU_METER_HEIGHT : rightBars;

    // Apply inertia effect: fast rise, slow fall
    const int maxStepUp = 2;    // fast rise (up to 2 bars per update)
    const int maxStepDown = 1;  // slow fall (only 1 bar per update)

    // Left channel inertia
    if (leftBars > prevLeftVU_[vuIndex]) {
        // Rising: allow faster response (up to maxStepUp)
        int diff = leftBars - prevLeftVU_[vuIndex];
        if (diff > maxStepUp) {
            leftBars = prevLeftVU_[vuIndex] + maxStepUp;
        }
    } else if (leftBars < prevLeftVU_[vuIndex]) {
        // Falling: limit to maxStepDown for inertia
        int diff = prevLeftVU_[vuIndex] - leftBars;
        if (diff > maxStepDown) {
            leftBars = prevLeftVU_[vuIndex] - maxStepDown;
        }
    }

    // Right channel inertia
    if (rightBars > prevRightVU_[vuIndex]) {
        int diff = rightBars - prevRightVU_[vuIndex];
        if (diff > maxStepUp) {
            rightBars = prevRightVU_[vuIndex] + maxStepUp;
        }
    } else if (rightBars < prevRightVU_[vuIndex]) {
        int diff = prevRightVU_[vuIndex] - rightBars;
        if (diff > maxStepDown) {
            rightBars = prevRightVU_[vuIndex] - maxStepDown;
        }
    }

    // Skip redraw if values unchanged (unless forced)
    if (!forceRedraw && leftBars == prevLeftVU_[vuIndex] &&
        rightBars == prevRightVU_[vuIndex]) {
        return;
    }

    // Store current values for next update
    prevLeftVU_[vuIndex] = leftBars;
    prevRightVU_[vuIndex] = rightBars;

    // Draw the VU meter bars (simple bar representation)
    std::string bar = "";
    for (int i = 0; i < VU_METER_HEIGHT; i++) {
        if (i < leftBars) {
            bar += "|";
        } else {
            bar += " ";
        }
    }
    bar += " ";
    for (int i = 0; i < VU_METER_HEIGHT; i++) {
        if (i < rightBars) {
            bar += "|";
        } else {
            bar += " ";
        }
    }
    DrawString(pos._x, pos._y, bar.c_str(), props);
}
