#include "plynth/core/detection/GrayImage.h"

#include "plynth/core/PlanImage.h"

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <cmath>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iterator>
#include <memory>
#include <new>
#include <string>
#include <utility>
#include <vector>

namespace plynth {

namespace {

// Makes a failed load.
GrayImageLoad refused(ErrorCode code, std::string message)
{
    GrayImageLoad load;
    load.status = fail(code, std::move(message));
    return load;
}

// Mixes one pixel's colour channels down to grey with the usual weights.
std::uint8_t toGray(double b, double g, double r)
{
    return static_cast<std::uint8_t>(std::lround(0.114 * b + 0.587 * g + 0.299 * r));
}

}

// Reads the file, decodes it from memory and turns it into grey levels with transparency counted as white.
// JPEG files are decoded as grey so their orientation tag is applied, the way the app shows them. Anything
// that throws while decoding or converting becomes an image error.
GrayImageLoad loadGrayImage(const std::filesystem::path& file)
{
    const PlanImageProbe probe = probePlanImage(file);
    if (!probe.ok()) {
        return refused(probe.error == PlanImageError::NotFound ? ErrorCode::NotFound : ErrorCode::ImageError, probe.message);
    }
    try {
        std::vector<std::uint8_t> bytes;
        {
            std::ifstream in(file, std::ios::binary);
            if (!in) return refused(ErrorCode::ImageError, "The image file could not be opened.");
            bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        }

        const int flags = probe.info.format == PlanImageFormat::Jpeg ? cv::IMREAD_GRAYSCALE : cv::IMREAD_UNCHANGED;
        cv::Mat decoded = cv::imdecode(bytes, flags);
        if (decoded.empty() || decoded.cols <= 0 || decoded.rows <= 0) {
            return refused(ErrorCode::ImageError, "The image could not be decoded.");
        }
        if (decoded.depth() != CV_8U) {
            cv::Mat eight;
            decoded.convertTo(eight, CV_8U, decoded.depth() == CV_16U ? 1.0 / 257.0 : 1.0);
            decoded = eight;
        }

        auto image = std::make_shared<GrayImage>();
        image->width = decoded.cols;
        image->height = decoded.rows;
        image->pixels.resize(static_cast<std::size_t>(decoded.cols) * static_cast<std::size_t>(decoded.rows));

        const int channels = decoded.channels();
        for (int y = 0; y < decoded.rows; ++y) {
            const std::uint8_t* src = decoded.ptr<std::uint8_t>(y);
            std::uint8_t* dst = image->pixels.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(decoded.cols);
            for (int x = 0; x < decoded.cols; ++x) {
                const std::uint8_t* p = src + x * channels;
                switch (channels) {
                case 1:
                    dst[x] = p[0];
                    break;
                case 2: {
                    const double a = p[1] / 255.0;
                    dst[x] = static_cast<std::uint8_t>(std::lround(p[0] * a + 255.0 * (1.0 - a)));
                    break;
                }
                case 3:
                    dst[x] = toGray(p[0], p[1], p[2]);
                    break;
                default: {
                    const double a = p[3] / 255.0;
                    dst[x] = toGray(p[0] * a + 255.0 * (1.0 - a), p[1] * a + 255.0 * (1.0 - a), p[2] * a + 255.0 * (1.0 - a));
                    break;
                }
                }
            }
        }

        GrayImageLoad load;
        load.image = std::move(image);
        return load;
    } catch (const std::bad_alloc&) {
        return refused(ErrorCode::ImageError, "The image is too large to load.");
    } catch (const std::exception&) {
        return refused(ErrorCode::ImageError, "The image could not be decoded.");
    }
}

}
