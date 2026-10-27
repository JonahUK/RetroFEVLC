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


#include "RetroFE.h"
#include "Collection/CollectionInfoBuilder.h"
#include "Collection/CollectionInfo.h"
#include "Database/Configuration.h"
#include "Collection/Item.h"
#include "Execute/Launcher.h"
#include "Menu/Menu.h"
#include "Utility/Log.h"
#include "Utility/Utils.h"
#include "Collection/MenuParser.h"
#include "SDL.h"
#include "Control/UserInput.h"
#include "Graphics/PageBuilder.h"
#include "Graphics/Page.h"
#include "Graphics/Component/ScrollingList.h"
#include "Graphics/Component/Video.h"
#include "Video/VideoFactory.h"
#include <algorithm>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>
#include <SDL2/SDL_ttf.h>

#if defined(__linux) || defined(__APPLE__)
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <cstring>
#endif

#ifdef WIN32
#include <Windows.h>
#include <SDL2/SDL_syswm.h>
#include <SDL2/SDL_thread.h>
#endif


RetroFE::RetroFE( Configuration &c )
    : initialized(false)
    , initializeError(false)
    , initializeThread(NULL)
    , config_(c)
    , db_(NULL)
    , metadb_(NULL)
    , input_(config_)
    , currentPage_(NULL)
    , keyInputDisable_(0)
    , currentTime_(0)
    , lastLaunchReturnTime_(0)
    , keyLastTime_(0)
    , keyDelayTime_(.3f)
    , reboot_(false)
{
    menuMode_                            = false;
    attractMode_                         = false;
    attractModePlaylistCollectionNumber_ = 0;
    firstPlaylist_                       = "all";
    searchJumpIndex_                     = -1;
    searchPendingItem_                   = NULL;
    searchPage_                         = NULL;
    searchLaunchItem_                   = NULL;
    searchFadeAlpha_ = 0;
    searchTargetItem_                   = NULL;
}


RetroFE::~RetroFE( )
{
    deInitialize( );
}


// Render the current page to the screen
void RetroFE::render( )
{

    SDL_LockMutex( SDL::getMutex( ) );
    for ( int i = 0; i < SDL::getNumDisplays( ); ++i )
    {
        SDL_SetRenderDrawColor( SDL::getRenderer( i ), 0x0, 0x0, 0x00, 0xFF );
        SDL_RenderClear( SDL::getRenderer( i ) );
    }

    if ( currentPage_ )
    {
        currentPage_->draw( );
    }

    // Search overlay (pop-up keyboard + results) is drawn above the page
    if ( ( search_.isActive( ) || search_.hasStatus( ) ) && SDL::getNumDisplays( ) > 0 )
    {
        search_.draw( SDL::getRenderer( 0 ) );
    }

    // List Search launch: fade to black above all page and search graphics.
    if ( searchFadeAlpha_ > 0 )
    {
        for ( int i = 0; i < SDL::getNumDisplays( ); ++i )
        {
            SDL_Renderer *renderer = SDL::getRenderer( i );
            SDL_BlendMode previousBlend;
            SDL_GetRenderDrawBlendMode( renderer, &previousBlend );
            SDL_SetRenderDrawBlendMode( renderer, SDL_BLENDMODE_BLEND );
            SDL_SetRenderDrawColor( renderer, 0, 0, 0, static_cast<Uint8>( searchFadeAlpha_ ) );
            SDL_RenderFillRect( renderer, NULL );
            SDL_SetRenderDrawBlendMode( renderer, previousBlend );
        }
    }

    for ( int i = 0; i < SDL::getNumDisplays( ); ++i )
    {
        SDL_RenderPresent( SDL::getRenderer( i ) );
    }
    SDL_UnlockMutex( SDL::getMutex( ) );

}


// Initialize the configuration and database
int RetroFE::initialize( void *context )
{

    RetroFE *instance = static_cast<RetroFE *>(context);

    Logger::write( Logger::ZONE_INFO, "RetroFE", "Initializing" );

    if ( !instance->input_.initialize( ) )
    {
        Logger::write( Logger::ZONE_ERROR, "RetroFE", "Could not initialize user controls" );
        instance->initializeError = true;
        return -1;
    }

    instance->db_ = new DB( Utils::combinePath( Configuration::absolutePath, "meta.db" ) );

    if ( !instance->db_->initialize( ) )
    {
        Logger::write( Logger::ZONE_ERROR, "RetroFE", "Could not initialize database" );
        instance->initializeError = true;
        return -1;
    }

    instance->metadb_ = new MetadataDatabase( *(instance->db_), instance->config_ );

    if ( !instance->metadb_->initialize( ) )
    {
        Logger::write( Logger::ZONE_ERROR, "RetroFE", "Could not initialize meta database" );
        instance->initializeError = true;
        return -1;
    }

    instance->initialized = true;
    return 0;

}


// Launch a game/program
void RetroFE::launchEnter( )
{

    // Disable window focus
    SDL_SetWindowGrab(SDL::getWindow( 0 ), SDL_FALSE);

    // Free the textures, and optionally take down SDL
    freeGraphicsMemory( );

    bool hideMouse = false;
    int  mouseX    = 5000;
    int  mouseY    = 5000;
    config_.getProperty( "hideMouse", hideMouse );
    config_.getProperty( "mouseX",    mouseX );
    config_.getProperty( "mouseY",    mouseY );
    if ( hideMouse )
        SDL_WarpMouseGlobal( mouseX, mouseY );
}


// Return from the launch of a game/program
void RetroFE::launchExit( )
{

    // Optionally set up SDL, and load the textures
    allocateGraphicsMemory( );

    // Restore the SDL settings
    SDL_RestoreWindow( SDL::getWindow( 0 ) );
    SDL_RaiseWindow( SDL::getWindow( 0 ) );
    SDL_SetWindowGrab( SDL::getWindow( 0 ), SDL_TRUE );

    // Empty event queue, but handle joystick add/remove events
    SDL_Event e;
    while ( SDL_PollEvent( &e ) )
    {
        if ( e.type == SDL_JOYDEVICEADDED || e.type == SDL_JOYDEVICEREMOVED )
        {
            input_.update( e );
        }
    }
    input_.resetStates( );
    attract_.reset( );

    // Restore time settings
    currentTime_ = static_cast<float>( SDL_GetTicks( ) ) / 1000;
    keyLastTime_ = currentTime_;
    lastLaunchReturnTime_ = currentTime_;

    bool hideMouse = false;
    int  mouseX    = 5000;
    int  mouseY    = 5000;
    config_.getProperty( "hideMouse", hideMouse );
    config_.getProperty( "mouseX",    mouseX );
    config_.getProperty( "mouseY",    mouseY );
    if ( hideMouse )
        SDL_WarpMouseGlobal( mouseX, mouseY );

}


// Free the textures, and optionall take down SDL
void RetroFE::freeGraphicsMemory( )
{

    // Free textures
    if ( currentPage_ )
    {
        currentPage_->freeGraphicsMemory( );
    }

    // Close down SDL
    bool unloadSDL = false;
    config_.getProperty( "unloadSDL", unloadSDL );
    if ( unloadSDL )
    {
        currentPage_->deInitializeFonts( );
        SDL::deInitialize( );
        input_.clearJoysticks( );
    }

}


// Optionally set up SDL, and load the textures
void RetroFE::allocateGraphicsMemory( )
{

    // Reopen SDL
    bool unloadSDL = false;
    config_.getProperty( "unloadSDL", unloadSDL );
    if ( unloadSDL )
    {
        SDL::initialize( config_ );
        currentPage_->initializeFonts( );
    }

    // Allocate textures
    if ( currentPage_ )
    {
        currentPage_->allocateGraphicsMemory( );
    }

}


// Deinitialize RetroFE
bool RetroFE::deInitialize( )
{

    bool retVal = true;

    // Free textures
    freeGraphicsMemory( );

    // Delete page
    if ( currentPage_ )
    {
        currentPage_->deInitialize( );
        delete currentPage_;
        currentPage_ = NULL;
    }

    // Delete the collections cached for the search overlay
    for ( std::map<std::string, CollectionInfo *>::iterator it = searchCollections_.begin( ); it != searchCollections_.end( ); ++it )
    {
        delete it->second;
    }
    searchCollections_.clear( );
    if ( searchPage_ )
    {
        searchPage_->playlists.clear( );
        searchPage_->ownsItems = false;
        delete searchPage_;
        searchPage_ = NULL;
    }
    if ( searchTargetItem_ )
    {
        delete searchTargetItem_;
        searchTargetItem_ = NULL;
    }
    searchPendingItem_ = NULL;

    // Delete databases
    if ( metadb_ )
    {
        delete metadb_;
        metadb_ = NULL;
    }

    if ( db_ )
    {
        delete db_;
        db_ = NULL;
    }

    initialized = false;

    if ( reboot_ )
    {
        VideoFactory::releaseSharedVideo();
        Logger::write( Logger::ZONE_INFO, "RetroFE", "Rebooting" );
    }
    else
    {
        Logger::write( Logger::ZONE_INFO, "RetroFE", "Exiting" );
        SDL::deInitialize( );
        // LibVLC instances are released by their video owners.
    }

    return retVal;
}


