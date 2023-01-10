#pragma once
#include "interception.h"
#include <set>
#include <thread>
#include <vector>
#include <tuple>
#include <string>
namespace zzj
{
enum KeyScanCode : unsigned short
{
    Escape                   = 1,
    One                      = 2,
    Two                      = 3,
    Three                    = 4,
    Four                     = 5,
    Five                     = 6,
    Six                      = 7,
    Seven                    = 8,
    Eight                    = 9,
    Nine                     = 10,
    Zero                     = 11,
    DashUnderscore           = 12,
    PlusEquals               = 13,
    Backspace                = 14,
    Tab                      = 15,
    Q                        = 16,
    W                        = 17,
    E                        = 18,
    R                        = 19,
    T                        = 20,
    Y                        = 21,
    U                        = 22,
    I                        = 23,
    O                        = 24,
    P                        = 25,
    OpenBracketBrace         = 26,
    CloseBracketBrace        = 27,
    Enter                    = 28,
    Control                  = 29,
    A                        = 30,
    S                        = 31,
    D                        = 32,
    F                        = 33,
    G                        = 34,
    H                        = 35,
    J                        = 36,
    K                        = 37,
    L                        = 38,
    SemicolonColon           = 39,
    SingleDoubleQuote        = 40,
    Tilde                    = 41,
    LeftShift                = 42,
    BackslashPipe            = 43,
    Z                        = 44,
    X                        = 45,
    C                        = 46,
    V                        = 47,
    B                        = 48,
    N                        = 49,
    M                        = 50,
    CommaLeftArrow           = 51,
    PeriodRightArrow         = 52,
    ForwardSlashQuestionMark = 53,
    RightShift               = 54,
    RightAlt                 = 56,
    Space                    = 57,
    CapsLock                 = 58,
    F1                       = 59,
    F2                       = 60,
    F3                       = 61,
    F4                       = 62,
    F5                       = 63,
    F6                       = 64,
    F7                       = 65,
    F8                       = 66,
    F9                       = 67,
    F10                      = 68,
    F11                      = 87,
    F12                      = 88,
    Up                       = 72,
    Down                     = 80,
    Right                    = 77,
    Left                     = 75,
    Home                     = 71,
    End                      = 79,
    Delete                   = 83,
    PageUp                   = 73,
    PageDown                 = 81,
    Insert                   = 82,
    PrintScreen              = 55, // And break key is 42
    NumLock                  = 69,
    ScrollLock               = 70,
    Menu                     = 93,
    WindowsKey               = 91,
    NumpadDivide             = 53,
    NumpadAsterisk           = 55,
    Numpad7                  = 71,
    Numpad8                  = 72,
    Numpad9                  = 73,
    Numpad4                  = 75,
    Numpad5                  = 76,
    Numpad6                  = 77,
    Numpad1                  = 79,
    Numpad2                  = 80,
    Numpad3                  = 81,
    Numpad0                  = 82,
    NumpadDelete             = 83,
    NumpadEnter              = 28,
    NumpadPlus               = 78,
    NumpadMinus              = 74,
};
inline std::tuple<KeyScanCode, bool> CharacterToKeysEnum(char c)
{
    switch (c)
    {
    case 'A':
        return std::tuple<KeyScanCode, bool>(KeyScanCode::A, true);
    case 'B':
        return std::tuple<KeyScanCode, bool>(KeyScanCode::B, true);
    case 'C':
        return std::tuple<KeyScanCode, bool>(KeyScanCode::C, true);
    case 'D':
        return std::tuple<KeyScanCode, bool>(KeyScanCode::D, true);
    case 'E':
        return std::tuple<KeyScanCode, bool>(KeyScanCode::E, true);
    case 'F':
        return std::tuple<KeyScanCode, bool>(KeyScanCode::F, true);
    case 'G':
        return std::tuple<KeyScanCode, bool>(KeyScanCode::G, true);
    case 'H':
        return std::tuple<KeyScanCode, bool>(KeyScanCode::H, true);
    case 'I':
        return std::tuple<KeyScanCode, bool>(KeyScanCode::I, true);
    case 'J':
        return std::tuple<KeyScanCode, bool>(KeyScanCode::J, true);
    case 'K':
        return std::tuple<KeyScanCode, bool>(KeyScanCode::K, true);
    case 'L':
        return std::tuple<KeyScanCode, bool>(KeyScanCode::L, true);
    case 'M':
        return std::tuple<KeyScanCode, bool>(KeyScanCode::M, true);
    case 'N':
        return std::tuple<KeyScanCode, bool>(KeyScanCode::N, true);
    case 'O':
        return std::tuple<KeyScanCode, bool>(KeyScanCode::O, true);
    case 'P':
        return std::tuple<KeyScanCode, bool>(KeyScanCode::P, true);
    case 'Q':
        return std::tuple<KeyScanCode, bool>(KeyScanCode::Q, true);
    case 'R':
        return std::tuple<KeyScanCode, bool>(KeyScanCode::R, true);
    case 'S':
        return std::tuple<KeyScanCode, bool>(KeyScanCode::S, true);
    case 'T':
        return std::tuple<KeyScanCode, bool>(KeyScanCode::T, true);
    case 'U':
        return std::tuple<KeyScanCode, bool>(KeyScanCode::U, true);
    case 'V':
        return std::tuple<KeyScanCode, bool>(KeyScanCode::V, true);
    case 'W':
        return std::tuple<KeyScanCode, bool>(KeyScanCode::W, true);
    case 'X':
        return std::tuple<KeyScanCode, bool>(KeyScanCode::X, true);
    case 'Y':
        return std::tuple<KeyScanCode, bool>(KeyScanCode::Y, true);
    case 'Z':
        return std::tuple<KeyScanCode, bool>(KeyScanCode::Z, true);
    case 'a':
        return std::tuple<KeyScanCode, bool>(KeyScanCode::A, false);
    case 'b':
        return std::tuple<KeyScanCode, bool>(KeyScanCode::B, false);
    case 'c':
        return std::tuple<KeyScanCode, bool>(KeyScanCode::C, false);
    case 'd':
        return std::tuple<KeyScanCode, bool>(KeyScanCode::D, false);
    case 'e':
        return std::tuple<KeyScanCode, bool>(KeyScanCode::E, false);
    case 'f':
        return std::tuple<KeyScanCode, bool>(KeyScanCode::F, false);
    case 'g':
        return std::tuple<KeyScanCode, bool>(KeyScanCode::G, false);
    case 'h':
        return std::tuple<KeyScanCode, bool>(KeyScanCode::H, false);
    case 'i':
        return std::tuple<KeyScanCode, bool>(KeyScanCode::I, false);
    case 'j':
        return std::tuple<KeyScanCode, bool>(KeyScanCode::J, false);
    case 'k':
        return std::tuple<KeyScanCode, bool>(KeyScanCode::K, false);
    case 'l':
        return std::tuple<KeyScanCode, bool>(KeyScanCode::L, false);
    case 'm':
        return std::tuple<KeyScanCode, bool>(KeyScanCode::M, false);
    case 'n':
        return std::tuple<KeyScanCode, bool>(KeyScanCode::N, false);
    case 'o':
        return std::tuple<KeyScanCode, bool>(KeyScanCode::O, false);
    case 'p':
        return std::tuple<KeyScanCode, bool>(KeyScanCode::P, false);
    case 'q':
        return std::tuple<KeyScanCode, bool>(KeyScanCode::Q, false);
    case 'r':
        return std::tuple<KeyScanCode, bool>(KeyScanCode::R, false);
    case 's':
        return std::tuple<KeyScanCode, bool>(KeyScanCode::S, false);
    case 't':
        return std::tuple<KeyScanCode, bool>(KeyScanCode::T, false);
    case 'u':
        return std::tuple<KeyScanCode, bool>(KeyScanCode::U, false);
    case 'v':
        return std::tuple<KeyScanCode, bool>(KeyScanCode::V, false);
    case 'w':
        return std::tuple<KeyScanCode, bool>(KeyScanCode::W, false);
    case 'x':
        return std::tuple<KeyScanCode, bool>(KeyScanCode::X, false);
    case 'y':
        return std::tuple<KeyScanCode, bool>(KeyScanCode::Y, false);
    case 'z':
        return std::tuple<KeyScanCode, bool>(KeyScanCode::Z, false);
    case '1':
        return std::tuple<KeyScanCode, bool>(KeyScanCode::One, false);
    case '2':
        return std::tuple<KeyScanCode, bool>(KeyScanCode::Two, false);
    case '3':
        return std::tuple<KeyScanCode, bool>(KeyScanCode::Three, false);
    case '4':
        return std::tuple<KeyScanCode, bool>(KeyScanCode::Four, false);
    case '5':
        return std::tuple<KeyScanCode, bool>(KeyScanCode::Five, false);
    case '6':
        return std::tuple<KeyScanCode, bool>(KeyScanCode::Six, false);
    case '7':
        return std::tuple<KeyScanCode, bool>(KeyScanCode::Seven, false);
    case '8':
        return std::tuple<KeyScanCode, bool>(KeyScanCode::Eight, false);
    case '9':
        return std::tuple<KeyScanCode, bool>(KeyScanCode::Nine, false);
    case '0':
        return std::tuple<KeyScanCode, bool>(KeyScanCode::Zero, false);
    case '-':
        return std::tuple<KeyScanCode, bool>(KeyScanCode::DashUnderscore, false);
    case '+':
        return std::tuple<KeyScanCode, bool>(KeyScanCode::PlusEquals, false);
    case '[':
        return std::tuple<KeyScanCode, bool>(KeyScanCode::OpenBracketBrace, false);
    case ']':
        return std::tuple<KeyScanCode, bool>(KeyScanCode::CloseBracketBrace, false);
    case ';':
        return std::tuple<KeyScanCode, bool>(KeyScanCode::SemicolonColon, false);
    case '\'':
        return std::tuple<KeyScanCode, bool>(KeyScanCode::SingleDoubleQuote, false);
    case ',':
        return std::tuple<KeyScanCode, bool>(KeyScanCode::CommaLeftArrow, false);
    case '.':
        return std::tuple<KeyScanCode, bool>(KeyScanCode::PeriodRightArrow, false);
    case '/':
        return std::tuple<KeyScanCode, bool>(KeyScanCode::ForwardSlashQuestionMark, false);
    case '{':
        return std::tuple<KeyScanCode, bool>(KeyScanCode::OpenBracketBrace, true);
    case '}':
        return std::tuple<KeyScanCode, bool>(KeyScanCode::CloseBracketBrace, true);
    case ':':
        return std::tuple<KeyScanCode, bool>(KeyScanCode::SemicolonColon, true);
    case '\"':
        return std::tuple<KeyScanCode, bool>(KeyScanCode::SingleDoubleQuote, true);
    case '<':
        return std::tuple<KeyScanCode, bool>(KeyScanCode::CommaLeftArrow, true);
    case '>':
        return std::tuple<KeyScanCode, bool>(KeyScanCode::PeriodRightArrow, true);
    case '?':
        return std::tuple<KeyScanCode, bool>(KeyScanCode::ForwardSlashQuestionMark, true);
    case '\\':
        return std::tuple<KeyScanCode, bool>(KeyScanCode::BackslashPipe, false);
    case '|':
        return std::tuple<KeyScanCode, bool>(KeyScanCode::BackslashPipe, true);
    case '`':
        return std::tuple<KeyScanCode, bool>(KeyScanCode::Tilde, false);
    case '~':
        return std::tuple<KeyScanCode, bool>(KeyScanCode::Tilde, true);
    case '!':
        return std::tuple<KeyScanCode, bool>(KeyScanCode::One, true);
    case '@':
        return std::tuple<KeyScanCode, bool>(KeyScanCode::Two, true);
    case '#':
        return std::tuple<KeyScanCode, bool>(KeyScanCode::Three, true);
    case '$':
        return std::tuple<KeyScanCode, bool>(KeyScanCode::Four, true);
    case '%':
        return std::tuple<KeyScanCode, bool>(KeyScanCode::Five, true);
    case '^':
        return std::tuple<KeyScanCode, bool>(KeyScanCode::Six, true);
    case '&':
        return std::tuple<KeyScanCode, bool>(KeyScanCode::Seven, true);
    case '*':
        return std::tuple<KeyScanCode, bool>(KeyScanCode::Eight, true);
    case '(':
        return std::tuple<KeyScanCode, bool>(KeyScanCode::Nine, true);
    case ')':
        return std::tuple<KeyScanCode, bool>(KeyScanCode::Zero, true);
    case ' ':
        return std::tuple<KeyScanCode, bool>(KeyScanCode::Space, true);
    default:
        return std::tuple<KeyScanCode, bool>(KeyScanCode::ForwardSlashQuestionMark, true);
    }
}
typedef bool (*KeyboardEventHandler)(InterceptionKeyStroke strokeInfo);
typedef bool (*MouseEventHandler)(InterceptionMouseStroke strokeInfo);
class InputInterceptor
{
  public:
    InterceptionFilterKeyState keyboardFilter;
    InterceptionFilterMouseState mouseFilter;
    int keyPressDelay   = 1;  // ms
    int clickPressDelay = 1;  // ms
    int scrollDelay     = 15; // ms

    static InputInterceptor *CreateInstance()
    {
        static InputInterceptor *instance = new InputInterceptor();
        return instance;
    }
    bool Load();
    void Unload();
    //With Up or Down etc.
    bool SendKey(KeyScanCode scanCode, InterceptionKeyState keyState);
    //Up and Down
    bool SendKey(KeyScanCode scanCode);
    bool SendKeys(const std::vector<KeyScanCode> &scanCodes);
    bool SendText(std::string text);
    void SetKeyBoardHandler(KeyboardEventHandler handler)
    {
        m_KeyboardHandler = handler;
    }

    void SetMouseHandler(MouseEventHandler handler)
    {
        m_MouseHandler = handler;
    }

  private:
    InputInterceptor();
    std::set<InterceptionDevice> m_Keyboards;
    std::set<InterceptionDevice> m_Mice;
    KeyboardEventHandler m_KeyboardHandler = nullptr;
    MouseEventHandler m_MouseHandler       = nullptr;
    InterceptionContext m_context          = nullptr;
    std::thread m_LoopThread;
    bool m_IsLoad      = false;
    bool m_RequestStop = false;
    void RunLoop();
};
} // namespace zzj