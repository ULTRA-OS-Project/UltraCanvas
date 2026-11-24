// OS/iOS/UltraCanvasiOSMain.mm
// The process entry for UltraCanvas applications on iOS.
//
// UIKit must own the main thread (UIApplicationMain never returns), while an
// UltraCanvas program's main() creates its windows and then blocks in
// Run(). So, as on Android, an application targeting iOS renames its main():
//
//     extern "C" int ultracanvas_app_main(int argc, char** argv);
//
// (a thin #if around the existing main body), and the framework provides
// main() here. When the app's scene connects, the scene delegate puts the
// root view in its window and schedules ultracanvas_app_main()
// with performSelector:afterDelay: - a run-loop timer, not a dispatch block,
// so the nested run loop Run() pumps (CollectAndProcessNativeEvents) still
// services the main dispatch queue, and the launch callback itself returns,
// as iOS requires. By then (the application delegate's launch callback ran
// first) HOME/XDG_* point into the app's container and the
// resources are the bundle's share/ (core/UltraCanvasConfig.cpp's
// exeDir + "/share/"), so path-based resource, settings and font code works
// unchanged. See Docs/UltraCanvas/iOSPortInvestigation.md 3.2 (option A).
// Version: 1.3.0 - UITextInput: composition (marked text) and the hardware keyboard through the text system while editing
// Version: 1.2.0 - the UIScene lifecycle: a scene delegate owns the window and the foreground events
// Version: 1.1.0 - the view takes the software keyboard (UIKeyInput) and hardware keys
// Version: 1.0.0
// Last Modified: 2026-11-24
// Author: UltraCanvas Framework

#include "UltraCanvasApplication.h"
#include "UltraCanvasiOSApplication.h"
#include "UltraCanvasiOSWindow.h"
#include "UltraCanvasDebug.h"

#import <UIKit/UIKit.h>

#include <algorithm>
#include <cstdlib>
#include <string>
#include <sys/stat.h>
#include <unordered_map>

// Provided by the application (its main() under a different name).
extern "C" int ultracanvas_app_main(int argc, char** argv);

namespace {
    int g_argc = 0;
    char** g_argv = nullptr;

    // Where settings, caches and data go: the container's Library, which iOS
    // keeps (Caches may be purged), set only where nothing set them already.
    void ExportSandboxEnvironment() {
        const char* home = std::getenv("HOME");
        if (!home || !*home) return;
        const std::string library = std::string(home) + "/Library";
        const std::string support = library + "/Application Support";
        const std::string caches = library + "/Caches";
        mkdir(library.c_str(), 0700);
        mkdir(support.c_str(), 0700);
        mkdir(caches.c_str(), 0700);
        setenv("XDG_CONFIG_HOME", support.c_str(), 0);
        setenv("XDG_DATA_HOME", support.c_str(), 0);
        setenv("XDG_CACHE_HOME", caches.c_str(), 0);
    }

    UltraCanvas::UltraCanvasiOSApplication* App() {
        return UltraCanvas::UltraCanvasiOSApplication::GetInstance();
    }

    std::string ToUtf8(NSString* text) {
        const char* utf8 = text.UTF8String;
        return utf8 ? std::string(utf8) : std::string();
    }

