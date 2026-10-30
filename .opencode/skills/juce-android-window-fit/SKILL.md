---
name: JUCE Android Window Fit
description: Fit a JUCE app's main window on Android - immersive fullscreen, rotation refit, NoTitleBar theme, status/nav bar autohide. Use for any JUCE guiapp targeting Android (targetSdk 35 edge-to-edge).
---

# JUCE Android window fitting

Reusable recipe for making a JUCE `DocumentWindow`-based app fill an Android
display correctly: no content behind the status bar, no ActionBar header,
status + navigation bars autohid, and automatic reflow on rotation/fold.
Desktop behaviour stays untouched.

Worked example in this repo: `Source/Main.cpp` (`MainWindow` ctor +
`WindowRefitter` + `AndroidBars`), `Builds/Android/app/src/main/AndroidManifest.xml`.

## Background facts (JUCE 8.0.6–8.0.8, verified in `JUCE/`)

- `targetSdkVersion 35` makes Android 15+ enforce edge-to-edge: the activity's
  content frame spans the whole display, so `Display::userArea` (which JUCE
  derives from the content view) includes the status-bar area.
- `Display::safeAreaInsets` carries **cutout insets only** (notch), not the
  status bar. Tablets often have no cutout at all. Do not use it to dodge
  the status bar.
- `configChanges` (orientation|screenSize|...) suppresses activity
  recreation. JUCE refreshes its `Displays` data from the activity layout
  listener on rotation, but **never re-applies top-level bounds** - and JUCE
  8.x has **no public display-change listener**, so the window keeps its
  startup size forever unless the app re-applies it.
- A manifest activity without `android:theme` gets the default theme's
  **ActionBar**: a dead icon slot plus the app label above the JUCE content.
- `ComponentPeer::setFullScreen (true)` does **not** hide the bars on its own:
  the Android peer only applies hide flags when the component is the kiosk-mode
  component (`shouldNavBarsBeHidden`, `juce_Windowing_android.cpp`), and even
  then uses deprecated `setSystemUiVisibility` flags that Android 15
  edge-to-edge ignores. Bars must be driven app-side (step 4).

## Recipe

### 1. Manifest: kill the ActionBar

