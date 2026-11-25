#include <JuceHeader.h>
#include "MainComponent.h"
#include "AndroidBars.h"
#include "SelfTest.h"

class UpicApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "UPICuitous"; }
    const juce::String getApplicationVersion() override { return "0.36.0"; } // keep in sync with Plugin/CMakeLists.txt
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise(const juce::String& commandLine) override
    {
        if (commandLine.contains("--selftest"))
        {
            // The headless checks live in SelfTest.cpp - they are much too
            // big for this file.
            if (upic::runSelfTest() > 0)
                std::exit(1);
            quit();
            return;
        }
        // F5: the app remembers the toolbar's fold levels and the view
        // toggles. The plugin leaves the file alone (a host session owns its
        // own window), and the selftest never enables it.
        MainComponent::setSessionSettingsPersistent (true);
        mainWindow.reset(new MainWindow(getApplicationName()));

        // v1.33.4 (L15, part 3): the crash recovery. The snapshot of the
        // last session is offered when the app did not close cleanly - the
        // offer waits for the window to be up (the message manager runs it
        // after the constructor).
        if (mainWindow != nullptr)
        {
            juce::Component::SafePointer<MainComponent> safeMc (
                dynamic_cast<MainComponent*> (mainWindow->getContentComponent()));
            juce::MessageManager::callAsync ([safeMc]
            {
                if (safeMc != nullptr)
                    safeMc->offerRecovery();
            });
        }
    }
    void shutdown() override { mainWindow = nullptr; }
    void systemRequestedQuit() override
    {
        // v1.33.4 (L15): the piece may have changes that are not saved. The
        // app asks before it closes - an accidental close must not throw the
        // work away (the review: "application close directly calls quit()").
        if (mainWindow != nullptr && ! mainWindow->askBeforeClosing())
            return;   // the window closes the app itself when the answer is known
        quit();
    }
    void anotherInstanceStarted(const juce::String&) override {}


    class MainWindow : public juce::DocumentWindow
    {
    public:
        MainWindow(juce::String name)
            : DocumentWindow(name, upic::uiBackground(),
                             DocumentWindow::allButtons)
        {
            // JUCE 8 works in logical pixels (dp) everywhere: the window
            // peers multiply component bounds/fonts by the display scale
            // themselves, so all layout sizes below are density-independent
            // and controls keep a constant physical size on tablets/HiDPI.
            auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();

            setUsingNativeTitleBar(true);
            setContentOwned(new MainComponent(), true);
            setResizable(true, true);
            setResizeLimits(460, 360, 100000, 100000); // dp — layout reflows below this

#if JUCE_ANDROID
            // Android (targetSdk 35 = enforced edge-to-edge): the content
            // frame spans the whole display, so start from the full display
            // area in logical px. The immersive switch + refit timer below
            // take it from there.
            if (display != nullptr)
                setSize (juce::roundToInt (display->userBounds.getWidth()),
                         juce::roundToInt (display->userBounds.getHeight()));
            else
                setSize(1024, 768);
#else
            // Default window: the 1280x800 dp design, clamped to the screen
            // (userBounds is in logical px too).
            int w = 1280, h = 800;
            if (display != nullptr)
            {
                const int availW = juce::roundToInt (display->userBounds.getWidth())  - 16;
                const int availH = juce::roundToInt (display->userBounds.getHeight()) - 48;
                w = juce::jmin(w, juce::jmax(460, availW));
                h = juce::jmin(h, juce::jmax(360, availH));
            }
            centreWithSize(w, h);
#endif
            setVisible(true);
#if JUCE_ANDROID
            if (getPeer() != nullptr)
                getPeer()->setFullScreen(true);
            // setFullScreen alone does NOT hide the bars (the peer only
            // raises its hide flags for the kiosk component, and those are
            // ignored under Android 15 edge-to-edge): drive them explicitly,
            // then keep re-asserting from the refitter.
            hideSystemBars();

            // Rotation/fold: JUCE refreshes its Displays data from the
            // activity's layout listener, but never re-applies top-level
            // bounds, so follow the display ourselves. The target mirrors
            // the peer's own fullscreen rule: logicalBounds while the bars
            // are hidden, userBounds otherwise.
            refitter = std::make_unique<WindowRefitter>(*this);
#endif
        }
        void closeButtonPressed() override
        {
            JUCEApplication::getInstance()->systemRequestedQuit();
        }

        // v1.33.4 (L15): the unsaved-changes prompt.
        //
        // It returns true when the app may close right away, and false when
        // the answer is pending: the save is a job (M28), so it cannot be
        // waited for - the window closes the app itself when the piece
        // really was written.
        bool askBeforeClosing()
        {
            auto* comp = getContentComponent();
            auto* mc = dynamic_cast<MainComponent*> (comp);
            if (mc == nullptr || ! mc->pieceHasUnsavedChanges())
                return true;

            juce::Component::SafePointer<MainComponent> safeMc (mc);
            auto options = juce::MessageBoxOptions()
                .withIconType (juce::MessageBoxIconType::WarningIcon)
                .withTitle ("Unsaved changes")
                .withMessage ("The piece has changes that are not saved. "
                              "Save them before closing?")
                .withButton ("Save")
                .withButton ("Discard")
                .withButton ("Cancel")
                .withAssociatedComponent (this);

            juce::AlertWindow::showAsync (options, [safeMc] (int buttonId)
            {
                if (safeMc == nullptr) return;
                if (buttonId == 1)   // Save
                {
                    safeMc->savePieceWithDialog ([safeMc] (bool saved)
                    {
                        // the app closes only when the piece was written and
                        // nothing changed since - a cancelled chooser, or an
                        // edit made while the save was running, leaves the
                        // window open (the work is not thrown away)
                        if (saved && safeMc != nullptr && ! safeMc->pieceHasUnsavedChanges())
                            JUCEApplication::getInstance()->quit();
                    });
                }
                else if (buttonId == 2)   // Discard
                {
                    // v1.33.4 (L15, part 3): the work is thrown away on
                    // purpose, so the recovery snapshot must not offer it
                    // again at the next start
                    MainComponent::deleteRecoverySnapshot (MainComponent::recoveryDirectory());
                    JUCEApplication::getInstance()->quit();
                }
                // 0 = Cancel: the app stays open
            });
            return false;
        }
    private:
#if JUCE_ANDROID
        // Keeps the window glued to the display across rotations, folds and
        // other display changes (5 Hz - JUCE refreshes the Displays data for
        // us, but never re-applies top-level bounds itself).
        class WindowRefitter final : private juce::Timer
        {
        public:
            explicit WindowRefitter (juce::DocumentWindow& w) : window (w) { startTimerHz (5); }

        private:
            void timerCallback() override
            {
                auto* d = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
                if (d == nullptr) return;

                const auto target = (window.getPeer() != nullptr && window.getPeer()->isFullScreen())
                                        ? d->logicalBounds.toNearestInt()
                                        : d->userBounds.toNearestInt();

                if (! target.isEmpty() && window.getBounds() != target)
                    window.setBounds (target);

                // Re-assert immersive bars (AndroidBars.h): swipe reveals,
                // focus loss, volume panels and rotation all clear the hide
                // request otherwise.
                if (target == d->logicalBounds.toNearestInt())
                    hideSystemBars();
            }

            juce::DocumentWindow& window;
        };

        std::unique_ptr<WindowRefitter> refitter;
#endif

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
    };

private:
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION(UpicApplication)
