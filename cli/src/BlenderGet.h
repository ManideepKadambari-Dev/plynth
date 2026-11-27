#pragma once

#include <plynth/core/BlenderDownload.h>

#include <functional>
#include <stop_token>

namespace plynth::cli {

// Where "blender get" reads blender.org's offer and gets the download. An empty member uses core's own
// findBlenderOffer or downloadBlender, which reach blender.org.
struct BlenderGetSource {
    std::function<BlenderOfferCheck(std::stop_token)> findOffer;
    std::function<BlenderDownload(const BlenderOffer&, const std::function<void(const BlenderDownloadProgress&)>&,
                                  std::stop_token)>
        download;
};

// Swaps the source "blender get" uses, so the tests run without the network. Give an empty source to go back.
void setBlenderGetSource(BlenderGetSource source);

}
