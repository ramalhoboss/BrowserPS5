
// BrowserPS5 (poc/webkit/editing): build-webkit.sh appends this to
// Source/WebKit/WebProcess/WebPage/playstation/WebPagePlayStation.cpp, in
// place of the port's stub (notImplemented(), false), which left every key
// in a text field without effect. The logic is WPE's WebEditorClient
// (WebEditorClientWPE.cpp): editing commands on keydown, the text on
// keypress. Not compiled on its own.

#include <WebCore/Document.h>
#include <WebCore/Editor.h>
#include <WebCore/EventNames.h>
#include <WebCore/FrameDestructionObserverInlines.h>
#include <WebCore/KeyboardEvent.h>
#include <WebCore/LocalFrame.h>
#include <WebCore/LocalFrameInlines.h>
#include <WebCore/Node.h>
#include <WebCore/NodeDocument.h>
#include <WebCore/NodeInlines.h>
#include <WebCore/PlatformKeyboardEvent.h>
#include <WebCore/WindowsKeyboardCodes.h>

namespace WebKit {

// The keys that edit rather than insert, without Ctrl or Alt (the
// controller and the system keyboard have neither).
static const char* editingCommandForKeyEvent(const WebCore::KeyboardEvent& event)
{
    bool shift = event.shiftKey();

    if (event.ctrlKey() || event.altKey() || event.metaKey())
        return nullptr;
    if (event.type() == WebCore::eventNames().keydownEvent) {
        switch (event.keyCode()) {
        case VK_BACK:
            return "DeleteBackward";
        case VK_DELETE:
            return "DeleteForward";
        case VK_LEFT:
            return shift ? "MoveLeftAndModifySelection" : "MoveLeft";
        case VK_RIGHT:
            return shift ? "MoveRightAndModifySelection" : "MoveRight";
        case VK_UP:
            return shift ? "MoveUpAndModifySelection" : "MoveUp";
        case VK_DOWN:
            return shift ? "MoveDownAndModifySelection" : "MoveDown";
        case VK_HOME:
            return shift ? "MoveToBeginningOfLineAndModifySelection" : "MoveToBeginningOfLine";
        case VK_END:
            return shift ? "MoveToEndOfLineAndModifySelection" : "MoveToEndOfLine";
        case VK_TAB:
            return shift ? "InsertBacktab" : "InsertTab";
        case VK_RETURN:
            return shift ? "InsertLineBreak" : "InsertNewline";
        }
        return nullptr;
    }
    switch (event.charCode()) {
    case '\t':
        return shift ? "InsertBacktab" : "InsertTab";
    case '\r':
        return shift ? "InsertLineBreak" : "InsertNewline";
    }
    return nullptr;
}

bool WebPage::handleEditingKeyboardEvent(WebCore::KeyboardEvent& event)
{
    RefPtr node = dynamicDowncast<WebCore::Node>(event.target());
    RefPtr frame = node ? node->document().frame() : nullptr;
    auto* platformEvent = event.underlyingPlatformEvent();
    if (!frame || !platformEvent)
        return false;

    const char* name = editingCommandForKeyEvent(event);
    if (event.type() == WebCore::eventNames().keydownEvent) {
        if (!name)
            return false;
        // Text insertions (Tab, Enter) wait for the keypress, so that
        // WebCore can still use Tab for the focus and Enter for a form.
        auto command = frame->editor().command(String::fromLatin1(name));
        return !command.isTextInsertion() && command.execute();
    }
    if (event.type() != WebCore::eventNames().keypressEvent || !frame->editor().canEdit())
        return false;
    if (name)
        return frame->editor().command(String::fromLatin1(name)).execute();
    // No null or control characters, nothing with Ctrl or Alt held.
    if (event.charCode() < ' ' || platformEvent->controlKey() || platformEvent->altKey())
        return false;
    return frame->editor().insertText(platformEvent->text(), &event);
}

} // namespace WebKit
