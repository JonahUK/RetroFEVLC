#include "ImageDecodeCache.h"

#include <SDL2/SDL_image.h>

#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace
{
    struct CachedSurface
    {
        SDL_Surface *surface = nullptr;
        std::size_t bytes = 0;
    };

    struct CacheState
    {
        std::mutex mutex;
        std::mutex lifecycleMutex;
        std::condition_variable condition;
        std::thread worker;

        std::deque<std::string> pending;
        std::unordered_set<std::string> scheduled;
        std::unordered_map<std::string, CachedSurface> ready;
        std::deque<std::string> readyOrder;

        std::size_t memoryLimit = 64u * 1024u * 1024u;
        std::size_t memoryUsed = 0;

        bool stopping = false;
        bool terminated = false;
        bool started = false;
    };

    CacheState &state()
    {
        static CacheState cache;
        return cache;
    }

    std::size_t surfaceBytes(SDL_Surface *surface)
    {
        if (!surface || surface->pitch <= 0 || surface->h <= 0)
            return 0;

        return static_cast<std::size_t>(surface->pitch) *
               static_cast<std::size_t>(surface->h);
    }

    void evictOldest(CacheState &cache)
    {
        while (!cache.readyOrder.empty())
        {
            const std::string path = cache.readyOrder.front();
            cache.readyOrder.pop_front();

            auto found = cache.ready.find(path);
            if (found == cache.ready.end())
                continue;

            cache.memoryUsed -= found->second.bytes;
            SDL_FreeSurface(found->second.surface);
            cache.ready.erase(found);
            return;
        }
    }

    void workerMain()
    {
        CacheState &cache = state();

        for (;;)
        {
            std::string path;

            {
                std::unique_lock<std::mutex> lock(cache.mutex);

                cache.condition.wait(lock, [&cache]() {
                    return cache.stopping || !cache.pending.empty();
                });

                if (cache.stopping)
                    return;

                path = cache.pending.front();
                cache.pending.pop_front();
            }

            SDL_Surface *surface = IMG_Load(path.c_str());
            const std::size_t bytes = surfaceBytes(surface);

            std::lock_guard<std::mutex> lock(cache.mutex);

            cache.scheduled.erase(path);

            if (cache.stopping || !surface ||
                bytes == 0 || bytes > cache.memoryLimit)
            {
                if (surface)
                    SDL_FreeSurface(surface);
                continue;
            }

            while (cache.memoryUsed + bytes > cache.memoryLimit &&
                   !cache.ready.empty())
            {
                evictOldest(cache);
            }

            auto existing = cache.ready.find(path);

            if (existing != cache.ready.end())
            {
                SDL_FreeSurface(surface);
                continue;
            }

            cache.ready[path] = { surface, bytes };
            cache.readyOrder.push_back(path);
            cache.memoryUsed += bytes;
        }
    }
}

ImageDecodeCache &ImageDecodeCache::instance()
{
    static ImageDecodeCache cache;
    return cache;
}

ImageDecodeCache::ImageDecodeCache()
{
    // Construct the shared state before the cache singleton is destroyed.
    (void)state();
}

ImageDecodeCache::~ImageDecodeCache()
{
    shutdown();
}

void ImageDecodeCache::request(const std::string &path)
{
    if (path.empty())
        return;

    CacheState &cache = state();
    std::lock_guard<std::mutex> lock(cache.mutex);

    if (cache.stopping || cache.terminated || cache.ready.count(path) ||
        cache.scheduled.count(path))
        return;

    // Avoid unbounded pending work.
    if (cache.pending.size() >= 32)
        return;

    if (!cache.started)
    {
        cache.started = true;
        cache.worker = std::thread(workerMain);
    }

    cache.scheduled.insert(path);
    cache.pending.push_back(path);
    cache.condition.notify_one();
}

SDL_Surface *ImageDecodeCache::take(const std::string &path)
{
    CacheState &cache = state();
    std::lock_guard<std::mutex> lock(cache.mutex);

    // Shutdown is terminal; do not transfer cached surfaces afterwards.
    if (cache.stopping || cache.terminated)
        return nullptr;

    auto found = cache.ready.find(path);
    if (found == cache.ready.end())
        return nullptr;

    SDL_Surface *surface = found->second.surface;
    cache.memoryUsed -= found->second.bytes;
    cache.ready.erase(found);

    // Remove the corresponding eviction-order entry.
    for (auto it = cache.readyOrder.begin();
         it != cache.readyOrder.end(); ++it)
    {
        if (*it == path)
        {
            cache.readyOrder.erase(it);
            break;
        }
    }

    return surface;
}

void ImageDecodeCache::setMemoryLimit(std::size_t bytes)
{
    CacheState &cache = state();
    std::lock_guard<std::mutex> lock(cache.mutex);

    cache.memoryLimit = bytes;

    while (cache.memoryUsed > cache.memoryLimit &&
           !cache.ready.empty())
    {
        evictOldest(cache);
    }
}

void ImageDecodeCache::stop()
{
    CacheState &cache = state();

    // Serialize stop operations while the worker is being joined.
    std::lock_guard<std::mutex> lifecycleLock(cache.lifecycleMutex);

    {
        std::lock_guard<std::mutex> lock(cache.mutex);
        cache.stopping = true;
        cache.condition.notify_all();
    }

    // Wait without holding the cache mutex.
    if (cache.worker.joinable())
        cache.worker.join();

    std::lock_guard<std::mutex> lock(cache.mutex);

    for (auto &entry : cache.ready)
        SDL_FreeSurface(entry.second.surface);

    cache.ready.clear();
    cache.readyOrder.clear();
    cache.pending.clear();
    cache.scheduled.clear();
    cache.memoryUsed = 0;

    // Remain paused until SDL explicitly resumes the cache.
    cache.started = false;
    cache.stopping = true;
}


void ImageDecodeCache::resume()
{
    CacheState &cache = state();

    // Serialize resume against stop and worker joining.
    std::lock_guard<std::mutex> lifecycleLock(cache.lifecycleMutex);
    std::lock_guard<std::mutex> lock(cache.mutex);

    if (!cache.terminated)
        cache.stopping = false;
}
void ImageDecodeCache::shutdown()
{
    CacheState &cache = state();

    {
        std::lock_guard<std::mutex> lock(cache.mutex);
        cache.terminated = true;
        cache.stopping = true;
        cache.condition.notify_all();
    }

    stop();
}