    // A hardware key (its USB HID usage, UIKeyboardHIDUsage) as a UCKeys.
    UltraCanvas::UCKeys KeyForHidUsage(long usage) {
        using UltraCanvas::UCKeys;
        if (usage >= 0x04 && usage <= 0x1D) return static_cast<UCKeys>(UCKeys::A + (usage - 0x04));
        if (usage >= 0x1E && usage <= 0x26) return static_cast<UCKeys>(UCKeys::Key1 + (usage - 0x1E));
        if (usage >= 0x3A && usage <= 0x45) return static_cast<UCKeys>(UCKeys::F1 + (usage - 0x3A));
        switch (usage) {
            case 0x27: return UCKeys::Key0;
            case 0x28: return UCKeys::Return;
            case 0x29: return UCKeys::Escape;
            case 0x2A: return UCKeys::Backspace;
            case 0x2B: return UCKeys::Tab;
            case 0x2C: return UCKeys::Space;
            case 0x2D: return UCKeys::Minus;
            case 0x2E: return UCKeys::Equal;
            case 0x2F: return UCKeys::LeftBracket;
            case 0x30: return UCKeys::RightBracket;
            case 0x31: return UCKeys::Backslash;
            case 0x33: return UCKeys::Semicolon;
            case 0x34: return UCKeys::Quote;
            case 0x35: return UCKeys::Grave;
            case 0x36: return UCKeys::Comma;
            case 0x37: return UCKeys::Period;
            case 0x38: return UCKeys::Slash;
            case 0x39: return UCKeys::CapsLock;
            case 0x46: return UCKeys::PrintScreen;
            case 0x47: return UCKeys::ScrollLock;
            case 0x48: return UCKeys::Pause;
            case 0x49: return UCKeys::Insert;
            case 0x4A: return UCKeys::Home;
            case 0x4B: return UCKeys::PageUp;
            case 0x4C: return UCKeys::Delete;
            case 0x4D: return UCKeys::End;
            case 0x4E: return UCKeys::PageDown;
            case 0x4F: return UCKeys::Right;
            case 0x50: return UCKeys::Left;
            case 0x51: return UCKeys::Down;
            case 0x52: return UCKeys::Up;
            case 0xE0: return UCKeys::LeftCtrl;
            case 0xE1: return UCKeys::LeftShift;
            case 0xE2: return UCKeys::LeftAlt;
            case 0xE3: return UCKeys::LeftMeta;
            case 0xE4: return UCKeys::RightCtrl;
            case 0xE5: return UCKeys::RightShift;
            case 0xE6: return UCKeys::RightAlt;
            case 0xE7: return UCKeys::RightMeta;
            default:   return UCKeys::Unknown;
        }
    }
}

// ===== TEXT POSITIONS =====
// UITextInput speaks in positions and ranges of its own classes. The view's
// "document" is only the text being composed (the marked text); everything
// already typed lives in the framework's widget, which edits it itself. So a
// position is an offset in UTF-16 units into the marked text.

@interface UltraCanvasiOSTextPosition : UITextPosition
@property (nonatomic, readonly) NSInteger index;
+ (instancetype)at:(NSInteger)index;
@end

@implementation UltraCanvasiOSTextPosition
+ (instancetype)at:(NSInteger)index {
    UltraCanvasiOSTextPosition* position = [[self alloc] init];
    position->_index = index;
    return position;
}
@end

@interface UltraCanvasiOSTextRange : UITextRange
@property (nonatomic, readonly) NSRange range;
+ (instancetype)with:(NSRange)range;
@end

@implementation UltraCanvasiOSTextRange
+ (instancetype)with:(NSRange)range {
    UltraCanvasiOSTextRange* textRange = [[self alloc] init];
    textRange->_range = range;
    return textRange;
}
- (UITextPosition*)start { return [UltraCanvasiOSTextPosition at:_range.location]; }
- (UITextPosition*)end { return [UltraCanvasiOSTextPosition at:NSMaxRange(_range)]; }
- (BOOL)isEmpty { return _range.length == 0; }
@end

// ===== THE VIEW: touches, size changes, the keyboards =====

// The view is also the keyboard's editor (UITextInput): it is first
// responder - which is what raises the software keyboard - only while a text
// widget holds the caret. Typed text is committed to the widget at once;
// what an input method is still composing is the view's marked text, which
// the widget sees as TextComposition events when it draws compositions
// (CJK input methods, the hardware keyboard's dead keys). Autocorrection
// stays off: it rewrites text already typed, and that text is the widget's,
// not the view's (iOSPortInvestigation.md 3.1).
@interface UltraCanvasiOSView : UIView <UITextInput>
@property (nonatomic) BOOL wantsKeyboard;
@property (nonatomic) UITextAutocorrectionType autocorrectionType;
@property (nonatomic) UITextAutocapitalizationType autocapitalizationType;
@property (nonatomic) UITextSpellCheckingType spellCheckingType;
@property (nonatomic) UITextSmartQuotesType smartQuotesType;
@property (nonatomic) UITextSmartDashesType smartDashesType;
@property (nonatomic) UIKeyboardType keyboardType;
@property (nonatomic, copy) NSDictionary<NSAttributedStringKey, id>* markedTextStyle;
@property (nonatomic, weak) id<UITextInputDelegate> inputDelegate;
@end

