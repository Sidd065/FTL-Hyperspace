#include "SdlTest.h"

#ifdef HS_TEST_SDL

#include "Global.h"

HOOK_METHOD(CApp, OnInit, () -> bool)
{
    LOG_HOOK("HOOK_METHOD -> CApp::OnInit -> Begin (SdlTestHooks.cpp)\n")
    SdlTest::Init();
    return super();
}

#ifdef HS_ANDROID
// The iPad game draws no cursor; the frame counter is its last drawing before the frame is shown
HOOK_METHOD(CFPS, OnRender, () -> void)
{
    LOG_HOOK("HOOK_METHOD -> CFPS::OnRender -> Begin (SdlTestHooks.cpp)\n")
    super();
    SdlTest::Render();
}
#else
// The cursor is drawn last, so rendering before it puts the report on top of everything
HOOK_METHOD(MouseControl, OnRender, () -> void)
{
    LOG_HOOK("HOOK_METHOD -> MouseControl::OnRender -> Begin (SdlTestHooks.cpp)\n")
    SdlTest::Render();
    super();
}
#endif

#endif // HS_TEST_SDL
