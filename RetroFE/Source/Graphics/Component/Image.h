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
#pragma once

#include "Component.h"
#include <SDL2/SDL.h>
#include <string>
#include <vector>
#include <webp/demux.h>

class Image : public Component
{
public:
    Image(std::string file, std::string altFile, Page &p, int monitor);
    virtual ~Image();
    void freeGraphicsMemory();
    void allocateGraphicsMemory();
    void draw();
    void update(float dt) override;

protected:
    SDL_Texture *texture_;
    std::string  file_;
    std::string  altFile_;
    WebPAnimDecoder *webpDecoder_;
    WebPData webpData_;
    std::vector<uint8_t> webpBytes_;
    double animationElapsedMs_;
    int nextFrameTimestampMs_;
    int animationTotalDurationMs_;
    std::vector<int> webpFrameDurations_;
    size_t currentWebPFrame_;
    int animationLoopCount_;
    int completedAnimationLoops_;
    bool animatedWebP_;
    struct APNGFrame
    {
        std::vector<uint8_t> pixels;
        double durationMs;
    };
    std::vector<APNGFrame> apngFrames_;
    size_t currentAPNGFrame_;
    double apngElapsedMs_;
    double apngNextFrameMs_;
    int apngLoopCount_;
    int apngCompletedLoops_;
    bool animatedAPNG_;
};
