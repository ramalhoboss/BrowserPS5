
// BrowserPS5 (poc/webkit/gpu/bps5_layer.h): the page's ID in its
// WebProcess, which names the shared memory its frames come in when the
// compositor draws them on the GPU. Appended to WKPagePrivatePlayStation.cpp
// by poc/webkit/build-webkit.sh.
uint64_t WKPageGetWebPageIDForBrowserPS5(WKPageRef pageRef)
{
    return WebKit::toImpl(pageRef)->webPageIDInMainFrameProcess().toUInt64();
}
