// The iOS app's own part of the platform layer: the game's data in the app's
// Documents folder, asked for with the system's document picker on first
// start, and the player's own files beside it.
//
// The data is the folder ios/package.py makes from the player's CD: the
// engine's assets, laid out as the source tree is, with manifest.txt (every
// file's CRC-32, size and path) and version.txt. It is copied in file by file,
// each checked against the manifest as it is written, and only then marked
// installed; the game is played from the folder's build/, so every
// "../assets/..." finds its file, as from build/ on a desktop.

#include "hal/PlatformiOS.h"

#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>

#include <zlib.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

// The version of the data this build plays from, given by ios/CMakeLists.txt
// from tools/data/prepare_data.py.
#ifndef OPENAOK_DATA_VERSION
#define OPENAOK_DATA_VERSION 0
#endif

namespace {

// The folder ios/package.py makes, looked for inside the chosen one if that is
// not it.
NSString* const kFolderName = @"OpenAoK-data";
NSString* const kManifest = @"manifest.txt";
NSString* const kVersion = @"version.txt";
NSString* const kMarker = @".installed";

// Set when the data has been copied in and the game may start.
std::atomic<bool> g_installed{false};

NSString* Documents() {
    return [NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES) firstObject];
}

NSString* GameFolder() {
    return [Documents() stringByAppendingPathComponent:kFolderName];
}

NSString* RunFolder() {
    return [GameFolder() stringByAppendingPathComponent:@"build"];
}

NSString* Marker() {
    return [GameFolder() stringByAppendingPathComponent:kMarker];
}

// The version of the data copied in, or 0 when there is none.
int InstalledVersion() {
    NSString* text = [NSString stringWithContentsOfFile:Marker()
                                               encoding:NSUTF8StringEncoding
                                                  error:nil];
    return text == nil ? 0 : text.intValue;
}

// Leaves the marker that says the data in Documents is whole, and which
// version of it it is.
bool MarkInstalled() {
    FILE* marker = std::fopen(Marker().fileSystemRepresentation, "w");
    if (marker == nullptr) return false;
    std::fprintf(marker, "%d\n", OPENAOK_DATA_VERSION);
    return std::fclose(marker) == 0;
}

// Whether this build's data is in place: its version, and the rules the game
// cannot start without. The marker is what the copy in Install leaves; a
// folder put in Documents by the Files app, or copied there by hand, has the
// data but not the marker, and is taken as installed -- and marked so, to
// tell a whole folder from one a copy left half written.
bool DataReady() {
    NSString* rules = [GameFolder() stringByAppendingPathComponent:@"assets/extracted_data/rules.json"];
    if (![[NSFileManager defaultManager] fileExistsAtPath:rules]) return false;
    if (InstalledVersion() == OPENAOK_DATA_VERSION) return true;
    NSString* version = [NSString stringWithContentsOfFile:
                         [GameFolder() stringByAppendingPathComponent:kVersion]
                                               encoding:NSUTF8StringEncoding
                                                  error:nil];
    if (version == nil || version.intValue != OPENAOK_DATA_VERSION) return false;
    MarkInstalled();
    return true;
}

// One line of manifest.txt: a file's CRC-32, its size and its path.
struct Entry {
    unsigned long crc = 0;
    long long size = 0;
    std::string path;
};

bool ReadManifest(NSString* manifest, std::vector<Entry>& entries, long long& total, NSString** why) {
    NSString* text = [NSString stringWithContentsOfFile:manifest
                                               encoding:NSUTF8StringEncoding
                                                  error:nil];
    if (text == nil) {
        *why = @"The folder's manifest.txt cannot be read: copy the folder to the device again.";
        return false;
    }
    std::istringstream lines(text.UTF8String);
    std::string line;
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        unsigned long crc = 0;
        long long size = 0;
        int used = 0;
        if (std::sscanf(line.c_str(), "%lx %lld %n", &crc, &size, &used) != 2 || used <= 0) continue;
        Entry entry;
        entry.crc = crc;
        entry.size = size;
        entry.path = line.substr(static_cast<size_t>(used));
        if (entry.path.empty()) continue;
        entries.push_back(entry);
        total += size;
    }
    if (entries.empty()) {
        *why = @"The folder's manifest.txt lists no files: copy the folder to the device again.";
        return false;
    }
    return true;
}