// Run RetroFE
bool RetroFE::run( )
{

    // Initialize SDL
    if(! SDL::initialize( config_ ) ) return false;
    fontcache_.initialize( );

    // Define control configuration
    std::string controlsConfPath = Utils::combinePath( Configuration::absolutePath, "controls.conf" );
    if ( !config_.import( "controls", controlsConfPath ) )
    {
        Logger::write( Logger::ZONE_ERROR, "RetroFE", "Could not import \"" + controlsConfPath + "\"" );
        return false;
    }

    float preloadTime = 0;

    // Initialize video
    bool videoEnable = true;
    int  videoLoop   = 0;
    config_.getProperty( "videoEnable", videoEnable );
    config_.getProperty( "videoLoop", videoLoop );
    VideoFactory::setEnabled( videoEnable );
    VideoFactory::setNumLoops( videoLoop );
    Video::setEnabled( videoEnable );

    initializeThread = SDL_CreateThread( initialize, "RetroFEInit", (void *)this );

    if ( !initializeThread )
    {
        Logger::write( Logger::ZONE_INFO, "RetroFE", "Could not initialize RetroFE" );
        return false;
    }

    int attractModeTime           = 0;
    int attractModeNextTime       = 0;
    int attractModePlaylistTime   = 0;
    int attractModeCollectionTime = 0;
    int attractModeMinTime        = 1000;
    int attractModeMaxTime        = 5000;
    std::string firstCollection   = "Main";
    bool running                  = true;
    RETROFE_STATE state           = RETROFE_NEW;

    config_.getProperty( "attractModeTime", attractModeTime );
    config_.getProperty( "attractModeNextTime", attractModeNextTime );
    config_.getProperty( "attractModePlaylistTime", attractModePlaylistTime );
    config_.getProperty( "attractModeCollectionTime", attractModeCollectionTime );
    config_.getProperty( "attractModeMinTime", attractModeMinTime );
    config_.getProperty( "attractModeMaxTime", attractModeMaxTime );
    config_.getProperty( "firstCollection", firstCollection );

    attract_.idleTime           = static_cast<float>(attractModeTime);
    attract_.idleNextTime       = static_cast<float>(attractModeNextTime);
    attract_.idlePlaylistTime   = static_cast<float>(attractModePlaylistTime);
    attract_.idleCollectionTime = static_cast<float>(attractModeCollectionTime);
    attract_.minTime            = attractModeMinTime;
    attract_.maxTime            = attractModeMaxTime;

    int fps     = 60;
    int fpsIdle = 60;
    config_.getProperty( "fps", fps );
    config_.getProperty( "fpsIdle", fpsIdle );
    double fpsTime     = 1000.0 / static_cast<double>(fps);
    double fpsIdleTime = 1000.0 / static_cast<double>(fpsIdle);

    int initializeStatus = 0;
    bool inputClear      = false;

    // load the initial splash screen, unload it once it is complete
    currentPage_        = loadSplashPage( );
    state               = RETROFE_ENTER;
    bool splashMode     = true;
    bool exitSplashMode = false;

    Launcher l( config_ );
    Menu     m( config_, input_ );
    preloadTime = static_cast<float>( SDL_GetTicks( ) ) / 1000;
    l.LEDBlinky( 1 );

    while ( running )
    {

        float lastTime = 0;
        float deltaTime = 0;

        // Exit splash mode when an active key is pressed
        SDL_Event e;
        if ( splashMode && SDL_PollEvent( &e ) )
        {
            if ( input_.update( e ) && input_.keystate(UserInput::KeyCodeSelect) )
            {
                exitSplashMode = true;
                while ( SDL_PollEvent( &e ) )
                {
                    if ( e.type == SDL_JOYDEVICEADDED || e.type == SDL_JOYDEVICEREMOVED )
                    {
                        input_.update( e );
                    }
                }
                input_.resetStates( );
                attract_.reset( );
            }
        }

        if ( !currentPage_ )
        {
            Logger::write( Logger::ZONE_WARNING, "RetroFE", "Could not load page"  );
            running = false;
            break;
        }

        switch(state)
        {

        // Idle state; waiting for input
        case RETROFE_IDLE:

            currentPage_->cleanup( );

            // Not in splash mode
            if ( currentPage_ && !splashMode )
            {
                // account for when returning from a menu and the previous key was still "stuck"
                if ( lastLaunchReturnTime_ == 0 || (currentTime_ - lastLaunchReturnTime_ > .3) )
                {
                    if ( currentPage_->isIdle( ) )
                    {
                        state = processUserInput( currentPage_ );
                    }
                    lastLaunchReturnTime_ = 0;
                }
            }

            // Handle end of splash mode
            if ( (initialized || initializeError) && splashMode && (exitSplashMode || (currentPage_->getMinShowTime( ) <= (currentTime_ - preloadTime) && !(currentPage_->isPlaying( )))) )
            {
                SDL_WaitThread( initializeThread, &initializeStatus );

                if ( initializeError )
                {
                    state = RETROFE_QUIT_REQUEST;
                    break;
                }

                currentPage_->stop( );
                state = RETROFE_SPLASH_EXIT;

            }

            break;

        // Load art on entering RetroFE
        case RETROFE_LOAD_ART:
            currentPage_->start( );
            state = RETROFE_ENTER;
            break;

        // Wait for onEnter animation to finish
        case RETROFE_ENTER:
            if ( currentPage_->isIdle( ) )
            {
                bool startCollectionEnter = false;
                config_.getProperty( "startCollectionEnter", startCollectionEnter );
                nextPageItem_ = currentPage_->getSelectedItem( );
                if ( !splashMode && startCollectionEnter && !nextPageItem_->leaf )
                {
                    state = RETROFE_NEXT_PAGE_REQUEST;
                }
                else
                {
                    state = RETROFE_IDLE;
                }
            }
            break;

        // Handle end of splash mode
        case RETROFE_SPLASH_EXIT:
            if ( currentPage_->isIdle( ) )
            {
                // delete the splash screen and use the standard menu
                currentPage_->deInitialize( );
                delete currentPage_;

                currentPage_ = loadPage( );
                splashMode = false;
                if ( currentPage_ )
                {
                    std::string firstCollection = "Main";

                    config_.getProperty( "firstCollection", firstCollection );
                    config_.setProperty( "currentCollection", firstCollection );
                    CollectionInfo *info = getCollection(firstCollection);

                    currentPage_->pushCollection(info);

                    config_.getProperty( "firstPlaylist", firstPlaylist_ );
                    currentPage_->selectPlaylist( firstPlaylist_ );
                    if (currentPage_->getPlaylistName() != firstPlaylist_ )
                        currentPage_->selectPlaylist( "all" );

                    currentPage_->onNewItemSelected( );
                    currentPage_->reallocateMenuSpritePoints( );

                    state = RETROFE_LOAD_ART;
                }
                else
                {
                    state = RETROFE_QUIT_REQUEST;
                }
            }
            break;

        // Switch playlist; start onHighlightExit animation
        case RETROFE_PLAYLIST_REQUEST:
            inputClear = false;
            config_.getProperty( "playlistInputClear", inputClear );
            if (  inputClear  )
            {
                // Empty event queue
                SDL_Event e;
                while ( SDL_PollEvent( &e ) )
                    input_.update(e);
                input_.resetStates( );
            }
            currentPage_->playlistExit( );
            currentPage_->setScrolling(Page::ScrollDirectionIdle);
            state = RETROFE_PLAYLIST_EXIT;
            break;

        // Switch playlist; wait for onHighlightExit animation to finish; load art
        case RETROFE_PLAYLIST_EXIT:
            if (currentPage_->isIdle( ))
            {
                currentPage_->onNewItemSelected( );
                state = RETROFE_PLAYLIST_LOAD_ART;
            }
            break;

        // Switch playlist; start onHighlightEnter animation
        case RETROFE_PLAYLIST_LOAD_ART:
            if (currentPage_->isIdle( ))
            {
                currentPage_->reallocateMenuSpritePoints( );
                currentPage_->playlistEnter( );
                state = RETROFE_PLAYLIST_ENTER;
            }
            break;

        // Switch playlist; wait for onHighlightEnter animation to finish
        case RETROFE_PLAYLIST_ENTER:
            if (currentPage_->isIdle( ))
            {
                state = RETROFE_IDLE;
            }
            break;

        // Jump in menu; start onMenuJumpExit animation
        case RETROFE_MENUJUMP_REQUEST:
            inputClear = false;
            config_.getProperty( "jumpInputClear", inputClear );
            if (  inputClear  )
            {
                // Empty event queue
                SDL_Event e;
                while ( SDL_PollEvent( &e ) )
                    input_.update(e);
                input_.resetStates( );
            }
            currentPage_->menuJumpExit( );
            currentPage_->setScrolling(Page::ScrollDirectionIdle);
            state = RETROFE_MENUJUMP_EXIT;
            break;

        // Jump in menu; wait for onMenuJumpExit animation to finish; load art
        case RETROFE_MENUJUMP_EXIT:
            if (currentPage_->isIdle( ))
            {
                currentPage_->onNewItemSelected( );
                state = RETROFE_MENUJUMP_LOAD_ART;
            }
            break;

        // Jump in menu; start onMenuJumpEnter animation
        case RETROFE_MENUJUMP_LOAD_ART:
            if (currentPage_->isIdle( ))
            {
                currentPage_->reallocateMenuSpritePoints( );
                currentPage_->menuJumpEnter( );
                state = RETROFE_MENUJUMP_ENTER;
            }
            break;

        // Jump in menu; wait for onMenuJump animation to finish
        case RETROFE_MENUJUMP_ENTER:
            if (currentPage_->isIdle( ))
            {
                state = RETROFE_IDLE;
            }
            break;

        // Start onHighlightExit animation
        case RETROFE_HIGHLIGHT_REQUEST:
            currentPage_->setScrolling(Page::ScrollDirectionIdle);
            currentPage_->highlightExit( );
            state = RETROFE_HIGHLIGHT_EXIT;
            break;

        // Wait for onHighlightExit animation to finish; load art
        case RETROFE_HIGHLIGHT_EXIT:
            if (currentPage_->isIdle( ))
            {
                currentPage_->highlightLoadArt( );
                state = RETROFE_HIGHLIGHT_LOAD_ART;
            }
            break;

        // Start onHighlightEnter animation
        case RETROFE_HIGHLIGHT_LOAD_ART:
            currentPage_->highlightEnter( );
            if ( currentPage_->getSelectedItem( ) )
                l.LEDBlinky( 9, currentPage_->getSelectedItem( )->collectionInfo->name, currentPage_->getSelectedItem( ) );
            state = RETROFE_HIGHLIGHT_ENTER;
            break;

        // Wait for onHighlightEnter animation to finish
        case RETROFE_HIGHLIGHT_ENTER:
            RETROFE_STATE state_tmp;
            if (currentPage_->isMenuIdle( ) &&
                ((state_tmp = processUserInput( currentPage_ )) == RETROFE_HIGHLIGHT_REQUEST ||
                  state_tmp                                     == RETROFE_MENUJUMP_REQUEST  ||
                  state_tmp                                     == RETROFE_PLAYLIST_REQUEST) )
            {
                state = state_tmp;
            }
            else if (currentPage_->isIdle( ))
            {
                state = RETROFE_IDLE;
            }
            break;

        // Next page; start onMenuExit animation
        case RETROFE_NEXT_PAGE_REQUEST:
            currentPage_->exitMenu( );
            state = RETROFE_NEXT_PAGE_MENU_EXIT;
            break;

        // Wait for onMenuExit animation to finish; load new page if applicable; load art
        case RETROFE_NEXT_PAGE_MENU_EXIT:
            if ( currentPage_->isIdle( ) )
            {
                if ( currentPage_->getSelectedItem( ) )
                    l.LEDBlinky( 8, currentPage_->getSelectedItem( )->name, currentPage_->getSelectedItem( ) );
                lastMenuOffsets_[currentPage_->getCollectionName( )]   = currentPage_->getScrollOffsetIndex( );
                lastMenuPlaylists_[currentPage_->getCollectionName( )] = currentPage_->getPlaylistName( );
                std::string nextPageName = nextPageItem_->name;

                // Search results: use the in-memory collection built by the
                // search, entered like any other collection (no disk writes).
                bool searchResultsNext = ( searchPage_ && nextPageName == "_search" );

                if ( !menuMode_ )
                {
                    // Load new layout if available
                    std::string layoutName;
                    config_.getProperty( "layout", layoutName );
                    if ( !searchResultsNext )
                    {
                        PageBuilder pb( layoutName, "layout", config_, &fontcache_ );
                        Page *page = pb.buildPage( nextPageItem_->name );
                        if ( page )
                        {
                            currentPage_->freeGraphicsMemory( );
                            pages_.push( currentPage_ );
                            currentPage_ = page;
                        }
                    }
                }

                config_.setProperty( "currentCollection", nextPageName );

                CollectionInfo *info;
                if ( searchResultsNext )
                    info = searchPage_;
                else if ( menuMode_ )
                    info = getMenuCollection( nextPageName );
                else
                    info = getCollection( nextPageName );

                currentPage_->pushCollection(info);

                // The page now owns the temporary results collection (it is
                // freed when the page pops it). Forget the pointer so nothing
                // else can delete it a second time.
                if ( searchResultsNext )
                {
                    searchPage_ = NULL;
                    if ( searchTargetItem_ )
                    {
                        delete searchTargetItem_;
                        searchTargetItem_ = NULL;
                    }
                }

                bool rememberMenu = false;
                config_.getProperty( "rememberMenu", rememberMenu );

                std::string autoPlaylist = "all";
                config_.getProperty( "autoPlaylist", autoPlaylist );

                if (rememberMenu && lastMenuPlaylists_.find( nextPageName ) != lastMenuPlaylists_.end( ))
                {
                    currentPage_->selectPlaylist( lastMenuPlaylists_[nextPageName] ); // Switch to last playlist
                }
                else
                {
                    currentPage_->selectPlaylist( autoPlaylist );
                    if (currentPage_->getPlaylistName() != autoPlaylist )
                        currentPage_->selectPlaylist( "all" );
                }

                if ( rememberMenu && lastMenuOffsets_.find( nextPageName ) != lastMenuOffsets_.end( ) )
                {
                    currentPage_->setScrollOffsetIndex( lastMenuOffsets_[nextPageName] );
                }

                // (searchJumpIndex_ legacy path removed: option A enters the
                // results collection directly, highlighting is handled there)
                currentPage_->onNewItemSelected( );
                currentPage_->reallocateMenuSpritePoints( );

                state = RETROFE_NEXT_PAGE_MENU_LOAD_ART;

                // Check if we've entered an empty collection and need to go back automatically
                if (currentPage_->getCollectionSize() == 0)
                {
                    bool backOnEmpty = false;
                    config_.getProperty( "backOnEmpty", backOnEmpty );
                    if (backOnEmpty)
                        state = RETROFE_BACK_MENU_EXIT;
                }


             }
             break;

        // Start onMenuEnter animation
        case RETROFE_NEXT_PAGE_MENU_LOAD_ART:
            if (currentPage_->getMenuDepth( ) != 1 )
            {
                currentPage_->enterMenu( );
            }
            else
            {
                currentPage_->start( );
            }
            if ( currentPage_->getSelectedItem( ) )
                l.LEDBlinky( 9, currentPage_->getSelectedItem( )->collectionInfo->name, currentPage_->getSelectedItem( ) );
            state = RETROFE_NEXT_PAGE_MENU_ENTER;
            break;

        // Wait for onMenuEnter animation to finish
        case RETROFE_NEXT_PAGE_MENU_ENTER:
            if ( currentPage_->isIdle( ) )
            {
                inputClear = false;
                config_.getProperty( "collectionInputClear", inputClear );
                if (  inputClear  )
                {
                    // Empty event queue
                    SDL_Event e;
                    while ( SDL_PollEvent( &e ) )
                        input_.update(e);
                    input_.resetStates( );
                }
                state = RETROFE_IDLE;
            }
            break;

        // Start exit animation
        case RETROFE_COLLECTION_DOWN_REQUEST:
            if ( !pages_.empty( ) && currentPage_->getMenuDepth( ) == 1) // Inside a collection with a different layout
            {
                currentPage_->stop( );
                m.clearPage( );
                menuMode_ = false;
                state = RETROFE_COLLECTION_DOWN_EXIT;
            }
            else if ( currentPage_->getMenuDepth( ) > 1 ) // Inside a collection with the same layout
            {
                currentPage_->exitMenu( );
                state = RETROFE_COLLECTION_DOWN_EXIT;
            }
            else // Not in a collection
            {
                state = RETROFE_COLLECTION_DOWN_ENTER;

                if ( attractMode_ ) // Check playlist change in attract mode
                {
                    attractModePlaylistCollectionNumber_   += 1;
                    int attractModePlaylistCollectionNumber = 0;
                    config_.getProperty( "attractModePlaylistCollectionNumber", attractModePlaylistCollectionNumber );
                    // Check if playlist should be changed
                    if ( attractModePlaylistCollectionNumber_ > 0 && attractModePlaylistCollectionNumber_ >= attractModePlaylistCollectionNumber )
                    {
                        attractModePlaylistCollectionNumber_ = 0;
                        currentPage_->nextPlaylist( );
                        std::string attractModeSkipPlaylist = "";
                        config_.getProperty( "attractModeSkipPlaylist", attractModeSkipPlaylist );
                        if (currentPage_->getPlaylistName( ) == attractModeSkipPlaylist)
                            currentPage_->nextPlaylist( );
                        state = RETROFE_PLAYLIST_REQUEST;
                    }
                }
            }
            break;

        // Wait for the menu exit animation to finish
        case RETROFE_COLLECTION_DOWN_EXIT:
            if ( currentPage_->isIdle( ) )
            {
                lastMenuOffsets_[currentPage_->getCollectionName( )]   = currentPage_->getScrollOffsetIndex( );
                lastMenuPlaylists_[currentPage_->getCollectionName( )] = currentPage_->getPlaylistName( );
                if (currentPage_->getMenuDepth( ) == 1) // Inside a collection with a different layout
                {
                    currentPage_->deInitialize( );
                    delete currentPage_;
                    currentPage_ = pages_.top( );
                    pages_.pop( );
                    currentPage_->allocateGraphicsMemory( );
                }
                else // Inside a collection with the same layout
                {
                    currentPage_->popCollection( );
                }
                config_.setProperty( "currentCollection", currentPage_->getCollectionName( ) );

                bool rememberMenu = false;
                config_.getProperty( "rememberMenu", rememberMenu );

                std::string autoPlaylist = "all";
                config_.getProperty( "autoPlaylist", autoPlaylist );

                if (rememberMenu && lastMenuPlaylists_.find( currentPage_->getCollectionName( ) ) != lastMenuPlaylists_.end( ))
                {
                    currentPage_->selectPlaylist( lastMenuPlaylists_[currentPage_->getCollectionName( )] ); // Switch to last playlist
                }
                else
                {
                    currentPage_->selectPlaylist( autoPlaylist );
                    if (currentPage_->getPlaylistName() != autoPlaylist )
                        currentPage_->selectPlaylist( "all" );
                }

                if (rememberMenu && lastMenuPlaylists_.find( currentPage_->getCollectionName( ) ) != lastMenuPlaylists_.end( ))
                {
                    currentPage_->setScrollOffsetIndex( lastMenuOffsets_[currentPage_->getCollectionName( )] );
                }

                state = RETROFE_COLLECTION_DOWN_MENU_ENTER;
                currentPage_->onNewItemSelected( );

                if ( attractMode_ ) // Check playlist change in attract mode
                {
                    attractModePlaylistCollectionNumber_   += 1;
                    int attractModePlaylistCollectionNumber = 0;
                    config_.getProperty( "attractModePlaylistCollectionNumber", attractModePlaylistCollectionNumber );
                    // Check if playlist should be changed
                    if ( attractModePlaylistCollectionNumber_ > 0 && attractModePlaylistCollectionNumber_ >= attractModePlaylistCollectionNumber )
                    {
                        attractModePlaylistCollectionNumber_ = 0;
                        currentPage_->nextPlaylist( );
                        std::string attractModeSkipPlaylist = "";
                        config_.getProperty( "attractModeSkipPlaylist", attractModeSkipPlaylist );
                        if (currentPage_->getPlaylistName( ) == attractModeSkipPlaylist)
                            currentPage_->nextPlaylist( );
                        state = RETROFE_PLAYLIST_REQUEST;
                    }
                }

            }
            break;


        // Start menu enter animation
        case RETROFE_COLLECTION_DOWN_MENU_ENTER:
            currentPage_->enterMenu( );
            state = RETROFE_COLLECTION_DOWN_ENTER;
            break;
            

        // Waiting for enter animation to stop
        case RETROFE_COLLECTION_DOWN_ENTER:
            if ( currentPage_->isIdle( ) )
            {
                int attractModePlaylistCollectionNumber = 0;
                config_.getProperty( "attractModePlaylistCollectionNumber", attractModePlaylistCollectionNumber );
                if (!( attractMode_ && attractModePlaylistCollectionNumber > 0 && attractModePlaylistCollectionNumber_ == 0 ))
                {
                    currentPage_->setScrolling(Page::ScrollDirectionForward);
                    currentPage_->scroll(true);
                    currentPage_->updateScrollPeriod( );
                }
                state = RETROFE_COLLECTION_DOWN_SCROLL;
            }
            break;

        // Waiting for scrolling animation to stop
        case RETROFE_COLLECTION_DOWN_SCROLL:
            if ( currentPage_->isMenuIdle( ) )
            {
                std::string attractModeSkipCollection = "";
                config_.getProperty( "attractModeSkipCollection", attractModeSkipCollection );
                // Check if we need to skip this collection in attract mode or if we can select it
                if ( attractMode_ && currentPage_->getSelectedItem( )->name == attractModeSkipCollection )
                {
                    currentPage_->setScrolling(Page::ScrollDirectionForward);
                    currentPage_->scroll(true);
                    currentPage_->updateScrollPeriod( );
                }
                else
                {
                    RETROFE_STATE state_tmp = processUserInput( currentPage_ );
                    if ( state_tmp == RETROFE_COLLECTION_DOWN_REQUEST )
                    {
                        state = RETROFE_COLLECTION_DOWN_REQUEST;
                    }
                    else if ( state_tmp == RETROFE_COLLECTION_UP_REQUEST )
                    {
                        state = RETROFE_COLLECTION_UP_REQUEST;
                    }
                    else
                    {
                        currentPage_->setScrolling(Page::ScrollDirectionIdle); // Stop scrolling
                        nextPageItem_ = currentPage_->getSelectedItem( );
                        bool enterOnCollection = true;
                        config_.getProperty( "enterOnCollection", enterOnCollection );
                        if ( currentPage_->getSelectedItem( )->leaf || (!attractMode_ && !enterOnCollection) ) // Current selection is a game or enterOnCollection is not set
                        {
                            state = RETROFE_HIGHLIGHT_REQUEST;
                        }
                        else // Current selection is a menu
                        {
                            state = RETROFE_COLLECTION_HIGHLIGHT_EXIT;
                        }
                    }
                }
            }
            break;


        // Start onHighlightExit animation
        case RETROFE_COLLECTION_HIGHLIGHT_REQUEST:
            currentPage_->highlightExit( );
            state = RETROFE_COLLECTION_HIGHLIGHT_EXIT;
            break;

        // Wait for onHighlightExit animation to finish; load art
        case RETROFE_COLLECTION_HIGHLIGHT_EXIT:
            if (currentPage_->isIdle( ))
            {
                currentPage_->highlightLoadArt( );
                state = RETROFE_COLLECTION_HIGHLIGHT_LOAD_ART;
            }
            break;

        // Start onHighlightEnter animation
        case RETROFE_COLLECTION_HIGHLIGHT_LOAD_ART:
            currentPage_->highlightEnter( );
            if ( currentPage_->getSelectedItem( ) )
                l.LEDBlinky( 9, currentPage_->getSelectedItem( )->collectionInfo->name, currentPage_->getSelectedItem( ) );
            state = RETROFE_COLLECTION_HIGHLIGHT_ENTER;
            break;

        // Wait for onHighlightEnter animation to finish
        case RETROFE_COLLECTION_HIGHLIGHT_ENTER:
            if (currentPage_->isIdle( ))
            {
                RETROFE_STATE state_tmp = processUserInput( currentPage_ );
                if ( state_tmp == RETROFE_COLLECTION_DOWN_REQUEST )
                {
                    state = RETROFE_COLLECTION_DOWN_REQUEST;
                }
                else if ( state_tmp == RETROFE_COLLECTION_UP_REQUEST )
                {
                    state = RETROFE_COLLECTION_UP_REQUEST;
                }
                else
                {
                    state = RETROFE_NEXT_PAGE_REQUEST;
                }
            }
            break;

        // Start exit animation
        case RETROFE_COLLECTION_UP_REQUEST:
            if ( !pages_.empty( ) && currentPage_->getMenuDepth( ) == 1) // Inside a collection with a different layout
            {
                currentPage_->stop( );
                m.clearPage( );
                menuMode_ = false;
                state = RETROFE_COLLECTION_UP_EXIT;
            }
            else if ( currentPage_->getMenuDepth( ) > 1 ) // Inside a collection with the same layout
            {
                currentPage_->exitMenu( );
                state = RETROFE_COLLECTION_UP_EXIT;
            }
            else // Not in a collection
            {
                state = RETROFE_COLLECTION_UP_ENTER;
            }
            break;

        // Wait for the menu exit animation to finish
        case RETROFE_COLLECTION_UP_EXIT:
            if ( currentPage_->isIdle( ) )
            {
                lastMenuOffsets_[currentPage_->getCollectionName( )]   = currentPage_->getScrollOffsetIndex( );
                lastMenuPlaylists_[currentPage_->getCollectionName( )] = currentPage_->getPlaylistName( );
                if (currentPage_->getMenuDepth( ) == 1) // Inside a collection with a different layout
                {
                    currentPage_->deInitialize( );
                    delete currentPage_;
                    currentPage_ = pages_.top( );
                    pages_.pop( );
                    currentPage_->allocateGraphicsMemory( );
                }
                else // Inside a collection with the same layout
                {
                    currentPage_->popCollection( );
                }
                config_.setProperty( "currentCollection", currentPage_->getCollectionName( ) );

                bool rememberMenu = false;
                config_.getProperty( "rememberMenu", rememberMenu );

                std::string autoPlaylist = "all";
                config_.getProperty( "autoPlaylist", autoPlaylist );

                if (rememberMenu && lastMenuPlaylists_.find( currentPage_->getCollectionName( ) ) != lastMenuPlaylists_.end( ))
                {
                  currentPage_->selectPlaylist( lastMenuPlaylists_[currentPage_->getCollectionName( )] ); // Switch to last playlist
                }
                else
                {
                    currentPage_->selectPlaylist( autoPlaylist );
                    if (currentPage_->getPlaylistName() != autoPlaylist )
                        currentPage_->selectPlaylist( "all" );
                }

                if ( rememberMenu && lastMenuOffsets_.find( currentPage_->getCollectionName( ) ) != lastMenuOffsets_.end( ) )
                {
                    currentPage_->setScrollOffsetIndex( lastMenuOffsets_[currentPage_->getCollectionName( )] );
                }

                currentPage_->onNewItemSelected( );
                state = RETROFE_COLLECTION_UP_MENU_ENTER;
            }
            break;


        // Start menu enter animation
        case RETROFE_COLLECTION_UP_MENU_ENTER:
            currentPage_->enterMenu( );
            state = RETROFE_COLLECTION_UP_ENTER;
            break;
            

        // Waiting for enter animation to stop
        case RETROFE_COLLECTION_UP_ENTER:
            if ( currentPage_->isIdle( ) )
            {
                currentPage_->setScrolling(Page::ScrollDirectionBack);
                currentPage_->scroll(false);
                currentPage_->updateScrollPeriod( );
                state = RETROFE_COLLECTION_UP_SCROLL;
            }
            break;

        // Waiting for scrolling animation to stop
        case RETROFE_COLLECTION_UP_SCROLL:
            if ( currentPage_->isMenuIdle( ) )
            {
                RETROFE_STATE state_tmp;
                state_tmp = processUserInput( currentPage_ );
                if ( state_tmp == RETROFE_COLLECTION_DOWN_REQUEST )
                {
                    state = RETROFE_COLLECTION_DOWN_REQUEST;
                }
                else if ( state_tmp == RETROFE_COLLECTION_UP_REQUEST )
                {
                    state = RETROFE_COLLECTION_UP_REQUEST;
                }
                else
                {
                    currentPage_->setScrolling(Page::ScrollDirectionIdle); // Stop scrolling
                    nextPageItem_ = currentPage_->getSelectedItem( );
                    bool enterOnCollection = true;
                    config_.getProperty( "enterOnCollection", enterOnCollection );
                    if ( currentPage_->getSelectedItem( )->leaf || !enterOnCollection ) // Current selection is a game or enterOnCollection is not set
                    {
                        state = RETROFE_HIGHLIGHT_REQUEST;
                    }
                    else // Current selection is a menu
                    {
                        state = RETROFE_COLLECTION_HIGHLIGHT_EXIT;
                    }
                }
            }
            break;


        // Launching a menu entry
        case RETROFE_HANDLE_MENUENTRY:

            // Empty event queue
            SDL_Event e;
            while ( SDL_PollEvent( &e ) )
                input_.update(e);
            input_.resetStates( );

            // Handle menu entry
            m.handleEntry( currentPage_->getSelectedItem( ) );

            // Empty event queue
            while ( SDL_PollEvent( &e ) )
                input_.update(e);
            input_.resetStates( );

            state = RETROFE_IDLE;
            break;

        // Launching game; start onGameEnter animation
        case RETROFE_LAUNCH_ENTER:
            // List Search launches bypass the layout's gameEnter animations.
            if ( !searchLaunchItem_ )
            {
                currentPage_->enterGame( );
            }
            currentPage_->playSelect( ); // Play launch sound
            state = RETROFE_LAUNCH_REQUEST;
            break;

        // Wait for onGameEnter animation to finish; launch game; start onGameExit animation
        case RETROFE_LAUNCH_REQUEST:
            if ( currentPage_->isIdle( ) && !currentPage_->isSelectPlaying( ) )
            {
                // launching a game ends any temporary search results page
                if ( searchPage_ )
                {
                    searchPage_->playlists.clear( );
                    searchPage_->ownsItems = false;
                    delete searchPage_;
                    searchPage_ = NULL;
                    if ( searchTargetItem_ )
                    {
                        delete searchTargetItem_;
                        searchTargetItem_ = NULL;
                    }
                }

                // Use the selected List Search game when available.
                // Normal launches and Page Search retain their original selection.
                nextPageItem_ = searchLaunchItem_
                    ? searchLaunchItem_
                    : currentPage_->getSelectedItem( );
                launchEnter( );
                CollectionInfoBuilder cib(config_, *metadb_);
                std::string attractModeSkipPlaylist  = "";
                std::string lastPlayedSkipCollection = "";
                int         size = 0;
                config_.getProperty( "attractModeSkipPlaylist",  attractModeSkipPlaylist );
                config_.getProperty( "lastPlayedSkipCollection", lastPlayedSkipCollection );
                config_.getProperty( "lastplayedSize", size );

                if (currentPage_->getPlaylistName( )    != attractModeSkipPlaylist &&
                    nextPageItem_->collectionInfo->name != lastPlayedSkipCollection)
                    cib.updateLastPlayedPlaylist( searchLaunchItem_ ? nextPageItem_->collectionInfo : currentPage_->getCollection(), nextPageItem_, size ); // Update last played playlist if not currently in the skip playlist (e.g. settings)

                l.LEDBlinky( 3, nextPageItem_->collectionInfo->name, nextPageItem_ );
                // The launcher runs synchronously. Clear the List Search override
                // once it returns, regardless of whether a reboot is requested.
                const bool launchRequiresReboot = l.run(nextPageItem_->collectionInfo->name, nextPageItem_);
                searchLaunchItem_ = NULL;
                if ( launchRequiresReboot ) // Run and check if we need to reboot
                {
                    attract_.reset( );
                    reboot_ = true;
                    state   = RETROFE_QUIT_REQUEST;
                }
                else
                {
                    launchExit( );
                    // Restore the display after a List Search game returns.
                    searchFadeAlpha_ = 0;
                    l.LEDBlinky( 4 );
                    currentPage_->exitGame( );
                    state = RETROFE_LAUNCH_EXIT;
                }
            }
            break;

        // Wait for onGameExit animation to finish
        case RETROFE_LAUNCH_EXIT:
            if ( currentPage_->isIdle( ) )
            {
                state = RETROFE_IDLE;
            }
            break;

        // Go back a page; start onMenuExit animation
        case RETROFE_BACK_REQUEST:
            // A temporary search-results collection is owned by the page and is
            // freed by the normal pop below, so nothing is deleted here.
            if (currentPage_->getMenuDepth( ) == 1)
            {
                currentPage_->stop( );
                m.clearPage( );
                menuMode_ = false;
            }
            else
            {
                currentPage_->exitMenu( );
            }
            state = RETROFE_BACK_MENU_EXIT;
            break;

        // Wait for onMenuExit animation to finish; load previous page; load art
        case RETROFE_BACK_MENU_EXIT:
            if ( currentPage_->isIdle( ) )
            {
                lastMenuOffsets_[currentPage_->getCollectionName( )]   = currentPage_->getScrollOffsetIndex( );
                lastMenuPlaylists_[currentPage_->getCollectionName( )] = currentPage_->getPlaylistName( );
                if (currentPage_->getMenuDepth( ) == 1)
                {
                    currentPage_->deInitialize( );
                    delete currentPage_;
                    currentPage_ = pages_.top( );
                    pages_.pop( );
                    currentPage_->allocateGraphicsMemory( );
                }
                else
                {
                    currentPage_->popCollection( );
                }
                config_.setProperty( "currentCollection", currentPage_->getCollectionName( ) );

                bool rememberMenu = false;
                config_.getProperty( "rememberMenu", rememberMenu );

                std::string autoPlaylist = "all";
                config_.getProperty( "autoPlaylist", autoPlaylist );

                if (rememberMenu && lastMenuPlaylists_.find( currentPage_->getCollectionName( ) ) != lastMenuPlaylists_.end( ))
                {
                    currentPage_->selectPlaylist( lastMenuPlaylists_[currentPage_->getCollectionName( )] ); // Switch to last playlist
                }
                else
                {
                    currentPage_->selectPlaylist( autoPlaylist );
                    if (currentPage_->getPlaylistName() != autoPlaylist )
                        currentPage_->selectPlaylist( "all" );
                }

                if ( rememberMenu && lastMenuOffsets_.find( currentPage_->getCollectionName( ) ) != lastMenuOffsets_.end( ) )
                {
                    currentPage_->setScrollOffsetIndex( lastMenuOffsets_[currentPage_->getCollectionName( )] );
                }

                currentPage_->onNewItemSelected( );
                currentPage_->reallocateMenuSpritePoints( );
                state = RETROFE_BACK_MENU_LOAD_ART;
            }
            break;

        // Start onMenuEnter animation
        case RETROFE_BACK_MENU_LOAD_ART:
            currentPage_->enterMenu( );
            state = RETROFE_BACK_MENU_ENTER;
            break;

        // Wait for onMenuEnter animation to finish
        case RETROFE_BACK_MENU_ENTER:
            if ( currentPage_->isIdle( ) )
            {
                bool collectionInputClear = false;
                config_.getProperty( "collectionInputClear", collectionInputClear );
                if (  collectionInputClear  )
                {
                    // Empty event queue
                    SDL_Event e;
                    while ( SDL_PollEvent( &e ) )
                        input_.update(e);
                    input_.resetStates( );
                }
                state = RETROFE_IDLE;

                // Search overlay (option A): the results collection was already
                // entered through NEXT_PAGE_REQUEST; nothing else to do here.
            }
            break;

        // Start menu mode
        case RETROFE_MENUMODE_START_REQUEST:
            if ( currentPage_->isIdle( ) )
            {
                lastMenuOffsets_[currentPage_->getCollectionName( )]   = currentPage_->getScrollOffsetIndex( );
                lastMenuPlaylists_[currentPage_->getCollectionName( )] = currentPage_->getPlaylistName( );
                std::string layoutName;
                config_.getProperty( "layout", layoutName );
                PageBuilder pb( layoutName, "layout", config_, &fontcache_, true );
                Page *page = pb.buildPage( );
                if ( page )
                {
                    currentPage_->freeGraphicsMemory( );
                    pages_.push( currentPage_ );
                    currentPage_ = page;
                    menuMode_ = true;
                    m.setPage( page );
                }
                config_.setProperty( "currentCollection", "menu" );
                CollectionInfo *info = getMenuCollection( "menu" );
                currentPage_->pushCollection(info);
                currentPage_->onNewItemSelected( );
                currentPage_->reallocateMenuSpritePoints( );
                state = RETROFE_MENUMODE_START_LOAD_ART;
            }
            break;

        case RETROFE_MENUMODE_START_LOAD_ART:
            currentPage_->start();
            state = RETROFE_MENUMODE_START_ENTER;
            break;

        case RETROFE_MENUMODE_START_ENTER:
            if ( currentPage_->isIdle( ) )
            {
                SDL_Event e;
                while ( SDL_PollEvent( &e ) )
                    input_.update(e);
                input_.resetStates( );
                state = RETROFE_IDLE;
            }
            break;

        // Wait for splash mode animation to finish
        case RETROFE_NEW:
            if ( currentPage_->isIdle( ) )
            {
                state = RETROFE_IDLE;
            }
            break;

        // Start the onExit animation
        case RETROFE_QUIT_REQUEST:
            currentPage_->stop( );
            state = RETROFE_QUIT;
            break;

        // Wait for onExit animation to finish before quitting RetroFE
        case RETROFE_QUIT:
            if ( currentPage_->isGraphicsIdle( ) )
            {
                l.LEDBlinky( 2 );
                running = false;
            }
            break;
        }

        // Handle screen updates and attract mode
        if ( running )
        {
            lastTime = currentTime_;
            currentTime_ = static_cast<float>( SDL_GetTicks( ) ) / 1000;

            if ( currentTime_ < lastTime )
            {
                currentTime_ = lastTime;
            }

            deltaTime = currentTime_ - lastTime;
            double sleepTime;
            if (state == RETROFE_IDLE)
                sleepTime = fpsIdleTime - deltaTime*1000;
            else
                sleepTime = fpsTime - deltaTime*1000;
            if ( sleepTime > 0 && sleepTime < 1000 )
            {
                SDL_Delay( static_cast<unsigned int>( sleepTime ) );
            }

            if ( currentPage_ )
            {
                if (!splashMode)
                {
                    int attractReturn = attract_.update( deltaTime, *currentPage_ );
                    if (attractReturn == 1) // Change playlist
                    {
                        attract_.reset( attract_.isSet( ) );

                        bool cyclePlaylist = false;
                        config_.getProperty( "attractModeCyclePlaylist", cyclePlaylist );

                        std::string cycleString;
                        config_.getProperty( "cyclePlaylist", cycleString );
                        std::vector<std::string> cycleVector;
                        Utils::listToVector( cycleString, cycleVector, ',' );

                        if ( cyclePlaylist )
                            currentPage_->nextCyclePlaylist( cycleVector );
                        else
                            currentPage_->nextPlaylist( );

                        std::string attractModeSkipPlaylist = "";
                        config_.getProperty( "attractModeSkipPlaylist", attractModeSkipPlaylist );
                        if (currentPage_->getPlaylistName( ) == attractModeSkipPlaylist)
                        {
                            if ( cyclePlaylist )
                                currentPage_->nextCyclePlaylist( cycleVector );
                            else
                                currentPage_->nextPlaylist( );
                        }
                        state = RETROFE_PLAYLIST_REQUEST;
                    }
                    if (attractReturn == 2) // Change collection
                    {
                        attract_.reset( attract_.isSet( ) );
                        state = RETROFE_COLLECTION_DOWN_REQUEST;
                    }
                }
                if ( menuMode_ )
                {
                    attract_.reset( );
                }
                currentPage_->update( deltaTime );
                SDL_PumpEvents( );
                input_.updateKeystate( );
                if (!splashMode)
                {
                    if ( currentPage_->isAttractIdle( ) )
                    {
                        if ( !attractMode_ && attract_.isSet( ) )
                        {
                            currentPage_->attractEnter( );
                            l.LEDBlinky( 5 );
                        }
                        else if ( attractMode_ && !attract_.isSet( ) )
                        {
                            currentPage_->attractExit( );
                            l.LEDBlinky( 6 );
                        }
                        else if ( attract_.isSet( ) )
                        {
                            currentPage_->attract( );
                        }
                        attractMode_ = attract_.isSet( );
                    }
                }
            }

            render( );
        }
    }
    return reboot_;
}


