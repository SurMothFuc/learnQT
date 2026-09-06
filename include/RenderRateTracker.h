#pragma once
#include <QtGlobal>
#include <algorithm>
#include <deque>

// Measure completed work at GPU boundaries, independently of the 5 Hz UI refresh.
class RenderRateTracker
{
    class Rate
    {
        struct Point
        {
            double time;
            int count;
        };
        std::deque<Point> points;

      public:
        void reset(double time, int count)
        {
            points = {{time, count}};
        }
        void observe(double time, int count)
        {
            if (points.empty() || count < points.back().count)
                reset(time, count);
            else if (count > points.back().count)
            {
                points.push_back({time, count});
                while (points.size() > 2 && points[1].time < time - 2.)
                    points.pop_front();
            }
        }
        double value(double time) const
        {
            if (points.size() < 2)
                return 0;
            double duration = points.back().time - points.front().time;
            int count = points.back().count - points.front().count;
            if (duration <= 0 || count <= 0)
                return 0;
            // Hold during the expected next completion interval, then decay during a real stall.
            duration += std::max(0., time - points.back().time - duration / count);
            return count / duration;
        }
    };
    Rate frames, tiles;
    quint64 epoch = ~quint64(0);
    bool active = false;

  public:
    void observe(double time, int samples, int completedTiles, quint64 version, bool running)
    {
        if (version != epoch || running != active)
        {
            frames.reset(time, samples);
            tiles.reset(time, completedTiles);
            epoch = version;
            active = running;
        }
        else if (active)
        {
            frames.observe(time, samples);
            tiles.observe(time, completedTiles);
        }
    }
    double fps(double time) const
    {
        return active ? frames.value(time) : 0;
    }
    double tileFps(double time) const
    {
        return active ? tiles.value(time) : 0;
    }
};
