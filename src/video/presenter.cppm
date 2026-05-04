export module wavsen.video.presenter;

import rstd.cppstd;

export namespace wavsen::video {

// PTS → wall-clock pacing helper for the video plugin's render loop.
//
// Behavior:
//   - First frame primes the baseline (t0_wall = now; t0_pts = pts).
//   - Subsequent frames sleep until t0_wall + (pts - t0_pts).
//   - PTS that drops backwards (loop wrap-around) re-baselines silently.
//   - Frames more than `max_lag` behind schedule are dropped (return
//     false) so a slow consumer or stalled decoder doesn't snowball.
//   - Frames more than `max_sleep` ahead of schedule treat the PTS as
//     a forward discontinuity and re-baseline rather than sleep forever.
//   - Frames with pts<0 (PTS unavailable) skip pacing entirely.
class Presenter {
public:
    using Clock     = std::chrono::steady_clock;
    using Duration  = Clock::duration;
    using TimePoint = Clock::time_point;

    explicit Presenter(Duration max_lag   = std::chrono::milliseconds(250),
                       Duration max_sleep = std::chrono::seconds(1))
        : max_lag_(max_lag), max_sleep_(max_sleep) {}

    // Force the next call to re-prime the baseline. Useful when the
    // caller knows the stream just looped or the decoder was reset.
    void reset() { primed_ = false; t0_pts_ = -1.0; }

    // Returns true if the caller should render the frame now (possibly
    // after sleeping); false if the frame is too far behind schedule and
    // should be dropped. Always advances the baseline on drop so we
    // recover instead of dropping every subsequent frame too.
    bool present_frame(double pts_seconds) {
        if (pts_seconds < 0.0) return true;

        const auto now = Clock::now();
        if (!primed_) {
            t0_wall_ = now;
            t0_pts_  = pts_seconds;
            primed_  = true;
            return true;
        }
        if (pts_seconds < t0_pts_) {
            t0_wall_ = now;
            t0_pts_  = pts_seconds;
            return true;
        }

        const auto delta = std::chrono::duration_cast<Duration>(
            std::chrono::duration<double>(pts_seconds - t0_pts_));
        const auto target = t0_wall_ + delta;

        if (target + max_lag_ < now) {
            t0_wall_ = now;
            t0_pts_  = pts_seconds;
            return false;
        }
        if (now < target) {
            if (target - now > max_sleep_) {
                t0_wall_ = now;
                t0_pts_  = pts_seconds;
                return true;
            }
            std::this_thread::sleep_until(target);
        }
        return true;
    }

private:
    Duration  max_lag_;
    Duration  max_sleep_;
    TimePoint t0_wall_ {};
    double    t0_pts_  { -1.0 };
    bool      primed_  { false };
};

} // namespace wavsen::video