// Check if we can go back a page or quite RetroFE
bool RetroFE::back(bool &exit)
{
    bool canGoBack  = false;
    bool exitOnBack = false;
    config_.getProperty( "exitOnFirstPageBack", exitOnBack );
    exit = false;

    if ( currentPage_->getMenuDepth( ) <= 1 && pages_.empty( ) )
    {
        exit = exitOnBack;
    }
    else
    {
        canGoBack = true;
    }

    return canGoBack;
}


// Process the user input
RetroFE::RETROFE_STATE RetroFE::processUserInput( Page *page )
{
    bool exit = false;
    RETROFE_STATE state = RETROFE_IDLE;

    // Poll all events until we find an active one
    SDL_Event e;
    while ( SDL_PollEvent( &e ) )
    {
        input_.update(e);
        if ( e.type == SDL_KEYDOWN && !SDL_KEYUP )
        {
            break;
        }
    }

    // Handle next/previous game inputs
    if ( page->isHorizontalScroll( ) )
    {
        if (input_.keystate(UserInput::KeyCodeRight))
        {
            attract_.reset( );
            page->setScrolling(Page::ScrollDirectionForward);
            page->scroll(true);
            page->updateScrollPeriod( );
            return state;
        }
        else if (input_.keystate(UserInput::KeyCodeLeft))
        {
            attract_.reset( );
            page->setScrolling(Page::ScrollDirectionBack);
            page->scroll(false);
            page->updateScrollPeriod( );
            return state;
        }
    }
    else
    {
        if (input_.keystate(UserInput::KeyCodeDown))
        {
            attract_.reset( );
            page->setScrolling(Page::ScrollDirectionForward);
            page->scroll(true);
            page->updateScrollPeriod( );
            return state;
        }
        else if (input_.keystate(UserInput::KeyCodeUp))
        {
            attract_.reset( );
            page->setScrolling(Page::ScrollDirectionBack);
            page->scroll(false);
            page->updateScrollPeriod( );
            return state;
        }
    }

    // Ignore other keys while the menu is scrolling
    if ( page->isIdle( ) && currentTime_ - keyLastTime_ > keyDelayTime_ )
    {
        if ( input_.keystate(UserInput::KeyCodeMenu) && !menuMode_)
        {
            state = RETROFE_MENUMODE_START_REQUEST;
        }

        // Open the search overlay
        else if ( input_.keystate(UserInput::KeyCodeSearch) && !menuMode_ )
        {
            // Only open the search from a real collection page; never from the
            // temporary results page (prevents a second search stacking on the
            // first and leaving the layout in a stuck state).
            if ( !( page->getCollection( ) && page->getCollection( )->name == "Search results" ) )
            {
                attract_.reset( );
                state        = runSearch( page );
                keyLastTime_ = currentTime_;
            }
        }

        // Handle Collection Up/Down keys
        else if ((input_.keystate(UserInput::KeyCodeCollectionUp)   && ( page->isHorizontalScroll( ) || !input_.keystate(UserInput::KeyCodeUp))) ||
                 (input_.keystate(UserInput::KeyCodeCollectionLeft) && (!page->isHorizontalScroll( ) || !input_.keystate(UserInput::KeyCodeLeft))))
        {
            attract_.reset( );
            bool backOnCollection = false;
            config_.getProperty( "backOnCollection", backOnCollection );
            if ( page->getMenuDepth( ) == 1 || !backOnCollection )
                state = RETROFE_COLLECTION_UP_REQUEST;
            else
                state = RETROFE_BACK_REQUEST;
        }

        else if ((input_.keystate(UserInput::KeyCodeCollectionDown)  && ( page->isHorizontalScroll( ) || !input_.keystate(UserInput::KeyCodeDown))) ||
                 (input_.keystate(UserInput::KeyCodeCollectionRight) && (!page->isHorizontalScroll( ) || !input_.keystate(UserInput::KeyCodeRight))))
        {
            attract_.reset( );
            bool backOnCollection = false;
            config_.getProperty( "backOnCollection", backOnCollection );
            if ( page->getMenuDepth( ) == 1 || !backOnCollection )
                state = RETROFE_COLLECTION_DOWN_REQUEST;
            else
                state = RETROFE_BACK_REQUEST;
        }

        else if (input_.keystate(UserInput::KeyCodePageUp))
        {
            attract_.reset( );
            page->pageScroll(Page::ScrollDirectionBack);
            state = RETROFE_MENUJUMP_REQUEST;
        }

        else if (input_.keystate(UserInput::KeyCodePageDown))
        {
            attract_.reset( );
            page->pageScroll(Page::ScrollDirectionForward);
            state = RETROFE_MENUJUMP_REQUEST;
        }

        else if (input_.keystate(UserInput::KeyCodeLetterUp))
        {
            attract_.reset( );
            bool cfwLetterSub;
            config_.getProperty( "cfwLetterSub", cfwLetterSub );
            if (cfwLetterSub && page->hasSubs())
                page->cfwLetterSubScroll(Page::ScrollDirectionBack);
            else
                page->letterScroll(Page::ScrollDirectionBack);
            state = RETROFE_MENUJUMP_REQUEST;
        }

        else if (input_.keystate(UserInput::KeyCodeLetterDown))
        {
            attract_.reset( );
            bool cfwLetterSub;
            config_.getProperty( "cfwLetterSub", cfwLetterSub );
            if (cfwLetterSub && page->hasSubs())
                page->cfwLetterSubScroll(Page::ScrollDirectionForward);
            else
                page->letterScroll(Page::ScrollDirectionForward);
            state = RETROFE_MENUJUMP_REQUEST;
        }

        else if ( input_.keystate(UserInput::KeyCodeFavPlaylist) )
        {
            attract_.reset( );
            page->favPlaylist( );
            state = RETROFE_PLAYLIST_REQUEST;
        }

        else if ( input_.keystate(UserInput::KeyCodeNextPlaylist) ||
                 (input_.keystate(UserInput::KeyCodePlaylistDown)  &&  page->isHorizontalScroll( )) ||
                 (input_.keystate(UserInput::KeyCodePlaylistRight) && !page->isHorizontalScroll( )))
        {
            attract_.reset( );
            page->nextPlaylist( );
            state = RETROFE_PLAYLIST_REQUEST;
        }

        else if ( input_.keystate(UserInput::KeyCodePrevPlaylist) ||
                 (input_.keystate(UserInput::KeyCodePlaylistUp)   &&  page->isHorizontalScroll( )) ||
                 (input_.keystate(UserInput::KeyCodePlaylistLeft) && !page->isHorizontalScroll( )))
        {
            attract_.reset( );
            page->prevPlaylist( );
            state = RETROFE_PLAYLIST_REQUEST;
        }

        else if ( input_.keystate(UserInput::KeyCodeCyclePlaylist) ||
                  input_.keystate(UserInput::KeyCodeNextCyclePlaylist) )
        {
            attract_.reset( );
            std::string cycleString;
            config_.getProperty( "cyclePlaylist", cycleString );
            std::vector<std::string> cycleVector;
            Utils::listToVector( cycleString, cycleVector, ',' );
            page->nextCyclePlaylist( cycleVector );
            state = RETROFE_PLAYLIST_REQUEST;
        }

        else if ( input_.keystate(UserInput::KeyCodePrevCyclePlaylist) )
        {
            attract_.reset( );
            std::string cycleString;
            config_.getProperty( "cyclePlaylist", cycleString );
            std::vector<std::string> cycleVector;
            Utils::listToVector( cycleString, cycleVector, ',' );
            page->prevCyclePlaylist( cycleVector );
            state = RETROFE_PLAYLIST_REQUEST;
        }

        else if ( input_.keystate(UserInput::KeyCodeRemovePlaylist) )
        {
            attract_.reset( );
            page->removePlaylist( );
            state = RETROFE_PLAYLIST_REQUEST;
        }

        else if ( input_.keystate(UserInput::KeyCodeAddPlaylist) )
        {
            attract_.reset( );
            page->addPlaylist( );
            state = RETROFE_PLAYLIST_REQUEST;
        }

        else if ( input_.keystate(UserInput::KeyCodeTogglePlaylist) )
        {
            attract_.reset( );
            page->togglePlaylist( );
            state = RETROFE_PLAYLIST_REQUEST;
        }

        else if ( input_.keystate(UserInput::KeyCodeSkipForward) )
        {
            attract_.reset( );
            page->skipForward( );
            page->jukeboxJump( );
            keyLastTime_ = currentTime_;
        }

        else if ( input_.keystate(UserInput::KeyCodeSkipBackward) )
        {
            attract_.reset( );
            page->skipBackward( );
            page->jukeboxJump( );
            keyLastTime_ = currentTime_;
        }

        else if ( input_.keystate(UserInput::KeyCodeSkipForwardp) )
        {
            attract_.reset( );
            page->skipForwardp( );
            page->jukeboxJump( );
            keyLastTime_ = currentTime_;
        }

        else if ( input_.keystate(UserInput::KeyCodeSkipBackwardp) )
        {
            attract_.reset( );
            page->skipBackwardp( );
            page->jukeboxJump( );
            keyLastTime_ = currentTime_;
        }

        else if ( input_.keystate(UserInput::KeyCodePause) )
        {
            attract_.reset( );
            page->pause( );
            page->jukeboxJump( );
            keyLastTime_ = currentTime_;
        }

        else if ( input_.keystate(UserInput::KeyCodeRestart) )
        {
            attract_.reset( );
            page->restart( );
            keyLastTime_ = currentTime_;
        }

        else if ( input_.keystate(UserInput::KeyCodeRandom) )
        {
            attract_.reset( );
            page->selectRandom( );
            state = RETROFE_MENUJUMP_REQUEST;
        }

        else if (input_.keystate(UserInput::KeyCodeAdminMode))
        {
            //todo: add admin mode support
        }

        else if (input_.keystate(UserInput::KeyCodeSelect))
        {
            attract_.reset( );
            nextPageItem_ = page->getSelectedItem( );

            if ( nextPageItem_ )
            {
                if ( nextPageItem_->leaf )
                {
                    if ( menuMode_ )
                    {
                        state = RETROFE_HANDLE_MENUENTRY;
                    }
                    else
                    {
                        state = RETROFE_LAUNCH_ENTER;
                    }
                }
                else
                {
                    CollectionInfoBuilder cib(config_, *metadb_);
                    std::string attractModeSkipPlaylist  = "";
                    std::string lastPlayedSkipCollection = "";
                    int         size = 0;
                    config_.getProperty( "attractModeSkipPlaylist",  attractModeSkipPlaylist );
                    config_.getProperty( "lastPlayedSkipCollection", lastPlayedSkipCollection );
                    config_.getProperty("lastplayedCollectionSize", size);
    
                    if (currentPage_->getPlaylistName( )    != attractModeSkipPlaylist &&
                        nextPageItem_->collectionInfo->name != lastPlayedSkipCollection)
                        cib.updateLastPlayedPlaylist( currentPage_->getCollection(), nextPageItem_, size ); // Update last played playlist if not currently in the skip playlist (e.g. settings)
                    state = RETROFE_NEXT_PAGE_REQUEST;
                }
            }
        }

        else if (input_.keystate(UserInput::KeyCodeBack))
        {
            attract_.reset( );
            if ( back( exit ) || exit )
            {
                state = (exit) ? RETROFE_QUIT_REQUEST : RETROFE_BACK_REQUEST;
            }
        }

        else if (input_.keystate(UserInput::KeyCodeQuit))
        {
            attract_.reset( );
            state = RETROFE_QUIT_REQUEST;
        }

        else if (input_.keystate(UserInput::KeyCodeReboot))
        {
            attract_.reset( );
            reboot_ = true;
            state   = RETROFE_QUIT_REQUEST;
        }

        else if (input_.keystate(UserInput::KeyCodeSaveFirstPlaylist))
        {
            attract_.reset( );
            if ( page->getMenuDepth( ) == 1 )
            {
                firstPlaylist_ = page->getPlaylistName( );
                saveRetroFEState( );
            }
        }
    }

    if ( state != RETROFE_IDLE )
    {
        keyLastTime_ = currentTime_;
        return state;
    }

    // Check if we're done scrolling
    if ( !input_.keystate(UserInput::KeyCodeUp) &&
         !input_.keystate(UserInput::KeyCodeLeft) &&
         !input_.keystate(UserInput::KeyCodeDown) &&
         !input_.keystate(UserInput::KeyCodeRight) &&
         !input_.keystate(UserInput::KeyCodePlaylistUp) &&
         !input_.keystate(UserInput::KeyCodePlaylistLeft) &&
         !input_.keystate(UserInput::KeyCodePlaylistDown) &&
         !input_.keystate(UserInput::KeyCodePlaylistRight) &&
         !input_.keystate(UserInput::KeyCodeCollectionUp) &&
         !input_.keystate(UserInput::KeyCodeCollectionLeft) &&
         !input_.keystate(UserInput::KeyCodeCollectionDown) &&
         !input_.keystate(UserInput::KeyCodeCollectionRight) &&
         !input_.keystate(UserInput::KeyCodePageUp) &&
         !input_.keystate(UserInput::KeyCodePageDown) &&
         !input_.keystate(UserInput::KeyCodeLetterUp) &&
         !input_.keystate(UserInput::KeyCodeLetterDown) &&
         !input_.keystate(UserInput::KeyCodeCollectionUp) &&
         !input_.keystate(UserInput::KeyCodeCollectionDown) &&
         !attract_.isActive( ) )
    {
        page->resetScrollPeriod( );
        if (page->isMenuScrolling( ))
        {
            attract_.reset( attract_.isSet( ) );
            state = RETROFE_HIGHLIGHT_REQUEST;
        }
    }

    return state;
}


