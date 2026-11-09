#include "Global.h"
#include "android/IpadUI.h"
#include <algorithm>

HOOK_STATIC(CSurface, GL_BlitPixelImage, (GL_Texture *tex, float x, float y, float x2, float y2, float rotation, GL_Color color, bool mirror, bool background) -> bool)
{
    LOG_HOOK("HOOK_STATIC -> CSurface::GL_BlitPixelImage -> Begin (Backgrounds.cpp)\n")
    if (!background || !tex || tex->width_ == tex->height_ || (tex->width_ == 1376 && tex->height_ == 768) || x2 <= 0.f || y2 <= 0.f)
    {
        return super(tex, x, y, x2, y2, rotation, color, mirror, background);
    }
    float scale = std::max(IpadUI::ScreenWidth() / x2, 768.f / y2);
    float w = x2 * scale;
    float h = y2 * scale;
    return super(tex, 688.f - w / 2 + x * scale, 384.f - h / 2 + y * scale, w, h, rotation, color, mirror, background);
}
