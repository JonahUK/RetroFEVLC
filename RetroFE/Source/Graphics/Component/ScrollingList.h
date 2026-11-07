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


#include <vector>
#include "Component.h"
#include "../Animate/Tween.h"
#include "../Page.h"
#include "../ViewInfo.h"
#include "../../Database/Configuration.h"
#include <SDL2/SDL.h>


class Configuration;
class Font;
class Text;

class ScrollingList : public Component
{

public:

    ScrollingList( Configuration &c,
                   Page          &p,
                   bool          layoutMode,
                   bool          commonMode,
                   Font         *font,
                   std::string   layoutKey,
                   std::string   imageType,
                   std::string   videoType );

    ScrollingList( const ScrollingList &copy );
    virtual ~ScrollingList( );
    void triggerEnterEvent( );
    void triggerExitEvent( );
    void triggerMenuEnterEvent( int menuIndex = -1 );
    void triggerMenuExitEvent( int menuIndex = -1 );
    void triggerGameEnterEvent( int menuIndex = -1 );
    void triggerGameExitEvent( int menuIndex = -1 );
    void triggerRailEvent(const std::string &event, int menuIndex);
    void triggerHighlightEnterEvent( int menuIndex = -1 );
    void triggerHighlightExitEvent( int menuIndex = -1 );
    void triggerPlaylistEnterEvent( int menuIndex = -1 );
    void triggerPlaylistExitEvent( int menuIndex = -1 );
    void triggerMenuJumpEnterEvent( int menuIndex = -1 );
    void triggerMenuJumpExitEvent( int menuIndex = -1 );
    void triggerAttractEnterEvent( int menuIndex = -1 );
    void triggerAttractEvent( int menuIndex = -1 );
    void triggerAttractExitEvent( int menuIndex = -1 );
    void triggerJukeboxJumpEvent( int menuIndex = -1 );

    bool allocateTexture( unsigned int index, Item *i, std::string *prefetchImagePath = nullptr );
    void deallocateTexture( unsigned int index );
    void setItems( std::vector<Item *> *items );
    std::vector<Item *> *getItems() const { return items_; }
    void destroyItems( );
    void setPoints( std::vector<ViewInfo *> *scrollPoints, std::vector<AnimationEvents *> *tweenPoints );
    void offsetVertical( float delta );
    void setRailTitle( Text *title ) { railTitle_ = title; }
    Text *getRailTitle() const { return railTitle_; }
    unsigned int getSelectedIndex( );
    void setSelectedIndex( unsigned int index );
    unsigned int getSize( );
    void pageUp( );
    void pageDown( );
    void letterUp( );
    void letterDown( );
    void letterChange( bool increment );
    void subUp( );
    void subDown( );
    void subChange( bool increment );
    void cfwLetterSubUp( );
    void cfwLetterSubDown( );
    void random( );
    bool isIdle( );
    bool isAttractIdle( );
    unsigned int getScrollOffsetIndex( );
    void setScrollOffsetIndex( unsigned int index );
    void setSelectedIndex( int selectedIndex );
    Item *getItemByOffset( int offset );
    Item *getSelectedItem( );
    void allocateGraphicsMemory( );
    void freeGraphicsMemory( );
    void update( float dt );
    void draw( );
    void draw( unsigned int layer );
    void setScrollAcceleration( float value );
    void setStartScrollTime( float value );
    void setMinScrollTime( float value );
    std::string requestedCollection;
    bool horizontalScroll;
    void deallocateSpritePoints( );
    void allocateSpritePoints( );
    void resetScrollPeriod( );
    void updateScrollPeriod( );
    void scroll( bool forward );
    void prefetchNearbyImages( unsigned int ahead = 2 );

private:

    void resetTweens( Component *c, AnimationEvents *sets, ViewInfo *currentViewInfo, ViewInfo *nextViewInfo, double scrollTime );
    unsigned int loopIncrement( unsigned int offset, unsigned int i, unsigned int size );
    unsigned int loopDecrement( unsigned int offset, unsigned int i, unsigned int size );

    bool layoutMode_;
    bool commonMode_;
    std::vector<Component *> *spriteList_;
    std::vector<ViewInfo *> *scrollPoints_;
    std::vector<AnimationEvents *> *tweenPoints_;

    unsigned int itemIndex_;
    unsigned int selectedOffsetIndex_;

    float scrollAcceleration_;
    float startScrollTime_;
    float minScrollTime_;
    float scrollPeriod_;

    Configuration &config_;
    Font          *fontInst_;
    std::string    layoutKey_;
    std::string    imageType_;
    std::string    videoType_;

    std::vector<Item *>     *items_;
    std::vector<Component *> components_;
    Text *railTitle_ = nullptr;

    // Track the last prefetched collection and rail position.
    std::vector<Item *> *prefetchedItems_ = nullptr;
    unsigned int prefetchedIndex_ = 0;
    bool hasPrefetchedPosition_ = false;

    // True only while this rail has active graphics resources.
    bool railGraphicsAllocated_ = false;

};