// Load a page
// Returns the fully built collection with the given name (cached), or NULL
// if there is no such collection on disk.
CollectionInfo *RetroFE::getSearchCollection( std::string collectionName )
{
    std::map<std::string, CollectionInfo *>::iterator it = searchCollections_.find( collectionName );
    if ( it != searchCollections_.end( ) )
    {
        return it->second;
    }

    CollectionInfo *info = NULL;
    std::string     path = Utils::combinePath( Configuration::absolutePath, "collections", collectionName );
    struct stat     st;
    if ( metadb_ && stat( path.c_str( ), &st ) == 0 )
    {
        info = getCollection( collectionName );
    }

    searchCollections_[collectionName] = info;
    return info;
}


// Draws the page with a short message on top (used while collections load)
void RetroFE::searchProgress( const std::string &text )
{
    search_.setStatus( text );
    SDL_PumpEvents( ); // keep the window responsive during long loads
    render( );
}


// Collects what the search overlay can find: the games of ALL collections.
// The current collection comes first so that its matches win ties.
void RetroFE::buildSearchEntries( Page *page, std::vector<SearchMatcher::Entry> &entries )
{
    entries.clear( );
    searchGroups_.clear( );

    // Collections listed in settings.conf (search.exclude = A, B, C) never
    // appear in search results.
    std::string excludedCsv;
    config_.getProperty( "search.exclude", excludedCsv );
    std::vector<std::string> excluded;
    Utils::listToVector( excludedCsv, excluded, ',' );
    for ( size_t e = 0; e < excluded.size( ); ++e )
        excluded[e] = Utils::trimEnds( excluded[e] );
    excluded.erase(
        std::remove( excluded.begin(), excluded.end(), std::string() ),
        excluded.end() );

    // Pattern rules: search.excludePattern = *Collection*, *Settings* etc.
    // A '*' matches any run of characters. Matching is case-insensitive.
    // By default the search matches the game title only. Set
    // search.matchMetadata = yes to also match publisher, year, genre and the
    // collection name (that is what made "sonic" hit Sonic Team games).
    bool matchMetadata = false;
    config_.getProperty( "search.matchMetadata", matchMetadata );

    std::string patternCsv;
    config_.getProperty( "search.excludePattern", patternCsv );
    std::vector<std::string> patterns;
    Utils::listToVector( patternCsv, patterns, ',' );
    for ( size_t e = 0; e < patterns.size( ); ++e )
        patterns[e] = Utils::trimEnds( patterns[e] );
    patterns.erase(
        std::remove( patterns.begin(), patterns.end(), std::string() ),
        patterns.end() );

    // Whether to exclude any collection whose name contains "collection" (a
    // common pattern for filter collections like "Donkey Kong Collection").
    bool excludeCollectionName = false;
    config_.getProperty( "search.excludeCollectionName", excludeCollectionName );

    std::string current;
    if ( page && page->getCollection( ) )
    {
        current = page->getCollection( )->name;
    }

    // Wildcard matcher: '*' matches any run, case-insensitive.
    auto wildcardMatch = []( const std::string &name, const std::string &pat )
    {
        std::string n = Utils::toLower( name );
        std::string p = Utils::toLower( pat );
        // iterate tokens separated by '*'
        std::vector<std::string> toks;
        size_t start = 0;
        while ( start <= p.size( ) )
        {
            size_t pos = p.find( '*', start );
            if ( pos == std::string::npos )
            {
                toks.push_back( p.substr( start ) );
                break;
            }
            toks.push_back( p.substr( start, pos - start ) );
            start = pos + 1;
        }
        size_t idx = 0;
        bool first = true;
        for ( size_t t = 0; t < toks.size( ); ++t )
        {
            const std::string &tok = toks[t];
            if ( tok.empty( ) ) continue;
            size_t found = n.find( tok, idx );
            if ( found == std::string::npos ) return false;
            if ( first && !p.empty( ) && p[0] != '*' && found != 0 ) return false;
            idx = found + tok.size( );
            first = false;
        }
        // if pattern ends with '*' it's fine; if not, the last token must end the name
        if ( !p.empty( ) && p[p.size( ) - 1] != '*' && idx != n.size( ) ) return false;
        return true;
    };

    // All collection folders (same rule as Main.cpp: skip names starting with '_')
    std::vector<std::string> names;
    std::string collectionsPath = Utils::combinePath( Configuration::absolutePath, "collections" );
    DIR *dp = opendir( collectionsPath.c_str( ) );
    if ( dp )
    {
        struct dirent *dirp;
        while ( ( dirp = readdir( dp ) ) != NULL )
        {
            std::string name = dirp->d_name;
            if ( name != "." && name != ".." && !name.empty( ) && name[0] != '_' )
            {
                bool skip = false;
                std::string nameLC = Utils::toLower( name );
                for ( size_t e = 0; e < excluded.size( ); ++e )
                {
                    const std::string &ex = excluded[e];
                    if ( ex.find( '*' ) != std::string::npos )
                    {
                        if ( wildcardMatch( name, ex ) ) { skip = true; break; }
                    }
                    else if ( nameLC == Utils::toLower( ex ) ) { skip = true; break; }
                }
                if ( !skip )
                {
                    for ( size_t e = 0; e < patterns.size( ) && !skip; ++e )
                    {
                        if ( wildcardMatch( name, patterns[e] ) ) skip = true;
                    }
                }
                if ( !skip && excludeCollectionName )
                {
                    if ( Utils::toLower( name ).find( "collection" ) != std::string::npos ) skip = true;
                }
                if ( skip ) continue;

                // Menu collections list other collections, not games: they are
                // skipped whether they use a menu.txt or a menu/ subfolder
                // (Type R uses the latter, e.g. collections/SETTINGS and
                // collections/collections, whose menu pages point at bats or
                // other collections).
                std::string dirPath = Utils::combinePath( collectionsPath, name );

                // Search only actual collection directories, not ordinary files.
                struct stat collectionStat;
                if ( stat( dirPath.c_str(), &collectionStat ) != 0 ||
                     ( collectionStat.st_mode & S_IFMT ) != S_IFDIR )
                    continue;
                std::ifstream menu( Utils::combinePath( dirPath, "menu.txt" ).c_str( ) );
                if ( menu.good( ) ) continue;
                {
                    DIR *mdp;
                    struct dirent *mdirp;
                    bool menuSubdir = false;
                    mdp = opendir( Utils::combinePath( dirPath, "menu" ).c_str( ) );
                    if ( mdp )
                    {
                        while ( ( mdirp = readdir( mdp ) ) != NULL )
                        {
                            std::string m = mdirp->d_name;
                            if ( m != "." && m != ".." && m.size() > 4 &&
                                 Utils::toLower( m.substr( m.size() - 4 ) ) == ".txt" )
                            {
                                menuSubdir = true;
                                break;
                            }
                        }
                        closedir( mdp );
                    }
                    if ( menuSubdir ) continue;
                }

                names.push_back( name );
            }
        }
        closedir( dp );
    }
    std::sort( names.begin( ), names.end( ) );

    // current collection first
    std::vector<std::string>::iterator cur = std::find( names.begin( ), names.end( ), current );
    if ( cur != names.end( ) )
    {
        std::string c = *cur;
        names.erase( cur );
        names.insert( names.begin( ), c );
    }

    for ( size_t n = 0; n < names.size( ); ++n )
    {
        const std::string &name = names[n];

        if ( searchCollections_.find( name ) == searchCollections_.end( ) )
        {
            searchProgress( "SEARCH: LOADING " + name + " (" + std::to_string( n + 1 ) + "/" + std::to_string( names.size( ) ) + ")" );
        }

        CollectionInfo *info = getSearchCollection( name );
        if ( !info || info->items.empty( ) ) continue;

        // Only games are searched; menu entries (e.g. everything in Main, or a
        // submenu inside a collection) are skipped. The index stays the
        // position in the collection's full list, which is what the page uses.
        int group = -1;
        for ( unsigned int j = 0; j < info->items.size( ); ++j )
        {
            Item *game = info->items[j];
            if ( !game || !game->leaf ) continue;

            if ( group < 0 )
            {
                group = static_cast<int>( searchGroups_.size( ) );
                searchGroups_.push_back( name );
            }
            SearchMatcher::Entry en = SearchMatcher::makeEntry( j, game->title, game->fullTitle, game->name,
                                                                 game->manufacturer, game->year, game->genre,
                                                                 group, name );
            if ( !matchMetadata )
            {
                en.secondary.clear( );
                en.secondaryNorm.clear( );
            }
            entries.push_back( en );
        }
    }

    search_.setStatus( "" );
}


