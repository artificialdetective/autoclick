/*
  function autoClick() handles the auto-clicking and auto-scrolling.
  function remindToPause() handles work break reminders.
  function WindowProcedure() handles the timers.
*/

#include <stdio.h>                    
#include <math.h>
#include <time.h>
#include <sys/stat.h>                 // for getting file info.
#include <limits.h>                   // for INT_MAX.
#define WINVER 0x0500                 // required to use SendInput() to simulate mouse presses. define before windows.
#include <windows.h>
#include <shellapi.h>                 // for system tray icon.

#include "autoclick.h"                // function declarations.


// Settings:
const int UPDATESPEED = 50;           // Interval at which to update the mouse coordinates and speed. in milliseconds.
const int MINUTE = 60;                // Just a number to convert minutes to seconds. const is preferred over #define.
int mainkey = VK_F10;                 // Main program control key, on/off toggle etc.
int win_key = 0;                      // Key number of windows + key combination if the program is started that way.
bool lefthanded = false;              // Set through command line parameter to auto-click right button when mouse is configured for left hand.

// User activity tracking:
bool activity = true;                 // Turned on when user clicks or types.
bool physicalclick = true;            // Set true if physical mouse button is pressed. 
                                      // Also set true at launch, as the user probably clicked to launch the program.
int moving = 0;                       // Speed of mouse in pixels per 50 milliseconds (i.e. per UPDATESPEED).
int fullpausetime = 0;                // Length of entire work pause in seconds.
int workedtime = 0;                   // The time that the user has been working without pausing, in minutes.
int totalworked = 0;                  // Count how long user has worked all day, in minutes.
int totalclicks = 0;                  // Count how many times a day user does mouseclicks.




