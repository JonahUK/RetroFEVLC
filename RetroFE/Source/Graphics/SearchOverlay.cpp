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

#include "SearchOverlay.h"
#include "Page.h"
#include "../Collection/Item.h"
#include "../Control/UserInput.h"
#include "../Database/Configuration.h"
#include "../Utility/Utils.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace
{

    // ------------------------------------------------------------------
    // Built-in 5x7 bitmap font. Only upper case letters are provided; text
    // is upper-cased before drawing. Using our own font keeps the overlay
    // independent of the layout's fonts.
    // ------------------------------------------------------------------
    struct GlyphDef
    {
        char        ch;
        const char *rows[7];
    };

    const GlyphDef kGlyphs[] =
    {
        { ' ', { "00000", "00000", "00000", "00000", "00000", "00000", "00000" } },
        { 'A', { "01110", "10001", "10001", "11111", "10001", "10001", "10001" } },
        { 'B', { "11110", "10001", "10001", "11110", "10001", "10001", "11110" } },
        { 'C', { "01110", "10001", "10000", "10000", "10000", "10001", "01110" } },
        { 'D', { "11110", "10001", "10001", "10001", "10001", "10001", "11110" } },
        { 'E', { "11111", "10000", "10000", "11110", "10000", "10000", "11111" } },
        { 'F', { "11111", "10000", "10000", "11110", "10000", "10000", "10000" } },
        { 'G', { "01110", "10001", "10000", "10111", "10001", "10001", "01111" } },
        { 'H', { "10001", "10001", "10001", "11111", "10001", "10001", "10001" } },
        { 'I', { "01110", "00100", "00100", "00100", "00100", "00100", "01110" } },
        { 'J', { "00111", "00010", "00010", "00010", "00010", "10010", "01100" } },
        { 'K', { "10001", "10010", "10100", "11000", "10100", "10010", "10001" } },
        { 'L', { "10000", "10000", "10000", "10000", "10000", "10000", "11111" } },
        { 'M', { "10001", "11011", "10101", "10101", "10001", "10001", "10001" } },
        { 'N', { "10001", "11001", "10101", "10011", "10001", "10001", "10001" } },
        { 'O', { "01110", "10001", "10001", "10001", "10001", "10001", "01110" } },
        { 'P', { "11110", "10001", "10001", "11110", "10000", "10000", "10000" } },
        { 'Q', { "01110", "10001", "10001", "10001", "10101", "10010", "01101" } },
        { 'R', { "11110", "10001", "10001", "11110", "10100", "10010", "10001" } },
        { 'S', { "01111", "10000", "10000", "01110", "00001", "00001", "11110" } },
        { 'T', { "11111", "00100", "00100", "00100", "00100", "00100", "00100" } },
        { 'U', { "10001", "10001", "10001", "10001", "10001", "10001", "01110" } },
        { 'V', { "10001", "10001", "10001", "10001", "10001", "01010", "00100" } },
        { 'W', { "10001", "10001", "10001", "10101", "10101", "10101", "01010" } },
        { 'X', { "10001", "10001", "01010", "00100", "01010", "10001", "10001" } },
        { 'Y', { "10001", "10001", "01010", "00100", "00100", "00100", "00100" } },
        { 'Z', { "11111", "00001", "00010", "00100", "01000", "10000", "11111" } },
        { '0', { "01110", "10001", "10011", "10101", "11001", "10001", "01110" } },
        { '1', { "00100", "01100", "00100", "00100", "00100", "00100", "01110" } },
        { '2', { "01110", "10001", "00001", "00010", "00100", "01000", "11111" } },
        { '3', { "11110", "00001", "00001", "01110", "00001", "00001", "11110" } },
        { '4', { "00010", "00110", "01010", "10010", "11111", "00010", "00010" } },
        { '5', { "11111", "10000", "11110", "00001", "00001", "10001", "01110" } },
        { '6', { "00110", "01000", "10000", "11110", "10001", "10001", "01110" } },
        { '7', { "11111", "00001", "00010", "00100", "01000", "01000", "01000" } },
        { '8', { "01110", "10001", "10001", "01110", "10001", "10001", "01110" } },
        { '9', { "01110", "10001", "10001", "01111", "00001", "00010", "01100" } },
        { '-', { "00000", "00000", "00000", "11111", "00000", "00000", "00000" } },
        { '\'', { "00100", "00100", "01000", "00000", "00000", "00000", "00000" } },
        { '.', { "00000", "00000", "00000", "00000", "00000", "01100", "01100" } },
        { ',', { "00000", "00000", "00000", "00000", "01100", "00100", "01000" } },
        { ':', { "00000", "01100", "01100", "00000", "01100", "01100", "00000" } },
        { '!', { "00100", "00100", "00100", "00100", "00100", "00000", "00100" } },
        { '?', { "01110", "10001", "00001", "00010", "00100", "00000", "00100" } },
        { '/', { "00001", "00010", "00010", "00100", "01000", "01000", "10000" } },
        { '&', { "01100", "10010", "10100", "01000", "10101", "10010", "01101" } },
        { '(', { "00010", "00100", "01000", "01000", "01000", "00100", "00010" } },
        { ')', { "01000", "00100", "00010", "00010", "00010", "00100", "01000" } },
        { '+', { "00000", "00100", "00100", "11111", "00100", "00100", "00000" } },
        { '_', { "00000", "00000", "00000", "00000", "00000", "00000", "11111" } },
        { '"', { "01010", "01010", "01010", "00000", "00000", "00000", "00000" } },
        { '*', { "00000", "10101", "01110", "11111", "01110", "10101", "00000" } },
        { '#', { "01010", "01010", "11111", "01010", "11111", "01010", "01010" } },
        { '=', { "00000", "00000", "11111", "00000", "11111", "00000", "00000" } },
        { '<', { "00010", "00100", "01000", "10000", "01000", "00100", "00010" } },
        { '>', { "01000", "00100", "00010", "00001", "00010", "00100", "01000" } }
    };

    const int kGlyphCount = static_cast<int>( sizeof( kGlyphs ) / sizeof( kGlyphs[0] ) );

    const GlyphDef *findGlyph( char c )
    {
        for ( int i = 0; i < kGlyphCount; ++i )
        {
            if ( kGlyphs[i].ch == c ) return &kGlyphs[i];
        }
        return NULL;
    }

    // Reduces a UTF-8 string to something the bitmap font can draw: upper
    // case ASCII, with every non-ASCII character shown as '?'.
    std::string decodeText( const std::string &s )
    {
        std::string out;
        out.reserve( s.size( ) );
        for ( size_t i = 0; i < s.size( ); ++i )
        {
            unsigned char c = static_cast<unsigned char>( s[i] );
            if ( c >= 0x80 )
            {
                if ( ( c & 0xC0 ) != 0x80 ) out += '?'; // lead byte; skip continuation bytes
            }
            else if ( c >= 32 && c < 127 )
            {
                out += static_cast<char>( std::toupper( c ) );
            }
        }
        return out;
    }

    const char *kCharRows[4] =
    {
        "1234567890",
        "QWERTYUIOP",
        "ASDFGHJKL-",
        "ZXCVBNM,.&"
    };

    const char *kSpecialLabels[5] =
    {
        "SPACE", "DEL", "CLEAR", "OK", "CANCEL"
    };

    inline int imax( int a, int b ) { return a > b ? a : b; }
    inline int imin( int a, int b ) { return a < b ? a : b; }

} // namespace


SearchOverlay::SearchOverlay( )
    : active_( false )
    , closing_( false )
    , outcome_( OutcomeNone )
    , chosenIndex_( 0 )
    , chosenGroup_( -1 )
    , zone_( ZoneKeys )
    , keyRow_( 0 )
    , keyCol_( 0 )
    , resultCursor_( 0 )
    , resultTop_( 0 )
    , visibleRows_( 8 )
    , listMode_( false )
    , typedMode_( false )
    , suppressText_( false )
    , textWasActive_( false )
    , openTicks_( 0 )
    , rend_( NULL )
    , fade_( 255 )
    , fontsResolved_( false )
    , fontsFailed_( false )
    , groupCount_( 0 )
{
}


