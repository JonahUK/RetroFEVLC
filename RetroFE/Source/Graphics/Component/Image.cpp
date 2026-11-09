/* This file is part of RetroFE.
 *
 * RetroFE is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * RetroFE is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with RetroFE.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "Image.h"
#include "../ViewInfo.h"
#include "../../SDL.h"
#include "../../Utility/Log.h"
#include <SDL2/SDL_image.h>
#include <png.h>
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <limits>
#include <sstream>

namespace
{
    bool isWebPFile(const std::string &file)
    {
        if (file.length() < 5)
            return false;

        std::string extension = file.substr(file.length() - 5);
        std::transform(extension.begin(), extension.end(), extension.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        return extension == ".webp";
    }

    void PNGCBAPI apngError(png_structp png, png_const_charp message)
    {
        Logger::write(Logger::ZONE_WARNING, "APNG",
            std::string("libpng error: ") +
            (message ? message : "unknown"));
        longjmp(png_jmpbuf(png), 1);
    }

    void PNGCBAPI apngWarning(png_structp png, png_const_charp message)
    {
        (void)png;
        (void)message;
    }
    struct APNGDecodeBuffers
    {
        png_bytep framePixels;
        png_bytep *rowPointers;
        size_t frameBytes;
    };

    void releaseAPNGDecodeBuffers(APNGDecodeBuffers *buffers)
    {
        if (!buffers)
            return;

        std::free(buffers->rowPointers);
        std::free(buffers->framePixels);

        buffers->rowPointers = NULL;
        buffers->framePixels = NULL;
        buffers->frameBytes = 0;
    }
    struct APNGAnimationInfo
    {
        png_uint_32 canvasWidth;
        png_uint_32 canvasHeight;
        png_uint_32 frameCount;
        png_uint_32 loopCount;
        bool firstFrameHidden;
        size_t canvasBytes;
    };
    struct APNGDecoderContext
    {
        png_structp png;
        png_infop info;
        APNGDecodeBuffers buffers;
    };

    void releaseAPNGDecoderContext(APNGDecoderContext *context)
    {
        if (!context)
            return;

        releaseAPNGDecodeBuffers(&context->buffers);

        if (context->png)
            png_destroy_read_struct(
                &context->png,
                &context->info,
                NULL);

        context->png = NULL;
        context->info = NULL;
    }
    struct APNGReadContext
    {
        const uint8_t *bytes;
        size_t size;
        size_t offset;
    };

    void PNGCBAPI apngReadData(
        png_structp png,
        png_bytep output,
        png_size_t length)
    {
        APNGReadContext *context =
            static_cast<APNGReadContext *>(png_get_io_ptr(png));

        if (!context ||
            context->offset > context->size ||
            static_cast<size_t>(length) > context->size - context->offset)
        {
            png_error(png, "Invalid or truncated APNG data");
            return;
        }

        std::memcpy(output,
                    context->bytes + context->offset,
                    static_cast<size_t>(length));

        context->offset += static_cast<size_t>(length);
    }
    bool initializeAPNGDecoder(
        APNGDecoderContext *decoder,
        APNGReadContext *reader)
    {
        if (!decoder || !reader ||
            !reader->bytes || reader->size == 0)
            return false;

        decoder->png = NULL;
        decoder->info = NULL;
        decoder->buffers.framePixels = NULL;
        decoder->buffers.rowPointers = NULL;
        decoder->buffers.frameBytes = 0;

        decoder->png = png_create_read_struct(
            PNG_LIBPNG_VER_STRING,
            NULL,
            apngError,
            apngWarning);

        if (!decoder->png)
            return false;

        decoder->info = png_create_info_struct(decoder->png);

        if (!decoder->info)
        {
            releaseAPNGDecoderContext(decoder);
            return false;
        }

        png_set_read_fn(decoder->png, reader, apngReadData);
        return true;
    }
    bool configureAPNGRGBA(png_structp png, png_infop info)
    {
        if (!png || !info)
            return false;

        const int colorType = png_get_color_type(png, info);
        const int bitDepth = png_get_bit_depth(png, info);
        const bool hasTransparency =
            png_get_valid(png, info, PNG_INFO_tRNS) != 0;

        if (bitDepth == 16)
            png_set_strip_16(png);

        if (colorType == PNG_COLOR_TYPE_PALETTE)
            png_set_palette_to_rgb(png);

        if (colorType == PNG_COLOR_TYPE_GRAY && bitDepth < 8)
            png_set_expand_gray_1_2_4_to_8(png);

        if (hasTransparency)
            png_set_tRNS_to_alpha(png);

        if (colorType == PNG_COLOR_TYPE_GRAY ||
            colorType == PNG_COLOR_TYPE_GRAY_ALPHA)
            png_set_gray_to_rgb(png);

        if (!(colorType & PNG_COLOR_MASK_ALPHA) &&
            !hasTransparency)
            png_set_add_alpha(png, 0xff, PNG_FILLER_AFTER);

        return true;
    }
    bool readAPNGAnimationInfo(
        png_structp png,
        png_infop info,
        APNGAnimationInfo &animation);
    bool readAPNGHeader(
        APNGDecoderContext *decoder,
        APNGAnimationInfo &animation,
        int &interlacePasses)
    {
        if (!decoder || !decoder->png || !decoder->info)
            return false;

        if (setjmp(png_jmpbuf(decoder->png)) != 0)
            return false;

        png_read_info(decoder->png, decoder->info);

        if (!readAPNGAnimationInfo(
                decoder->png, decoder->info, animation))
            return false;

        if (!configureAPNGRGBA(
                decoder->png, decoder->info))
            return false;

        interlacePasses = png_set_interlace_handling(decoder->png);

        if (interlacePasses < 1 || interlacePasses > 7)
            return false;

        png_read_update_info(decoder->png, decoder->info);

        return true;
    }
    bool prepareAPNGFrameBuffers(
        APNGDecodeBuffers *buffers,
        png_uint_32 width,
        png_uint_32 height)
    {
        if (!buffers || width == 0 || height == 0 ||
            static_cast<size_t>(width) > SIZE_MAX / 4 ||
            static_cast<size_t>(height) >
                SIZE_MAX / (static_cast<size_t>(width) * 4) ||
            static_cast<size_t>(height) > SIZE_MAX / sizeof(png_bytep))
            return false;

        releaseAPNGDecodeBuffers(buffers);

        const size_t stride = static_cast<size_t>(width) * 4;
        buffers->frameBytes = stride * height;

        buffers->framePixels =
            static_cast<png_bytep>(std::calloc(buffers->frameBytes, 1));

        buffers->rowPointers = static_cast<png_bytep *>(
            std::calloc(height, sizeof(png_bytep)));

        if (!buffers->framePixels || !buffers->rowPointers)
        {
            releaseAPNGDecodeBuffers(buffers);
            return false;
        }

        for (png_uint_32 y = 0; y < height; ++y)
            buffers->rowPointers[y] =
                buffers->framePixels + static_cast<size_t>(y) * stride;

        return true;
    }

    bool validateAPNGDecodedRows(
        png_structp png,
        png_infop info,
        png_uint_32 frameWidth)
    {
        if (!png || !info || frameWidth == 0)
            return false;

        if (static_cast<size_t>(frameWidth) > SIZE_MAX / 4)
            return false;

        const size_t expectedRowBytes =
            static_cast<size_t>(frameWidth) * 4;

        if (png_get_bit_depth(png, info) != 8)
            return false;

        if (png_get_channels(png, info) != 4)
            return false;

        // APNG subframes can be smaller than the original PNG canvas.
        // png_get_rowbytes() returns info_ptr->rowbytes, which retains
        // the canvas stride after png_read_frame_head(). The patched
        // libpng decoder reinitialises its internal dimensions using
        // the current fcTL before decoding each subsequent frame.
        // Validate RGBA format and calculate the subframe stride
        // from the already-validated frame width instead.
        if (expectedRowBytes == 0)
            return false;

        return true;
    }
    void readAPNGFrameRows(
        png_structp png,
        APNGDecodeBuffers *buffers,
        png_uint_32 height,
        int passes)
    {
        // The caller establishes setjmp and owns buffers.
        // No C++ objects requiring destruction are created here.
        for (int pass = 0; pass < passes; ++pass)
        {
            for (png_uint_32 y = 0; y < height; ++y)
                png_read_rows(png, &buffers->rowPointers[y], NULL, 1);
        }
    }
    bool calculateAPNGCanvasBytes(
        png_uint_32 width,
        png_uint_32 height,
        size_t &canvasBytes)
    {
        canvasBytes = 0;

        if (width == 0 || height == 0)
            return false;

        if (static_cast<size_t>(width) > SIZE_MAX / 4)
            return false;

        const size_t stride =
            static_cast<size_t>(width) * 4;

        if (static_cast<size_t>(height) > SIZE_MAX / stride)
            return false;

        const size_t bytes = stride * height;

        // Limit a single decoded RGBA canvas to 256 MiB.
        const size_t maxCanvasBytes =
            static_cast<size_t>(256) * 1024 * 1024;

        if (bytes > maxCanvasBytes)
            return false;

        canvasBytes = bytes;
        return true;
    }
    bool validateAPNGAnimationMemory(
        size_t canvasBytes,
        png_uint_32 frameCount)
    {
        if (canvasBytes == 0 || frameCount == 0)
            return false;

        // Maximum memory allocated for stored animation frames.
        const size_t maxAnimationBytes =
            static_cast<size_t>(512) * 1024 * 1024;

        if (canvasBytes > maxAnimationBytes)
            return false;

        // Division avoids multiplication overflow.
        if (static_cast<size_t>(frameCount) >
            maxAnimationBytes / canvasBytes)
            return false;

        return true;
    }
    double apngFrameDurationMs(
        png_uint_16 delayNumerator,
        png_uint_16 delayDenominator)
    {
        // APNG specifies a denominator of 100 when it is zero.
        const unsigned int denominator =
            delayDenominator == 0 ? 100 : delayDenominator;

        // Preserve fractional milliseconds for accurate scheduling.
        const double duration =
            static_cast<double>(delayNumerator) * 1000.0 /
            static_cast<double>(denominator);

        // Avoid a zero-duration frame causing a playback loop.
        return duration > 0.0 ? duration : 10.0;
    }
    bool readAPNGAnimationInfo(
        png_structp png,
        png_infop info,
        APNGAnimationInfo &animation)
    {
        if (!png || !info)
            return false;

        if (png_get_acTL(
                png, info,
                &animation.frameCount,
                &animation.loopCount) == 0)
            return false;

        animation.canvasWidth = png_get_image_width(png, info);
        animation.canvasHeight = png_get_image_height(png, info);
        animation.firstFrameHidden =
            png_get_first_frame_is_hidden(png, info) != 0;
        animation.canvasBytes = 0;

        if (animation.frameCount < 2)
            return false;

        if (!calculateAPNGCanvasBytes(
                animation.canvasWidth,
                animation.canvasHeight,
                animation.canvasBytes))
            return false;

        if (!validateAPNGAnimationMemory(
                animation.canvasBytes,
                animation.frameCount))
            return false;

        return true;
    }
    struct APNGFrameControl
    {
        png_uint_32 width;
        png_uint_32 height;
        png_uint_32 xOffset;
        png_uint_32 yOffset;
        png_uint_16 delayNumerator;
        png_uint_16 delayDenominator;
        png_byte disposeOp;
        png_byte blendOp;
        double durationMs;
    };

    struct APNGFrameReadState
    {
        APNGFrameControl control;
        int interlacePasses;
        bool hasFrameControl;
        bool decoded;

        APNGFrameReadState()
            : control(),
              interlacePasses(1),
              hasFrameControl(false),
              decoded(false)
        {
        }
    };
    bool validateAPNGFrameBounds(
        png_uint_32 canvasWidth,
        png_uint_32 canvasHeight,
        png_uint_32 frameWidth,
        png_uint_32 frameHeight,
        png_uint_32 xOffset,
        png_uint_32 yOffset);
    bool readAPNGFrameControl(
        png_structp png,
        png_infop info,
        const APNGAnimationInfo &animation,
        APNGFrameControl &frame)
    {
        if (!png || !info)
            return false;

        if (png_get_next_frame_fcTL(
                png, info,
                &frame.width,
                &frame.height,
                &frame.xOffset,
                &frame.yOffset,
                &frame.delayNumerator,
                &frame.delayDenominator,
                &frame.disposeOp,
                &frame.blendOp) == 0)
            return false;

        if (!validateAPNGFrameBounds(
                animation.canvasWidth,
                animation.canvasHeight,
                frame.width,
                frame.height,
                frame.xOffset,
                frame.yOffset))
            return false;

        if (frame.disposeOp != PNG_DISPOSE_OP_NONE &&
            frame.disposeOp != PNG_DISPOSE_OP_BACKGROUND &&
            frame.disposeOp != PNG_DISPOSE_OP_PREVIOUS)
            return false;

        if (frame.blendOp != PNG_BLEND_OP_SOURCE &&
            frame.blendOp != PNG_BLEND_OP_OVER)
            return false;

        frame.durationMs = apngFrameDurationMs(
            frame.delayNumerator,
            frame.delayDenominator);

        return true;
    }
    bool prepareAPNGFrameHeader(
        png_structp png,
        png_infop info,
        const APNGAnimationInfo &animation,
        bool hiddenDefaultFrame,
        APNGFrameReadState &state)
    {
        if (!png || !info)
            return false;

        state = APNGFrameReadState();

        png_read_frame_head(png, info);

        if (hiddenDefaultFrame)
        {
            state.control.width = animation.canvasWidth;
            state.control.height = animation.canvasHeight;
            state.control.xOffset = 0;
            state.control.yOffset = 0;
            state.control.durationMs = 0.0;
            state.hasFrameControl = false;
            return true;
        }

        if (!readAPNGFrameControl(
                png, info, animation, state.control))
        {
            Logger::write(Logger::ZONE_WARNING, "APNG",
                "Frame control validation failed");
            return false;
        }

        state.hasFrameControl = true;
        return true;
    }
    bool decodeAPNGFramePixels(
        APNGDecoderContext *decoder,
        APNGFrameReadState &state)
    {
        if (!decoder || !decoder->png || !decoder->info)
            return false;

        if (state.control.width == 0 ||
            state.control.height == 0)
            return false;

        if (!prepareAPNGFrameBuffers(
                &decoder->buffers,
                state.control.width,
                state.control.height))
            return false;

        if (!validateAPNGDecodedRows(
                decoder->png,
                decoder->info,
                state.control.width))
        {
            std::ostringstream diagnostic;
            diagnostic << "Decoded row validation failed: "
                       << state.control.width << "x"
                       << state.control.height
                       << ", bitDepth="
                       << png_get_bit_depth(decoder->png, decoder->info)
                       << ", channels="
                       << png_get_channels(decoder->png, decoder->info)
                       << ", rowBytes="
                       << png_get_rowbytes(decoder->png, decoder->info);
            Logger::write(Logger::ZONE_WARNING, "APNG",
                diagnostic.str());
            return false;
        }

        if (state.interlacePasses < 1 ||
            state.interlacePasses > 7)
            return false;

        readAPNGFrameRows(
            decoder->png,
            &decoder->buffers,
            state.control.height,
            state.interlacePasses);

        state.decoded = true;
        return true;
    }

    bool readProtectedAPNGFrame(
        APNGDecoderContext *decoder,
        const APNGAnimationInfo &animation,
        bool hiddenDefaultFrame,
        int interlacePasses,
        APNGFrameReadState &state)
    {
        if (!decoder || !decoder->png || !decoder->info)
            return false;

        if (setjmp(png_jmpbuf(decoder->png)) != 0)
            return false;

        if (!prepareAPNGFrameHeader(
                decoder->png,
                decoder->info,
                animation,
                hiddenDefaultFrame,
                state))
            return false;

        state.interlacePasses = interlacePasses;

        if (!decodeAPNGFramePixels(decoder, state))
            return false;

        return state.decoded;
    }
    bool validateAPNGFrameBounds(
        png_uint_32 canvasWidth,
        png_uint_32 canvasHeight,
        png_uint_32 frameWidth,
        png_uint_32 frameHeight,
        png_uint_32 xOffset,
        png_uint_32 yOffset)
    {
        if (canvasWidth == 0 || canvasHeight == 0 ||
            frameWidth == 0 || frameHeight == 0)
            return false;

        if (frameWidth > canvasWidth ||
            frameHeight > canvasHeight)
            return false;

        if (xOffset > canvasWidth - frameWidth ||
            yOffset > canvasHeight - frameHeight)
            return false;

        if (static_cast<size_t>(canvasWidth) > SIZE_MAX / 4)
            return false;

        const size_t stride =
            static_cast<size_t>(canvasWidth) * 4;

        if (static_cast<size_t>(canvasHeight) > SIZE_MAX / stride)
            return false;

        return true;
    }
    void disposeAPNGFrame(
        std::vector<uint8_t> &canvas,
        const std::vector<uint8_t> &previousCanvas,
        unsigned int canvasWidth,
        unsigned int frameWidth,
        unsigned int frameHeight,
        unsigned int xOffset,
        unsigned int yOffset,
        png_byte disposeOp)
    {
        if (disposeOp == PNG_DISPOSE_OP_NONE)
            return;

        if (disposeOp == PNG_DISPOSE_OP_PREVIOUS)
        {
            if (previousCanvas.size() == canvas.size())
                canvas = previousCanvas;

            return;
        }

        if (disposeOp == PNG_DISPOSE_OP_BACKGROUND)
        {
            for (unsigned int y = 0; y < frameHeight; ++y)
            {
                const size_t offset =
                    (static_cast<size_t>(y + yOffset) * canvasWidth +
                     xOffset) * 4;

                std::memset(
                    canvas.data() + offset,
                    0,
                    static_cast<size_t>(frameWidth) * 4);
            }
        }
    }
    void compositeAPNGFrame(
        std::vector<uint8_t> &canvas,
        const std::vector<uint8_t> &frame,
        unsigned int canvasWidth,
        unsigned int frameWidth,
        unsigned int frameHeight,
        unsigned int xOffset,
        unsigned int yOffset,
        png_byte blendOp)
    {
        for (unsigned int y = 0; y < frameHeight; ++y)
        {
            for (unsigned int x = 0; x < frameWidth; ++x)
            {
                const size_t source =
                    (static_cast<size_t>(y) * frameWidth + x) * 4;

                const size_t destination =
                    (static_cast<size_t>(y + yOffset) * canvasWidth +
                     x + xOffset) * 4;

                if (blendOp == PNG_BLEND_OP_SOURCE)
                {
                    std::memcpy(&canvas[destination],
                                &frame[source], 4);
                    continue;
                }

                const unsigned int sa = frame[source + 3];
                const unsigned int da = canvas[destination + 3];

                if (sa == 0)
                    continue;

                if (sa == 255 || da == 0)
                {
                    std::memcpy(&canvas[destination],
                                &frame[source], 4);
                    continue;
                }

                const unsigned int outA =
                    sa + (da * (255 - sa) + 127) / 255;

                if (outA == 0)
                    continue;

                for (unsigned int channel = 0; channel < 3; ++channel)
                {
                    const unsigned int numerator =
                        static_cast<unsigned int>(frame[source + channel]) *
                            sa * 255 +
                        static_cast<unsigned int>(canvas[destination + channel]) *
                            da * (255 - sa);

                    canvas[destination + channel] =
                        static_cast<uint8_t>(
                            (numerator + outA * 127) / (outA * 255));
                }

                canvas[destination + 3] =
                    static_cast<uint8_t>(outA);
            }
        }
    }
    bool composeDecodedAPNGFrame(
        const APNGDecoderContext *decoder,
        const APNGAnimationInfo &animation,
        const APNGFrameReadState &state,
        std::vector<uint8_t> &canvas,
        std::vector<uint8_t> &previousCanvas)
    {
        if (!decoder || !state.decoded ||
            !state.hasFrameControl ||
            !decoder->buffers.framePixels)
            return false;

        if (canvas.size() != animation.canvasBytes)
            return false;

        const APNGFrameControl &frame = state.control;

        if (!validateAPNGFrameBounds(
                animation.canvasWidth,
                animation.canvasHeight,
                frame.width,
                frame.height,
                frame.xOffset,
                frame.yOffset))
            return false;

        if (decoder->buffers.frameBytes !=
            static_cast<size_t>(frame.width) *
            static_cast<size_t>(frame.height) * 4)
            return false;

        if (frame.disposeOp == PNG_DISPOSE_OP_PREVIOUS)
            previousCanvas = canvas;

        const std::vector<uint8_t> framePixels(
            decoder->buffers.framePixels,
            decoder->buffers.framePixels + decoder->buffers.frameBytes);

        compositeAPNGFrame(
            canvas,
            framePixels,
            animation.canvasWidth,
            frame.width,
            frame.height,
            frame.xOffset,
            frame.yOffset,
            frame.blendOp);

        return true;
    }
    bool storeComposedAPNGFrame(
        const APNGAnimationInfo &animation,
        const APNGFrameReadState &state,
        std::vector<uint8_t> &canvas,
        const std::vector<uint8_t> &previousCanvas,
        std::vector<std::vector<uint8_t>> &storedPixels,
        std::vector<double> &storedDurations)
    {
        if (!state.decoded || !state.hasFrameControl ||
            canvas.size() != animation.canvasBytes)
            return false;

        if (storedPixels.size() >= animation.frameCount ||
            storedPixels.size() != storedDurations.size())
            return false;

        const APNGFrameControl &frame = state.control;

        if (frame.durationMs <= 0.0)
            return false;

        if (frame.disposeOp == PNG_DISPOSE_OP_PREVIOUS &&
            previousCanvas.size() != animation.canvasBytes)
            return false;

        // Save the visible frame before applying disposal.
        storedPixels.push_back(canvas);
        storedDurations.push_back(frame.durationMs);

        disposeAPNGFrame(
            canvas,
            previousCanvas,
            animation.canvasWidth,
            frame.width,
            frame.height,
            frame.xOffset,
            frame.yOffset,
            frame.disposeOp);

        return true;
    }
    bool decodeAPNGAnimation(
        const std::vector<uint8_t> &bytes,
        std::vector<std::vector<uint8_t>> &storedPixels,
        std::vector<double> &storedDurations,
        unsigned int &canvasWidth,
        unsigned int &canvasHeight,
        unsigned int &loopCount)
    {
        storedPixels.clear();
        storedDurations.clear();
        canvasWidth = 0;
        canvasHeight = 0;
        loopCount = 0;

        if (bytes.empty())
            return false;

        APNGDecoderContext decoder = {};
        APNGReadContext reader = {
            bytes.data(), bytes.size(), 0
        };
        APNGAnimationInfo animation = {};
        int interlacePasses = 1;

        if (!initializeAPNGDecoder(&decoder, &reader))
            return false;

        bool success = readAPNGHeader(
            &decoder, animation, interlacePasses);

        if (!success)
            Logger::write(Logger::ZONE_WARNING, "APNG",
                "Decoder failed while reading animation header");

        if (success)
        {
            std::vector<uint8_t> canvas(
                animation.canvasBytes, 0);
            std::vector<uint8_t> previousCanvas;

            // The default PNG image is not always an animation frame.
            // When hidden, decode and discard it before the APNG frames.
            if (animation.firstFrameHidden)
            {
                APNGFrameReadState hiddenFrame;

                success = readProtectedAPNGFrame(
                    &decoder, animation, true,
                    interlacePasses, hiddenFrame);
            }

            for (png_uint_32 frameIndex = 0;
                 success && frameIndex < animation.frameCount;
                 ++frameIndex)
            {
                APNGFrameReadState frame;

                success = readProtectedAPNGFrame(
                    &decoder, animation, false,
                    interlacePasses, frame);

                if (!success)
                {
                    Logger::write(Logger::ZONE_WARNING, "APNG",
                        "Frame decode failed at index " +
                        std::to_string(frameIndex));
                    break;
                }

                success = composeDecodedAPNGFrame(
                    &decoder, animation, frame,
                    canvas, previousCanvas);

                if (!success)
                {
                    Logger::write(Logger::ZONE_WARNING, "APNG",
                        "Frame composition failed at index " +
                        std::to_string(frameIndex));
                    break;
                }

                success = storeComposedAPNGFrame(
                    animation, frame,
                    canvas, previousCanvas,
                    storedPixels, storedDurations);
            }

            if (success &&
                storedPixels.size() == animation.frameCount &&
                storedDurations.size() == animation.frameCount)
            {
                canvasWidth = animation.canvasWidth;
                canvasHeight = animation.canvasHeight;
                loopCount = animation.loopCount;
            }
            else
            {
                success = false;
            }
        }

        releaseAPNGDecoderContext(&decoder);

        if (!success)
        {
            storedPixels.clear();
            storedDurations.clear();
            canvasWidth = 0;
            canvasHeight = 0;
            loopCount = 0;
        }

        return success;
    }
    bool validateDecodedAPNGFrames(
        const std::vector<std::vector<uint8_t>> &pixels,
        const std::vector<double> &durations,
        unsigned int width,
        unsigned int height)
    {
        if (pixels.size() < 2 ||
            pixels.size() != durations.size() ||
            width == 0 || height == 0)
            return false;

        const size_t frameBytes =
            static_cast<size_t>(width) *
            static_cast<size_t>(height) * 4;

        for (size_t i = 0; i < pixels.size(); ++i)
        {
            if (pixels[i].size() != frameBytes ||
                !std::isfinite(durations[i]) ||
                durations[i] <= 0.0)
                return false;
        }

        return true;
    }
    bool isAnimatedPNG(const std::vector<uint8_t> &bytes)
    {
        static const uint8_t signature[8] =
            { 137, 80, 78, 71, 13, 10, 26, 10 };

        if (bytes.size() < 8 ||
            !std::equal(signature, signature + 8, bytes.begin()))
            return false;

        size_t offset = 8;

        while (offset <= bytes.size() &&
               bytes.size() - offset >= 12)
        {
            const uint32_t length =
                (static_cast<uint32_t>(bytes[offset]) << 24) |
                (static_cast<uint32_t>(bytes[offset + 1]) << 16) |
                (static_cast<uint32_t>(bytes[offset + 2]) << 8) |
                static_cast<uint32_t>(bytes[offset + 3]);

            if (static_cast<size_t>(length) > bytes.size() - offset - 12)
                return false;

            const uint8_t *type = bytes.data() + offset + 4;

            if (type[0] == 'a' && type[1] == 'c' &&
                type[2] == 'T' && type[3] == 'L')
                return length == 8;

            if ((type[0] == 'I' && type[1] == 'D' &&
                 type[2] == 'A' && type[3] == 'T') ||
                (type[0] == 'I' && type[1] == 'E' &&
                 type[2] == 'N' && type[3] == 'D'))
                return false;

            offset += static_cast<size_t>(length) + 12;
        }

        return false;
    }
    bool loadFileBytes(const std::string &file, std::vector<uint8_t> &bytes)
    {
        std::ifstream stream(file.c_str(), std::ios::binary | std::ios::ate);
        if (!stream)
            return false;

        const std::streamoff size = stream.tellg();
        if (size <= 0 || static_cast<unsigned long long>(size) >
            static_cast<unsigned long long>(std::numeric_limits<size_t>::max()))
            return false;

        bytes.resize(static_cast<size_t>(size));
        stream.seekg(0, std::ios::beg);

        return static_cast<bool>(
            stream.read(reinterpret_cast<char *>(bytes.data()),
                        static_cast<std::streamsize>(bytes.size())));
    }
}

Image::Image(std::string file, std::string altFile, Page &p, int monitor)
    : Component(p)
    , texture_(NULL)
    , file_(file)
    , altFile_(altFile)
    , webpDecoder_(NULL)
    , webpData_()
    , webpBytes_()
    , animationElapsedMs_(0.0)
    , nextFrameTimestampMs_(0)
    , animationTotalDurationMs_(0)
    , webpFrameDurations_()
    , currentWebPFrame_(0)
    , animationLoopCount_(0)
    , completedAnimationLoops_(0)
    , animatedWebP_(false)
    , apngFrames_()
    , currentAPNGFrame_(0)
    , apngElapsedMs_(0.0)
    , apngNextFrameMs_(0.0)
    , apngLoopCount_(0)
    , apngCompletedLoops_(0)
    , animatedAPNG_(false)
{
    baseViewInfo.Monitor = monitor;
    allocateGraphicsMemory();
}

Image::~Image()
{
    freeGraphicsMemory();
}

void Image::freeGraphicsMemory()
{
    Component::freeGraphicsMemory();

    SDL_LockMutex(SDL::getMutex());

    if (texture_ != NULL)
    {
        SDL_DestroyTexture(texture_);
        texture_ = NULL;
    }

    SDL_UnlockMutex(SDL::getMutex());

    if (webpDecoder_ != NULL)
    {
        WebPAnimDecoderDelete(webpDecoder_);
        webpDecoder_ = NULL;
    }

    webpData_.bytes = NULL;
    webpData_.size = 0;
    webpBytes_.clear();
    animationElapsedMs_ = 0.0;
    nextFrameTimestampMs_ = 0;
    animationTotalDurationMs_ = 0;
    webpFrameDurations_.clear();
    currentWebPFrame_ = 0;
    animationLoopCount_ = 0;
    completedAnimationLoops_ = 0;
    animatedWebP_ = false;
    apngFrames_.clear();
    currentAPNGFrame_ = 0;
    apngElapsedMs_ = 0.0;
    apngNextFrameMs_ = 0.0;
    apngLoopCount_ = 0;
    apngCompletedLoops_ = 0;
    animatedAPNG_ = false;
}

void Image::allocateGraphicsMemory()
{
    if (!texture_)
    {
        const std::string candidates[] = { file_, altFile_ };

        for (unsigned int candidate = 0; candidate < 2 && !texture_; ++candidate)
        {
            const std::string &path = candidates[candidate];

            if (path.empty())
                continue;

            // Animated PNG uses the native APNG decoder.
            // Ordinary PNG files continue through SDL2_image.
            if (path.size() >= 4 &&
                path.substr(path.size() - 4) == ".png")
            {
                std::vector<uint8_t> pngBytes;

                if (loadFileBytes(path, pngBytes) &&
                    isAnimatedPNG(pngBytes))
                {
                    std::vector<std::vector<uint8_t>> pixels;
                    std::vector<double> durations;
                    unsigned int width = 0;
                    unsigned int height = 0;
                    unsigned int loops = 0;

                    const bool decoded = decodeAPNGAnimation(
                        pngBytes, pixels, durations,
                        width, height, loops);

                    const bool valid = decoded &&
                        validateDecodedAPNGFrames(
                            pixels, durations, width, height);


                    if (valid)
                    {
                        SDL_LockMutex(SDL::getMutex());

                        SDL_Renderer *renderer =
                            SDL::getRenderer(baseViewInfo.Monitor);

                        texture_ = SDL_CreateTexture(
                            renderer,
                            SDL_PIXELFORMAT_RGBA32,
                            SDL_TEXTUREACCESS_STREAMING,
                            static_cast<int>(width),
                            static_cast<int>(height));

                        if (texture_)
                        {
                            if (SDL_UpdateTexture(
                                    texture_, NULL,
                                    pixels[0].data(),
                                    static_cast<int>(width * 4)) != 0)
                            {
                                SDL_DestroyTexture(texture_);
                                texture_ = NULL;
                            }
                            else
                            {
                                SDL_SetTextureBlendMode(
                                    texture_, SDL_BLENDMODE_BLEND);

                                baseViewInfo.ImageWidth =
                                    static_cast<float>(width);
                                baseViewInfo.ImageHeight =
                                    static_cast<float>(height);
                            }
                        }

                        SDL_UnlockMutex(SDL::getMutex());

                        if (texture_)
                        {
                            apngFrames_.clear();
                            apngFrames_.reserve(pixels.size());

                            for (size_t i = 0; i < pixels.size(); ++i)
                            {
                                APNGFrame frame;
                                frame.pixels.swap(pixels[i]);
                                frame.durationMs = durations[i];
                                apngFrames_.push_back(std::move(frame));
                            }

                            currentAPNGFrame_ = 0;
                            apngElapsedMs_ = 0.0;
                            apngNextFrameMs_ = durations[0];
                            apngLoopCount_ = static_cast<int>(loops);
                            apngCompletedLoops_ = 0;
                            animatedAPNG_ = true;
                        }
                    }
                }
            }
            if (isWebPFile(path) && loadFileBytes(path, webpBytes_))
            {
                webpData_.bytes = webpBytes_.data();
                webpData_.size = webpBytes_.size();

                WebPAnimDecoderOptions options;
                if (WebPAnimDecoderOptionsInit(&options))
                {
                    options.color_mode = MODE_RGBA;

                    webpDecoder_ = WebPAnimDecoderNew(&webpData_, &options);

                    if (webpDecoder_)
                    {
                        WebPAnimInfo info;

                        if (WebPAnimDecoderGetInfo(webpDecoder_, &info) &&
                            info.frame_count > 1 &&
                            info.canvas_width > 0 &&
                            info.canvas_height > 0 &&
                            WebPAnimDecoderHasMoreFrames(webpDecoder_))
                        {
                            const WebPDemuxer *demux =
                                WebPAnimDecoderGetDemuxer(webpDecoder_);

                            WebPIterator iterator;

                            if (demux && WebPDemuxGetFrame(demux, 1, &iterator))
                            {
                                do
                                {
                                    webpFrameDurations_.push_back(iterator.duration);
                                    animationTotalDurationMs_ += iterator.duration;
                                }
                                while (WebPDemuxNextFrame(&iterator));

                                WebPDemuxReleaseIterator(&iterator);
                            }

                            const bool validDurations =
                                webpFrameDurations_.size() ==
                                    static_cast<size_t>(info.frame_count) &&
                                animationTotalDurationMs_ > 0 &&
                                std::all_of(
                                    webpFrameDurations_.begin(),
                                    webpFrameDurations_.end(),
                                    [](int duration) { return duration > 0; });

                            uint8_t *pixels = NULL;
                            int timestamp = 0;

                            if (validDurations && WebPAnimDecoderGetNext(
                                    webpDecoder_, &pixels, &timestamp))
                            {
                                SDL_LockMutex(SDL::getMutex());

                                SDL_Renderer *renderer =
                                    SDL::getRenderer(baseViewInfo.Monitor);

                                texture_ = SDL_CreateTexture(
                                    renderer,
                                    SDL_PIXELFORMAT_RGBA32,
                                    SDL_TEXTUREACCESS_STREAMING,
                                    info.canvas_width,
                                    info.canvas_height);

                                if (texture_)
                                {
                                    if (SDL_UpdateTexture(
                                            texture_, NULL, pixels,
                                            info.canvas_width * 4) != 0)
                                    {
                                        SDL_DestroyTexture(texture_);
                                        texture_ = NULL;
                                    }
                                    else
                                    {
                                        SDL_SetTextureBlendMode(
                                            texture_, SDL_BLENDMODE_BLEND);

                                        baseViewInfo.ImageWidth =
                                            static_cast<float>(info.canvas_width);
                                        baseViewInfo.ImageHeight =
                                            static_cast<float>(info.canvas_height);

                                        nextFrameTimestampMs_ = timestamp + webpFrameDurations_[0];
                                        animationLoopCount_ = info.loop_count;
                                        animatedWebP_ = true;
                                    }
                                }

                                SDL_UnlockMutex(SDL::getMutex());
                            }
                        }

                        if (!animatedWebP_)
                        {
                            WebPAnimDecoderDelete(webpDecoder_);
                            webpDecoder_ = NULL;
                        }
                    }
                }

                if (!animatedWebP_)
                {
                    webpData_.bytes = NULL;
                    webpData_.size = 0;
                    webpBytes_.clear();
                }
            }

            if (!texture_)
            {
                SDL_LockMutex(SDL::getMutex());

                texture_ = IMG_LoadTexture(
                    SDL::getRenderer(baseViewInfo.Monitor), path.c_str());

                if (texture_)
                {
                    int width = 0;
                    int height = 0;

                    SDL_SetTextureBlendMode(texture_, SDL_BLENDMODE_BLEND);
                    SDL_QueryTexture(texture_, NULL, NULL, &width, &height);

                    baseViewInfo.ImageWidth = static_cast<float>(width);
                    baseViewInfo.ImageHeight = static_cast<float>(height);
                }

                SDL_UnlockMutex(SDL::getMutex());
            }
        }
    }

    Component::allocateGraphicsMemory();
}

void Image::update(float dt)
{
    Component::update(dt);

    if (animatedAPNG_ && texture_ && dt > 0.0f &&
        !apngFrames_.empty())
    {
        apngElapsedMs_ += static_cast<double>(dt) * 1000.0;

        unsigned int framesAdvanced = 0;
        const unsigned int maxFramesPerUpdate = 100;

        while (framesAdvanced < maxFramesPerUpdate &&
               apngElapsedMs_ >= apngNextFrameMs_)
        {
            size_t nextFrame = currentAPNGFrame_ + 1;

            if (nextFrame >= apngFrames_.size())
            {
                ++apngCompletedLoops_;

                if (apngLoopCount_ > 0 &&
                    apngCompletedLoops_ >= apngLoopCount_)
                {
                    animatedAPNG_ = false;
                    break;
                }

                nextFrame = 0;
            }

            int width = 0;
            int height = 0;

            SDL_LockMutex(SDL::getMutex());

            SDL_QueryTexture(
                texture_, NULL, NULL, &width, &height);

            const int result = SDL_UpdateTexture(
                texture_, NULL,
                apngFrames_[nextFrame].pixels.data(),
                width * 4);

            SDL_UnlockMutex(SDL::getMutex());

            if (result != 0)
            {
                animatedAPNG_ = false;
                break;
            }

            currentAPNGFrame_ = nextFrame;
            apngNextFrameMs_ +=
                apngFrames_[currentAPNGFrame_].durationMs;

            ++framesAdvanced;
        }
    }

    if (!animatedWebP_ || !webpDecoder_ || !texture_ || dt <= 0.0f)
        return;

    animationElapsedMs_ += static_cast<double>(dt) * 1000.0;

    unsigned int framesAdvanced = 0;
    const unsigned int maxFramesPerUpdate = 100;

    while (framesAdvanced < maxFramesPerUpdate &&
           animationElapsedMs_ >= nextFrameTimestampMs_)
    {
        if (!WebPAnimDecoderHasMoreFrames(webpDecoder_))
        {
            ++completedAnimationLoops_;

            if (animationLoopCount_ > 0 &&
                completedAnimationLoops_ >= animationLoopCount_)
            {
                animatedWebP_ = false;
                break;
            }

            WebPAnimDecoderReset(webpDecoder_);
            currentWebPFrame_ = 0;
        }
        else
        {
            ++currentWebPFrame_;
        }

        uint8_t *pixels = NULL;
        int timestamp = 0;

        if (!WebPAnimDecoderGetNext(webpDecoder_, &pixels, &timestamp))
        {
            animatedWebP_ = false;
            break;
        }

        WebPAnimInfo info;

        if (!WebPAnimDecoderGetInfo(webpDecoder_, &info))
        {
            animatedWebP_ = false;
            break;
        }

        if (currentWebPFrame_ >= webpFrameDurations_.size())
        {
            animatedWebP_ = false;
            break;
        }

        SDL_LockMutex(SDL::getMutex());

        const int result = SDL_UpdateTexture(
            texture_, NULL, pixels, info.canvas_width * 4);

        SDL_UnlockMutex(SDL::getMutex());

        if (result != 0)
        {
            animatedWebP_ = false;
            break;
        }

        if (currentWebPFrame_ == 0)
        {
            nextFrameTimestampMs_ =
                completedAnimationLoops_ * animationTotalDurationMs_ +
                webpFrameDurations_[0];
        }
        else
        {
            nextFrameTimestampMs_ =
                completedAnimationLoops_ * animationTotalDurationMs_ +
                timestamp + webpFrameDurations_[currentWebPFrame_];
        }

        ++framesAdvanced;
    }
}
void Image::draw()
{
    Component::draw();

    if (texture_)
    {
        SDL_Rect rect;

        rect.x = static_cast<int>(baseViewInfo.XRelativeToOrigin());
        rect.y = static_cast<int>(baseViewInfo.YRelativeToOrigin());
        rect.h = static_cast<int>(baseViewInfo.ScaledHeight());
        rect.w = static_cast<int>(baseViewInfo.ScaledWidth());

        SDL::renderCopy(
            texture_,
            baseViewInfo.Alpha,
            NULL,
            &rect,
            baseViewInfo,
            page.getLayoutWidth(baseViewInfo.Monitor),
            page.getLayoutHeight(baseViewInfo.Monitor));
    }
}