void autoClick(HWND hwnd) {
/*
  Tracks mouse movement, speed, and scrollbar mouseover,
  simulates a mouse click when mouse stops moving.
*/

// Configuration:
const int SCROLLBARTOP = 60;          // pixels from top of window to top of scrollbar.
const int SCROLLBARWIDTH = 27;        // standard scrollbar width in pixels.
const int SCROLLBARRANGE = 100;       // distance between scrollbar and mouse x-coordinate beyond which the scrollbar is released.
const int CLICKTIME = 200;            // delay between stop moving and auto-click, in milliseconds.
const int MENUCLICKTIME = 500;        // delay between stop moving and auto-click when over window close buttons or menu bars.
const int TIMEBETWEENCLICKS = 350;    // minimum time between two autoclicks, in milliseconds.
const int leeway = 1;                 // ignorable accidental movement of mouse, in pixels. 
                                      // e.g. when removing hand from mouse, or wonky mouse.

// Variables for autoclicking:
POINT mouse = {};                     // a struct with long ints .x and .y to contain the mouse coordinates
static POINT prevmouse = {};          // previous mouse position, for calculating mouse speed.
static POINT prevclick = {};          // coordinates of last auto-click, for auto-selecting text.
static int clicktimer = 0;            // timer in microseconds. can be interrupted with further movement or right-click.
static int justclicked = 0;           // timer in microseconds that temporarily blocks consequtive auto-clicks.
static int parking = 0;               // set when mouse is "parked"
static int dragging = 0;              // represents shift-click-drag
static int scrolling = 0;

// DETECT PHYSICAL CLICKS:
// If the user is actually physically clicking a mouse button,
// don't auto-click until they've released, and moved the mouse elsewhere.
// Prevent and/or cancel any auto-click when mouse buttons are pressed down:
if(keyPressed(VK_LBUTTON) || keyPressed(VK_RBUTTON)) {
  if(!physicalclick && !justclicked && !dragging && !scrolling) {   
    physicalclick = true;
    clicktimer = 0;
  }
  activity = true;
}
// On mouse release, restart at current coordinates.
else if(physicalclick) {
  totalclicks += 1;
  physicalclick = false;
  moving = 0;
  // Set prevmouse position as if it was current position.
  GetCursorPos(&prevmouse);
}

// DETECT MOVEMENT AND SPEED:
// Get current mouse screen coordinates,
GetCursorPos(&mouse);

// then compare them with previous recorded mouse coordinates to calculate current mouse speed 
// (speed value is equal to pixels distance between previous x,y and current x,y)
double speed = sqrt( pow(mouse.x - prevmouse.x, 2) + pow(mouse.y - prevmouse.y, 2) );
// Mark that the mouse has been moved, in preparation for a new click. 
if(speed > leeway) {
  moving = int(speed); 
  // If an autoclick timer was started and we're moving again, interrupt it.
  clicktimer = 0;
}

// DETECT SCROLLBAR MOUSEOVER:
bool overscrollbar = false;
static RECT scrollbararea = {};   
// Set default location of window scrollbar:
RECT windowframe = {};
GetWindowRect(GetForegroundWindow(), &windowframe);
RECT windowscrollarea = {windowframe.right - SCROLLBARWIDTH, windowframe.top + SCROLLBARTOP, windowframe.right, windowframe.bottom};   

// Check if mouse is over a scrollbar area, and set scrollbar coordinates.
// Because GetScrollBarInfo() doesn't work on Firefox, always check right edge of any program's window as well:
if(PtInRect(&windowscrollarea, mouse)) {
  overscrollbar = true; 
  scrollbararea = windowscrollarea;
}
if(!overscrollbar) {
  // Check sub-window scrollbars:
  SCROLLBARINFO subscrollbar = {sizeof(SCROLLBARINFO)};   
  GetScrollBarInfo(WindowFromPoint(mouse), OBJID_VSCROLL, &subscrollbar);   
  // Check if mouse is over a sub-window scrollbar:
  if(PtInRect(&subscrollbar.rcScrollBar, mouse)) {
    overscrollbar = true; 
    scrollbararea = subscrollbar.rcScrollBar;
  }
}

// START SHIFT BUTTON DRAG:
// Check if shift button is being held. 
// shift + move should react instantly and even to minor movement in case of large documents' scrollbars.
if(keyPressed(VK_SHIFT)) {
  // Clear autoclick countdown while shift is pressed.
  clicktimer = 0;
  // Click-and-hold if mouse moves while shift is held.
  if(!dragging && speed > 0) {
    clickMouse(true);
    dragging = true;
  }
}   

// RELEASE SHIFT DRAG:
// Instantly release mouse hold when shift key is released during shift-dragging.
if(dragging && !keyPressed(VK_SHIFT)) {
  clickMouse(false);
  dragging = 0;
  // Reset 'moving' or would de-click selected text.
  moving = 0;
  // Also wipe the countdown that was set when the mouse stopped moving,
  // to not de-click immediately after dragging.
  clicktimer = 0;
  justclicked = TIMEBETWEENCLICKS;
}

// DETECT "PARKING" THE MOUSE:
if(moving) {
  // Block auto-clicking when moving mouse downward-right, 
  // to allow the user to abort or "park" the mouse without clicking.
  if(mouse.x >= prevmouse.x && mouse.y > prevmouse.y) {
    parking = true;
  }
  if(parking) {
    // Cancel parking when moving up
    if(mouse.y < prevmouse.y
    // or left
    || mouse.x < prevmouse.x
    // or straight horizontally right
    || mouse.x > prevmouse.x + leeway && mouse.y <= prevmouse.y
    // or when x distance is further than y distance, i.e. less than 45 degree angle.
    || overscrollbar
    ) {
      parking = false;
    }
  }
}

// Block auto-click timer for a duration:
if(justclicked) {
  // Reduce timer to 0:
  justclicked = max(0, justclicked - UPDATESPEED);
  // Resetting 'moving' keeps from auto-clicking once time is up (after e.g. physical click)
  moving = 0;
}

// START AUTO-CLICK TIMER WHEN MOUSE STOPS:
// If the mouse was moving but coordinates are now the same, the mouse has stopped. 
// Activate a countdown to click, but only if mouse speed was decreasing, to avoid accidental fast stops from clicking.
if(moving && speed <= leeway && !physicalclick && !justclicked && !parking && !dragging) {
  // Mouse no longer moves.
  moving = 0;
  // Immediately auto-click when mouse stops over scrollbar:
  if(overscrollbar) {
    // Set mouse coordinates in middle of scrollbar:
    if(mouse.x > windowscrollarea.left && mouse.x <= windowscrollarea.right) {
      SetCursorPos(windowscrollarea.left + 9, mouse.y);
    }
    // Release any mouse keys to RE-click WHENEVER mouse stops over scrollbar area
    if(keyPressed(VK_LBUTTON) || keyPressed(VK_RBUTTON)) {
      clickMouse(false);
    }
    // Momentarily press shift + click to automatically line up the scrollbar with the mouse cursor, wherever it is.
    pressKey(VK_SHIFT, true);
    clickMouse(true);
    pressKey(VK_SHIFT, false);
    scrolling = true;
    clicktimer = 0;
  }
  // Set a long delay countdown when over the window close button or main program menu's, to prevent accidents:
  else if(mouse.y >= windowframe.top && mouse.y <= windowframe.top + SCROLLBARTOP) {
    clicktimer = MENUCLICKTIME;
  }
  // Normal countdown to auto-click:
  else {clicktimer = CLICKTIME;}
}

// AUTO-CLICK AT END OF TIMER:
if(clicktimer > 0 && !justclicked && !parking && !scrolling && !dragging) {
  // Count down.
  clicktimer -= UPDATESPEED;
  // Click at end of timer. Optionally hold escape to override autoclicking temporarily.
  if(clicktimer <= 0 && !keyPressed(VK_ESCAPE)) {
    // Auto-click at end of countdown (press and release mouse button)
    clickMouse(true);
    clickMouse(false);
    // After an auto-click, wait a minimum time before another auto-click.
    justclicked = TIMEBETWEENCLICKS;
    clicktimer = 0;
    // Remember last auto-click location.
    prevclick = mouse;                    
  }   
}

// RELEASE SCROLLBAR: 
if(scrolling) {
  // If the mouse goes off the scrollbar and moves twice as far horizontally as vertically, release the scroll bar.
  if(moving && !overscrollbar && abs(prevmouse.x - mouse.x) > 2*abs(prevmouse.y - mouse.y)
  // Also release scrollbar when mouse moves too far away from it:
  || abs(scrollbararea.left+7 - mouse.x) > SCROLLBARRANGE   // left and right maximum range
  || mouse.y < scrollbararea.top
  || mouse.y > scrollbararea.bottom
  || physicalclick   
  || keyPressed(VK_ESCAPE)
  ) {
    clickMouse(false);
    scrolling = false;
  }
}

// Store the current coordinates for the next movement check:
GetCursorPos(&prevmouse);
}   // End of function autoClick()





