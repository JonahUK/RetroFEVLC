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
#include "ImageDecodeCache.h"
#include "../ViewInfo.h"
#include "../../SDL.h"
#include "../../Utility/Log.h"
#include <SDL2/SDL_image.h>
#include <sstream>
#include <algorithm>
#include <chrono>
#include <cctype>

Image::Image(std::string file, std::string altFile, Page &p, int monitor)
    : Component(p)
    , texture_(NULL)
    , file_(file)
    , altFile_(altFile)
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
}

void Image::allocateGraphicsMemory()
{
    int width;
    int height;

    if(!texture_)
    {
        SDL_LockMutex(SDL::getMutex());
        static unsigned long long streamImageLoads = 0;
        static unsigned long long streamImageHits = 0;
        static unsigned long long streamImageLoadUs = 0;

        const auto streamImageStart = std::chrono::steady_clock::now();

        texture_ = loadTexture();

        if (texture_ != NULL)
        {
            SDL_SetTextureBlendMode(texture_, SDL_BLENDMODE_BLEND);
            SDL_QueryTexture(texture_, NULL, NULL, &width, &height);
            baseViewInfo.ImageWidth  = (float)width;
            baseViewInfo.ImageHeight = (float)height;
        }
        SDL_UnlockMutex(SDL::getMutex());

        const auto streamImageEnd = std::chrono::steady_clock::now();

        ++streamImageLoads;

        if (texture_)
            ++streamImageHits;

        streamImageLoadUs += static_cast<unsigned long long>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                streamImageEnd - streamImageStart).count());

        if (streamImageLoads % 25 == 0)
        {
            Logger::write(Logger::ZONE_INFO, "STREAM_PERF",
                "STREAM_PERF image_load" +
                std::string(" loads=") + std::to_string(streamImageLoads) +
                " hits=" + std::to_string(streamImageHits) +
                " total_ms=" + std::to_string(streamImageLoadUs / 1000));
        }

    }

    Component::allocateGraphicsMemory();

}


SDL_Texture *Image::loadTexture()
{
    SDL_Renderer *renderer = SDL::getRenderer(baseViewInfo.Monitor);
    ImageDecodeCache &cache = ImageDecodeCache::instance();

    // Use an image decoded by the background worker, if ready.
    SDL_Surface *surface = cache.take(file_);

    if (surface)
    {
        SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
        SDL_FreeSurface(surface);

        if (texture)
            return texture;
    }

    // Preserve the original synchronous loading behaviour on a cache miss.
    SDL_Texture *texture = IMG_LoadTexture(renderer, file_.c_str());

    if (!texture && !altFile_.empty())
    {
        surface = cache.take(altFile_);

        if (surface)
        {
            texture = SDL_CreateTextureFromSurface(renderer, surface);
            SDL_FreeSurface(surface);
        }

        if (!texture)
            texture = IMG_LoadTexture(renderer, altFile_.c_str());
    }

    return texture;
}

void Image::draw()
{
    Component::draw();

    if(texture_)
    {
        SDL_Rect rect;

        rect.x = static_cast<int>(baseViewInfo.XRelativeToOrigin());
        rect.y = static_cast<int>(baseViewInfo.YRelativeToOrigin());
        rect.h = static_cast<int>(baseViewInfo.ScaledHeight());
        rect.w = static_cast<int>(baseViewInfo.ScaledWidth());

        SDL::renderCopy(texture_, baseViewInfo.Alpha, NULL, &rect, baseViewInfo, page.getLayoutWidth(baseViewInfo.Monitor), page.getLayoutHeight(baseViewInfo.Monitor));
    }
}
