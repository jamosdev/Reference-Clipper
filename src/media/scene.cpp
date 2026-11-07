// A hard cut is a change that happens all at once and stays. Comparing frames
// is not enough for either half of that: in content with a fixed background
// (game footage, screen captures) a cut can move less of the picture than a
// muzzle flash, and animation can move as much without being a cut. So:
//
//   1. a frame step must be a spike against the steps around it, which cheaply
//      rejects most frames;
//   2. the median of the frames before is compared with the median of the frames
//      after, which removes transients, and the step must account for most of
//      that difference ("explained": rejects gradual motion) and most of the
//      step must survive into it ("persisted": rejects flashes).
//
// Tuned on a 112 s clip with a static background and 16 known cuts, where the
// usual fixed threshold (ffmpeg scene >= 0.3) finds none. This finds 15, plus
// 7 false hits in 6740 frames on the first shot of a burst of gunfire. Dissolves
// have no single step and are not detected.
#include "media/scene.h"

#include <algorithm>
#include <cmath>
#include <deque>

#include "media/frame_image.h"
#include "media/stream_reader.h"

namespace rc {
namespace {

constexpr int kThumbW = 96;
constexpr int kThumbH = 54;
constexpr int kThumbBytes = kThumbW * kThumbH * 3;
constexpr int kWindow = 8;           // frames each side
constexpr double kMinStep = 1.0;     // mean absolute difference, 0..255
constexpr double kMinSpike = 2.0;    // step over the median of nearby steps
constexpr double kMinExplained = 0.7;
constexpr double kMinPersisted = 0.7;

struct Sample {
    double time = 0;
    double step = 0;
    std::vector<uint8_t> thumb;
};

class Detector {
public:
    Detector(const ClipInfo& clip, const std::optional<Crop>& crop, const std::atomic<bool>& cancel)
        : clip_(clip), crop_(crop), cancel_(cancel), reader_(clip.path, AVMEDIA_TYPE_VIDEO), frame_(makeFrame())
    {
        reader_.setFastDecode(true);
    }

    ~Detector() { sws_freeContext(scaler_); }

    std::vector<double> run(double from, double to)
    {
        cuts_.clear();
        samples_.clear();
        centre_ = 1;
        from_ = from;
        to_ = to;
        const double context = (2 * kWindow + 2) / clip_.fps;
        reader_.seekBefore(from - context);

        while (!cancel_.load(std::memory_order_relaxed) && reader_.next(frame_.get())) {
            const double t = reader_.timeOf(frame_.get());
            push(t);
            if (t >= to + context) break;
        }
        for (; centre_ < samples_.size(); ++centre_) evaluate(centre_);
        return cuts_;
    }

private:
    void push(double t)
    {
        const AVFrame* source = frame_.get();
        FramePtr cropped;
        if (crop_) {
            cropped = croppedFrame(frame_.get(), *crop_, clip_.rotation, clip_.display);
            source = cropped.get();
        }
        scaler_ = sws_getCachedContext(scaler_, source->width, source->height, AVPixelFormat(source->format), kThumbW,
                                       kThumbH, AV_PIX_FMT_RGB24, SWS_AREA, nullptr, nullptr, nullptr);
        if (!scaler_) throw MediaError("cannot create scaler");

        Sample sample;
        sample.time = t;
        sample.thumb.resize(kThumbBytes);
        uint8_t* planes[4] = {sample.thumb.data(), nullptr, nullptr, nullptr};
        const int strides[4] = {kThumbW * 3, 0, 0, 0};
        sws_scale(scaler_, source->data, source->linesize, 0, source->height, planes, strides);

        if (!samples_.empty()) {
            long sum = 0;
            const auto& previous = samples_.back().thumb;
            for (int i = 0; i < kThumbBytes; ++i) sum += std::abs(int(sample.thumb[i]) - int(previous[i]));
            sample.step = double(sum) / kThumbBytes;
        }
        samples_.push_back(std::move(sample));

        while (centre_ + kWindow < samples_.size()) {
            evaluate(centre_);
            ++centre_;
            if (centre_ > size_t(kWindow) + 1) {
                samples_.pop_front();
                --centre_;
            }
        }
    }