int remindToPause(HWND hwnd, time_t &endofpause, bool minutemark) {
/*
  Sets and tracks work pause duration.
  Shows a popup window every half hour of work to tell the user to pause.
  Activated at regular interval from WindowProcedure() timer.
  Parameters: 'minutemark' is passed as true at every full minute passed.
*/

// Variables for tracking how long the user has been working, and how much time they have paused, in minutes:
static int lastwarned = 1;            // time of last warning. set 0 to launch reminder.
static int today = 0;                 // day nr to track total work time per day.
static int MAXwork = 30;              // 30 minutes = 3 minute break.
static int MINpause = 3;              
time_t now = time(0);

// Check how long the user has been working:
const int activitykeys[] = {VK_SPACE, VK_RETURN, VK_LEFT, VK_UP, VK_RIGHT, VK_DOWN};
for(unsigned k = 0; k < sizeof(activitykeys); k++) {
  if(keyPressed(activitykeys[k])) {activity = true;  break;}
}

// Log total worked time per day, at every minute tick, unless pausing. 
if(minutemark) {
  struct tm *date = localtime(&now);
  // Reset total time worked if resuming from a night's computer hybernation.
  if(date->tm_mday != today) {
    today = date->tm_mday;
    totalworked = workedtime = 0;
  }
  if(totalworked < INT_MAX) {totalworked += 1;}
}

// CHECK FOR END OF PAUSE:
// endofpause is automatically postponed on activity.
if(now >= endofpause) {
  // Tell the user that pause has ended, but only if the user had overworked earlier. otherwise just reset 'worked' silently.
  if(workedtime >= MAXwork) {
    // Wake up the screen by simulating a shift keypress:
    pressKey(VK_SHIFT, true);
    pressKey(VK_SHIFT, false);
    MessageBeep(MB_ICONEXCLAMATION);
    // Hide the pause reminder window:
    ShowWindow(hwnd, SW_HIDE);
    // Subtract the paused time from total worked time only once it is fully depleted.
    // Not if the computer was off during the pause, because then you weren't working.
    // You can tell by whether the pause's end time is overshot by more than a few seconds of runtime, since this program runs 20x/second
    if(endofpause >= now-1) {totalworked -= fullpausetime/MINUTE;}
  }

  // Reset work timer whenever user takes a break of 3 minutes or longer, whether warned or not.
  workedtime = 0;
  // Move the next pause, or would never reactivate.
  endofpause = now + MINpause * MINUTE;
  activity = false;   
}

// SET PAUSE:
// (Re-)set when the pause should end in UTC time.
// Update whenever there is activity within a minute. 
if(activity) {
  endofpause = now + max(MINpause, workedtime * MINpause/MAXwork) * MINUTE;
  fullpausetime = (endofpause - now);
}

// COUNT CURRENTLY WORKED TIME:
// check for activity within every minute:
if(minutemark) {
  if(activity || workedtime > 0) {workedtime += 1;}
  // Prepare to remind to pause every 30 minutes (MAXwork), unless user is potentially pausing (no activity) in this minute:
  // Set lastwarned = 0 to activate reminder when user is still working and last-ignored reminder was at least 5 minutes ago.
  if(activity && workedtime >= MAXwork && lastwarned <= now - 5*MINUTE) {lastwarned = 0;}
  // Reset for the next minute.
  activity = false;
}

// LAUNCH/POPUP RSI WARNING:
// Do not immediately popup if user is currently busy holding mouse button or in the midst of typing. Minimum pause of a few seconds.
// lastwarned is set to 0 when the warning should trigger.
if(lastwarned == 0 && !physicalclick && !moving) {   
  // Set length of pause in seconds.
  fullpausetime = int(endofpause - now);
  // Make the window visible on the screen. 
  ShowWindow(hwnd, SW_SHOWNORMAL);
  // Put window in foreground.
  popup(hwnd);
  // Remember the last time we warned the user.
  lastwarned = now;
  activity = false;   
}
return 0;
}   // End of function remindToPause()






