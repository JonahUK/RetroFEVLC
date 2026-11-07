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

#include "ReloadableText.h"
#include "../ViewInfo.h"
#include "../../Database/Configuration.h"
#include "../../Utility/Log.h"
#include "../../SDL.h"
#include <fstream>
#include <vector>
#include <iostream>
#include <time.h>
#include <algorithm>

ReloadableText::ReloadableText(std::string type, Page &page, Configuration &config, bool systemMode, Font *font, std::string layoutKey, std::string timeFormat, std::string textFormat, std::string singlePrefix, std::string singlePostfix, std::string pluralPrefix, std::string pluralPostfix, std::string separator)
    : Component(page)
    , config_(config)
    , systemMode_(systemMode)
    , imageInst_(NULL)
    , type_(type)
    , layoutKey_(layoutKey)
    , fontInst_(font)
    , timeFormat_(timeFormat)
    , textFormat_(textFormat)
    , separator_(separator)
    , singlePrefix_(singlePrefix)
    , singlePostfix_(singlePostfix)
    , pluralPrefix_(pluralPrefix)
    , pluralPostfix_(pluralPostfix)
{
    allocateGraphicsMemory();
}



ReloadableText::~ReloadableText()
{
    if (imageInst_ != NULL)
    {
        delete imageInst_;
    }
}

void ReloadableText::update(float dt)
{
    if (newItemSelected ||
       (newScrollItemSelected && getMenuScrollReload()) ||
        type_ == "time" || type_ == "current" || type_ == "duration" || type_ == "isPaused")
    {
        ReloadTexture();
        newItemSelected = false;
    }

    // needs to be ran at the end to prevent the NewItemSelected flag from being detected
    Component::update(dt);

}

void ReloadableText::allocateGraphicsMemory()
{
    ReloadTexture();

    // NOTICE! needs to be done last to prevent flags from being missed
    Component::allocateGraphicsMemory();
}


void ReloadableText::freeGraphicsMemory()
{
    Component::freeGraphicsMemory();

    if (imageInst_ != NULL)
    {
        delete imageInst_;
        imageInst_ = NULL;
    }
}


void ReloadableText::initializeFonts()
{
    fontInst_->initialize();
}


void ReloadableText::deInitializeFonts()
{
    fontInst_->deInitialize();
}


