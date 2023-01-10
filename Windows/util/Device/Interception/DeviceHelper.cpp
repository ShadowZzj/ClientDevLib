#include "DeviceHelper.h"
#include <Windows/util/Process/ProcessHelper.h>
#include <Windows/util/Process/ThreadHelper.h>
zzj::InputInterceptor::InputInterceptor()
{
    
}

bool zzj::InputInterceptor::Load()
{
    if (m_IsLoad)
        return true;

    m_RequestStop = false;
    
    m_Keyboards.clear();
    m_Mice.clear();
    m_KeyboardHandler = nullptr;
    m_MouseHandler       = nullptr;
    m_context          = nullptr;
    m_IsLoad      = false;

    if (m_context = interception_create_context();m_context != nullptr)
    {
        m_LoopThread = std::move(std::thread([this]() { this->RunLoop(); }));
        if (!zzj::Thread::SetThreadPriority(m_LoopThread.native_handle(), THREAD_PRIORITY_HIGHEST))
        {
            m_RequestStop = true;
            m_LoopThread.join();
            return false;
        }
        m_IsLoad = true;
        return true;
    }
    return false;
}

void zzj::InputInterceptor::Unload()
{
    if (!m_IsLoad)
        return;

    m_RequestStop = true;
    m_LoopThread.join();
    interception_destroy_context(m_context);
    m_IsLoad = false;
    return;
}

bool zzj::InputInterceptor::SendKey(KeyScanCode scanCode, InterceptionKeyState keyState)
{
    InterceptionKeyStroke stroke;
    stroke.code = scanCode;
    stroke.state = keyState;

    if (m_Keyboards.empty())
        return false;
    InterceptionDevice device;
    //get the first device. Later, we may get the active device.
    for (auto& _device :m_Keyboards)
    {
        device = _device;
        break;
    }
    auto res = interception_send(m_context, device, (InterceptionStroke*)&stroke, 1);
    if (res > 0)
        return true;
    return false;
}

bool zzj::InputInterceptor::SendKey(KeyScanCode scanCode)
{
    auto res  = SendKey(scanCode, INTERCEPTION_KEY_DOWN);
    if (!res)
        return false;

    if (keyPressDelay > 0)
        Sleep(keyPressDelay);

    res = SendKey(scanCode, INTERCEPTION_KEY_UP);
    return res;
}

bool zzj::InputInterceptor::SendKeys(const std::vector<KeyScanCode> &scanCodes)
{
    for (auto& scanCode: scanCodes)
    {
        SendKey(scanCode);
    }
    return true;
}

bool zzj::InputInterceptor::SendText(std::string text)
{
    for(const auto& ch: text)
    {
        auto [scanCode,needShift] = CharacterToKeysEnum(ch);

        if (needShift == true) // We need to press shift to get the next character
            SendKey(KeyScanCode::LeftShift, INTERCEPTION_KEY_DOWN);

        SendKey(scanCode);

        if (needShift == true)
            SendKey(KeyScanCode::LeftShift, INTERCEPTION_KEY_UP);
    }
    return true;
}

void zzj::InputInterceptor::RunLoop()
{
    interception_set_filter(m_context, interception_is_keyboard, keyboardFilter);
    interception_set_filter(m_context, interception_is_mouse, mouseFilter);
    InterceptionStroke stroke;

    InterceptionDevice device; 
    while (1)
    {
        device = interception_wait_with_timeout(m_context, 1000);
        //device = interception_wait(m_context);

        //timeout
        if (0 == device)
        {
            if (m_RequestStop)
                break;
            continue;
        }

        if (interception_receive(m_context, device, &stroke, 1) <= 0)
            break;

        if (interception_is_keyboard(device))
        {
            
            InterceptionKeyStroke &keystroke = *(InterceptionKeyStroke *)&stroke;
            if (auto iter = m_Keyboards.find(device);iter == m_Keyboards.end())
                m_Keyboards.insert(device);
            else
            {
            }
            if (m_KeyboardHandler)
            {
                bool isHanded = m_KeyboardHandler(keystroke);
                if (isHanded)
                    continue;
            }
        }

        if (interception_is_mouse(device))
        {
            InterceptionMouseStroke &mousestroke = *(InterceptionMouseStroke *)&stroke;
            if (auto iter = m_Mice.find(device); iter == m_Mice.end())
                m_Mice.insert(device);
            else
            {
            }
            if (m_MouseHandler)
            {
                bool isHanded = m_MouseHandler(mousestroke);
                if (isHanded)
                    continue;
            }
        }
        interception_send(m_context, device, &stroke, 1);
    }
    return;
}