// Show the search overlay and run it until the user picks a result or
// cancels. If a result was picked, the page is moved to that item and a
// menu jump is requested (the same path the "random" key uses).
RetroFE::RETROFE_STATE RetroFE::runSearch( Page *page )
{
    RETROFE_STATE state = RETROFE_IDLE;

    // Guard against re-entrancy (a second search while the results of the
    // first are still on screen) - should not normally happen, but the state
    // machine is not re-entrant.
    if ( currentPage_->getCollection( ) && currentPage_->getCollection( )->name == "Search results" )
    {
        Logger::write( Logger::ZONE_WARNING, "RetroFE", "Search requested while already on a search results page; ignoring" );
        return RETROFE_IDLE;
    }

    searchPendingItem_ = NULL;
    searchJumpIndex_   = -1;

    // Search display mode: page (default) or list.
    // The existing Page Search behaviour remains unchanged.
    std::string searchMode = "page";
    config_.getProperty( "searchMode", searchMode );
    search_.setListMode( searchMode == "list" );

    // Optional: searchFont = <path to a .ttf> in settings.conf (absolute, or relative to the RetroFE folder)
    std::string searchFont;
    if ( config_.getProperty( "searchFont", searchFont ) && !searchFont.empty( ) )
    {
        bool absolute = ( searchFont.size( ) > 1 && searchFont[1] == ':' ) || searchFont[0] == '/' || searchFont[0] == '\\';
        if ( !absolute ) searchFont = Utils::combinePath( Configuration::absolutePath, searchFont );
    }
    search_.setFontPath( searchFont );

    std::vector<SearchMatcher::Entry> entries;
    buildSearchEntries( page, entries );
    search_.open( entries );
    Logger::write( Logger::ZONE_INFO, "RetroFE", "Search overlay opened (" + std::to_string( search_.itemCount( ) ) +
                   " items, " + std::to_string( searchGroups_.size( ) ) + " collections searched)" );

    Uint32 lastTicks = SDL_GetTicks( );
    // List Search: 200 ms fade to black.
    Uint32 searchFadeStart = 0;
    bool searchFadeRunning = false;

    while ( search_.isActive( ) )
    {
        SDL_Event e;
        while ( SDL_PollEvent( &e ) )
        {
            search_.handleEvent( e, input_ );
        }

        Uint32 now = SDL_GetTicks( );
        float  dt  = static_cast<float>( now - lastTicks ) / 1000.0f;
        lastTicks  = now;

        // Keep the page behind the overlay alive (video, animations)
        page->update( dt );
        if ( searchMode == "list" && search_.isClosing( ) )
        {
            if ( !searchFadeRunning )
            {
                searchFadeRunning = true;
                searchFadeStart = SDL_GetTicks( );
            }
            const Uint32 elapsed = SDL_GetTicks( ) - searchFadeStart;
            searchFadeAlpha_ = elapsed >= 200 ? 255 : static_cast<int>( elapsed * 255 / 200 );
        }

        render( );
        // Close List Search only after the fully black frame is displayed.
        if ( searchFadeRunning && searchFadeAlpha_ == 255 )
        {
            search_.completeClose( );
        }
        SDL_Delay( 16 );
    }

    // List Search resolves the selected game directly from its original collection.
    // Page Search continues through the existing temporary results-page code below.
    if ( searchMode == "list" )
    {
        searchLaunchItem_ = NULL;

        if ( search_.wasChosen( ) )
        {
            const int group = search_.chosenGroup( );
            const unsigned int index = search_.chosenIndex( );

            if ( group >= 0 && group < static_cast<int>( searchGroups_.size( ) ) )
            {
                CollectionInfo *info = getSearchCollection( searchGroups_[group] );

                if ( info && index < info->items.size( ) )
                {
                    searchLaunchItem_ = info->items[index];
                }
            }
        }

        // Do not leave the display black if no valid game was resolved.
        if ( !searchLaunchItem_ )
        {
            searchFadeAlpha_ = 0;
        }

        if ( searchLaunchItem_ )
        {
            // Use the normal launch animations and state machine.
            state = RETROFE_LAUNCH_ENTER;
        }
    }
    else if ( search_.wasChosen( ) || !search_.query( ).empty( ) )
    {
        // New behaviour (option A): a non-empty query builds a temporary
        // results collection containing every match stolen from its own
        // collection, and enters it like a normal collection page. If nothing
        // matched, we just close and stay put.
        std::string query = search_.query( );
        std::vector<Item *> hits;
        for ( size_t i = 0; i < search_.resultCount( ); ++i )
        {
            const SearchMatcher::Entry &en = search_.resultEntry( i );
            if ( en.group < 0 ) continue;
            if ( en.group >= static_cast<int>( searchGroups_.size( ) ) ) continue;

            CollectionInfo *info = getSearchCollection( searchGroups_[en.group] );
            if ( !info ) continue;

            // entry.index is the position in the collection's full item list
            if ( en.index < info->items.size( ) )
            {
                hits.push_back( info->items[en.index] );
            }
        }

        if ( !hits.empty( ) )
        {
            // Build an in-memory "Search results" collection (no disk write).
            if ( searchPage_ )
            {
                searchPage_->playlists.clear( );
                searchPage_->ownsItems = false;
                delete searchPage_;
                searchPage_ = NULL;
            }
            searchPage_ = new CollectionInfo( "Search results", "", "", "", "" );
            searchPage_->items = hits;

            searchPage_->menusort   = false;
            searchPage_->ownsItems = false;
            searchPage_->subsSplit  = false;
            searchPage_->playlists[ "all" ] = & searchPage_->items;
            searchPage_->playlists[ "favorites" ] = new std::vector<Item *>( );

            // Enter it like any other collection via the normal page transition.
            if ( searchTargetItem_ ) delete searchTargetItem_;
            searchTargetItem_ = new Item( );
            searchTargetItem_->name = "_search";
            searchTargetItem_->title = "Search results";
            searchTargetItem_->fullTitle = "Search results";
            searchTargetItem_->leaf = false;
            searchTargetItem_->collectionInfo = currentPage_->getCollection( );

            // nextPageItem_ is what NEXT_PAGE_REQUEST uses; searchPendingItem_
            // is only for the legacy back-then-open path (unused now).
            nextPageItem_ = searchTargetItem_;
            state         = RETROFE_NEXT_PAGE_REQUEST;
        }
        else
        {
            Logger::write( Logger::ZONE_INFO, "RetroFE", "Search had no matches; closing" );
        }
    }
    else
    {
        Logger::write( Logger::ZONE_INFO, "RetroFE", "Search overlay closed without a selection" );
    }

    // Throw away whatever is left over so that no key appears "stuck"
    SDL_Event e;
    while ( SDL_PollEvent( &e ) )
    {
        if ( e.type == SDL_JOYDEVICEADDED || e.type == SDL_JOYDEVICEREMOVED )
        {
            input_.update( e );
        }
    }
    input_.resetStates( );

    return state;
}