SearchOverlay::~SearchOverlay( )
{
    releaseResources( );
}


void SearchOverlay::setListMode( bool enabled )
{
    listMode_ = enabled;
}


namespace
{
    std::map<std::string, SDL_Color> loadSearchTheme( const std::string &name );
}
void SearchOverlay::setTheme( const std::string &name )
{
    themeName_ = name.empty( ) ? "default" : name;
    themeColours_ = loadSearchTheme( themeName_ );
}


bool SearchOverlay::isActive( ) const
{
    return active_;
}


void SearchOverlay::setStatus( const std::string &text )
{
    status_ = text;
    if ( text.empty( ) && !active_ )
    {
        releaseResources( );
    }
}


void SearchOverlay::setFontPath( const std::string &path )
{
    if ( path != userFontPath_ )
    {
        userFontPath_  = path;
        fontsResolved_ = false;
        fontsFailed_   = false;
        releaseResources( );
    }
}


bool SearchOverlay::hasStatus( ) const
{
    return !status_.empty( );
}


unsigned int SearchOverlay::itemCount( ) const
{
    return static_cast<unsigned int>( entries_.size( ) );
}


bool SearchOverlay::wasChosen( ) const
{
    return outcome_ == OutcomeChosen;
}


unsigned int SearchOverlay::chosenIndex( ) const
{
    return chosenIndex_;
}


int SearchOverlay::chosenGroup( ) const
{
    return chosenGroup_;
}


// ----------------------------------------------------------------------
// Opening / closing
// ----------------------------------------------------------------------

void SearchOverlay::open( Page *page )
{
    std::vector<SearchMatcher::Entry> entries;

    unsigned int size     = page ? page->getCollectionSize( ) : 0;
    int          selected = page ? static_cast<int>( page->getSelectedIndex( ) ) : 0;

    entries.reserve( size );
    for ( unsigned int i = 0; i < size; ++i )
    {
        // Offsets are relative to the selected item; this walks the list in
        // its natural order and wraps around as needed.
        Item *item = page->getSelectedItem( static_cast<int>( i ) - selected );
        if ( !item ) continue;

        entries.push_back( SearchMatcher::makeEntry( i,
                                                     item->title,
                                                     item->fullTitle,
                                                     item->name,
                                                     item->manufacturer,
                                                     item->year,
                                                     item->genre ) );
    }

    open( entries );
}


void SearchOverlay::open( const std::vector<SearchMatcher::Entry> &entries )
{
    entries_ = entries;
    groupCount_ = 0;
    for ( size_t i = 0; i < entries_.size( ); ++i )
    {
        if ( entries_[i].group + 1 > groupCount_ ) groupCount_ = entries_[i].group + 1;
    }
    results_.clear( );
    query_.clear( );
    zone_          = ZoneKeys;
    keyRow_        = 0;
    keyCol_        = 0;
    resultCursor_  = 0;
    resultTop_     = 0;
    visibleRows_   = 8;
    typedMode_     = false;
    suppressText_  = false;
    closing_ = false;
    outcome_ = OutcomeNone;
    chosenIndex_   = 0;
    chosenGroup_   = -1;

    if ( !active_ )
    {
        textWasActive_ = ( SDL_IsTextInputActive( ) == SDL_TRUE );
    }
    SDL_StartTextInput( );

    // Ignore characters produced by the key that opened the overlay.
    SDL_FlushEvent( SDL_TEXTINPUT );
    SDL_FlushEvent( SDL_TEXTEDITING );
    openTicks_ = SDL_GetTicks( );

    active_ = true;
}


bool SearchOverlay::isClosing( ) const
{
    return closing_;
}


void SearchOverlay::completeClose( )
{
    if ( closing_ )
    {
        finish( OutcomeChosen );
    }
}


void SearchOverlay::cancel( )
{
    finish( OutcomeCancelled );
}


void SearchOverlay::finish( Outcome outcome )
{
    if ( active_ && !textWasActive_ )
    {
        SDL_StopTextInput( );
    }
    active_ = false;
    closing_ = false;
    outcome_ = outcome;
    releaseResources( );
}


// ----------------------------------------------------------------------
// Input
// ----------------------------------------------------------------------

bool SearchOverlay::fired( UserInput &input, int code, bool repeat ) const
{
    UserInput::KeyCode_E k = static_cast<UserInput::KeyCode_E>( code );
    return repeat ? input.keystate( k ) : input.newKeyPressed( k );
}


// Handles the controls from controls.conf. Returns true if one fired.
bool SearchOverlay::dispatchMapped( UserInput &input, bool repeat )
{
    // Select and back never auto-repeat.
    if ( !repeat && fired( input, UserInput::KeyCodeBack, false ) )
    {
        cancel( );
        return true;
    }
    if ( !repeat && fired( input, UserInput::KeyCodeSelect, false ) )
    {
        pressSelect( );
        return true;
    }
    if ( fired( input, UserInput::KeyCodeUp, repeat ) )
    {
        move( 0, -1 );
        return true;
    }
    if ( fired( input, UserInput::KeyCodeDown, repeat ) )
    {
        move( 0, 1 );
        return true;
    }
    if ( fired( input, UserInput::KeyCodeLeft, repeat ) )
    {
        move( -1, 0 );
        return true;
    }
    if ( fired( input, UserInput::KeyCodeRight, repeat ) )
    {
        move( 1, 0 );
        return true;
    }
    return false;
}


// Fallback for keys that are not part of controls.conf, so that a plain
// keyboard always works for typing and basic navigation.
bool SearchOverlay::handleRawKey( SDL_Keycode sym, bool repeat )
{
    switch ( sym )
    {
        case SDLK_ESCAPE:
            if ( !repeat ) cancel( );
            return true;

        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            // Physical Enter commits the typed query (the results are then
            // shown as a collection); with an empty query it presses the
            // highlighted on-screen key instead.
            if ( !repeat ) commit( );
            return true;

        case SDLK_BACKSPACE:
            backspace( );
            return true;

        case SDLK_TAB:
            if ( listMode_ && !repeat && !results_.empty( ) )
            {
                zone_ = ( zone_ == ZoneKeys ) ? ZoneResults : ZoneKeys;
                ensureCursorVisible( );
            }
            return true;

        case SDLK_UP:
            move( 0, -1 );
            return true;

        case SDLK_DOWN:
            move( 0, 1 );
            return true;

        case SDLK_LEFT:
            move( -1, 0 );
            return true;

        case SDLK_RIGHT:
            move( 1, 0 );
            return true;

        case SDLK_PAGEUP:
        case SDLK_PAGEDOWN:
            // Results card hidden: page keys are consumed without effect.
            return true;

        default:
            break;
    }
    return false;
}


