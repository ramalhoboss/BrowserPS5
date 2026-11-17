
// BrowserPS5 (2026-11-17): KeyboardEvent.code for the keys the browser sends.
// WKPageHandleKeyboardEvent gave the key's X11 keysym as its hardware key
// code too, and PlatformKeyboardEvent::keyCodeForHardwareKeyCode() knows the
// key codes of a keyboard (X11's, a US layout): every key came to the page
// with the code "Unidentified", and a page that reads the code (games and
// emulators: "Enter", "ArrowLeft", "KeyX") did not see the controller's
// keys. This gives the key code of the key that makes that keysym on a US
// keyboard, and 0 (no code, as before) for the others.
static unsigned browserPS5HardwareKeyCode(unsigned keysym)
{
    static const struct {
        unsigned keysym;
        unsigned keyCode;
    } keys[] = {
        { 0xff1b, 9 }, // Escape
        { 0x2d, 20 }, { 0x3d, 21 }, // - =
        { 0xff08, 22 }, { 0xff09, 23 }, // BackSpace, Tab
        { 0x5b, 34 }, { 0x5d, 35 }, // [ ]
        { 0xff0d, 36 }, // Return
        { 0xffe3, 37 }, // Control_L
        { 0x3b, 47 }, { 0x27, 48 }, { 0x60, 49 }, // ; ' `
        { 0xffe1, 50 }, // Shift_L
        { 0x5c, 51 }, // backslash
        { 0x2c, 59 }, { 0x2e, 60 }, { 0x2f, 61 }, // , . /
        { 0xffe2, 62 }, // Shift_R
        { 0xffe9, 64 }, // Alt_L
        { 0x20, 65 }, // space
        { 0xffe5, 66 }, // Caps_Lock
        { 0xffe4, 105 }, { 0xffea, 108 }, // Control_R, Alt_R
        { 0xff50, 110 }, { 0xff52, 111 }, { 0xff55, 112 }, // Home, Up, Page_Up
        { 0xff51, 113 }, { 0xff53, 114 }, // Left, Right
        { 0xff57, 115 }, { 0xff54, 116 }, { 0xff56, 117 }, // End, Down, Page_Down
        { 0xff63, 118 }, { 0xffff, 119 }, // Insert, Delete
    };
    static const char letters[] = "qwertyuiop\0\0\0\0asdfghjkl\0\0\0\0\0zxcvbnm"; // from key code 24
    if (keysym >= 'A' && keysym <= 'Z')
        keysym += 'a' - 'A';
    if (keysym >= 'a' && keysym <= 'z') {
        for (unsigned i = 0; i < sizeof(letters) - 1; i++) {
            if (static_cast<unsigned>(letters[i]) == keysym)
                return 24 + i;
        }
        return 0;
    }
    if (keysym >= '1' && keysym <= '9')
        return 10 + (keysym - '1');
    if (keysym == '0')
        return 19;
    if (keysym >= 0xffbe && keysym <= 0xffc7) // F1 to F10
        return 67 + (keysym - 0xffbe);
    for (const auto& key : keys) {
        if (key.keysym == keysym)
            return key.keyCode;
    }
    return 0;
}
