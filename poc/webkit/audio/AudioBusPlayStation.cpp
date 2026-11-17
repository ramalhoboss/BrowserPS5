/* BrowserPS5 (2026-11-11, poc/webkit/audio): Web Audio's own sound files
 * (the HRTF panner's impulse responses: WebCore/platform/audio/resources),
 * which GTK and WPE keep inside the library as GResources. Here they are
 * files: BROWSERPS5_AUDIO_DIR, or /app0/audio (the title, where the
 * WebProcess runs). None there: no HRTF panner; the equal-power one, the
 * default, needs none. */
#include "config.h"

#if ENABLE(WEB_AUDIO)
#include "AudioBus.h"

#include "AudioFileReader.h"
#include <stdlib.h>
#include <wtf/FileSystem.h>
#include <wtf/text/MakeString.h>

namespace WebCore {

RefPtr<AudioBus> AudioBus::loadPlatformResource(StringView name, float sampleRate)
{
    const char* directory = getenv("BROWSERPS5_AUDIO_DIR");
    auto path = makeString(String::fromUTF8(directory ? directory : "/app0/audio"), '/', name, ".wav"_s);
    auto data = FileSystem::readEntireFile(path);
    if (!data)
        return nullptr;
    return createBusFromInMemoryAudioFile(data->span(), false, sampleRate);
}

} // namespace WebCore

#endif // ENABLE(WEB_AUDIO)