// One file, copied through a buffer with its CRC-32 and size taken as it is
// written. `copied` is given each block's length as it is read.
bool CopyOne(NSURL* from, NSString* to, unsigned long& crc, long long& size,
             const std::function<void(long long)>& copied, NSString** why) {
    NSInputStream* in = [NSInputStream inputStreamWithURL:from];
    if (in == nil) {
        *why = [NSString stringWithFormat:@"Cannot open %@.", from.lastPathComponent];
        return false;
    }
    [in open];
    if (in.streamStatus == NSStreamStatusError) {
        NSString* described = in.streamError.localizedDescription;
        NSString* reason = described != nil ? described : @"the file is unavailable";
        *why = [NSString stringWithFormat:@"Cannot read %@: %@", from.lastPathComponent, reason];
        [in close];
        return false;
    }
    FILE* out = std::fopen(to.fileSystemRepresentation, "wb");
    if (out == nullptr) {
        *why = [NSString stringWithFormat:@"Cannot write %@: the device may be out of space.",
                                          to.lastPathComponent];
        [in close];
        return false;
    }
    uint8_t buffer[1 << 16];
    crc = crc32(0L, Z_NULL, 0);
    size = 0;
    bool ok = true;
    while (true) {
        const NSInteger got = [in read:buffer maxLength:sizeof buffer];
        if (got < 0) {
            *why = [NSString stringWithFormat:@"Cannot read %@.", from.lastPathComponent];
            ok = false;
            break;
        }
        if (got == 0) break;
        if (std::fwrite(buffer, 1, static_cast<size_t>(got), out) != static_cast<size_t>(got)) {
            *why = [NSString stringWithFormat:@"Cannot write %@: the device may be out of space.",
                                              to.lastPathComponent];
            ok = false;
            break;
        }
        crc = crc32(crc, buffer, static_cast<uInt>(got));
        size += got;
        if (copied) copied(got);
    }
    [in close];
    if (std::fclose(out) != 0 && ok) {
        *why = [NSString stringWithFormat:@"Cannot write %@: the device may be out of space.",
                                          to.lastPathComponent];
        ok = false;
    }
    return ok;
}

