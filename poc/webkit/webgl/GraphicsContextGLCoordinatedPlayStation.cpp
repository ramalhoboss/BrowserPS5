/* BrowserPS5 (2026-11-11, poc/webkit/webgl): GraphicsContextGLCoordinated's
 * one function that the port has only with libepoxy
 * (GraphicsContextGLCoordinatedEpoxy.cpp, which this is, with the system's
 * own OpenGL ES headers). */
#include "config.h"
#include "GraphicsContextGLCoordinated.h"

#if ENABLE(WEBGL) && USE(COORDINATED_GRAPHICS) && !USE(LIBEPOXY)
#include <GLES2/gl2.h>

namespace WebCore {

GCGLuint GraphicsContextGLCoordinated::setupCurrentTexture() const
{
    // The texture bound now was bound by ANGLE: asked of the system's OpenGL ES, it is the real one.
    GLint texture;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture);

    // ANGLE configured it too, but keeps the values until another call makes it apply them, which
    // does not come: the same here, so that the texture is as the texture mapper expects.
    GLenum textureTarget = GL_TEXTURE_2D;
    glTexParameteri(textureTarget, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(textureTarget, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(textureTarget, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(textureTarget, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return texture;
}

} // namespace WebCore

#endif // ENABLE(WEBGL) && USE(COORDINATED_GRAPHICS) && !USE(LIBEPOXY)