void SearchOverlay::handleEvent( SDL_Event &e, UserInput &input )
{
    if ( !active_ || closing_ ) return;

    switch ( e.type )
    {
        case SDL_QUIT:
            cancel( );
            break;

        // Events that only change the input state
        case SDL_JOYDEVICEADDED:
        case SDL_JOYDEVICEREMOVED:
        case SDL_JOYBUTTONUP:
        case SDL_MOUSEBUTTONUP:
            input.update( e );
            break;

        case SDL_KEYUP:
            // SDL_TEXTINPUT always arrives right after its SDL_KEYDOWN, i.e.
            // before the key is released.
            suppressText_ = false;
            input.update( e );
            break;

        // Joystick / mouse events that may trigger a configured control
        case SDL_JOYAXISMOTION:
        case SDL_JOYBUTTONDOWN:
        case SDL_JOYHATMOTION:
        case SDL_MOUSEBUTTONDOWN:
            input.update( e );
            dispatchMapped( input, false );
            break;

        case SDL_KEYDOWN:
        {
            bool repeat = ( e.key.repeat != 0 );
            input.update( e );

            // Physical Enter is bound to "select" in controls.conf, so the
            // mapped handler would fire first and do nothing (with the OK key
            // highlighted). Handle Enter explicitly BEFORE mapped dispatch so
            // a plain keyboard Enter always commits the query.
            SDL_Keycode sym = e.key.keysym.sym;
            bool acted = false;
            if ( ( sym == SDLK_RETURN || sym == SDLK_KP_ENTER ) && !repeat )
            {
                commit( );
                acted = true;
            }
            else
            {
                acted = dispatchMapped( input, repeat );
                if ( !acted ) acted = handleRawKey( sym, repeat );
            }
            // A key that did something must not also type its character.
            // Only keys that produce a character are followed by an
            // SDL_TEXTINPUT event (not Backspace, Enter, arrows, ...), so
            // only those need the next text event dropped.
            suppressText_   = acted && sym >= 32 && sym < 127;
            break;
        }

        case SDL_TEXTINPUT:
        {
            if ( suppressText_ )
            {
                suppressText_ = false;
                break;
            }
            if ( SDL_GetTicks( ) - openTicks_ < 150 ) break;

            bool typed = false;
            for ( const char *p = e.text.text; *p != '\0'; ++p )
            {
                unsigned char c = static_cast<unsigned char>( *p );
                if ( c >= 32 && c < 127 )
                {
                    appendChar( static_cast<char>( c ) );
                    typed = true;
                }
            }
            if ( typed ) typedMode_ = true;
            break;
        }

        default:
            break;
    }
}


void SearchOverlay::move( int dx, int dy )
{
    // List Mode can navigate the live results; Page Mode retains keyboard navigation.
    typedMode_ = false;

    if ( listMode_ && zone_ == ZoneResults )
    {
        if ( results_.empty( ) )
        {
            zone_ = ZoneKeys;
            return;
        }

        const int count = static_cast<int>( results_.size( ) );

        if ( dy > 0 && resultCursor_ < count - 1 )
            ++resultCursor_;
        else if ( dy < 0 && resultCursor_ > 0 )
            --resultCursor_;
        else if ( dx < 0 )
            zone_ = ZoneKeys;

        ensureCursorVisible( );
        return;
    }

    if ( listMode_ && dx > 0 && !results_.empty( ) &&
         ( ( keyRow_ == kSpecialRow && keyCol_ / 2 == kSpecialKeys - 1 ) ||
           ( keyRow_ != kSpecialRow && keyCol_ == kCols - 1 ) ) )
    {
        zone_ = ZoneResults;
        ensureCursorVisible( );
        return;
    }

    if ( dy > 0 )
    {
        if ( keyRow_ < kSpecialRow ) ++keyRow_;
        else keyRow_ = 0;
    }
    else if ( dy < 0 )
    {
        keyRow_ = ( keyRow_ + kSpecialRow ) % ( kSpecialRow + 1 );
    }
    else if ( dx != 0 )
    {
        if ( keyRow_ == kSpecialRow )
        {
            int b = keyCol_ / 2 + dx;
            if ( b >= kSpecialKeys ) b = 0;
            if ( b < 0 ) b = kSpecialKeys - 1;
            keyCol_ = b * 2;
        }
        else
        {
            int c = keyCol_ + dx;
            if ( c >= kCols ) c = 0;
            if ( c < 0 ) c = kCols - 1;
            keyCol_ = c;
        }
    }
}


void SearchOverlay::pressSelect( )
{
    // In List Mode, Select chooses the highlighted result when focused.
    if ( listMode_ && zone_ == ZoneResults )
    {
        commit( );
        return;
    }

    // Page Mode and the List Mode keyboard retain their existing controls.
    pressKey( );
}


void SearchOverlay::pressKey( )
{
    if ( keyRow_ < kNumCharRows )
    {
        appendChar( kCharRows[keyRow_][keyCol_] );
        return;
    }

    switch ( keyCol_ / 2 )
    {
        case 0: appendChar( ' ' ); break;
        case 1: backspace( );      break;
        case 2: clearQuery( );     break;
        case 3: commit( );         break;
        case 4: cancel( );         break;
        default: break;
    }
}


// Accept the typed query; the caller shows every match as a collection.
void SearchOverlay::commit( )
{
    if ( query_.empty( ) ) return;
    if ( results_.empty( ) ) return;   // nothing matches: stay open and keep typing

    // In List Mode, submitting from the keyboard moves focus to the
    // results list. Only selecting a focused result can launch a game.
    if ( listMode_ && zone_ != ZoneResults )
    {
        zone_ = ZoneResults;
        ensureCursorVisible( );
        return;
    }

    // Page Mode keeps its original first-match selection.
    // List Mode uses the highlighted result when the results list has focus.
    size_t selected = 0;
    if ( listMode_ && zone_ == ZoneResults &&
         resultCursor_ >= 0 &&
         static_cast<size_t>( resultCursor_ ) < results_.size( ) )
    {
        selected = static_cast<size_t>( resultCursor_ );
    }

    chosenIndex_ = entries_[results_[selected]].index;
    chosenGroup_ = entries_[results_[selected]].group;
    if ( listMode_ )
    {
        outcome_ = OutcomeChosen;
        closing_ = true;
    }
    else
    {
        finish( OutcomeChosen );
    }
}


void SearchOverlay::appendChar( char c )
{
    if ( query_.size( ) >= kMaxQuery ) return;

    // No leading or doubled spaces
    if ( c == ' ' && ( query_.empty( ) || query_[query_.size( ) - 1] == ' ' ) ) return;

    query_ += c;
    refresh( );
}


void SearchOverlay::backspace( )
{
    if ( query_.empty( ) ) return;
    query_.erase( query_.size( ) - 1 );
    refresh( );
}


void SearchOverlay::clearQuery( )
{
    query_.clear( );
    refresh( );
}


void SearchOverlay::refresh( )
{
    SearchMatcher::filter( entries_, query_, results_ );
    resultCursor_ = 0;
    resultTop_    = 0;
    if ( results_.empty( ) && zone_ == ZoneResults )
    {
        zone_ = ZoneKeys;
    }
}


void SearchOverlay::ensureCursorVisible( )
{
    if ( visibleRows_ < 1 ) visibleRows_ = 1;

    if ( resultCursor_ < resultTop_ )
    {
        resultTop_ = resultCursor_;
    }
    else if ( resultCursor_ >= resultTop_ + visibleRows_ )
    {
        resultTop_ = resultCursor_ - visibleRows_ + 1;
    }
}


// ----------------------------------------------------------------------
// Drawing
// ----------------------------------------------------------------------

namespace
{

    SDL_Color rgb( int r, int g, int b )
    {
        SDL_Color c;
        c.r = static_cast<Uint8>( r );
        c.g = static_cast<Uint8>( g );
        c.b = static_cast<Uint8>( b );
        c.a = 255;
        return c;
    }