// Copies the folder the picker gave into Documents/OpenAoK-data, every file
// checked against the manifest, and marks it installed. The old copy is left
// in place until the new one is written over it; its marker goes first, so an
// interrupted copy is never taken for a whole one.
bool Install(NSURL* picked, const std::function<void(long long, long long)>& progress, NSString** why) {
    NSFileManager* files = [NSFileManager defaultManager];
    NSURL* root = picked;
    if (![files fileExistsAtPath:[root URLByAppendingPathComponent:kManifest].path]) {
        NSURL* nested = [root URLByAppendingPathComponent:kFolderName];
        if ([files fileExistsAtPath:[nested URLByAppendingPathComponent:kManifest].path]) root = nested;
    }
    NSString* manifest = [root URLByAppendingPathComponent:kManifest].path;
    NSString* version = [root URLByAppendingPathComponent:kVersion].path;
    if (![files fileExistsAtPath:manifest] || ![files fileExistsAtPath:version]) {
        *why = [NSString stringWithFormat:
                @"That folder is not the game's data: choose the %@ folder made by ios/package.py.",
                kFolderName];
        return false;
    }
    NSString* version_text = [NSString stringWithContentsOfFile:version
                                                       encoding:NSUTF8StringEncoding
                                                          error:nil];
    const int data_version = version_text == nil ? 0 : version_text.intValue;
    if (data_version != OPENAOK_DATA_VERSION) {
        *why = [NSString stringWithFormat:
                @"That folder holds game data of version %d; this OpenAoK plays from version %d. "
                @"Make it again with this release's source code.",
                data_version, OPENAOK_DATA_VERSION];
        return false;
    }
    // Chosen from the app's own folder in the Files app: the data is already
    // in place, and copying it over itself would truncate every file. Only
    // mark it.
    if ([root.path.stringByStandardizingPath isEqualToString:GameFolder().stringByStandardizingPath]) {
        if (!MarkInstalled()) {
            *why = @"Cannot write to the app's Documents folder.";
            return false;
        }
        return true;
    }
    std::vector<Entry> entries;
    long long total = 0;
    if (!ReadManifest(manifest, entries, total, why)) return false;

    [files createDirectoryAtPath:GameFolder() withIntermediateDirectories:YES attributes:nil error:nil];
    [files removeItemAtPath:Marker() error:nil];
    [files createDirectoryAtPath:RunFolder() withIntermediateDirectories:YES attributes:nil error:nil];

    long long done = 0;
    for (const Entry& entry : entries) {
        NSString* relative = [NSString stringWithUTF8String:entry.path.c_str()];
        if (relative == nil) continue;
        NSString* from = [root.path stringByAppendingPathComponent:relative];
        NSString* to = [GameFolder() stringByAppendingPathComponent:relative];
        if (![files fileExistsAtPath:from]) {
            *why = [NSString stringWithFormat:@"The folder is missing %@: copy it to the device again.",
                                              relative];
            return false;
        }
        [files createDirectoryAtPath:[to stringByDeletingLastPathComponent]
           withIntermediateDirectories:YES
                            attributes:nil
                                 error:nil];
        unsigned long crc = 0;
        long long size = 0;
        if (!CopyOne([NSURL fileURLWithPath:from], to, crc, size, [&](long long got) {
                done += got;
                if (progress) progress(done, total);
            }, why)) {
            return false;
        }
        if (crc != entry.crc || size != entry.size) {
            *why = [NSString stringWithFormat:
                    @"%@ in the folder is damaged: copy the folder to the device again.", relative];
            return false;
        }
    }
    if (!MarkInstalled()) {
        *why = @"Cannot write to the app's Documents folder.";
        return false;
    }
    return true;
}

} // namespace

// The first start's screen: what the game needs and where it comes from, and
// the system's document picker to choose the folder with, with the copy's
// progress beneath.
@interface OpenAoKSetupViewController : UIViewController <UIDocumentPickerDelegate>
@end

@implementation OpenAoKSetupViewController {
    UILabel* _body;
    UILabel* _status;
    UIProgressView* _bar;
    UIButton* _choose;
    BOOL _presented;
}

