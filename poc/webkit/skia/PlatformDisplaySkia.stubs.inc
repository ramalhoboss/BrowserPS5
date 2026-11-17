#else // BrowserPS5: PlayStation with coordinated graphics has no Skia GL context (skiaGLContext() above): the tiles are painted on the CPU
void PlatformDisplay::setSkiaGLContextForCurrentThread(std::unique_ptr<GLContext>&&)
{
}

GrDirectContext* PlatformDisplay::skiaGrContext() const
{
    return nullptr;
}

unsigned PlatformDisplay::msaaSampleCount() const
{
    return 0;
}

size_t PlatformDisplay::maxSkiaResourceCacheBytes() const
{
    return 0;
}

void PlatformDisplay::skiaReleaseUnusedResources(WTF::Critical)
{
}

void PlatformDisplay::clearSkiaGLContext()
{
}
#endif