HINSTANCE programInstance = NULL;

// Start of program:
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {

// If launched through a windows + nr keyboard combination, record the numeric key that was pressed:
while(keyPressed(VK_LWIN) && !win_key) {
  for(int k = 48+1; k <= 48+9; k++) {
    if(keyPressed(k)) {win_key = k; break;}
  }
}

// Prevent multiple launches of the program by creating a named mutex:
CreateMutex(0, FALSE, "Local\\$autoclick$");
if(GetLastError() == ERROR_ALREADY_EXISTS) {
  // Simulate the main on/off key to turn the already running autoclick on/off.
  // unless we launched by pressing win+key, then the existing program,
  // presumably launched the same way, is already listening for those keys.
  if(!win_key) {pressKey(mainkey, true);  Sleep(UPDATESPEED*2);  pressKey(mainkey, false);}
  // If mutex already exists, quit.
  return 0;
}

// Parse the command line parameters:
char *parameter = strtok(lpCmdLine, " ");
while(parameter) {
  // Pass parameter "autoclick.exe left" to auto-click the right mouse button,
  // when mouse is set to left-handed in computer settings.
  if(!stricmp(parameter, "left")) {lefthanded = true;} 
  // Get the next parameter:
  parameter = strtok(NULL, " ");
}

// Display a donation message after 30 days:
checkTrialTime(30);

// Register a window class:
WNDCLASSEX winclass = {};
winclass.hInstance       = hInstance;
const char szClassName[] = "Windows App";
winclass.lpszClassName   = szClassName;
winclass.lpfnWndProc     = WindowProcedure;   
winclass.cbSize          = sizeof(WNDCLASSEX);
winclass.hIcon           = LoadIcon(NULL, IDI_APPLICATION);   
winclass.hIconSm         = LoadIcon(NULL, IDI_APPLICATION);   
winclass.hCursor         = LoadCursor(NULL, IDC_ARROW);   
winclass.lpszMenuName    = NULL;   // No menu 
winclass.hbrBackground   = (HBRUSH) GetStockObject(WHITE_BRUSH);   
// Register the window class, and if it fails, quit the program.
if(!RegisterClassEx(&winclass)) {return 0;}   

// Store instance handle in a global variable to pass around:
programInstance = hInstance;

// Create the pause window
HWND hwnd = CreateWindowEx(
  0,                       // optional window styles
  szClassName,             // window class
  "RSI warning",           // window text
  WS_OVERLAPPED | WS_MINIMIZEBOX | WS_SYSMENU,   // window style. WS_OVERLAPPEDWINDOW allows resizing the window.
  CW_USEDEFAULT, CW_USEDEFAULT, 100, 100,   // x, y, width, height
  NULL,                    // parent window
  NULL,                    // No menu 
  hInstance,               // program Instance handle 
  NULL                     // additional application data 
);
if(hwnd == NULL) {return 0;}

MSG msg;                  
while(GetMessage(&msg, NULL, 0, 0)) {
  TranslateMessage(&msg);  
  DispatchMessage(&msg);   
}

return int(msg.wParam);
}   // End of WinMain()