- (void)loadView {
    UIView* view = [[UIView alloc] initWithFrame:[UIScreen mainScreen].bounds];
    view.backgroundColor = [UIColor blackColor];
    self.view = view;

    UILabel* title = [[UILabel alloc] init];
    title.text = @"OpenAoK";
    title.font = [UIFont boldSystemFontOfSize:40.0];
    title.textColor = [UIColor colorWithRed:0.90 green:0.78 blue:0.47 alpha:1.0];
    title.textAlignment = NSTextAlignmentCenter;

    _body = [[UILabel alloc] init];
    _body.text =
        @"OpenAoK needs the game's data.\n\n"
        @"It is made on a computer from your own Age of Empires II CD: copy the CD into "
        @"original_game_1999/ in OpenAoK's source code, run python3 ios/package.py, then copy "
        @"the OpenAoK-data folder it makes to this device and choose it here.\n\n"
        @"The folder can also be put in OpenAoK's folder in the Files app.";
    _body.numberOfLines = 0;
    _body.font = [UIFont systemFontOfSize:15.0];
    _body.textColor = [UIColor lightGrayColor];
    _body.textAlignment = NSTextAlignmentCenter;

    _bar = [[UIProgressView alloc] initWithProgressViewStyle:UIProgressViewStyleDefault];
    _bar.hidden = YES;

    _choose = [UIButton buttonWithType:UIButtonTypeSystem];
    [_choose setTitle:@"Choose the OpenAoK-data folder" forState:UIControlStateNormal];
    [_choose setTitleColor:[UIColor blackColor] forState:UIControlStateNormal];
    _choose.titleLabel.font = [UIFont boldSystemFontOfSize:19.0];
    _choose.backgroundColor = [UIColor colorWithRed:0.90 green:0.78 blue:0.47 alpha:1.0];
    _choose.layer.cornerRadius = 12.0;
    [_choose addTarget:self
                action:@selector(chooseFolder)
      forControlEvents:UIControlEventTouchUpInside];

    _status = [[UILabel alloc] init];
    _status.numberOfLines = 0;
    _status.font = [UIFont systemFontOfSize:14.0];
    _status.textColor = [UIColor lightGrayColor];
    _status.textAlignment = NSTextAlignmentCenter;

    NSArray* views = @[ title, _body, _bar, _choose, _status ];
    for (UIView* v in views) {
        v.translatesAutoresizingMaskIntoConstraints = NO;
        [view addSubview:v];
    }

    UILayoutGuide* safe = view.safeAreaLayoutGuide;
    [NSLayoutConstraint activateConstraints:@[
        [title.topAnchor constraintEqualToAnchor:safe.topAnchor constant:28.0],
        [title.centerXAnchor constraintEqualToAnchor:view.centerXAnchor],
        [_body.topAnchor constraintEqualToAnchor:title.bottomAnchor constant:20.0],
        [_body.leadingAnchor constraintEqualToAnchor:safe.leadingAnchor constant:40.0],
        [_body.trailingAnchor constraintEqualToAnchor:safe.trailingAnchor constant:-40.0],
        [_choose.topAnchor constraintGreaterThanOrEqualToAnchor:_body.bottomAnchor constant:20.0],
        [_choose.centerXAnchor constraintEqualToAnchor:view.centerXAnchor],
        [_choose.widthAnchor constraintGreaterThanOrEqualToConstant:320.0],
        [_choose.heightAnchor constraintEqualToConstant:56.0],
        [_bar.topAnchor constraintEqualToAnchor:_choose.bottomAnchor constant:20.0],
        [_bar.leadingAnchor constraintEqualToAnchor:safe.leadingAnchor constant:60.0],
        [_bar.trailingAnchor constraintEqualToAnchor:safe.trailingAnchor constant:-60.0],
        [_status.topAnchor constraintEqualToAnchor:_bar.bottomAnchor constant:14.0],
        [_status.leadingAnchor constraintEqualToAnchor:safe.leadingAnchor constant:40.0],
        [_status.trailingAnchor constraintEqualToAnchor:safe.trailingAnchor constant:-40.0],
        [_status.bottomAnchor constraintEqualToAnchor:safe.bottomAnchor constant:-20.0],
    ]];
}

- (void)viewDidLoad {
    [super viewDidLoad];
    // The data may have been put in Documents by the Files app while the game
    // was away.
    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(becameActive)
                                                 name:UIApplicationDidBecomeActiveNotification
                                               object:nil];
}

- (void)dealloc {
    [[NSNotificationCenter defaultCenter] removeObserver:self];
}

- (BOOL)prefersStatusBarHidden {
    return YES;
}

- (void)viewDidAppear:(BOOL)animated {
    [super viewDidAppear:animated];
    // Straight to the picker the first time; a cancel leaves this screen and
    // its button.
    if (!_presented) {
        _presented = YES;
        [self chooseFolder];
    }
}

- (void)becameActive {
    if (DataReady()) g_installed = true;
}