@implementation UltraCanvasiOSView {
    // Each finger on the screen and the small id it keeps for its touch
    // (the lowest free one, like Android's pointer ids).
    std::unordered_map<void*, int> _fingerIds;
    CGSize _lastSize;
    // The composition: the marked text and the selection in it.
    NSString* _marked;
    NSRange _selected;
    UITextInputStringTokenizer* _tokenizer;
}

- (instancetype)initWithFrame:(CGRect)frame {
    if ((self = [super initWithFrame:frame])) {
        self.multipleTouchEnabled = YES;
        self.opaque = YES;
        self.backgroundColor = [UIColor blackColor];
        _lastSize = CGSizeZero;
        _marked = @"";
        _selected = NSMakeRange(0, 0);
        // Text arrives as typed: the framework's widgets do their own
        // editing, and an autocorrection would replace text behind them.
        self.autocorrectionType = UITextAutocorrectionTypeNo;
        self.autocapitalizationType = UITextAutocapitalizationTypeNone;
        self.spellCheckingType = UITextSpellCheckingTypeNo;
        self.smartQuotesType = UITextSmartQuotesTypeNo;
        self.smartDashesType = UITextSmartDashesTypeNo;
        self.keyboardType = UIKeyboardTypeDefault;
    }
    return self;
}

// ===== The keyboard: committing and deleting =====

- (BOOL)canBecomeFirstResponder {
    return self.wantsKeyboard;
}

- (BOOL)hasText {
    // Always: the backspace must reach the widget, which knows whether it
    // has anything to delete.
    return YES;
}

- (void)clearComposition {
    _marked = @"";
    _selected = NSMakeRange(0, 0);
}

- (void)insertText:(NSString*)text {
    [self clearComposition];   // what was composed is replaced by the text
    if (auto* app = App()) {
        app->HandleInsertText(ToUtf8(text));
        app->WakeUpEventLoopFromView();
    }
}

- (void)deleteBackward {
    auto* app = App();
    if (!app) return;
    if (_marked.length > 0) {
        // Inside a composition the input method deletes in its own text.
        NSRange last = [_marked rangeOfComposedCharacterSequenceAtIndex:_marked.length - 1];
        [self setMarkedText:[_marked substringToIndex:last.location]
              selectedRange:NSMakeRange(last.location, 0)];
        return;
    }
    app->HandleDeleteBackward();
    app->WakeUpEventLoopFromView();
}

// ===== The keyboard: composing =====

- (UITextRange*)markedTextRange {
    if (_marked.length == 0) return nil;
    return [UltraCanvasiOSTextRange with:NSMakeRange(0, _marked.length)];
}

- (void)setMarkedText:(NSString*)markedText selectedRange:(NSRange)selectedRange {
    _marked = markedText ? [markedText copy] : @"";
    const NSUInteger at = std::min<NSUInteger>(selectedRange.location, _marked.length);
    _selected = NSMakeRange(at, std::min<NSUInteger>(selectedRange.length, _marked.length - at));
    // The caret as a byte offset into the UTF-8 text the widget gets.
    const int caretBytes = static_cast<int>(
            [[_marked substringToIndex:at] lengthOfBytesUsingEncoding:NSUTF8StringEncoding]);
    if (auto* app = App()) {
        app->HandleSetMarkedText(ToUtf8(_marked), caretBytes);
        app->WakeUpEventLoopFromView();
    }
}