LRESULT CALLBACK WindowProcedure(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
PAINTSTRUCT ps;
HDC hdc;
const int WINDOW_W = 340;
const int WINDOW_H = 200;

// Taskbar mini icon:
static HICON trayiconON = (HICON) LoadImage(NULL, "../icons/autoclick.ico", IMAGE_ICON, 0,0, LR_LOADFROMFILE);
static HICON trayiconOFF = (HICON) LoadImage(NULL, "../icons/autoclickoff.ico", IMAGE_ICON, 0,0, LR_LOADFROMFILE);
// Set up and link a tray icon to a valid window hwnd.
const int trayiconID = 1;
static NOTIFYICONDATA trayicon = {sizeof(NOTIFYICONDATA), hwnd, trayiconID, NIF_ICON | NIF_TIP, 0, trayiconON, "on"};

// Load mouse cursors. must be in either .CUR or .ANI format.
static HCURSOR normalcursor = CopyCursor( LoadCursor(NULL, IDC_ARROW) );   
static HCURSOR autocursor = CopyCursor( LoadCursorFromFile("../icons/autoclick.cur") );

// Feature toggles:
static bool autoClickON = true;
static bool pauseremindersON = true;
static bool mainkeypressed = true;   

// 'endofpause' is time in UTC seconds until when the user should pause. This is more consistent if the computer is shut down inbetween.
static time_t endofpause = 0;
// Set pause bar lengths in pixels:
const int BARLENGTHPERMINUTE = 60;
int fullbarlength = fullpausetime * BARLENGTHPERMINUTE/MINUTE;
int currentbarlength = (endofpause - time(0)) * BARLENGTHPERMINUTE/MINUTE;

const int timerID = 1;
static int minutecounter = 0;   // count off each minute in microseconds.

// Handle the input messages 
switch(message) {       
case WM_CREATE: {
  // Notify that the program has activated.
  MessageBeep(MB_ICONEXCLAMATION);   

  // Change mouse cursor:
  SetSystemCursor(CopyCursor(autocursor), OCR_NORMAL);   

  // Display the tray icon:
  Shell_NotifyIcon(NIM_ADD, &trayicon);

  // Start a timer interval to pass WM_TIMER to this callback function. Isn't super exact though, and stops on any input.
  SetTimer(hwnd, timerID, UPDATESPEED, NULL);
  break;
}

// Activated at regular intervals:
case WM_TIMER: {
  // Because keypresses are not sent to this program running in the background, we have to monitor them ourselves.

  // Check if shutdown keys are held:
  if(keyPressed(mainkey) && keyPressed(VK_ESCAPE)) {
    PostMessage(hwnd, WM_DESTROY, 0,0);
    break;
  }

  // Switch auto-clicking on or off (not yet at holding, for other function toggles):
  if(!mainkeypressed) {
    if(keyPressed(mainkey)
    || keyPressed(VK_LWIN) && keyPressed(win_key) && win_key != 0   // This taskbar shortcut key combination is recorded at launch.
    ) {
      mainkeypressed = true;
      autoClickON = !autoClickON;   // Toggle autoclick on/off
      physicalclick = moving = 0;   
      // Change mouse cursor and tray icon:
      if(autoClickON) {
        MessageBeep(MB_ICONEXCLAMATION);
        SetSystemCursor(CopyCursor(autocursor), OCR_NORMAL);   
        trayicon.hIcon = trayiconON;
      } else {
        MessageBeep(MB_ICONSTOP);
        SetSystemCursor(CopyCursor(normalcursor), OCR_NORMAL);
        trayicon.hIcon = trayiconOFF;
      }
      // Update tray icon:
      Shell_NotifyIcon(NIM_MODIFY, &trayicon);
    }
  }

  // Check when main key is released. set true to block repeating a control function while the key is held.
  if(!keyPressed(mainkey) && !keyPressed(VK_LWIN) && !keyPressed(win_key)) {mainkeypressed = false;}

  // Press main key + pause to toggle reminders on/off
  if(!mainkeypressed && keyPressed(mainkey) && keyPressed(VK_PAUSE)) {
    mainkeypressed = true;
    pauseremindersON = !pauseremindersON;   // Toggle reminders on/off
    const char *boxtext = (pauseremindersON)? "RSI break reminders ON" : "RSI break reminders OFF";
    MessageBox(NULL, boxtext, "RSI reminders", MB_SETFOREGROUND | MB_OK);
  }
  
  // Run autoclicking functionality:
  if(autoClickON) {autoClick(hwnd);}

  // Count each minute:
  minutecounter += UPDATESPEED;
  bool minutemark = false;
  if(minutecounter >= 60*1000) {minutemark = true;  minutecounter = 0;}

  // Remind the user to pause:
  if(pauseremindersON) {remindToPause(hwnd, endofpause, minutemark);}

  // Update RSI popup window graphics every second:
  if(minutecounter%1000 == 0) {
    // Change window size if necessary to accommodate pause bar length:
    int needwindowwidth = max(WINDOW_W, fullbarlength + 50);
    // Get the outer dimensions of our popup window including borders, because that is the size MoveWindow will set.
    RECT windowframe = {};
    GetWindowRect(hwnd, &windowframe);
    if(windowframe.right - windowframe.left != needwindowwidth) {
      MoveWindow(hwnd, 40, 40, needwindowwidth, WINDOW_H, true);
    }

    InvalidateRect(hwnd, NULL, false);
  }

  // Update tray icon mouseover text to show the minutes worked:
  if(minutemark) {
    sprintf(trayicon.szTip, "worked total %i:%s%i", totalworked/60, padwithzero(totalworked%60), totalworked%60);
    Shell_NotifyIcon(NIM_MODIFY, &trayicon);
  }

  break;
}

case WM_LBUTTONDOWN: {
  // Minimise the window when clicked:
  CloseWindow(hwnd);
  break;
}


case WM_PAINT: {
  hdc = BeginPaint(hwnd, &ps);

  // Set font:
  HFONT font = CreateFont(-13, 0, 0, 0, FW_NORMAL, 0, 0, 0, 0, 0, 0, 0, 0, "Arial");   
  SelectObject(hdc, font);

  // Prepare colours:
  HBRUSH colourgreen = CreateSolidBrush(RGB(255,255,0));
  HBRUSH colourorange = CreateSolidBrush(RGB(255,170,0));
  HBRUSH colourred = CreateSolidBrush(RGB(255,0,0));

  // Update window title:
  char title[200+1] = "";
  struct tm *endtime = localtime(&endofpause);
  snprintf(title, sizeof(title), "RSI %i:%s%i", endtime->tm_hour, padwithzero(endtime->tm_min), endtime->tm_min);
  SetWindowText(hwnd, title);

  // Paint graphics:
  FillRect(hdc, &ps.rcPaint, (HBRUSH) GetStockObject(WHITE_BRUSH) );

  // Display a deminishing health bar, in three colours.
  // Set energy bar coords in window (left, top, right, bottom)
  RECT fullbar = {20, 20, 100, 20+16};
  RECT midbar = fullbar;
  RECT bar = fullbar;

  // Set bar widths:
  fullbar.right = bar.left + fullbarlength;
  midbar.right = bar.left + min(currentbarlength*4/3, fullbarlength);
  bar.right = bar.left + currentbarlength;

  // Paint outline of full bar:
  Rectangle(hdc, fullbar.left-1, fullbar.top-1, fullbar.right+1, fullbar.bottom+1); 

  // Paint the bars:
  FillRect(hdc, &fullbar, colourgreen);
  FillRect(hdc, &midbar, colourorange);
  FillRect(hdc, &bar, colourred);

  // Display text:
  char workedtext[100] = "";  
  snprintf(workedtext, sizeof(workedtext), "You have been sitting still for %i minutes.", workedtime);
  TextOut(hdc, bar.left, bar.bottom + 20, workedtext, strlen(workedtext));

  // Display health tips:
  const char *tiptext[4] = {"1. Breathe", "2. Stretch muscles", "3. Drink", "4. Do a chore"};
  for(int tip = 0; tip < 4; tip++) {
    TextOut(hdc, bar.left, bar.bottom + 50 + tip*20, tiptext[tip], strlen(tiptext[tip]));
  }

  // Delete all created brushes or will create memory leak and randomly malfunction.
  DeleteObject(colourgreen);
  DeleteObject(colourorange);
  DeleteObject(colourred);  
  DeleteObject(font);
  EndPaint(hwnd, &ps);
  break;
}

case WM_CLOSE: {
  // Don't actually exit the program when the user clicks close,
  // just hide the window and keep a visible reminder in the taskbar.
  CloseWindow(hwnd);
  SetFocus(GetForegroundWindow());
  break;
}

case WM_DESTROY: {
  // Clean up when program is shut down.
  KillTimer(hwnd, timerID);

  // Restore the normal cursor and destroy 'normalcursor'
  SetSystemCursor(normalcursor, OCR_NORMAL);
  DestroyCursor(autocursor);   

  // Remove the taskbar mini icon.
  Shell_NotifyIcon(NIM_DELETE, &trayicon);  

  // Inform user that program is deactivated.
  MessageBeep(MB_ICONSTOP);  

  // Failsafe: release any simulated keypresses that might be stuck:
  pressKey(VK_SHIFT, false);
  clickMouse(false);

  PostQuitMessage(0);   
  break;
}

default: {return DefWindowProc(hwnd, message, wParam, lParam);}   
}   // end of switch
return 0;   
}   // End of function WindowProcedure()