- (void)chooseFolder {
    if (self.presentedViewController != nil) return;
    UIDocumentPickerViewController* picker =
        [[UIDocumentPickerViewController alloc] initForOpeningContentTypes:@[ UTTypeFolder ]
                                                                  asCopy:NO];
    picker.delegate = self;
    picker.allowsMultipleSelection = NO;
    [self presentViewController:picker animated:YES completion:nil];
}

- (void)documentPicker:(UIDocumentPickerViewController*)controller
    didPickDocumentsAtURLs:(NSArray<NSURL*>*)urls {
    NSURL* folder = urls.firstObject;
    if (folder == nil) return;

    _choose.hidden = YES;
    _bar.hidden = NO;
    _bar.progress = 0.0f;
    _status.textColor = [UIColor lightGrayColor];
    _status.text = @"Reading the folder...";

    // A folder from another app's storage is read only while its permission
    // is held; the copy runs off the main thread, with the screen's progress
    // updated from it.
    const BOOL scoped = [folder startAccessingSecurityScopedResource];
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        int shown = -1;
        NSString* why = nil;
        const bool ok = Install(folder, [&](long long done, long long total) {
            const int permille = total > 0 ? static_cast<int>(done * 1000 / total) : 1000;
            if (permille == shown) return;
            shown = permille;
            dispatch_async(dispatch_get_main_queue(), ^{
                self->_bar.progress = total > 0 ? static_cast<float>(done) / static_cast<float>(total)
                                                : 1.0f;
                self->_status.text = [NSString stringWithFormat:
                                      @"Copying the game data...  %lld / %lld MB",
                                      done >> 20, total >> 20];
            });
        }, &why);
        if (scoped) [folder stopAccessingSecurityScopedResource];
        dispatch_async(dispatch_get_main_queue(), ^{
            if (ok) {
                g_installed = true;
            } else {
                [self failed:why];
            }
        });
    });
}

- (void)documentPickerWasCancelled:(UIDocumentPickerViewController*)controller {
    if (DataReady()) {
        g_installed = true;
        return;
    }
    _status.textColor = [UIColor lightGrayColor];
    _status.text = @"Choose the OpenAoK-data folder made by ios/package.py.";
}

- (void)failed:(NSString*)why {
    _bar.hidden = YES;
    _choose.hidden = NO;
    _status.textColor = [UIColor colorWithRed:1.0 green:0.55 blue:0.45 alpha:1.0];
    _status.text = why != nil ? why : @"The folder could not be copied.";
}

@end

// iOS 27, the SDK this app is built with, only launches apps that use the
// scene life cycle. SDL 2 still uses the application life cycle and makes its
// window with initWithFrame:, so the app adopts scenes itself: Info.plist
// declares one scene and names this delegate, and the setup screen's window
// and SDL's game window are made in the scene that delegate is given. Without
// a scene they would not be shown. One scene only, as the game always was.
@interface OpenAoKSceneDelegate : UIResponder <UIWindowSceneDelegate>
@end

namespace {

UIWindowScene* g_scene = nil;
UIWindow* g_setup_window = nil;
OpenAoKSetupViewController* g_setup_controller = nil;
// SDL's window, kept weak: SDL owns it and destroys it with its SDL_Window.
__weak UIWindow* g_game_window = nil;

// The scene the app's windows belong to: the one this delegate was given, or
// while it is being connected, the one UIKit already has.
UIWindowScene* ActiveScene() {
    if (g_scene != nil && g_scene.activationState != UISceneActivationStateUnattached) {
        return g_scene;
    }
    UIWindowScene* inactive = nil;
    for (UIScene* scene in [UIApplication sharedApplication].connectedScenes) {
        if (![scene isKindOfClass:[UIWindowScene class]]) continue;
        UIWindowScene* window_scene = (UIWindowScene*)scene;
        if (window_scene.activationState == UISceneActivationStateForegroundActive) return window_scene;
        if (window_scene.activationState == UISceneActivationStateForegroundInactive) inactive = window_scene;
    }
    return inactive;
}

void PresentSetupScreen() {
    if (g_setup_window == nil) {
        g_setup_controller = [[OpenAoKSetupViewController alloc] init];
        UIWindowScene* scene = ActiveScene();
        if (scene != nil) {
            g_setup_window = [[UIWindow alloc] initWithWindowScene:scene];
            g_setup_window.frame = scene.coordinateSpace.bounds;
        } else {
            // The scene delegate will attach this window when its scene is
            // active, if the scene is not ready quite yet.
            g_setup_window = [[UIWindow alloc] initWithFrame:[UIScreen mainScreen].bounds];
        }
        g_setup_window.backgroundColor = [UIColor blackColor];
        g_setup_window.rootViewController = g_setup_controller;
    }
    [g_setup_window makeKeyAndVisible];
}

void HideSetupScreen() {
    g_setup_window.hidden = YES;
    g_setup_window = nil;
    g_setup_controller = nil;
}

} // namespace