- (void)unmarkText {
    if (_marked.length == 0) return;
    [self clearComposition];
    if (auto* app = App()) {
        app->HandleUnmarkText();
        app->WakeUpEventLoopFromView();
    }
}

// ===== The document: the marked text =====

- (NSString*)textInRange:(UITextRange*)range {
    if (![range isKindOfClass:[UltraCanvasiOSTextRange class]]) return @"";
    const NSRange r = ((UltraCanvasiOSTextRange*)range).range;
    if (r.location > _marked.length) return @"";
    return [_marked substringWithRange:NSMakeRange(r.location,
            std::min<NSUInteger>(r.length, _marked.length - r.location))];
}

- (void)replaceRange:(UITextRange*)range withText:(NSString*)text {
    // Only the composition is the view's to replace; anything else the
    // input method replaces is a word it settled on - typed as it is.
    [self insertText:text];
}

- (UITextRange*)selectedTextRange {
    return [UltraCanvasiOSTextRange with:_selected];
}

- (void)setSelectedTextRange:(UITextRange*)selectedTextRange {
    if ([selectedTextRange isKindOfClass:[UltraCanvasiOSTextRange class]]) {
        const NSRange r = ((UltraCanvasiOSTextRange*)selectedTextRange).range;
        if (NSMaxRange(r) <= _marked.length) _selected = r;
    }
}

- (UITextPosition*)beginningOfDocument {
    return [UltraCanvasiOSTextPosition at:0];
}

- (UITextPosition*)endOfDocument {
    return [UltraCanvasiOSTextPosition at:_marked.length];
}

- (UITextRange*)textRangeFromPosition:(UITextPosition*)fromPosition toPosition:(UITextPosition*)toPosition {
    const NSInteger a = ((UltraCanvasiOSTextPosition*)fromPosition).index;
    const NSInteger b = ((UltraCanvasiOSTextPosition*)toPosition).index;
    return [UltraCanvasiOSTextRange with:NSMakeRange(std::min(a, b), std::labs(b - a))];
}

- (UITextPosition*)positionFromPosition:(UITextPosition*)position offset:(NSInteger)offset {
    const NSInteger at = ((UltraCanvasiOSTextPosition*)position).index + offset;
    if (at < 0 || at > static_cast<NSInteger>(_marked.length)) return nil;
    return [UltraCanvasiOSTextPosition at:at];
}

- (UITextPosition*)positionFromPosition:(UITextPosition*)position
                            inDirection:(UITextLayoutDirection)direction offset:(NSInteger)offset {
    const bool back = direction == UITextLayoutDirectionLeft || direction == UITextLayoutDirectionUp;
    return [self positionFromPosition:position offset:(back ? -offset : offset)];
}

- (NSComparisonResult)comparePosition:(UITextPosition*)position toPosition:(UITextPosition*)other {
    const NSInteger a = ((UltraCanvasiOSTextPosition*)position).index;
    const NSInteger b = ((UltraCanvasiOSTextPosition*)other).index;
    return a < b ? NSOrderedAscending : (a > b ? NSOrderedDescending : NSOrderedSame);
}

- (NSInteger)offsetFromPosition:(UITextPosition*)from toPosition:(UITextPosition*)toPosition {
    return ((UltraCanvasiOSTextPosition*)toPosition).index - ((UltraCanvasiOSTextPosition*)from).index;
}

- (id<UITextInputTokenizer>)tokenizer {
    if (!_tokenizer) _tokenizer = [[UITextInputStringTokenizer alloc] initWithTextInput:self];
    return _tokenizer;
}

- (UITextPosition*)positionWithinRange:(UITextRange*)range farthestInDirection:(UITextLayoutDirection)direction {
    const bool back = direction == UITextLayoutDirectionLeft || direction == UITextLayoutDirectionUp;
    return back ? range.start : range.end;
}