    // Parse #RRGGBB or R,G,B. Invalid values leave the colour unchanged.
    bool parseThemeColour( const std::string &value, SDL_Color &colour )
    {
        std::string s = value;
        const size_t first = s.find_first_not_of( " \t\r\n" );
        if ( first == std::string::npos ) return false;
        const size_t last = s.find_last_not_of( " \t\r\n" );
        s = s.substr( first, last - first + 1 );

        if ( s.size( ) == 7 && s[0] == '#' )
        {
            for ( size_t i = 1; i < 7; ++i )
                if ( !std::isxdigit( static_cast<unsigned char>( s[i] ) ) )
                    return false;

            unsigned int r = 0, g = 0, b = 0;
            if ( std::sscanf( s.c_str( ), "#%2x%2x%2x", &r, &g, &b ) != 3 )
                return false;

            colour = rgb( static_cast<int>( r ),
                          static_cast<int>( g ),
                          static_cast<int>( b ) );
            return true;
        }

        int r = 0, g = 0, b = 0;
        char comma1 = 0, comma2 = 0;
        std::istringstream input( s );

        if ( !( input >> r >> comma1 >> g >> comma2 >> b ) )
            return false;

        if ( comma1 != ',' || comma2 != ',' )
            return false;

        input >> std::ws;
        if ( !input.eof( ) )
            return false;

        if ( r < 0 || r > 255 || g < 0 || g > 255 || b < 0 || b > 255 )
            return false;

        colour = rgb( r, g, b );
        return true;
    }
    // Read a search theme from the RetroFE configuration root.
    // Invalid or missing entries are ignored.
    std::map<std::string, SDL_Color> loadSearchTheme( const std::string &name )
    {
        std::map<std::string, SDL_Color> colours;

        if ( name.empty( ) )
            return colours;

        // Theme names must be filenames, not paths.
        if ( name.find_first_not_of(
                 "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-" )
             != std::string::npos )
            return colours;

        const std::string path = Utils::combinePath(
            Configuration::absolutePath, "search_themes", name + ".conf" );

        std::ifstream file( path.c_str( ) );
        if ( !file )
            return colours;

        std::string line;
        while ( std::getline( file, line ) )
        {
            const size_t equals = line.find( '=' );
            if ( equals == std::string::npos )
                continue;

            std::string key = line.substr( 0, equals );
            std::string value = line.substr( equals + 1 );

            const size_t first = key.find_first_not_of( " \t\r\n" );
            if ( first == std::string::npos )
                continue;

            const size_t last = key.find_last_not_of( " \t\r\n" );
            key = key.substr( first, last - first + 1 );

            if ( key.empty( ) || key[0] == '#' || key[0] == ';' )
                continue;

            SDL_Color colour;
            if ( parseThemeColour( value, colour ) )
                colours[key] = colour;
        }

        return colours;
    }

    SDL_Color themeColour(
        const std::map<std::string, SDL_Color> &colours,
        const std::string &key,
        SDL_Color fallback )
    {
        std::map<std::string, SDL_Color>::const_iterator it = colours.find( key );
        return it == colours.end( ) ? fallback : it->second;
    }
    SDL_Color mix( SDL_Color a, SDL_Color b, int t256 )
    {
        return rgb( a.r + ( b.r - a.r ) * t256 / 256,
                    a.g + ( b.g - a.g ) * t256 / 256,
                    a.b + ( b.b - a.b ) * t256 / 256 );
    }

    // A stable, pleasant colour for a collection name (used for its tag).
    SDL_Color hueColor( const std::string &name )
    {
        unsigned int hash = 2166136261u;
        for ( size_t i = 0; i < name.size( ); ++i )
        {
            hash ^= static_cast<unsigned char>( name[i] );
            hash *= 16777619u;
        }
        float h = static_cast<float>( hash % 360 ) / 60.0f;
        float s = 0.50f;
        float v = 0.98f;
        int   i = static_cast<int>( h );
        float f = h - static_cast<float>( i );
        float p = v * ( 1.0f - s );
        float q = v * ( 1.0f - s * f );
        float t = v * ( 1.0f - s * ( 1.0f - f ) );
        float r = v, g = t, b = p;
        switch ( i % 6 )
        {
            case 0: r = v; g = t; b = p; break;
            case 1: r = q; g = v; b = p; break;
            case 2: r = p; g = v; b = t; break;
            case 3: r = p; g = q; b = v; break;
            case 4: r = t; g = p; b = v; break;
            default: r = v; g = p; b = q; break;
        }
        return rgb( static_cast<int>( r * 255 ), static_cast<int>( g * 255 ), static_cast<int>( b * 255 ) );
    }

    bool fileExists( const std::string &path )
    {
        if ( path.empty( ) ) return false;
        FILE *f = fopen( path.c_str( ), "rb" );
        if ( !f ) return false;
        fclose( f );
        return true;
    }

    // Cuts a UTF-8 string to at most n bytes without splitting a character.
    size_t utf8Boundary( const std::string &s, size_t n )
    {
        if ( n >= s.size( ) ) return s.size( );
        while ( n > 0 && ( static_cast<unsigned char>( s[n] ) & 0xC0 ) == 0x80 ) --n;
        return n;
    }

    int scaleAlpha( int a, int fade )
    {
        return a * fade / 255;
    }

} // namespace


struct SearchOverlay::Layout
{
    int      w, h, u, pad, gap;
    SDL_Rect panel;
    SDL_Rect titleRow;
    SDL_Rect field;
    SDL_Rect kbCard;
    SDL_Rect resultsCard;
    SDL_Rect footer;
    int      cell, keyGap, keysX, keysY;
    int      cardPad;
};


// ---- fonts -----------------------------------------------------------

void SearchOverlay::resolveFonts( )
{
    if ( fontsResolved_ ) return;
    fontsResolved_ = true;
    fontRegular_.clear( );
    fontBold_.clear( );

    if ( !TTF_WasInit( ) && TTF_Init( ) != 0 )
    {
        fontsFailed_ = true;
        return;
    }

    if ( fileExists( userFontPath_ ) )
    {
        fontRegular_ = userFontPath_;
        fontBold_    = userFontPath_;
        return;
    }

    std::vector<std::string> regular;
    std::vector<std::string> bold;

    const char *win = getenv( "WINDIR" );
    if ( !win ) win = getenv( "SystemRoot" );
    std::string fonts = std::string( win ? win : "C:\\Windows" ) + "\\Fonts\\";

    const char *winNames[][2] =
    {
        { "segoeui.ttf",  "segoeuib.ttf"  },
        { "calibri.ttf",  "calibrib.ttf"  },
        { "tahoma.ttf",   "tahomabd.ttf"  },
        { "arial.ttf",    "arialbd.ttf"   },
        { "verdana.ttf",  "verdanab.ttf"  }
    };
    for ( size_t i = 0; i < sizeof( winNames ) / sizeof( winNames[0] ); ++i )
    {
        regular.push_back( fonts + winNames[i][0] );
        bold.push_back( fonts + winNames[i][1] );
    }

    regular.push_back( "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf" );
    bold.push_back( "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf" );
    regular.push_back( "/usr/share/fonts/TTF/DejaVuSans.ttf" );
    bold.push_back( "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf" );
    regular.push_back( "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf" );
    bold.push_back( "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf" );
    regular.push_back( "/System/Library/Fonts/Supplemental/Arial.ttf" );
    bold.push_back( "/System/Library/Fonts/Supplemental/Arial Bold.ttf" );

    for ( size_t i = 0; i < regular.size( ); ++i )
    {
        if ( fileExists( regular[i] ) )
        {
            fontRegular_ = regular[i];
            fontBold_    = fileExists( bold[i] ) ? bold[i] : regular[i];
            return;
        }
    }
}


bool SearchOverlay::useTtf( ) const
{
    return fontsResolved_ && !fontsFailed_ && !fontRegular_.empty( );
}


TTF_Font *SearchOverlay::getFont( int px, bool bold )
{
    if ( !useTtf( ) ) return NULL;
    if ( px < 6 ) px = 6;

    int key = px * 2 + ( bold ? 1 : 0 );
    std::map<int, TTF_Font *>::iterator it = fonts_.find( key );
    if ( it != fonts_.end( ) ) return it->second;

    TTF_Font *font = TTF_OpenFont( ( bold ? fontBold_ : fontRegular_ ).c_str( ), px );
    if ( !font && bold ) font = TTF_OpenFont( fontRegular_.c_str( ), px );
    if ( !font )
    {
        fontsFailed_ = true; // fall back to the bitmap font
        return NULL;
    }
    fonts_[key] = font;
    return font;
}