@implementation OpenAoKSceneDelegate

- (void)scene:(UIScene*)scene
    willConnectToSession:(UISceneSession*)session
                 options:(UISceneConnectionOptions*)connectionOptions {
    if (![scene isKindOfClass:[UIWindowScene class]]) return;
    g_scene = (UIWindowScene*)scene;
}

- (void)sceneDidBecomeActive:(UIScene*)scene {
    if (![scene isKindOfClass:[UIWindowScene class]]) return;
    g_scene = (UIWindowScene*)scene;
    // A window made before the scene was ready belongs to it now.
    if (g_setup_window != nil && g_setup_window.windowScene == nil) {
        g_setup_window.windowScene = g_scene;
        [g_setup_window makeKeyAndVisible];
    }
    UIWindow* game = g_game_window;
    if (game != nil && game.windowScene == nil) {
        game.windowScene = g_scene;
        [game makeKeyAndVisible];
    }
}

- (void)sceneDidDisconnect:(UIScene*)scene {
    if (g_scene == scene) g_scene = nil;
}

@end

namespace HAL::iOS {

void AttachWindowToScene(SDL_Window* window) {
    @autoreleasepool {
        SDL_SysWMinfo info;
        SDL_VERSION(&info.version);
        if (!SDL_GetWindowWMInfo(window, &info)) return;
        UIWindow* ui_window = info.info.uikit.window;
        if (ui_window == nil) return;
        g_game_window = ui_window;
        UIWindowScene* scene = ActiveScene();
        if (scene != nil && ui_window.windowScene != scene) {
            ui_window.windowScene = scene;
            [ui_window makeKeyAndVisible];
        }
    }
}

bool Start(int, char*[]) {
    @autoreleasepool {
        // The class is named only by Info.plist's scene manifest; naming it
        // here as well keeps it in the app's image.
        (void)[OpenAoKSceneDelegate class];
        if (!DataReady()) {
            PresentSetupScreen();
            // SDL's main runs on the main thread: the run loop is spun here so
            // the picker, the copy's progress and the button all work.
            while (!g_installed.load()) {
                @autoreleasepool {
                    [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode
                                             beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.1]];
                }
            }
            HideSetupScreen();
        }
        NSFileManager* files = [NSFileManager defaultManager];
        [files createDirectoryAtPath:RunFolder() withIntermediateDirectories:YES attributes:nil error:nil];
        if (![files changeCurrentDirectoryPath:RunFolder()]) {
            NSLog(@"[Platform] cannot enter %@", RunFolder());
            return false;
        }
    }
    return true;
}

std::string PrefPath(const char* kind) {
    @autoreleasepool {
        NSString* dir = [Documents() stringByAppendingPathComponent:[NSString stringWithUTF8String:kind]];
        [[NSFileManager defaultManager] createDirectoryAtPath:dir
                                  withIntermediateDirectories:YES
                                                   attributes:nil
                                                        error:nil];
        return std::string(dir.fileSystemRepresentation) + "/";
    }
}

} // namespace HAL::iOS
