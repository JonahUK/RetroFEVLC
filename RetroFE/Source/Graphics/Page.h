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

#include "../Collection/CollectionInfo.h"

#include <functional>
#include <map>
#include <string>
#include <list>
#include <vector>

class Component;
class Configuration;
class ScrollingList;
class Text;
class Container;
class Font;
class Item;
class Sound;

class Page
{
public:
    enum ScrollDirection
    {
        ScrollDirectionForward,
        ScrollDirectionBack,
        ScrollDirectionIdle

    };

    Page(Configuration &c, int layoutWidth, int layoutHeight );
    virtual ~Page();
    void deInitialize();
    virtual void onNewItemSelected();
    virtual void onNewScrollItemSelected();
    void highlightLoadArt();
    bool pushCollection(CollectionInfo *collection);
    bool registerRailCollection(CollectionInfo *collection);
    void bindRailCollections();
    bool ensureRailCollectionLoaded(ScrollingList *rail);
    void prepareVisibleRailCollections();
    void setRailCollectionLoader(
        const std::function<CollectionInfo *(const std::string &)> &loader)
    {
        railCollectionLoader_ = loader;
    }
    std::vector<std::string> getRequestedRailCollections() const;
    bool popCollection();
    void enterMenu();
    void exitMenu();
    void enterGame();
    void exitGame();
    std::string getPlaylistName();
    void favPlaylist();
    void nextPlaylist();
    void prevPlaylist();
    void selectPlaylist(std::string playlist);
    void nextCyclePlaylist(std::vector<std::string> list);
    void prevCyclePlaylist(std::vector<std::string> list);
    void pushMenu(ScrollingList *s, int index = -1);
    bool isMenusFull();
    void setLoadSound(Sound *chunk);
    void setUnloadSound(Sound *chunk);
    void setHighlightSound(Sound *chunk);
    void setSelectSound(Sound *chunk);
    bool addComponent(Component *c);
    void pageScroll(ScrollDirection direction);
    void letterScroll(ScrollDirection direction);
    void subScroll(ScrollDirection direction);
    void cfwLetterSubScroll(ScrollDirection direction);
    unsigned int getCollectionSize();
    unsigned int getSelectedIndex();
    void selectRandom();
    void start();
    void stop();
    void setScrolling(ScrollDirection direction);
    void setIndependentRails(bool enabled);
    void setVisibleRails(unsigned int count);
    void setLoopRails(bool enabled) { loopRails_ = enabled; }
    void setCategoryNavigation(bool enabled) { categoryNavigation_ = enabled; }
    bool isCategoryNavigation() const { return categoryNavigation_; }
    void setCategoryBarData(const std::vector<std::string> &names,
                            unsigned int selectedIndex)
    {
        categoryBarNames_ = names;
        categoryBarSelectedIndex_ = selectedIndex;
        buildCategoryBarLabels();
    }
    void configureCategoryBar(float x, float y, float spacing,
                              float underlineOffset, float underlineHeight,
                              unsigned int labelLayer, unsigned int underlineLayer)
    {
        categoryBarStyle_.x = x;
        categoryBarStyle_.y = y;
        categoryBarStyle_.spacing = spacing;
        categoryBarStyle_.underlineOffset = underlineOffset;
        categoryBarStyle_.underlineHeight = underlineHeight;
        categoryBarStyle_.labelLayer = labelLayer;
        categoryBarStyle_.underlineLayer = underlineLayer;
    }
    // STREAM: XML-defined template for dynamic category labels.
    void setCategoryBarTemplate(Text *label)
    {
        categoryBarTemplate_ = label;
    }
    void setCategoryBarFonts(Font *selectedFont, Font *inactiveFont)
    {
        categoryBarSelectedFont_ = selectedFont;
        categoryBarInactiveFont_ = inactiveFont;
    }
    void setCategoryBarUnderlineTemplate(Container *underline);
    void setCategoryBarBackgroundTemplate(Container *background);
    void buildCategoryBarLabels();
    void registerCategoryBarLabel(Text *label)
    {
        if (label)
            categoryBarLabels_.push_back(label);
    }
    const std::vector<std::string> &getCategoryBarNames() const
    {
        return categoryBarNames_;
    }
    unsigned int getCategoryBarSelectedIndex() const
    {
        return categoryBarSelectedIndex_;
    }
    void setRailSpacing(float spacing) { railSpacing_ = spacing; }
    void setRailAnchorY(float y) { railAnchorY_ = y; }
    bool isIndependentRails() const;
    bool moveRailFocus(bool forward);
    bool isHorizontalScroll();
    unsigned int getMenuDepth();
    Item *getSelectedItem();
    Item *getSelectedItem(int offset);
    void removeSelectedItem();
    void setScrollOffsetIndex(unsigned int i);
    unsigned int getScrollOffsetIndex();
    bool isIdle();
    bool isAttractIdle();
    bool isGraphicsIdle();
    bool isMenuIdle();
    void setStatusTextComponent(Text *t);
    void update(float dt);
    void cleanup();
    void draw();
    void suspendVideos();
    void resumeVideos();
    void freeGraphicsMemory();
    void allocateGraphicsMemory();
    void deInitializeFonts( );
    void initializeFonts( );
    void playSelect();
    bool isSelectPlaying();
    std::string getCollectionName();
    CollectionInfo *getCollection();
    void  setMinShowTime(float value);
    float getMinShowTime();
    void  menuScroll();
    void  highlightEnter();
    void  highlightExit();
    void  playlistEnter();
    void  playlistExit();
    void  menuJumpEnter();
    void  menuJumpExit();
    void  attractEnter( );
    void  attract( );
    void  attractExit( );
    void  jukeboxJump( );
    void  triggerEvent( std::string action );
    void  setText( std::string text, int id );
    void  addPlaylist();
    void  removePlaylist();
    void  togglePlaylist();
    void  updateLastPlayedPlaylist( Item *item );
    void  reallocateMenuSpritePoints();
    void  allocateVisibleRailSprites();
    bool  isMenuScrolling();
    bool  isPlaying();
    void  resetScrollPeriod();
    void  updateScrollPeriod();
    void  scroll(bool forward);
    bool  hasSubs();
    int   getLayoutWidth(int monitor);
    int   getLayoutHeight(int monitor);
    void  setLayoutWidth(int monitor, int width);
    void  setLayoutHeight(int monitor, int height);
    void  setJukebox();
    bool  isJukebox();
    bool  isJukeboxPlaying();
    void  skipForward( );
    void  skipBackward( );
    void  skipForwardp( );
    void  skipBackwardp( );
    void  pause( );
    void  restart( );
    unsigned long long getCurrent( );
    unsigned long long getDuration( );
    bool  isPaused( );

private:
    ScrollingList *getFocusedMenu();
    void playlistChange();
    std::string collectionName_;
    Configuration &config_;