Page *RetroFE::loadPage( )
{
    std::string layoutName;

    config_.getProperty( "layout", layoutName );

    PageBuilder pb( layoutName, "layout", config_, &fontcache_ );
    Page *page = pb.buildPage( );

    if ( !page )
    {
        Logger::write( Logger::ZONE_ERROR, "RetroFE", "Could not create page" );
    }

    return page;
}


// Load the splash page
Page *RetroFE::loadSplashPage( )
{
    std::string layoutName;
    config_.getProperty( "layout", layoutName );

    PageBuilder pb( layoutName, "splash", config_, &fontcache_ );
    Page * page = pb.buildPage( );
    page->start( );

    return page;
}


// Load a collection
CollectionInfo *RetroFE::getCollection(std::string collectionName)
{

    // Check if subcollections should be merged or split
    bool subsSplit = false;
    config_.getProperty( "subsSplit", subsSplit );

    // Build the collection
    CollectionInfoBuilder cib(config_, *metadb_);
    CollectionInfo *collection = cib.buildCollection( collectionName );
    collection->subsSplit = subsSplit;
    cib.injectMetadata( collection );

    DIR *dp;
    struct dirent *dirp;

    std::string path = Utils::combinePath( Configuration::absolutePath, "collections", collectionName );
    dp = opendir( path.c_str( ) );

    // Loading sub collection files
    while ( (dirp = readdir( dp )) != NULL )
    {
        std::string file = dirp->d_name;

        size_t position = file.find_last_of( "." );
        std::string basename = (std::string::npos == position)? file : file.substr( 0, position );

        std::string comparator = ".sub";
        int start = file.length( ) - comparator.length( );

        if ( start >= 0 )
        {
            if ( file.compare( start, comparator.length( ), comparator ) == 0 )
            {
                Logger::write( Logger::ZONE_INFO, "RetroFE", "Loading subcollection into menu: " + basename );

                CollectionInfo *subcollection = cib.buildCollection( basename, collectionName );
                collection->addSubcollection( subcollection );
                subcollection->subsSplit = subsSplit;
                cib.injectMetadata( subcollection );
                collection->hasSubs = true;
            }
        }
    }
    if (dp) closedir( dp );

    bool menuSort = true;
    config_.getProperty( "collections." + collectionName + ".list.menuSort", menuSort );

    if (menuSort)
        collection->sortItems( );

    MenuParser mp;
    mp.buildMenuItems( collection, menuSort);

    cib.addPlaylists( collection );
    collection->sortPlaylists( );

    // Add extra info, if available
    for ( std::vector<Item *>::iterator it = collection->items.begin( ); it != collection->items.end( ); it++ )
    {
        std::string path = Utils::combinePath( Configuration::absolutePath, "collections", collectionName, "info", (*it)->name + ".conf" );
        (*it)->loadInfo( path );
    }

    // Remove parenthesis and brackets, if so configured
    bool showParenthesis    = true;
    bool showSquareBrackets = true;

    (void)config_.getProperty( "showParenthesis", showParenthesis );
    (void)config_.getProperty( "showSquareBrackets", showSquareBrackets );

    typedef std::map<std::string, std::vector <Item *> *> Playlists_T;
    for ( Playlists_T::iterator itP = collection->playlists.begin( ); itP != collection->playlists.end( ); itP++ )
    {
        for ( std::vector <Item *>::iterator itI = itP->second->begin( ); itI != itP->second->end( ); itI++ )
        {
            if ( !showParenthesis )
            {
                std::string::size_type firstPos  = (*itI)->title.find_first_of( "(" );
                std::string::size_type secondPos = (*itI)->title.find_first_of( ")", firstPos );
    
                while ( firstPos != std::string::npos && secondPos != std::string::npos )
                {
                    firstPos  = (*itI)->title.find_first_of( "(" );
                    secondPos = (*itI)->title.find_first_of( ")", firstPos );
    
                    if ( firstPos != std::string::npos )
                    {
                        (*itI)->title.erase( firstPos, (secondPos - firstPos) + 1 );
                    }
                }
            }
            if ( !showSquareBrackets )
            {
                std::string::size_type firstPos  = (*itI)->title.find_first_of( "[" );
                std::string::size_type secondPos = (*itI)->title.find_first_of( "]", firstPos );
    
                while ( firstPos != std::string::npos && secondPos != std::string::npos )
                {
                    firstPos  = (*itI)->title.find_first_of( "[" );
                    secondPos = (*itI)->title.find_first_of( "]", firstPos );
    
                    if ( firstPos != std::string::npos && secondPos != std::string::npos )
                    {
                        (*itI)->title.erase( firstPos, (secondPos - firstPos) + 1 );
                    }
                }
            }
        }
    }

    return collection;
}


