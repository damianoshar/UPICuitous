#include "JobRunner.h"

namespace upic
{
    JobRunner::JobRunner() = default;

    JobRunner::~JobRunner()
    {
        shutdown();
        // a report that is still queued must not touch anything any more
        state->alive.store (false, std::memory_order_relaxed);
    }

    void JobRunner::start (const juce::String& label,
                           std::function<void (Job&)> work,
                           std::function<void (Job&)> report,
                           bool announceIt)
    {
        // one job at a time - a second one would fight the first over the disk
        if (state->busy.load (std::memory_order_relaxed))
        {
            if (announceIt && announce != nullptr)
                announce ("Another job is still running (" + this->label()
                          + ") - wait for it, or cancel it");
            return;
        }

        // the previous job's thread is done (it cleared the flag), so it can go
        if (worker.joinable()) worker.join();

        auto job = std::make_shared<Job>();
        job->label = label;
        job->silent = ! announceIt;
        job->work = std::move (work);
        job->report = std::move (report);
        state->active = job;
        state->busy.store (true, std::memory_order_relaxed);
        if (announceIt && announce != nullptr)
            announce (label + "...");

        auto st = state;
        worker = std::thread ([st, job]
        {
            if (job->work)
            {
                try { job->work (*job); }
                catch (...) { job->ok = false; }
            }
            // the job is done - the next one may start (the report is only a
            // message, it does not block anything)
            job->done.store (true, std::memory_order_relaxed);
            st->busy.store (false, std::memory_order_relaxed);
            // the report belongs to the message thread
            juce::MessageManager::callAsync ([st, job]
            {
                if (! st->alive.load (std::memory_order_relaxed)) return;
                if (st->active == job) st->active.reset();
                if (job->report) job->report (*job);
            });
        });
    }

    void JobRunner::cancel()
    {
        if (! state->busy.load (std::memory_order_relaxed)) return;
        if (state->active) state->active->cancelled.store (true, std::memory_order_relaxed);
        if (announce != nullptr) announce ("Cancelling " + label() + "...");
    }

    int JobRunner::progress() const
    {
        return state->active ? state->active->progress.load (std::memory_order_relaxed) : 0;
    }

    juce::String JobRunner::label() const
    {
        return state->active ? state->active->label : juce::String();
    }

    void JobRunner::shutdown()
    {
        if (state->active) state->active->cancelled.store (true, std::memory_order_relaxed);
        if (worker.joinable()) worker.join();
        state->active.reset();
    }
}