    struct MenuInfo_S
    {
        CollectionInfo *collection;
        CollectionInfo::Playlists_T::iterator playlist; 
        bool queueDelete;
    };

    typedef std::vector< std::vector<ScrollingList *> > MenuVector_T;
    typedef std::list<MenuInfo_S> CollectionVector_T;

    std::vector<ScrollingList *> activeMenu_;
    unsigned int menuDepth_;
    MenuVector_T menus_;
    CollectionVector_T collections_;
    CollectionVector_T deleteCollections_;
    std::map<std::string, CollectionInfo *> railCollections_;
    std::function<CollectionInfo *(const std::string &)> railCollectionLoader_;

    static const unsigned int NUM_LAYERS = 20;
    std::vector<Component *> LayerComponents;
    std::list<ScrollingList *> deleteMenuList_;
    std::list<CollectionInfo *> deleteCollectionList_;

    bool independentRails_;
    unsigned int visibleRails_;
    bool loopRails_;
    bool categoryNavigation_ = false;
    // STREAM: category bar geometry supplied by the layout XML.
    struct CategoryBarStyle
    {
        float x = 0.0f;
        float y = 0.0f;
        float spacing = 0.0f;
        float underlineOffset = 0.0f;
        float underlineHeight = 0.0f;
        unsigned int labelLayer = 0;
        unsigned int underlineLayer = 0;
    };

    CategoryBarStyle categoryBarStyle_;
    Font *categoryBarSelectedFont_ = nullptr;
    Font *categoryBarInactiveFont_ = nullptr;
    Container *categoryBarBackgroundTemplate_ = nullptr;
    float categoryBarBackgroundAlpha_ = 0.0f;
    Container *categoryBarUnderlineTemplate_ = nullptr;
    float categoryBarUnderlineAlpha_ = 0.0f;
    Text *categoryBarTemplate_ = nullptr;
    std::vector<Text *> categoryBarLabels_;
    std::vector<std::string> categoryBarNames_;
    unsigned int categoryBarSelectedIndex_ = 0;
    float railSpacing_;
    float railAnchorY_;
    unsigned int focusedRailIndex_;
    std::vector<ScrollingList *> allocatedRailSprites_;
    bool scrollActive_;

    Item *selectedItem_;
    Text *textStatusComponent_;
    Sound *loadSoundChunk_;
    Sound *unloadSoundChunk_;
    Sound *highlightSoundChunk_;
    Sound *selectSoundChunk_;
    float minShowTime_;
    float elapsedTime_;
    CollectionInfo::Playlists_T::iterator playlist_;
    std::vector<int> layoutWidth_;
    std::vector<int> layoutHeight_;
    bool jukebox_;

};