void SearchOverlay::releaseResources( )
{
    for ( std::map<std::string, TextTex>::iterator it = textCache_.begin( ); it != textCache_.end( ); ++it )
    {
        if ( it->second.tex ) SDL_DestroyTexture( it->second.tex );
    }
    textCache_.clear( );
    fitCache_.clear( );

    for ( std::map<int, SDL_Texture *>::iterator it = discs_.begin( ); it != discs_.end( ); ++it )
    {
        if ( it->second ) SDL_DestroyTexture( it->second );
    }
    discs_.clear( );

    if ( TTF_WasInit( ) )
    {
        for ( std::map<int, TTF_Font *>::iterator it = fonts_.begin( ); it != fonts_.end( ); ++it )
        {
            if ( it->second ) TTF_CloseFont( it->second );
        }
    }
    fonts_.clear( );
}


// ---- text ------------------------------------------------------------

int SearchOverlay::bitmapScale( int px ) const
{
    return imax( 1, px / 9 );
}


int SearchOverlay::textW( const std::string &s, int px, bool bold )
{
    if ( s.empty( ) ) return 0;

    TTF_Font *font = getFont( px, bold );
    if ( font )
    {
        int w = 0;
        int h = 0;
        if ( TTF_SizeUTF8( font, s.c_str( ), &w, &h ) == 0 ) return w;
        return 0;
    }

    int scale = bitmapScale( px );
    std::string d = decodeText( s );
    if ( d.empty( ) ) return 0;
    return static_cast<int>( d.size( ) ) * 6 * scale - scale;
}


int SearchOverlay::textH( int px, bool bold )
{
    TTF_Font *font = getFont( px, bold );
    if ( font ) return TTF_FontHeight( font );
    return 7 * bitmapScale( px );
}


// Shortens text with "..." so that it is at most maxW pixels wide.
std::string SearchOverlay::fitTo( const std::string &s, int px, bool bold, int maxW )
{
    if ( maxW <= 0 || s.empty( ) ) return "";

    std::string key = std::to_string( px ) + ( bold ? "b" : "r" ) + ":" + std::to_string( maxW ) + ":" + s;
    std::map<std::string, std::string>::iterator it = fitCache_.find( key );
    if ( it != fitCache_.end( ) ) return it->second;

    std::string result;
    if ( textW( s, px, bold ) <= maxW )
    {
        result = s;
    }
    else
    {
        const std::string ell = "...";
        int ew = textW( ell, px, bold );
        if ( ew <= maxW )
        {
            size_t lo = 0;
            size_t hi = s.size( );
            while ( lo < hi )
            {
                size_t mid = ( lo + hi + 1 ) / 2;
                size_t cut = utf8Boundary( s, mid );
                if ( textW( s.substr( 0, cut ), px, bold ) + ew <= maxW ) lo = mid;
                else hi = mid - 1;
            }
            size_t cut = utf8Boundary( s, lo );
            std::string head = s.substr( 0, cut );
            while ( !head.empty( ) && head[head.size( ) - 1] == ' ' ) head.erase( head.size( ) - 1 );
            result = head + ell;
        }
    }

    if ( fitCache_.size( ) > 4000 ) fitCache_.clear( );
    fitCache_[key] = result;
    return result;
}


void SearchOverlay::drawBitmapText( int x, int y, int scale, const std::string &s, SDL_Color c, int alpha )
{
    std::string d = decodeText( s );
    if ( d.empty( ) ) return;

    std::vector<SDL_Rect> rects;
    rects.reserve( d.size( ) * 16 );
    for ( size_t i = 0; i < d.size( ); ++i )
    {
        const GlyphDef *g = findGlyph( d[i] );
        if ( !g ) g = findGlyph( '?' );
        if ( !g ) continue;

        int gx = x + static_cast<int>( i ) * 6 * scale;
        for ( int row = 0; row < 7; ++row )
        {
            for ( int col = 0; col < 5; ++col )
            {
                if ( g->rows[row][col] == '1' )
                {
                    SDL_Rect px;
                    px.x = gx + col * scale;
                    px.y = y + row * scale;
                    px.w = scale;
                    px.h = scale;
                    rects.push_back( px );
                }
            }
        }
    }
    if ( rects.empty( ) ) return;
    SDL_SetRenderDrawColor( rend_, c.r, c.g, c.b, static_cast<Uint8>( alpha ) );
    SDL_RenderFillRects( rend_, &rects[0], static_cast<int>( rects.size( ) ) );
}


// Draws text with its top-left corner at (x, y).
void SearchOverlay::text( int x, int y, int px, bool bold, const std::string &s, SDL_Color c, int alpha )
{
    if ( s.empty( ) || !rend_ ) return;
    alpha = scaleAlpha( alpha, fade_ );
    if ( alpha <= 0 ) return;

    TTF_Font *font = getFont( px, bold );
    if ( !font )
    {
        drawBitmapText( x, y, bitmapScale( px ), s, c, alpha );
        return;
    }

    std::string key = std::to_string( px ) + ( bold ? "b" : "r" ) + ":" + s;
    std::map<std::string, TextTex>::iterator it = textCache_.find( key );
    if ( it == textCache_.end( ) )
    {
        if ( textCache_.size( ) > 800 )
        {
            for ( it = textCache_.begin( ); it != textCache_.end( ); ++it )
            {
                if ( it->second.tex ) SDL_DestroyTexture( it->second.tex );
            }
            textCache_.clear( );
        }

        TextTex tt;
        tt.tex = NULL;
        tt.w   = 0;
        tt.h   = 0;

        SDL_Color white = rgb( 255, 255, 255 );
        SDL_Surface *surface = TTF_RenderUTF8_Blended( font, s.c_str( ), white );
        if ( surface )
        {
            tt.tex = SDL_CreateTextureFromSurface( rend_, surface );
            tt.w   = surface->w;
            tt.h   = surface->h;
            SDL_FreeSurface( surface );
            if ( tt.tex ) SDL_SetTextureBlendMode( tt.tex, SDL_BLENDMODE_BLEND );
        }
        it = textCache_.insert( std::make_pair( key, tt ) ).first;
    }

    if ( !it->second.tex ) return;

    SDL_Rect dst;
    dst.x = x;
    dst.y = y;
    dst.w = it->second.w;
    dst.h = it->second.h;
    SDL_SetTextureColorMod( it->second.tex, c.r, c.g, c.b );
    SDL_SetTextureAlphaMod( it->second.tex, static_cast<Uint8>( alpha ) );
    SDL_RenderCopy( rend_, it->second.tex, NULL, &dst );
}


// Text where the characters flagged in 'mask' are drawn highlighted (bold, other colour).
void SearchOverlay::textRuns( int x, int y, int px, const std::string &s, const std::vector<bool> &mask,
                              SDL_Color normal, SDL_Color hit, int alpha )
{
    size_t i = 0;
    while ( i < s.size( ) )
    {
        bool   h = i < mask.size( ) && mask[i];
        size_t j = i;
        while ( j < s.size( ) && ( ( j < mask.size( ) && mask[j] ) == h ) ) ++j;

        std::string run = s.substr( i, j - i );
        text( x, y, px, h, run, h ? hit : normal, alpha );
        x += textW( run, px, h );
        i = j;
    }
}


// ---- shapes ----------------------------------------------------------

void SearchOverlay::rect( int x, int y, int w, int h, SDL_Color c, int alpha )
{
    alpha = scaleAlpha( alpha, fade_ );
    if ( w <= 0 || h <= 0 || alpha <= 0 ) return;
    SDL_Rect r;
    r.x = x;
    r.y = y;
    r.w = w;
    r.h = h;
    SDL_SetRenderDrawColor( rend_, c.r, c.g, c.b, static_cast<Uint8>( alpha ) );
    SDL_RenderFillRect( rend_, &r );
}