On the `<activity>` element (JUCE's documented value):

```xml
android:theme="@android:style/Theme.NoTitleBar"
```

### 2. Window ctor: desktop path unchanged, Android path fullscreen

```cpp
MainWindow (juce::String name)
    : DocumentWindow (name, juce::Colours::black, DocumentWindow::allButtons)
{
    setUsingNativeTitleBar (true);
    setContentOwned (new MainComponent(), true);
    // ... resize limits etc ...
#if JUCE_ANDROID
    if (auto* d = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
        setSize (d->userArea.getWidth(), d->userArea.getHeight());
    else
        setSize (1024, 768);
#else
    // ... existing centreWithSize(...) desktop logic, unchanged ...
#endif
    setVisible (true);
#if JUCE_ANDROID
    if (getPeer() != nullptr)
        getPeer()->setFullScreen (true);
    // setFullScreen alone does NOT hide the bars (see background facts):
    // drive them explicitly, then keep re-asserting from the refitter.
    hideSystemBars();
    refitter = std::make_unique<WindowRefitter> (*this);
#endif
}
```

### 3. Refit watcher: follow the display across rotations

Private nested helper + member on the window (Android only):

```cpp
#if JUCE_ANDROID
class WindowRefitter final : private juce::Timer
{
public:
    explicit WindowRefitter (juce::DocumentWindow& w) : window (w) { startTimerHz (5); }

private:
    void timerCallback() override
    {
        auto* d = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
        if (d == nullptr) return;

        // Mirrors the peer's own fullscreen rule: totalArea while the bars
        // are hidden, userArea otherwise.
        const auto target = (window.getPeer() != nullptr && window.getPeer()->isFullScreen())
                                ? d->totalArea
                                : d->userArea;

        if (! target.isEmpty() && window.getBounds() != target)
            window.setBounds (target);

        // Re-assert immersive bars (step 4): swipe reveals, focus loss,
        // volume panels and rotation all clear the hide request otherwise.
        if (target == d->totalArea)
            hideSystemBars();
    }

    juce::DocumentWindow& window;
};

std::unique_ptr<WindowRefitter> refitter;
#endif
```

Notes:

- Re-query `getPrimaryDisplay()` every tick; never cache the pointer across
  `Displays::refresh()`.
- `setBounds` on the window drives `resized()` on the content component, so
  an existing responsive layout reflows with no extra work.
- Poll at ~5 Hz. It only fires `setBounds` when the target actually changed,
  so there is no layout churn while idle.

### 4. Autohide both bars app-side (API 30+ controller, legacy fallback)

New header (e.g. `AndroidBars.h`, included by the window's TU). Desktop
compiles it to a no-op so the TU stays shared:

```cpp
#if JUCE_ANDROID
#include <jni.h>
#include <juce_core/native/juce_JNIHelpers_android.h>  // juce::getEnv, juce::getCurrentActivity

inline void hideSystemBars()
{
    JNIEnv* env = juce::getEnv();
    if (env == nullptr || env->PushLocalFrame (16) != 0)
        return;

    auto clearFail = [&] { if (env->ExceptionCheck()) { env->ExceptionClear(); return true; } return false; };
    auto activity = juce::getCurrentActivity();
    // ... null/exception-check every step (FindClass/GetMethodID/Call*) ...
}
#endif
```

- Prerequisite: app TUs must see `juce::getEnv`/`getCurrentActivity`, which
  `juce_core.h` only declares when `JUCE_CORE_INCLUDE_JNI_HELPERS=1`
  (`juce_core.cpp` sets it for its own TU only). Without it the Android
  build fails with `no member named 'getEnv' in namespace 'juce'`. Set it in
  the `.jucer` project `defines` (propagates to the Android CMake build on
  resave; harmless on desktop where it compiles out).
- Read `android.os.Build$VERSION.SDK_INT` (static int field, no native dep).
- API 30+: `Activity.getWindow()` → `Window.getInsetsController()` →
  `setSystemBarsBehavior(2)` (`BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE`) then
  `hide(statusBars() | navigationBars())` from `android/view/WindowInsets$Type`
  (inner class: `FindClass("android/view/WindowInsets$Type")`, static `()I`
  methods). Signatures: `getWindow "()Landroid/view/Window;"`,
  `getInsetsController "()Landroid/view/WindowInsetsController;"`,
  `hide "(I)V"`, `setSystemBarsBehavior "(I)V"`.
- Below 30: `Window.getDecorView()` → `View.setSystemUiVisibility("(I)V")`
  with `IMMERSIVE_STICKY|FULLSCREEN|HIDE_NAVIGATION` (`0x1000|0x4|0x2`).
- JNI hygiene (the message thread stays attached forever, so locals never
  auto-free): `PushLocalFrame` up front / `PopLocalFrame` at the end, and
  clear any pending exception after every call. `getEnv()` and
  `getCurrentActivity()` live directly in `namespace juce` (no `android`
  nesting) in JUCE 8.x.

Call it once after `setFullScreen` in the ctor and on every refit tick while
the fullscreen target is active (a few JNI calls at 5 Hz is negligible).

### 5. Verify

- Windows/macOS build: the `#if JUCE_ANDROID` blocks compile out; the
  desktop path must be byte-identical in behaviour. Run the normal test
  suite (`--selftest`, expect ALL CHECKS PASSED).
- The Android block itself cannot compile on desktop (no NDK here): it uses
  only public API plus the JNI pattern above, so check every class/method
  name and signature against the bundled JUCE headers (`juce_JNIHelpers_android.h`,
  `juce_ComponentPeer.h`, `juce_Displays.h`) before handing over the APK build.
  A clean `assembleDebug` is the real compile check for the Android block.
- On device: bars hidden at launch, top UI never under the notification
  area, rotation reflows within ~200 ms with no gaps in either orientation.
  Edge swipe reveals the bars transiently; they must drop away again within
  ~200 ms (next refit tick). If a bar sticks, get model + Android version +
  which bar (status, nav, or both) before adjusting.

## Gotchas learned the hard way

- Timers never fire inside `--selftest` (it runs synchronously in
  `initialise()`), so selftest cannot catch timer-driven bugs - review the
  refit logic by reading, and validate on device.
- Keep UI string literals ASCII-only: `juce::String(const char*)` asserts on
  non-ASCII bytes in Debug builds.
- Don't touch the app's Gradle signing/build edits when applying this; the
  recipe is source + manifest-theme only.
- The Projucer-generated `AndroidManifest.xml` is overwritten on every
  Android resave: re-apply the one-line `NoTitleBar` theme edit afterwards.
