#pragma once

#include <plynth/core/Plan.h>

#include <QImage>

#include <filesystem>

namespace plynth::app {

// Draws a small picture of a plan for the home screen: the plan image with the rooms and walls over it.
// The longest side is maxSide pixels. A null image gives a null result.
QImage renderProjectThumbnail(const plynth::Plan& plan, const QImage& planImage, int maxSide);

// Makes the small picture of a saved project folder for the home screen from its files on disk, and writes it
// to recentThumbnailPath. False when the project or its image cannot be read.
bool writeRecentThumbnail(const std::filesystem::path& projectFolder);

}
