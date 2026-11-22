#pragma once

#include "plynth/core/Stairs.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <string_view>

namespace plynth::stairs {

// Base class for the built-in stair types: holds the id, name, sort key and whether the turn side matters.
class BuiltInStair : public StairType {
public:
    BuiltInStair(std::string_view id, std::string_view name, int sortKey, bool usesTurn)
        : id_(id), name_(name), sortKey_(sortKey), usesTurn_(usesTurn)
    {
    }

    std::string_view id() const override { return id_; }
    std::string_view name() const override { return name_; }
    int sortKey() const override { return sortKey_; }
    bool usesTurn() const override { return usesTurn_; }

    // Builds the shape for a turn to the right, then mirrors it for a turn to the left.
    StairShape shape(const StairInput& input) const final
    {
        StairShape result = rightShape(input);
        if (usesTurn_ && input.turn == StairTurn::Left) mirror(result);
        return result;
    }

protected:
    virtual StairShape rightShape(const StairInput& input) const = 0;

    // Puts the corners in the order that gives a positive signedArea.
    static void orient(std::vector<Vec2>& outline)
    {
        if (signedArea(outline) < 0.0) std::reverse(outline.begin(), outline.end());
    }

    // A step shaped as a rectangle from (u0, v0) to (u1, v1).
    static StairStep box(StairPart part, double u0, double u1, double v0, double v1, double bottom, double top)
    {
        StairStep step;
        step.part = part;
        step.outline = {{u0, v0}, {u1, v0}, {u1, v1}, {u0, v1}};
        orient(step.outline);
        step.bottom = bottom;
        step.top = top;
        return step;
    }

private:
    // Swaps left and right: every u changes sign, and outlines turn round to stay positive.
    static void mirror(StairShape& shape)
    {
        for (StairStep& step : shape.steps) {
            for (Vec2& p : step.outline) p.x = -p.x;
            orient(step.outline);
        }
        for (Vec2& p : shape.walkLine) p.x = -p.x;
    }

    std::string_view id_;
    std::string_view name_;
    int sortKey_;
    bool usesTurn_;
};

}