- (UITextRange*)characterRangeByExtendingPosition:(UITextPosition*)position
                                      inDirection:(UITextLayoutDirection)direction {
    const NSInteger at = ((UltraCanvasiOSTextPosition*)position).index;
    const bool back = direction == UITextLayoutDirectionLeft || direction == UITextLayoutDirectionUp;
    const NSInteger other = std::clamp<NSInteger>(back ? at - 1 : at + 1, 0, _marked.length);
    return [UltraCanvasiOSTextRange with:NSMakeRange(std::min(at, other), std::labs(other - at))];
}

- (NSWritingDirection)baseWritingDirectionForPosition:(UITextPosition*)position
                                          inDirection:(UITextStorageDirection)direction {
    return NSWritingDirectionNatural;
}

- (void)setBaseWritingDirection:(NSWritingDirection)writingDirection forRange:(UITextRange*)range {}

// ===== Geometry: where the input method puts its candidates =====
// The focused widget's caret, which is where the composition is drawn.

- (CGRect)caretRect {
    auto* app = App();
    if (!app) return CGRectZero;
    const UltraCanvas::Rect2Df caret = app->CaretRectInWindow();
    return CGRectMake(caret.x, caret.y, std::max(1.0f, caret.width), std::max(1.0f, caret.height));
}

- (CGRect)firstRectForRange:(UITextRange*)range {
    return [self caretRect];
}

- (CGRect)caretRectForPosition:(UITextPosition*)position {
    return [self caretRect];
}

- (NSArray<UITextSelectionRect*>*)selectionRectsForRange:(UITextRange*)range {
    return @[];
}

- (UITextPosition*)closestPositionToPoint:(CGPoint)point {
    return [self endOfDocument];
}

- (UITextPosition*)closestPositionToPoint:(CGPoint)point withinRange:(UITextRange*)range {
    return range.end;
}

- (UITextRange*)characterRangeAtPoint:(CGPoint)point {
    return [UltraCanvasiOSTextRange with:NSMakeRange(_marked.length, 0)];
}

// ===== A hardware keyboard =====
// Outside text editing every press is the framework's: a key event with the
// key's own text. While a widget edits text, a press that types (no Control
// or Command), Return and Backspace go to the text system instead (super),
// which hands them back as insertText: / deleteBackward / marked text - so an
// input method composes from a hardware keyboard too, and nothing types
// twice. Arrows, Escape, Tab, function keys and shortcuts stay key events.

- (BOOL)deliverPresses:(NSSet<UIPress*>*)presses down:(BOOL)down {
    auto* app = App();
    BOOL handled = NO;
    for (UIPress* press in presses) {
        UIKey* key = press.key;
        if (!key) continue;
        const UltraCanvas::UCKeys ucKey = KeyForHidUsage(static_cast<long>(key.keyCode));
        const UIKeyModifierFlags flags = key.modifierFlags;
        const bool ctrl = (flags & UIKeyModifierControl) != 0;
        const bool meta = (flags & UIKeyModifierCommand) != 0;
        std::string text;
        // The key's text as the layout makes it; the keys that type
        // nothing (arrows, F-keys) give the private-use characters UIKit
        // names them with, which are not text.
        NSString* characters = key.characters;
        if (characters.length > 0 && [characters characterAtIndex:0] >= 0x20 &&
            !([characters characterAtIndex:0] >= 0xF700 && [characters characterAtIndex:0] <= 0xF8FF)) {
            text = ToUtf8(characters);
        }
        if (ucKey == UltraCanvas::UCKeys::Unknown && text.empty()) continue;
        if (self.wantsKeyboard && !ctrl && !meta &&
            (!text.empty() || ucKey == UltraCanvas::UCKeys::Return ||
             ucKey == UltraCanvas::UCKeys::Backspace)) {
            continue;   // the text system's (see above)
        }
        if (app) {
            app->HandleKey(down, ucKey, static_cast<int>(key.keyCode),
                           (flags & UIKeyModifierShift) != 0, ctrl,
                           (flags & UIKeyModifierAlternate) != 0, meta, text);
        }
        handled = YES;
    }
    if (handled && app) app->WakeUpEventLoopFromView();
    return handled;
}