// A white, anti-aliased disc; quarters of it make the corners of rounded rectangles.
SDL_Texture *SearchOverlay::discTexture( int radius )
{
    std::map<int, SDL_Texture *>::iterator it = discs_.find( radius );
    if ( it != discs_.end( ) ) return it->second;

    int d = radius * 2;
    std::vector<Uint8> pixels( static_cast<size_t>( d ) * d * 4 );
    const int ss = 4;
    for ( int y = 0; y < d; ++y )
    {
        for ( int x = 0; x < d; ++x )
        {
            int inside = 0;
            for ( int sy = 0; sy < ss; ++sy )
            {
                for ( int sx = 0; sx < ss; ++sx )
                {
                    float fx = static_cast<float>( x ) + ( static_cast<float>( sx ) + 0.5f ) / ss - static_cast<float>( radius );
                    float fy = static_cast<float>( y ) + ( static_cast<float>( sy ) + 0.5f ) / ss - static_cast<float>( radius );
                    if ( fx * fx + fy * fy <= static_cast<float>( radius * radius ) ) ++inside;
                }
            }
            size_t o = ( static_cast<size_t>( y ) * d + x ) * 4;
            pixels[o + 0] = 255;
            pixels[o + 1] = 255;
            pixels[o + 2] = 255;
            pixels[o + 3] = static_cast<Uint8>( 255 * inside / ( ss * ss ) );
        }
    }

    SDL_Texture *tex = SDL_CreateTexture( rend_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, d, d );
    if ( tex )
    {
        SDL_UpdateTexture( tex, NULL, &pixels[0], d * 4 );
        SDL_SetTextureBlendMode( tex, SDL_BLENDMODE_BLEND );
    }
    discs_[radius] = tex;
    return tex;
}


void SearchOverlay::rrect( int x, int y, int w, int h, int radius, SDL_Color c, int alpha )
{
    alpha = scaleAlpha( alpha, fade_ );
    if ( w <= 0 || h <= 0 || alpha <= 0 ) return;

    radius = imin( radius, imin( w / 2, h / 2 ) );
    SDL_Texture *disc = radius >= 2 ? discTexture( radius ) : NULL;
    if ( !disc )
    {
        int saved = fade_;
        fade_ = 255;
        rect( x, y, w, h, c, alpha );
        fade_ = saved;
        return;
    }

    int saved = fade_;
    fade_ = 255; // alpha is already scaled
    rect( x + radius, y, w - 2 * radius, h, c, alpha );
    rect( x, y + radius, radius, h - 2 * radius, c, alpha );
    rect( x + w - radius, y + radius, radius, h - 2 * radius, c, alpha );
    fade_ = saved;

    SDL_SetTextureColorMod( disc, c.r, c.g, c.b );
    SDL_SetTextureAlphaMod( disc, static_cast<Uint8>( alpha ) );

    int r = radius;
    SDL_Rect src;
    SDL_Rect dst;
    src.w = src.h = dst.w = dst.h = r;

    src.x = 0; src.y = 0; dst.x = x;         dst.y = y;
    SDL_RenderCopy( rend_, disc, &src, &dst );
    src.x = r; src.y = 0; dst.x = x + w - r; dst.y = y;
    SDL_RenderCopy( rend_, disc, &src, &dst );
    src.x = 0; src.y = r; dst.x = x;         dst.y = y + h - r;
    SDL_RenderCopy( rend_, disc, &src, &dst );
    src.x = r; src.y = r; dst.x = x + w - r; dst.y = y + h - r;
    SDL_RenderCopy( rend_, disc, &src, &dst );
}


// A soft coloured halo around a rounded rectangle.
void SearchOverlay::glow( int x, int y, int w, int h, int radius, SDL_Color c, int strength )
{
    const int layers = 5;
    int step = imax( 2, radius / 3 );
    for ( int i = layers; i >= 1; --i )
    {
        int g = i * step;
        rrect( x - g, y - g, w + 2 * g, h + 2 * g, radius + g, c, strength / ( i + 1 ) );
    }
}


// A soft drop shadow below a rounded rectangle.
void SearchOverlay::shadow( int x, int y, int w, int h, int radius, int spread )
{
    SDL_Color black = rgb( 0, 0, 0 );
    for ( int i = spread; i >= 1; --i )
    {
        rrect( x - i, y - i + spread / 2, w + 2 * i, h + 2 * i, radius + i, black, 9 );
    }
}


// ---- layout ----------------------------------------------------------

void SearchOverlay::computeLayout( int w, int h, Layout &l ) const
{
    l.w = w;
    l.h = h;
    l.u = imax( 8, h / 54 );
    int u = l.u;

    l.pad = u * 3 / 2;
    l.gap = u;

    int mx = imax( u, w * 5 / 100 );
    int my = imax( u, h * 6 / 100 );
    l.panel.x = mx;
    l.panel.y = my;
    l.panel.w = w - 2 * mx;
    l.panel.h = h - 2 * my;

    int innerX = l.panel.x + l.pad;
    int innerW = l.panel.w - 2 * l.pad;

    l.titleRow.x = innerX;
    l.titleRow.y = l.panel.y + l.pad;
    l.titleRow.w = innerW;
    l.titleRow.h = u * 3;

    l.field.x = innerX;
    l.field.y = l.titleRow.y + l.titleRow.h + u / 2;
    l.field.w = innerW;
    l.field.h = u * 4;

    l.footer.x = innerX;
    l.footer.w = innerW;
    l.footer.h = u * 3;
    l.footer.y = l.panel.y + l.panel.h - l.pad - l.footer.h;

    int contentY = l.field.y + l.field.h + l.gap;
    int contentH = l.footer.y - l.gap - contentY;
    if ( contentH < 1 ) contentH = 1;

    // The keyboard fills the whole content area; the results card is not drawn
    // (the caller shows the matches as a temporary collection page instead).
    l.kbCard.x = innerX;
    l.kbCard.y = contentY;
    l.kbCard.w = innerW;
    l.kbCard.h = contentH;
    l.resultsCard = { 0, 0, 0, 0 };

    if ( listMode_ )
    {
        // Divide the available space between keyboard and live results.
        const int splitGap = l.gap;
        const int keyboardW = ( innerW - splitGap ) * 52 / 100;

        l.kbCard.w = keyboardW;
        l.resultsCard.x = innerX + keyboardW + splitGap;
        l.resultsCard.y = contentY;
        l.resultsCard.w = innerW - keyboardW - splitGap;
        l.resultsCard.h = contentH;
    }

    l.cardPad = u;
    int availW = l.kbCard.w - 2 * l.cardPad;
    int availH = l.kbCard.h - 2 * l.cardPad;
    l.cell   = imax( 6, imin( availW / kCols, availH / ( kSpecialRow + 1 ) ) );
    l.keyGap = imax( 3, l.cell / 9 );
    l.keysX  = l.kbCard.x + ( l.kbCard.w - l.cell * kCols ) / 2;
    l.keysY  = l.kbCard.y + ( l.kbCard.h - l.cell * ( kSpecialRow + 1 ) ) / 2;
}


void SearchOverlay::drawStatusBox( int w, int h )
{
    int u   = imax( 8, h / 54 );
    int px  = u * 3 / 2;
    int pad = u * 2;

    std::string msg = fitTo( status_, px, false, w * 7 / 10 - 4 * pad );
    int tw   = textW( msg, px, false );
    int th   = textH( px, false );
    int spin = u * 3;
    int bw   = tw + spin + 3 * pad;
    int bh   = imax( th, spin ) + 2 * pad;
    int bx   = ( w - bw ) / 2;
    int by   = ( h - bh ) / 2;

    rect( 0, 0, w, h, themeColour( themeColours_, "statusBackdrop", rgb( 0, 0, 0 ) ), 150 );
    shadow( bx, by, bw, bh, u, u );
    rrect( bx, by, bw, bh, u, themeColour( themeColours_, "statusBorder", rgb( 58, 66, 96 ) ), 255 );
    rrect( bx + 1, by + 1, bw - 2, bh - 2, u, themeColour( themeColours_, "statusPanel", rgb( 24, 28, 42 ) ), 255 );

    // little spinner
    int   cx    = bx + pad + spin / 2;
    int   cy    = by + bh / 2;
    int   ring  = spin / 2 - u / 3;
    int   dot   = imax( 2, u / 3 );
    Uint32 tick = SDL_GetTicks( ) / 90;
    for ( int i = 0; i < 8; ++i )
    {
        float a  = 6.2831853f * static_cast<float>( i ) / 8.0f;
        int   dx = static_cast<int>( static_cast<float>( ring ) * sinf( a ) );
        int   dy = static_cast<int>( -static_cast<float>( ring ) * cosf( a ) );
        int   age = static_cast<int>( ( tick + 8 - static_cast<Uint32>( i ) ) % 8 );
        rrect( cx + dx - dot, cy + dy - dot, dot * 2, dot * 2, dot, themeColour( themeColours_, "statusSpinner", rgb( 255, 191, 64 ) ), 255 - age * 28 );
    }

    text( bx + pad + spin + pad / 2, cy - th / 2, px, false, msg, themeColour( themeColours_, "statusText", rgb( 236, 239, 248 ) ) );
}


