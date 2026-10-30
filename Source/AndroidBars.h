#pragma once

// App-side autohide for the Android status bar (notifications) and system
// nav bar. JUCE's peer only raises its (deprecated) hide flags for the kiosk
// component, which Android 15 edge-to-edge ignores - so drive the bars
// directly: WindowInsetsController on API 30+, setSystemUiVisibility below.
// Re-assert on every refit tick while fullscreen: edge swipes, focus loss,
// volume panels and rotation all clear the hide request.
//
// Include AFTER JuceHeader.h (it provides juce::getEnv /
// juce::getCurrentActivity, used here exactly the way JUCE's own Android
// code uses them). Compiles to a no-op off Android, so the TU stays shared.

#if JUCE_ANDROID
#include <jni.h>

inline void hideSystemBars()
{
    JNIEnv* const env = juce::getEnv();
    if (env == nullptr || env->PushLocalFrame (32) != 0)
        return;

    // Every JNI call below is exception/null-checked: a single failed
    // lookup must abort the sequence, never run with a null handle.
    // All local refs belong to the pushed frame and are freed by the
    // PopLocalFrame at the end.
    do
    {
        const juce::LocalRef<jobject> activity (juce::getCurrentActivity());
        if (env->ExceptionCheck() != 0 || activity.get() == nullptr) { env->ExceptionClear(); break; }

        const jclass activityClass = env->GetObjectClass (activity.get());
        if (env->ExceptionCheck() != 0 || activityClass == nullptr) { env->ExceptionClear(); break; }

        const jclass versionClass = env->FindClass ("android/os/Build$VERSION");
        if (env->ExceptionCheck() != 0 || versionClass == nullptr) { env->ExceptionClear(); break; }
        const jfieldID sdkField = env->GetStaticFieldID (versionClass, "SDK_INT", "I");
        if (env->ExceptionCheck() != 0 || sdkField == nullptr) { env->ExceptionClear(); break; }
        const jint sdkInt = env->GetStaticIntField (versionClass, sdkField);
        if (env->ExceptionCheck() != 0) { env->ExceptionClear(); break; }

        const jmethodID getWindow = env->GetMethodID (activityClass, "getWindow",
                                                      "()Landroid/view/Window;");
        if (env->ExceptionCheck() != 0 || getWindow == nullptr) { env->ExceptionClear(); break; }
        const jobject window = env->CallObjectMethod (activity.get(), getWindow);
        if (env->ExceptionCheck() != 0 || window == nullptr) { env->ExceptionClear(); break; }
        const jclass windowClass = env->GetObjectClass (window);
        if (env->ExceptionCheck() != 0 || windowClass == nullptr) { env->ExceptionClear(); break; }

        if (sdkInt >= 30)
        {
            const jmethodID getInsetsController =
                env->GetMethodID (windowClass, "getInsetsController",
                                  "()Landroid/view/WindowInsetsController;");
            if (env->ExceptionCheck() != 0 || getInsetsController == nullptr) { env->ExceptionClear(); break; }
            const jobject controller = env->CallObjectMethod (window, getInsetsController);
            if (env->ExceptionCheck() != 0 || controller == nullptr) { env->ExceptionClear(); break; }
            const jclass controllerClass = env->GetObjectClass (controller);
            if (env->ExceptionCheck() != 0 || controllerClass == nullptr) { env->ExceptionClear(); break; }

            const jclass typeClass = env->FindClass ("android/view/WindowInsets$Type");
            if (env->ExceptionCheck() != 0 || typeClass == nullptr) { env->ExceptionClear(); break; }
            const jmethodID statusBars = env->GetStaticMethodID (typeClass, "statusBars", "()I");
            const jmethodID navigationBars = env->GetStaticMethodID (typeClass, "navigationBars", "()I");
            if (env->ExceptionCheck() != 0 || statusBars == nullptr || navigationBars == nullptr)
                { env->ExceptionClear(); break; }
            const jint mask = (jint) (env->CallStaticIntMethod (typeClass, statusBars)
                                    | env->CallStaticIntMethod (typeClass, navigationBars));
            if (env->ExceptionCheck() != 0) { env->ExceptionClear(); break; }

            const jmethodID setBehavior =
                env->GetMethodID (controllerClass, "setSystemBarsBehavior", "(I)V");
            if (env->ExceptionCheck() != 0 || setBehavior == nullptr) { env->ExceptionClear(); break; }
            env->CallVoidMethod (controller, setBehavior, (jint) 2); // BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
            if (env->ExceptionCheck() != 0) { env->ExceptionClear(); break; }

            const jmethodID hide = env->GetMethodID (controllerClass, "hide", "(I)V");
            if (env->ExceptionCheck() != 0 || hide == nullptr) { env->ExceptionClear(); break; }
            env->CallVoidMethod (controller, hide, mask);
            if (env->ExceptionCheck() != 0) env->ExceptionClear();
        }
        else
        {
            const jmethodID getDecorView =
                env->GetMethodID (windowClass, "getDecorView", "()Landroid/view/View;");
            if (env->ExceptionCheck() != 0 || getDecorView == nullptr) { env->ExceptionClear(); break; }
            const jobject decorView = env->CallObjectMethod (window, getDecorView);
            if (env->ExceptionCheck() != 0 || decorView == nullptr) { env->ExceptionClear(); break; }
            const jclass viewClass = env->GetObjectClass (decorView);
            if (env->ExceptionCheck() != 0 || viewClass == nullptr) { env->ExceptionClear(); break; }
            const jmethodID setUiVisibility =
                env->GetMethodID (viewClass, "setSystemUiVisibility", "(I)V");
            if (env->ExceptionCheck() != 0 || setUiVisibility == nullptr) { env->ExceptionClear(); break; }
            // IMMERSIVE_STICKY | FULLSCREEN | HIDE_NAVIGATION
            env->CallVoidMethod (decorView, setUiVisibility, (jint) 0x1006);
            if (env->ExceptionCheck() != 0) env->ExceptionClear();
        }
    } while (false);

    env->PopLocalFrame (nullptr);
}

#else
inline void hideSystemBars() {}
#endif