- (void)pressesBegan:(NSSet<UIPress*>*)presses withEvent:(UIPressesEvent*)event {
    if (![self deliverPresses:presses down:YES]) [super pressesBegan:presses withEvent:event];
}

- (void)pressesEnded:(NSSet<UIPress*>*)presses withEvent:(UIPressesEvent*)event {
    if (![self deliverPresses:presses down:NO]) [super pressesEnded:presses withEvent:event];
}

- (void)pressesCancelled:(NSSet<UIPress*>*)presses withEvent:(UIPressesEvent*)event {
    if (![self deliverPresses:presses down:NO]) [super pressesCancelled:presses withEvent:event];
}

// ===== Appearance =====

- (void)traitCollectionDidChange:(UITraitCollection*)previous {
    [super traitCollectionDidChange:previous];
    if (auto* app = App()) app->HandleTraitsChanged();
}

- (int)idForTouch:(UITouch*)touch create:(BOOL)create {
    void* key = (__bridge void*)touch;
    auto it = _fingerIds.find(key);
    if (it != _fingerIds.end()) return it->second;
    if (!create) return -1;
    int candidate = 0;
    for (bool taken = true; taken; ) {
        taken = false;
        for (const auto& entry : _fingerIds) {
            if (entry.second == candidate) { taken = true; ++candidate; break; }
        }
    }
    _fingerIds[key] = candidate;
    return candidate;
}

- (void)deliver:(NSSet<UITouch*>*)touches phase:(int)phase {
    auto* app = App();
    for (UITouch* touch in touches) {
        const int fingerId = [self idForTouch:touch create:(phase == 0)];
        if (fingerId < 0) continue;
        if (phase >= 2) _fingerIds.erase((__bridge void*)touch);
        const CGPoint p = [touch locationInView:self];
        float pressure = 1.0f;
        if (touch.maximumPossibleForce > 0) {
            pressure = static_cast<float>(touch.force / touch.maximumPossibleForce);
        }
        if (app) {
            app->HandleTouch(phase, fingerId, static_cast<float>(p.x), static_cast<float>(p.y),
                             pressure, static_cast<int>(_fingerIds.size()), touch.timestamp);
        }
    }
    if (app) app->WakeUpEventLoopFromView();
}

- (void)touchesBegan:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
    [self deliver:touches phase:0];
}

- (void)touchesMoved:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
    [self deliver:touches phase:1];
}

- (void)touchesEnded:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
    [self deliver:touches phase:2];
}

- (void)touchesCancelled:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
    [self deliver:touches phase:3];
}

- (void)layoutSubviews {
    [super layoutSubviews];
    const CGSize size = self.bounds.size;
    if (CGSizeEqualToSize(size, _lastSize)) return;
    _lastSize = size;
    if (auto* app = App()) {
        if (auto* win = app->GetPrimaryWindow()) {
            win->HandleViewResized(static_cast<int>(size.width), static_cast<int>(size.height));
        }
    }
}

@end

// ===== THE VIEW CONTROLLER =====

@interface UltraCanvasiOSViewController : UIViewController
@end

@implementation UltraCanvasiOSViewController

- (void)loadView {
    self.view = [[UltraCanvasiOSView alloc] initWithFrame:[UIScreen mainScreen].bounds];
}

// The whole screen is the window's: the status bar and the home indicator
// stay out of the way until the safe area reaches the core
// (iOSPortInvestigation.md 3.3).
- (BOOL)prefersStatusBarHidden {
    return YES;
}

- (BOOL)prefersHomeIndicatorAutoHidden {
    return YES;
}

@end

// ===== THE APPLICATION DELEGATE =====

// ===== THE SCENE DELEGATE: the window, and the app's foreground life =====
// iOS 13 moved the window and the foreground/background events from the
// application delegate to the scene, and from iOS 26 an app without the
// scene lifecycle is warned that it "will soon be required" (an assert later).
// UltraCanvas has one window, so one scene: UIApplicationSupportsMultipleScenes
// is false (OS/iOS/packaging/Info.plist.in), and the configuration that names
// this class comes from the application delegate below.