// ---- the overlay -----------------------------------------------------

void SearchOverlay::draw( SDL_Renderer *r )
{
    if ( !r ) return;
    if ( !active_ && status_.empty( ) ) return;

    int w = 0;
    int h = 0;
    SDL_GetRendererOutputSize( r, &w, &h );
    if ( w <= 0 || h <= 0 ) return;

    rend_ = r;
    resolveFonts( );

    SDL_BlendMode prevBlend = SDL_BLENDMODE_NONE;
    SDL_GetRenderDrawBlendMode( r, &prevBlend );
    SDL_SetRenderDrawBlendMode( r, SDL_BLENDMODE_BLEND );

    // fade in
    fade_ = 255;
    if ( active_ )
    {
        Uint32 elapsed = SDL_GetTicks( ) - openTicks_;
        if ( elapsed < 180 )
        {
            float t = static_cast<float>( elapsed ) / 180.0f;
            fade_   = static_cast<int>( 255.0f * ( 1.0f - ( 1.0f - t ) * ( 1.0f - t ) ) );
        }
    }

    if ( !active_ )
    {
        drawStatusBox( w, h );
        SDL_SetRenderDrawBlendMode( r, prevBlend );
        return;
    }

    Layout l;
    computeLayout( w, h, l );
    const int u = l.u;

    visibleRows_ = imax( 1, ( 8 * u ) / ( 4 * u ) );
    ensureCursorVisible( );

    // palette: missing or invalid theme entries use original colours.
    const std::map<std::string, SDL_Color> &theme = themeColours_;
    const SDL_Color accent   = themeColour( theme, "accent",   rgb( 255, 191, 64 ) );
    const SDL_Color accentDk = themeColour( theme, "accentDk", rgb( 28, 22, 8 ) );
    const SDL_Color panelCol = themeColour( theme, "panel",    rgb( 22, 26, 40 ) );
    const SDL_Color border   = themeColour( theme, "border",   rgb( 58, 66, 98 ) );
    const SDL_Color cardCol  = themeColour( theme, "card",     rgb( 29, 34, 52 ) );
    const SDL_Color fieldCol = themeColour( theme, "field",    rgb( 13, 16, 26 ) );
    const SDL_Color textHi   = themeColour( theme, "textHi",   rgb( 236, 239, 248 ) );
    const SDL_Color textMid  = themeColour( theme, "textMid",  rgb( 152, 162, 192 ) );
    const SDL_Color textLow  = themeColour( theme, "textLow",  rgb( 100, 110, 142 ) );
    const bool      ttf       = useTtf( );
    const std::string dot     = ttf ? "  \xC2\xB7  " : "  -  ";

    // ---- backdrop + panel ----
    rect( 0, 0, w, h, themeColour( theme, "backdrop", rgb( 2, 3, 8 ) ), 185 );
    shadow( l.panel.x, l.panel.y, l.panel.w, l.panel.h, u * 2, u * 3 / 2 );
    rrect( l.panel.x, l.panel.y, l.panel.w, l.panel.h, u * 2, border, 255 );
    rrect( l.panel.x + 1, l.panel.y + 1, l.panel.w - 2, l.panel.h - 2, u * 2 - 1, panelCol, 255 );

    // ---- title row ----
    {
        int px  = u * 17 / 10;
        int th  = textH( px, true );
        text( l.titleRow.x + u / 2, l.titleRow.y + ( l.titleRow.h - th ) / 2, px, true, "Search", textHi );

        std::string info = std::to_string( entries_.size( ) ) + " games";
        if ( groupCount_ > 0 )
        {
            info = std::to_string( groupCount_ ) + ( groupCount_ == 1 ? " collection" : " collections" ) + dot + info;
        }
        int ipx = u;
        int iw  = textW( info, ipx, false );
        text( l.titleRow.x + l.titleRow.w - iw - u / 2,
              l.titleRow.y + ( l.titleRow.h - textH( ipx, false ) ) / 2, ipx, false, info, textLow );
    }

    // ---- query field ----
    {
        const SDL_Rect &f = l.field;
        int rad = f.h / 2;
        int bw  = imax( 2, u / 8 );
        glow( f.x, f.y, f.w, f.h, rad, accent, 26 );
        rrect( f.x, f.y, f.w, f.h, rad, accent, 150 );
        rrect( f.x + bw, f.y + bw, f.w - 2 * bw, f.h - 2 * bw, rad - bw, fieldCol, 255 );

        // magnifier icon
        int cx = f.x + u * 5 / 2;
        int cy = f.y + f.h / 2 - u / 5;
        int ro = u * 8 / 10;
        int ri = ro - imax( 2, u / 7 );
        rrect( cx - ro, cy - ro, ro * 2, ro * 2, ro, textMid, 255 );
        rrect( cx - ri, cy - ri, ri * 2, ri * 2, ri, fieldCol, 255 );
        int hr = imax( 2, u / 7 );
        for ( int i = 0; i <= 6; ++i )
        {
            int hx = cx + ro * 7 / 10 + i * u / 9;
            int hy = cy + ro * 7 / 10 + i * u / 9;
            rrect( hx - hr, hy - hr, hr * 2, hr * 2, hr, textMid, 255 );
        }

        // result count chip (right)
        std::string chip;
        SDL_Color   chipCol = accent;
        if ( query_.empty( ) )         chip = "";
        else if ( results_.empty( ) )  { chip = "No matches"; chipCol = themeColour( theme, "noMatches", rgb( 255, 120, 120 ) ); }
        else chip = std::to_string( results_.size( ) ) + ( results_.size( ) == 1 ? " result" : " results" );

        int chipLeft = f.x + f.w - u;
        if ( !chip.empty( ) )
        {
            int cpx = u * 11 / 10;
            int cw  = textW( chip, cpx, true ) + u * 2;
            int ch  = u * 2 + u / 2;
            int chx = f.x + f.w - u * 3 / 2 - cw;
            rrect( chx, f.y + ( f.h - ch ) / 2, cw, ch, ch / 2, chipCol, 40 );
            text( chx + u, f.y + ( f.h - textH( cpx, true ) ) / 2, cpx, true, chip, chipCol );
            chipLeft = chx - u;
        }

        // query text
        int qpx = u * 18 / 10;
        int qx  = f.x + u * 5;
        int qth = textH( qpx, false );
        int qy  = f.y + ( f.h - qth ) / 2;
        int avail = chipLeft - qx - u;

        std::string q = query_;
        while ( q.size( ) > 1 && textW( q, qpx, false ) > avail ) q.erase( 0, 1 );

        if ( query_.empty( ) )
        {
            text( qx, qy, qpx, false, fitTo( "Type a game name, or pick letters below", qpx, false, avail ), textLow );
        }
        else
        {
            text( qx, qy, qpx, false, q, textHi );
        }

        if ( ( SDL_GetTicks( ) / 500 ) % 2 == 0 )
        {
            int caretW = imax( 2, u / 6 );
            int cx2    = qx + ( query_.empty( ) ? 0 : textW( q, qpx, false ) ) + u / 5;
            rrect( cx2, f.y + ( f.h - qth ) / 2 + qth / 8, caretW, qth * 3 / 4, caretW / 2, accent, 255 );
        }
    }

    // ---- cards ----
    shadow( l.kbCard.x, l.kbCard.y, l.kbCard.w, l.kbCard.h, u, u / 2 );
    rrect( l.kbCard.x, l.kbCard.y, l.kbCard.w, l.kbCard.h, u, cardCol, 255 );

    // ---- on-screen keyboard ----
    for ( int row = 0; row <= kSpecialRow; ++row )
    {
        int buttons = ( row == kSpecialRow ) ? kSpecialKeys : kCols;
        for ( int b = 0; b < buttons; ++b )
        {
            int cols  = ( row == kSpecialRow ) ? 2 : 1;
            int x     = l.keysX + b * cols * l.cell + l.keyGap / 2;
            int y     = l.keysY + row * l.cell + l.keyGap / 2;
            int bw    = cols * l.cell - l.keyGap;
            int bh    = l.cell - l.keyGap;
            int depth = imax( 2, l.cell / 14 );
            int rad   = imax( 3, l.cell / 5 );
            int faceH = bh - depth;

            bool focused = false;
            if ( zone_ == ZoneKeys && row == keyRow_ )
            {
                int fb  = ( row == kSpecialRow ) ? keyCol_ / 2 : keyCol_;
                focused = ( fb == b );
            }

            SDL_Color face = themeColour( theme, "keyFace", rgb( 54, 62, 90 ) );
            SDL_Color edge = themeColour( theme, "keyEdge", rgb( 34, 39, 60 ) );
            if ( row == kSpecialRow )
            {
                face = themeColour( theme, "specialFace", rgb( 66, 75, 106 ) );
                edge = themeColour( theme, "specialEdge", rgb( 40, 46, 70 ) );
                if ( b == 3 ) { face = themeColour( theme, "okFace", rgb( 48, 128, 98 ) ); edge = themeColour( theme, "okEdge", rgb( 30, 84, 64 ) ); }  // OK
                if ( b == 4 ) { face = themeColour( theme, "cancelFace", rgb( 132, 64, 72 ) ); edge = themeColour( theme, "cancelEdge", rgb( 90, 40, 47 ) ); }  // CANCEL
            }
            SDL_Color label = textHi;

            if ( focused )
            {
                glow( x, y, bw, bh, rad, accent, 70 );
                face  = accent;
                edge  = themeColour( theme, "focusEdge", rgb( 190, 128, 24 ) );
                label = accentDk;
            }

            rrect( x, y + depth, bw, faceH, rad, edge, 255 );
            rrect( x, y, bw, faceH, rad, face, 255 );
            // light top edge
            rrect( x + 2, y + 1, bw - 4, imax( 2, faceH / 3 ), rad, rgb( 255, 255, 255 ), focused ? 38 : 16 );

            std::string text_;
            int         px;
            if ( row == kSpecialRow )
            {
                static const char *names[kSpecialKeys] = { "SPACE", "DEL", "CLEAR", "OK", "CANCEL" };
                text_ = names[b];
                px    = imax( 7, l.cell * 27 / 100 );
                while ( px > 7 && textW( text_, px, true ) > bw - l.keyGap * 2 ) --px;
            }
            else
            {
                text_ = std::string( 1, kCharRows[row][b] );
                px    = imax( 7, l.cell * 42 / 100 );
            }

            int tw = textW( text_, px, true );
            int th = textH( px, true );
            text( x + ( bw - tw ) / 2, y + ( faceH - th ) / 2, px, true, text_, label );
        }
    }

    // ---- live list results ----
    if ( listMode_ && l.resultsCard.w > 0 )
    {
        const SDL_Rect &rc = l.resultsCard;

        shadow( rc.x, rc.y, rc.w, rc.h, u, u / 2 );
        rrect( rc.x, rc.y, rc.w, rc.h, u, cardCol, 255 );

        const int inset = u;
        const int titlePx = imax( 10, u * 13 / 10 );
        const int itemPx = imax( 10, u * 12 / 10 );
        const int detailPx = imax( 9, u * 9 / 10 );
        const int rowH = imax( u * 4, textH( itemPx, true ) +
                              textH( detailPx, false ) + u / 2 );

        const int headingY = rc.y + inset;
        text( rc.x + inset, headingY, titlePx, true, "MATCHING GAMES", textHi );

        const int listY = headingY + textH( titlePx, true ) + u;
        const int listH = imax( 0, rc.y + rc.h - inset - listY );
        const int rows = imax( 1, listH / rowH );
        visibleRows_ = rows;
        ensureCursorVisible( );

        if ( query_.empty( ) )
        {
            text( rc.x + inset, listY, detailPx, false,
                  fitTo( "Enter a game name to see matches",
                         detailPx, false, rc.w - 2 * inset ), textMid );
        }
        else if ( results_.empty( ) )
        {
            text( rc.x + inset, listY, detailPx, false,
                  "No matching games", textMid );
        }
        else
        {
            for ( int i = 0; i < rows; ++i )
            {
                const size_t resultPos = static_cast<size_t>( resultTop_ + i );
                if ( resultPos >= results_.size( ) ) break;

                const SearchMatcher::Entry &entry = entries_[results_[resultPos]];
                const int y = listY + i * rowH;
                const int maxTextW = imax( 1, rc.w - 2 * inset );

                // Highlight the selected result only in List Mode.
                if ( static_cast<int>( resultPos ) == resultCursor_ )
                {
                    const SDL_Color highlight = zone_ == ZoneResults
                        ? themeColour( theme, "resultFocused", rgb( 70, 64, 48 ) )
                        : themeColour( theme, "resultInactive", rgb( 43, 48, 65 ) );

                    rrect( rc.x + inset / 2,
                           y - u / 4,
                           rc.w - inset,
                           rowH - u / 4,
                           u / 2,
                           highlight,
                           255 );

                    if ( zone_ == ZoneResults )
                    {
                        rect( rc.x + inset / 2,
                              y,
                              imax( 2, u / 4 ),
                              rowH - u,
                              themeColour( theme, "resultStripe", rgb( 255, 191, 64 ) ),
                              255 );
                    }
                }

                text( rc.x + inset, y, itemPx, true,
                      fitTo( entry.display, itemPx, true, maxTextW ), textHi );

                const std::string secondary = entry.groupName.empty( )
                    ? entry.detail : entry.groupName;

                text( rc.x + inset,
                      y + textH( itemPx, true ) + u / 4,
                      detailPx, false,
                      fitTo( secondary, detailPx, false, maxTextW ), textMid );
            }
        }
    }

    // ---- footer hints ----
    {
        int  px = u * 9 / 10;
        int  x  = l.footer.x + u / 2;
        int  cy = l.footer.y + l.footer.h / 2;
        struct Hint { const char *key; const char *what; };
        static const Hint hints[] =
        {
            { "DIRECTIONS", "move" },
            { "SELECT",     "type" },
            { "BACK",       "close" }
        };
        for ( size_t i = 0; i < sizeof( hints ) / sizeof( hints[0] ); ++i )
        {
            int kw = textW( hints[i].key, px, true ) + u * 3 / 2;
            int kh = u * 2 + u / 4;
            rrect( x, cy - kh / 2 + 2, kw, kh, u / 2, themeColour( theme, "footerEdge", rgb( 34, 39, 60 ) ), 255 );
            rrect( x, cy - kh / 2, kw, kh, u / 2, themeColour( theme, "footerFace", rgb( 60, 69, 100 ) ), 255 );
            text( x + u * 3 / 4, cy - kh / 2 + ( kh - textH( px, true ) ) / 2, px, true, hints[i].key, textHi );
            x += kw + u / 2;

            text( x, cy - textH( px, false ) / 2, px, false, hints[i].what, textMid );
            x += textW( hints[i].what, px, false ) + u * 2;
        }
    }

    SDL_SetRenderDrawBlendMode( r, prevBlend );
}
