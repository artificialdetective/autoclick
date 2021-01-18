// Main functions
LRESULT CALLBACK WindowProcedure(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
void autoClick(HWND hwnd);
int remindToPause(HWND hwnd, time_t &endofpause, bool minutemark);
void checkTrialTime(int days);

// Support functions
bool keyPressed(unsigned char k);
void pressKey(const char key, bool down);
void clickMouse(bool down);
void popup(HWND window);
const char *padwithzero(int nr);