@interface UltraCanvasiOSAppDelegate : UIResponder <UIApplicationDelegate>
// Runs the application's main body (ultracanvas_app_main) and ends the
// process when it returns; scheduled once, by the first scene to connect.
- (void)runApplication;
@end

@interface UltraCanvasiOSSceneDelegate : UIResponder <UIWindowSceneDelegate>
@property (strong, nonatomic) UIWindow* window;
@end

@implementation UltraCanvasiOSSceneDelegate

- (void)scene:(UIScene*)scene willConnectToSession:(UISceneSession*)session
      options:(UISceneConnectionOptions*)connectionOptions {
    if (![scene isKindOfClass:[UIWindowScene class]]) return;
    self.window = [[UIWindow alloc] initWithWindowScene:(UIWindowScene*)scene];
    UltraCanvasiOSViewController* controller = [[UltraCanvasiOSViewController alloc] init];
    self.window.rootViewController = controller;
    [self.window makeKeyAndVisible];
    UltraCanvas::SetiOSRootView((__bridge void*)controller.view);

    // The application's main body, once: after this callback has returned
    // (see the head of the file). A scene that connects again later - after
    // the system discarded it - only gets a window; the body runs on.
    static bool started = false;
    if (!started) {
        started = true;
        auto* delegate = (UltraCanvasiOSAppDelegate*)[UIApplication sharedApplication].delegate;
        [delegate performSelector:@selector(runApplication) withObject:nil afterDelay:0];
    }
}

- (void)sceneDidEnterBackground:(UIScene*)scene {
    if (auto* app = App()) app->HandleDidEnterBackground();
}

- (void)sceneWillEnterForeground:(UIScene*)scene {
    if (auto* app = App()) app->HandleWillEnterForeground();
}

- (void)sceneDidBecomeActive:(UIScene*)scene {
    if (auto* app = App()) app->HandleDidBecomeActive();
}

- (void)sceneWillResignActive:(UIScene*)scene {
    if (auto* app = App()) app->HandleWillResignActive();
}

@end

// ===== THE APPLICATION DELEGATE =====

@implementation UltraCanvasiOSAppDelegate

- (BOOL)application:(UIApplication*)application
        didFinishLaunchingWithOptions:(NSDictionary*)launchOptions {
    ExportSandboxEnvironment();
    return YES;
}

// The one scene's configuration, in code: Info.plist only opts into scenes.
- (UISceneConfiguration*)application:(UIApplication*)application
        configurationForConnectingSceneSession:(UISceneSession*)session
                                       options:(UISceneConnectionOptions*)options {
    UISceneConfiguration* configuration =
            [[UISceneConfiguration alloc] initWithName:@"Default" sessionRole:session.role];
    configuration.delegateClass = [UltraCanvasiOSSceneDelegate class];
    return configuration;
}

- (void)runApplication {
    const int status = ultracanvas_app_main(g_argc, g_argv);
    debugOutput << "UltraCanvas iOS: the application's main returned " << status << std::endl;
    // An iOS app does not quit itself; when its main body is over there is
    // nothing left to show.
    std::exit(status);
}

@end

namespace UltraCanvas {

    void SetiOSKeyboardWanted(bool wanted) {
        UltraCanvasiOSView* view = (__bridge UltraCanvasiOSView*)GetiOSRootView();
        if (!view || view.wantsKeyboard == wanted) return;
        view.wantsKeyboard = wanted;
        if (wanted) {
            [view becomeFirstResponder];
        } else {
            // A composition still open when editing stops is kept as typed,
            // before the widget loses the caret it would go to.
            [view unmarkText];
            [view resignFirstResponder];
        }
    }

} // namespace UltraCanvas

int main(int argc, char* argv[]) {
    g_argc = argc;
    g_argv = argv;
    @autoreleasepool {
        return UIApplicationMain(argc, argv, nil, NSStringFromClass([UltraCanvasiOSAppDelegate class]));
    }
}
