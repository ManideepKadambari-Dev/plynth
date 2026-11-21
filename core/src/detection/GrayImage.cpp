#include "plynth/core/detection/GrayImage.h"

namespace plynth {

// Placeholder until the OpenCV based loader arrives.
GrayImageLoad loadGrayImage(const std::filesystem::path& file)
{
    (void)file;
    GrayImageLoad load;
    load.status = fail(ErrorCode::ImageError, "Image loading is not available yet.");
    return load;
}

}
