#pragma once
#include <JuceHeader.h>
#include <atomic>
#include <functional>
#include <memory>
#include <thread>

//==============================================================================
// The long jobs off the message thread (v1.33.4, M28).
//
// The sample decode, the base64 write and the whole-page render used to run
// in the file chooser's callback: on the message thread, and mostly while
// holding dataLock. A 120 s export at 96 kHz wants ~88 MiB of stereo floats
// and minutes of CPU - the window froze, the audio thread waited for the
// lock, and the app looked hung.
//
// A job now runs on a worker thread. The caller captures what it needs on the
// message thread first (the sample audio is shared, so the copy is a few
// pointers), the worker never touches the component, and the report comes
// back on the message thread. The job reports its progress and can be
// cancelled.
//
// The rules:
//   - the model is captured on the message thread (under the lock) and the
//     worker never touches the component again
//   - the work checks the cancel flag between the blocks, and the progress
//     is published as a percentage the status line shows
//   - the report comes back on the message thread (the worker posts it), so
//     the messages and the UI are only touched there
//
// One job at a time: a second one would fight the first over the disk.
namespace upic
{
    struct Job
    {
        juce::String label;                    // what the status line says
        std::atomic<bool> cancelled { false };
        std::atomic<bool> done { false };      // the work is finished
        std::atomic<int> progress { 0 };       // 0..100
        bool ok = false;                       // the job's result (the report reads it)
        // v1.33.4 (L15, part 3): a silent job does not take over the status
        // line and does not flash a message - the recovery snapshot runs
        // every half minute and must not keep interrupting the user.
        bool silent = false;
        juce::String message;                  // what the report says
        std::function<void (Job&)> work;       // the heavy part (the worker thread)
        std::function<void (Job&)> report;     // the result (the message thread)
    };

    class JobRunner
    {
    public:
        using JobPtr = std::shared_ptr<Job>;

        // What the status line says when a job starts or is cancelled (the
        // message thread; the app's flash). Optional.
        std::function<void (const juce::String&)> announce;

        JobRunner();
        ~JobRunner();

        // Starts a job on the worker thread (message thread). A second job is
        // refused while one runs. `announceIt` = false is a silent job.
        void start (const juce::String& label,
                    std::function<void (Job&)> work,
                    std::function<void (Job&)> report,
                    bool announceIt = true);

        void cancel();                                  // asks the running job to stop
        bool running() const { return state->busy.load (std::memory_order_relaxed); }
        int progress() const;
        juce::String label() const;

        // The running job, for the status line (the message thread).
        JobPtr current() const { return state->active; }

        // Cancels the job and waits for the worker (the destructor does it
        // too; the app calls it before the audio goes down).
        void shutdown();

    private:
        // Shared with the worker and with the queued report, so a report that
        // arrives after the runner is gone sees alive == false and does
        // nothing.
        struct State
        {
            JobPtr active;
            std::atomic<bool> busy { false };
            std::atomic<bool> alive { true };
        };

        std::shared_ptr<State> state = std::make_shared<State>();
        std::thread worker;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (JobRunner)
    };
}