// Load a menu
CollectionInfo *RetroFE::getMenuCollection( std::string collectionName )
{
    std::string menuPath = Utils::combinePath( Configuration::absolutePath, "menu" );
    std::string menuFile = Utils::combinePath( menuPath, collectionName + ".txt" );
    std::vector<Item *> menuVector;
    CollectionInfoBuilder cib( config_, *metadb_ );
    CollectionInfo *collection = new CollectionInfo( collectionName, menuPath, "", "", "" );
    cib.ImportBasicList( collection, menuFile, menuVector );
    for ( std::vector<Item *>::iterator it = menuVector.begin( ); it != menuVector.end( ); ++it)
    {
        (*it)->leaf = false;
        size_t position = (*it)->name.find( "=" );
        if ( position != std::string::npos )
        {
            (*it)->ctrlType  = Utils::trimEnds( (*it)->name.substr( position+1, (*it)->name.size( )-1 ) );
            (*it)->name      = Utils::trimEnds( (*it)->name.substr( 0, position ) );
            (*it)->title     = (*it)->name;
            (*it)->fullTitle = (*it)->name;
            (*it)->leaf      = true;
        }
        (*it)->collectionInfo = collection;
        collection->items.push_back( *it );
    }
    collection->playlists["all"] = &collection->items;
    return collection;
}


void RetroFE::saveRetroFEState( )
{
    std::string file = Utils::combinePath(Configuration::absolutePath, "settings_saved.conf");
    Logger::write(Logger::ZONE_INFO, "RetroFE", "Saving settings_saved.conf");
    std::ofstream filestream;
    try
    {
        filestream.open(file.c_str());
        filestream << "firstPlaylist = " << firstPlaylist_ << std::endl;
        filestream.close();
    }
    catch(std::exception &)
    {
        Logger::write(Logger::ZONE_ERROR, "RetroFE", "Save failed: " + file);
    }
}
