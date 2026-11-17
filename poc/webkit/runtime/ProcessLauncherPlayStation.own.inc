// BrowserPS5 RN (2026-11-11, poc/webkit/build-webkit.sh): an engine variant
// built with other switches is a closed set (its messages between the
// processes have other numbers): BROWSERPS5_PROCESS_DIR, which the UI linked
// with that library sets before it makes its context, names the folder of
// that engine's own NetworkProcess.elf and WebProcess.elf. This port never
// uses the paths in WKContextConfiguration, and the "plus" engine's first
// run (12:22) started the usual NetworkProcess: "Received invalid message",
// ten times, and no page. Without the variable nothing changes.
WTF_ALLOW_UNSAFE_BUFFER_USAGE_BEGIN
static const char* browserps5OwnProcess(const char* name, const char* usual)
{
    static char paths[2][256];
    const char* directory = getenv("BROWSERPS5_PROCESS_DIR");
    if (!directory || !directory[0])
        return usual;
    char* path = paths[name[0] == 'N'];
    snprintf(path, sizeof(paths[0]), "%s/%sProcess.elf", directory, name);
    return path;
}
WTF_ALLOW_UNSAFE_BUFFER_USAGE_END
