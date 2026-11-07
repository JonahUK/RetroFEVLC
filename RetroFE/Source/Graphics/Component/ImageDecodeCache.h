#pragma once

#include <SDL2/SDL.h>
#include <cstddef>
#include <string>

class ImageDecodeCache
{
public:
    static ImageDecodeCache &instance();

    // Queue an image for background decoding.
    void request(const std::string &path);

    // Take ownership of a prepared surface, if available.
    // The caller must release it with SDL_FreeSurface().
    SDL_Surface *take(const std::string &path);

    // Limit memory retained by decoded images.
    void setMemoryLimit(std::size_t bytes);

    // Stop background processing and release cached surfaces.
    void stop();
    void resume();
    void shutdown();

private:
    ImageDecodeCache();
    ~ImageDecodeCache();

    ImageDecodeCache(const ImageDecodeCache &) = delete;
    ImageDecodeCache &operator=(const ImageDecodeCache &) = delete;
};