void checkTrialTime(int days) {
// Shows a donation reminder x days after installation:
// Get the program's own path including filename:
char programfilename[MAX_PATH] = "";
if(GetModuleFileName(NULL, programfilename, MAX_PATH)) {
  // Retrieve the file info.
  struct stat filedata = {0};
  if(stat(programfilename, &filedata) == 0) {
    const time_t creationtime = filedata.st_ctime;
    double owneddays = difftime(time(0), creationtime) /60/60/24;
    // Show message after x days:
    if(floor(owneddays) == days) {
      MessageBox(NULL, "You may continue to use RSI autoclick for free, but a small donation \nwould be quite welcome, and allows further improvements.", "RSI autoclick: Donate if you like it", MB_OK | MB_ICONINFORMATION | MB_SETFOREGROUND);
    }
  }
}
}





// Computer control functions:

// Checks if a particular key is held at the moment.
bool keyPressed(unsigned char k) {return GetAsyncKeyState(k);}


void pressKey(const char key, bool down = true) {
// Simulates a key press or release.
// e.g. use "pressKey(VkKeyScan('A'),true);" to press the 'a' key.
INPUT Input = {INPUT_KEYBOARD};
Input.ki.wVk = key;      
Input.ki.dwFlags = (down)? 0 : KEYEVENTF_KEYUP;
SendInput(1, &Input, sizeof(INPUT));
// Give external programs a little time to process our simulated keypresses.
Sleep(50);
}


void clickMouse(bool down = true) {
// Simulate left mouse button down or up click.
INPUT Input = {INPUT_MOUSE};
if(lefthanded) {Input.mi.dwFlags = (down)? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP;}
else           {Input.mi.dwFlags = (down)? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;}
SendInput(1, &Input, sizeof(INPUT));
// Give external programs a little time to process our simulated keypresses.
Sleep(50);
}


void popup(HWND window) {
DWORD thisprogram = GetCurrentThreadId();
DWORD frontprogram = GetWindowThreadProcessId(GetForegroundWindow(), NULL);
if(frontprogram != thisprogram) {
  // BringWindowToTop() requires that we temporarily attach to the foreground program.
  if(AttachThreadInput(frontprogram, thisprogram, true)
  && BringWindowToTop(window)   // This moves the target window to the screen foreground.
  && AttachThreadInput(frontprogram, thisprogram, false)
  ) {OpenIcon(window);}   // Maximize if the window was minimized
}
}


// This function is used to pad numbers 0 to 9 by returning a "0" to inject in a string.
// e.g. sprintf(var, "%i:%s%i", 12, padwithzero(9), 9); prints the time "12:09"
const char *padwithzero(int nr) {return (abs(nr) < 10)? "0" : "";}   

