#if PLATFORM(PLAYSTATION)
// BrowserPS5 RN (2026-11-11, the "plus" engine: ENABLE_CONTENT_EXTENSIONS):
// the content rule store hands its compiled file to the other processes as
// shared memory, and this port had no way to make one from a mapped file
// (the Windows one above shares the file's mapping; ENABLE_SHAREABLE_RESOURCE
// is off here, so the network cache never asks). A copy in shared memory of
// the usual kind, which the processes of this port already pass around.
RefPtr<WebCore::SharedMemory> Data::tryCreateSharedMemory() const
{
    if (isNull())
        return nullptr;

    auto memory = WebCore::SharedMemory::allocate(size());
    if (!memory)
        return nullptr;

    memcpySpan(memory->mutableSpan(), span());
    return memory;
}
#endif