void ReloadableText::ReloadTexture()
{
    if (imageInst_ != NULL)
    {
        delete imageInst_;
        imageInst_ = NULL;
    }

    Item *selectedItem = page.getSelectedItem();

    if (selectedItem != NULL)
    {
        std::stringstream ss;
        std::string text = "";
        // Optional multi-field metadata, configured entirely by the layout.
        // Single-field reloadableText behaviour remains unchanged.
        if (type_.find(",") != std::string::npos)
        {
            std::stringstream fields(type_);
            std::string field;
            std::string combined;

            bool overwriteXML = false;
            config_.getProperty("overwriteXML", overwriteXML);

            while (std::getline(fields, field, ','))
            {
                const std::string::size_type first =
                    field.find_first_not_of(" \t\r\n");

                if (first == std::string::npos)
                {
                    continue;
                }

                const std::string::size_type last =
                    field.find_last_not_of(" \t\r\n");

                field = field.substr(first, last - first + 1);

                std::string value;

                if (field == "year")
                    value = selectedItem->year;
                else if (field == "genre")
                    value = selectedItem->genre;
                else if (field == "manufacturer")
                    value = selectedItem->manufacturer;
                else if (field == "developer")
                {
                    value = selectedItem->developer;
                    if (value.empty())
                        value = selectedItem->manufacturer;
                }
                else if (field == "title")
                    value = selectedItem->title;
                else if (field == "numberPlayers")
                    value = selectedItem->numberPlayers;
                else if (field == "numberButtons")
                    value = selectedItem->numberButtons;
                else if (field == "rating")
                    value = selectedItem->rating;
                else if (field == "score")
                    value = selectedItem->score;

                // Preserve the existing collection configuration lookups.
                if (!selectedItem->leaf || systemMode_)
                {
                    config_.getProperty(
                        "collections." + selectedItem->name + "." + field,
                        value);
                }

                if (systemMode_)
                {
                    value.clear();
                    config_.getProperty(
                        "collections." + page.getCollectionName() + "." + field,
                        value);
                }

                // Preserve the existing XML metadata fallback behaviour.
                if (value.empty() || overwriteXML)
                {
                    std::string xmlValue;
                    selectedItem->getInfo(field, xmlValue);

                    if (!xmlValue.empty())
                        value = xmlValue;
                }

                // Ignore empty or whitespace-only metadata.
                const std::string::size_type valueFirst =
                    value.find_first_not_of(" \t\r\n");

                if (valueFirst == std::string::npos)
                    continue;

                const std::string::size_type valueLast =
                    value.find_last_not_of(" \t\r\n");

                value = value.substr(
                    valueFirst, valueLast - valueFirst + 1);

                if (!combined.empty())
                    combined += separator_;

                combined += value;
            }

            if (!combined.empty())
            {
                if (textFormat_ == "uppercase")
                {
                    std::transform(
                        combined.begin(), combined.end(),
                        combined.begin(), ::toupper);
                }
                else if (textFormat_ == "lowercase")
                {
                    std::transform(
                        combined.begin(), combined.end(),
                        combined.begin(), ::tolower);
                }

                ss << pluralPrefix_ << combined << pluralPostfix_;
            }

            imageInst_ = new Text(
                ss.str(), page, fontInst_, baseViewInfo.Monitor);

            return;
        }
        if (type_ == "time")
        {
          time_t    now = time(0);
          struct tm tstruct;
          char      buf[80];
          tstruct = *localtime(&now);
          strftime(buf, sizeof(buf), timeFormat_.c_str(), &tstruct);
          ss << buf;
        }
        if (type_ == "numberButtons")
        {
            text = selectedItem->numberButtons;
        }
        else if (type_ == "numberPlayers")
        {
            text = selectedItem->numberPlayers;
        }
        else if (type_ == "ctrlType")
        {
            text = selectedItem->ctrlType;
        }
        else if (type_ == "numberJoyWays")
        {
            text = selectedItem->joyWays;
        }
        else if (type_ == "rating")
        {
            text = selectedItem->rating;
        }
        else if (type_ == "score")
        {
            text = selectedItem->score;
        }
        else if (type_ == "year")
        {
              text = selectedItem->year;
        }
        else if (type_ == "title")
        {
            text = selectedItem->title;
        }
        else if(type_ == "developer")
        {
            text = selectedItem->developer;
            // Overwrite in case developer has not been specified
            if (text == "")
            {
                text = selectedItem->manufacturer;
            }
        }
        else if (type_ == "manufacturer")
        {
            text = selectedItem->manufacturer;
        }
        else if (type_ == "genre")
        {
            text = selectedItem->genre;
        }
        else if (type_.rfind( "playlist", 0 ) == 0)
        {
            text = playlistName;
        }
        else if (type_ == "firstLetter")
        {
          text = selectedItem->fullTitle.at(0);
        }
        else if (type_ == "collectionName")
        {
            text = page.getCollectionName();
        }
        else if (type_ == "collectionSize")
        {
            if (page.getCollectionSize() == 0)
            {
                ss << singlePrefix_ << page.getCollectionSize() << pluralPostfix_;
            }
            else if (page.getCollectionSize() == 1)
            {
                ss << singlePrefix_ << page.getCollectionSize() << singlePostfix_;
            }
            else
            {
                ss << pluralPrefix_ << page.getCollectionSize() << pluralPostfix_;
            }
        }
        else if (type_ == "collectionIndex")
        {
            if (page.getSelectedIndex() == 0)
            {
                ss << singlePrefix_ << (page.getSelectedIndex()+1) << pluralPostfix_;
            }
            else if (page.getSelectedIndex() == 1)
            {
                ss << singlePrefix_ << (page.getSelectedIndex()+1) << singlePostfix_;
            }
            else
            {
                ss << pluralPrefix_ << (page.getSelectedIndex()+1) << pluralPostfix_;
            }
        }
        else if (type_ == "collectionIndexSize")
        {
            if (page.getSelectedIndex() == 0)
            {
                ss << singlePrefix_ << (page.getSelectedIndex()+1) << "/" << page.getCollectionSize() << pluralPostfix_;
            }
            else if (page.getSelectedIndex() == 1)
            {
                ss << singlePrefix_ << (page.getSelectedIndex()+1) << "/" << page.getCollectionSize() << singlePostfix_;
            }
            else
            {
                ss << pluralPrefix_ << (page.getSelectedIndex()+1) << "/" << page.getCollectionSize() << pluralPostfix_;
            }
        }
        else if (type_ == "isFavorite")
        {
            if (selectedItem->isFavorite)
                text = "yes";
            else
                text = "no";
        }
        else if (type_ == "isPaused")
        {
            if (page.isPaused( ))
                text = "Paused";
            else
                text = "";
        }
        else if (type_ == "current")
        {
            unsigned long long current = 0;
            current     = page.getCurrent( );
            current    /= 1000000000;
            int seconds = current%60;
            int minutes = (current/60)%60;
            int hours   = int( current/3600 );
            text        = std::to_string( hours ) + ":";
            if ( minutes < 10 )
                text   += "0" + std::to_string( minutes ) + ":";
            else
                text   += std::to_string( minutes ) + ":";
            if ( seconds < 10 )
                text   +=  "0" + std::to_string( seconds );
            else
                text   += std::to_string( seconds );
			if ( page.getDuration( ) == 0 )
				text    = "--:--:--";
        }
        else if (type_ == "duration")
        {
            unsigned long long duration = 0;
            duration    = page.getDuration( );
            duration   /= 1000000000;
            int seconds = duration%60;
            int minutes = (duration/60)%60;
            int hours   = int( duration/3600 );
            text        = std::to_string( hours ) + ":";
            if ( minutes < 10 )
                text   += "0" + std::to_string( minutes ) + ":";
            else
                text   += std::to_string( minutes ) + ":";
            if ( seconds < 10 )
                text   +=  "0" + std::to_string( seconds );
            else
                text   += std::to_string( seconds );
			if ( page.getDuration( ) == 0 )
				text    = "--:--:--";
        }

        if (!selectedItem->leaf || systemMode_) // item is not a leaf
        {
            (void)config_.getProperty("collections." + selectedItem->name + "." + type_, text );
        }

        if (systemMode_) // Get the system information in stead
        {
            text = "";
            (void)config_.getProperty("collections." + page.getCollectionName() + "." + type_, text );
        }

        bool overwriteXML = false;
        config_.getProperty( "overwriteXML", overwriteXML );
        if ( text == "" || overwriteXML ) // No text was found yet; check the info in stead
        {
            std::string text_tmp;
            selectedItem->getInfo( type_, text_tmp );
            if ( text_tmp != "" )
            {
                text = text_tmp;
            }
        }

        if (text == "0")
        {
            text = singlePrefix_ + text + pluralPostfix_;
        }
        else if (text == "1")
        {
            text = singlePrefix_ + text + singlePostfix_;
        }
        else if (text != "")
        {
            text = pluralPrefix_ + text + pluralPostfix_;
        }

        if (text != "")
        {
            if (textFormat_ == "uppercase")
            {
                std::transform(text.begin(), text.end(), text.begin(), ::toupper);
            }
            if (textFormat_ == "lowercase")
            {
                std::transform(text.begin(), text.end(), text.begin(), ::tolower);
            }
            ss << text;
        }

        imageInst_ = new Text(ss.str(), page, fontInst_, baseViewInfo.Monitor);
    }
}


void ReloadableText::draw()
{
    if(imageInst_)
    {
        imageInst_->baseViewInfo = baseViewInfo;
        imageInst_->draw();
    }
}