    void evaluate(size_t c)
    {
        const Sample& sample = samples_[c];
        if (sample.time < from_ || sample.time >= to_ || sample.step < kMinStep) return;

        std::vector<double> nearby;
        for (size_t i = c > size_t(kWindow) ? c - kWindow : 1; i < samples_.size() && i <= c + kWindow; ++i)
            if (i != c) nearby.push_back(samples_[i].step);
        if (!nearby.empty()) {
            std::nth_element(nearby.begin(), nearby.begin() + nearby.size() / 2, nearby.end());
            if (sample.step < kMinSpike * (nearby[nearby.size() / 2] + 0.1)) return;
        }

        const size_t preFirst = c > size_t(kWindow) ? c - kWindow : 0;
        const size_t postLast = std::min(samples_.size(), c + kWindow);
        const uint8_t* before = samples_[c - 1].thumb.data();
        const uint8_t* after = sample.thumb.data();

        // Over all byte positions: how far the medians moved, and how much of
        // that movement, in the same direction, this one step made.
        double moved = 0, stepped = 0, shared = 0;
        for (int i = 0; i < kThumbBytes; ++i) {
            const double pre = median(preFirst, c, i);
            const double post = median(c, postLast, i);
            const double change = post - pre;
            const double step = double(after[i]) - double(before[i]);
            moved += std::abs(change);
            stepped += std::abs(step);
            if (change * step > 0) shared += std::min(std::abs(change), std::abs(step));
        }
        if (moved <= 0 || stepped <= 0) return;
        if (shared / moved >= kMinExplained && shared / stepped >= kMinPersisted) cuts_.push_back(sample.time);
    }

    double median(size_t first, size_t last, int byte) const
    {
        uint8_t values[kWindow];
        const size_t count = last - first;  // at most kWindow; see the callers
        for (size_t i = 0; i < count; ++i) {  // insertion sort: this runs per byte, on eight values
            const uint8_t value = samples_[first + i].thumb[size_t(byte)];
            size_t j = i;
            for (; j > 0 && values[j - 1] > value; --j) values[j] = values[j - 1];
            values[j] = value;
        }
        return count % 2 ? values[count / 2] : (values[count / 2 - 1] + values[count / 2]) / 2.0;
    }

    const ClipInfo& clip_;
    const std::optional<Crop>& crop_;
    const std::atomic<bool>& cancel_;
    StreamReader reader_;
    FramePtr frame_;
    SwsContext* scaler_ = nullptr;
    std::deque<Sample> samples_;
    size_t centre_ = 1;
    double from_ = 0, to_ = 0;
    std::vector<double> cuts_;
};

}  // namespace

std::vector<double> detectCuts(const ClipInfo& clip, double from, double to, const std::optional<Crop>& crop,
                               const std::atomic<bool>& cancel)
{
    Detector detector(clip, crop, cancel);
    return detector.run(from, to);
}

SceneRange findScene(const ClipInfo& clip, double time, double lower, double upper, const std::optional<Crop>& crop,
                     const std::atomic<bool>& cancel, const std::function<void(const SceneRange&)>& onUpdate)
{
    constexpr double kFirstChunk = 2;
    constexpr double kLargestChunk = 30;
    constexpr double kEpsilon = 1e-4;

    Detector detector(clip, crop, cancel);
    SceneRange range{time, time};
    double chunk = kFirstChunk;
    bool startFound = time <= lower, endFound = time >= upper;
    if (startFound) range.start = lower;
    if (endFound) range.end = upper;

    while (!(startFound && endFound) && !cancel.load(std::memory_order_relaxed)) {
        if (!endFound) {
            // A frame exactly at `time` belongs to this scene, so forward starts after it.
            const double from = range.end + (range.end == time ? kEpsilon : 0);
            const double to = std::min(upper, range.end + chunk);
            const auto cuts = detector.run(from, to);
            if (!cuts.empty()) {
                range.end = cuts.front();
                endFound = true;
            } else {
                range.end = to;
                endFound = to >= upper;
            }
        }
        if (!startFound && !cancel.load(std::memory_order_relaxed)) {
            const double to = range.start + (range.start == time ? kEpsilon : 0);
            const double from = std::max(lower, range.start - chunk);
            const auto cuts = detector.run(from, to);
            if (!cuts.empty()) {
                range.start = cuts.back();
                startFound = true;
            } else {
                range.start = from;
                startFound = from <= lower;
            }
        }
        if (onUpdate) onUpdate(range);
        chunk = std::min(chunk * 2, kLargestChunk);
    }
    return range;
}

}  // namespace rc
