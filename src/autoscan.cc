/*MT*
    
    MediaTomb - http://www.mediatomb.cc/
    
    autoscan.cc - this file is part of MediaTomb.
    
    Copyright (C) 2005 Gena Batyan <bgeradz@mediatomb.cc>,
                       Sergey 'Jin' Bostandzhyan <jin@mediatomb.cc>
    
    Copyright (C) 2006-2010 Gena Batyan <bgeradz@mediatomb.cc>,
                            Sergey 'Jin' Bostandzhyan <jin@mediatomb.cc>,
                            Leonhard Wimmer <leo@mediatomb.cc>
    
    MediaTomb is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 2
    as published by the Free Software Foundation.
    
    MediaTomb is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
    
    You should have received a copy of the GNU General Public License
    version 2 along with MediaTomb; if not, write to the Free Software
    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
    
    $Id$
*/

/// \file autoscan.cc

#include "autoscan.h"
#include "storage.h"
#include "content_manager.h"

using namespace zmm;
using namespace std;

AutoscanDirectory::AutoscanDirectory()
{
    taskCount = 0;
    objectID = INVALID_OBJECT_ID;
    storageID = INVALID_OBJECT_ID;
    last_mod_previous_scan = 0;
    last_mod_current_scan = 0;
    timer_parameter = shared_ptr<Timer::Parameter> (new Timer::Parameter(Timer::Parameter::IDAutoscan, INVALID_SCAN_ID));
}

AutoscanDirectory::AutoscanDirectory(String location, scan_mode_t mode,
        scan_level_t level, bool recursive, bool persistent,
        int id, unsigned int interval, bool hidden)
{
    this->location = location;
    this->mode = mode;
    this->level = level;
    this->recursive = recursive;
    this->hidden = hidden;
    this->interval = interval;
    this->persistent_flag = persistent;
    scanID = id;
    taskCount = 0;
    objectID = INVALID_OBJECT_ID;
    storageID = INVALID_OBJECT_ID;
    last_mod_previous_scan = 0;
    last_mod_current_scan = 0;
    timer_parameter = shared_ptr<Timer::Parameter>(new Timer::Parameter(Timer::Parameter::IDAutoscan, INVALID_SCAN_ID));
}

void AutoscanDirectory::setCurrentLMT(time_t lmt) 
{
    if (lmt > last_mod_current_scan)
        last_mod_current_scan = lmt;
}

AutoscanList::AutoscanList()
{
    list = shared_ptr<Array<AutoscanDirectory> > (new Array<AutoscanDirectory>());
}

void AutoscanList::updateLMinDB()
{
    AutoLock lock(mutex);
    for (int i = 0; i < list->size(); i++)
    {
        log_debug("i: %d\n", i);
        shared_ptr<AutoscanDirectory> ad = list->get(i);
        if (ad != nullptr)
            Storage::getInstance()->autoscanUpdateLM(ad);
    }
}

int AutoscanList::add(shared_ptr<AutoscanDirectory> dir)
{
    AutoLock lock(mutex);
    return _add(dir);
}

int AutoscanList::_add(shared_ptr<AutoscanDirectory> dir)
{

    String loc = dir->getLocation();
    int nil_index = -1;
    
    for (int i = 0; i < list->size(); i++)
    {
        if (list->get(i) == nullptr)
        {
            nil_index = i;
            continue;
        }
        
        if (loc == list->get(i)->getLocation())
        {
            throw _Exception(_("Attempted to add same autoscan path twice"));
        }
    }
    
    if (nil_index != -1)
    {
        dir->setScanID(nil_index);
        list->set(dir, nil_index);
    }
    else
    {
        dir->setScanID(list->size());
        list->append(dir);
    }

    return dir->getScanID();
}

void AutoscanList::addList(zmm::shared_ptr<AutoscanList> list)
{
    AutoLock lock(mutex);
    
    for (int i = 0; i < list->list->size(); i++)
    {
        if (list->list->get(i) == nullptr)
            continue;

        _add(list->list->get(i));
    }
}

shared_ptr<Array<AutoscanDirectory> > AutoscanList::getArrayCopy()
{
    AutoLock lock(mutex);
    shared_ptr<Array<AutoscanDirectory> > copy(new Array<AutoscanDirectory>(list->size()));
    for (int i = 0; i < list->size(); i++)
        copy->append(list->get(i));

    return copy;
}

shared_ptr<AutoscanDirectory> AutoscanList::get(int id)
{
    AutoLock lock(mutex);

    if ((id < 0) || (id >= list->size()))
        return nullptr;

    return list->get(id);
}

shared_ptr<AutoscanDirectory> AutoscanList::getByObjectID(int objectID)
{
    AutoLock lock(mutex);

    for (int i = 0; i < list->size(); i++)
    {
        if (list->get(i) != nullptr && objectID == list->get(i)->getObjectID())
            return list->get(i);
    }
    return nullptr;
}

shared_ptr<AutoscanDirectory> AutoscanList::get(String location)
{
    AutoLock lock(mutex);
    for (int i = 0; i < list->size(); i++)
    {
        if (list->get(i) != nullptr && (location == list->get(i)->getLocation()))
            return list->get(i);
    }
    return nullptr;

}

void AutoscanList::remove(int id)
{
    AutoLock lock(mutex);
    
    if ((id < 0) || (id >= list->size()))
    {
        log_debug("No such ID %d!\n", id);
        return;
    }
   
    shared_ptr<AutoscanDirectory> dir = list->get(id);
    dir->setScanID(INVALID_SCAN_ID);

    if (id == list->size()-1)
    {
        list->removeUnordered(id);
    }
    else
    {
        list->set(nullptr, id);
    }

    log_debug("ID %d removed!\n", id);
}

int AutoscanList::removeByObjectID(int objectID)
{
    AutoLock lock(mutex);

    for (int i = 0; i < list->size(); i++)
    {
        if (list->get(i) != nullptr && objectID == list->get(i)->getObjectID())
        {
            shared_ptr<AutoscanDirectory> dir = list->get(i);
            dir->setScanID(INVALID_SCAN_ID);
            if (i == list->size()-1)
            {
                list->removeUnordered(i);
            }
            else
            {
                list->set(nullptr, i);
            }
            return i;
        }
    }
    return INVALID_SCAN_ID;
}

int AutoscanList::remove(String location)
{
    AutoLock lock(mutex);
    
    for (int i = 0; i < list->size(); i++)
    {
        if (list->get(i) != nullptr && location == list->get(i)->getLocation())
        {
            shared_ptr<AutoscanDirectory> dir = list->get(i);
            dir->setScanID(INVALID_SCAN_ID);
            if (i == list->size()-1)
            {
                list->removeUnordered(i);
            }
            else
            {
                list->set(nullptr, i);
            }
            return i;
        }
    }
    return INVALID_SCAN_ID;
}

shared_ptr<AutoscanList> AutoscanList::removeIfSubdir(String parent, bool persistent)
{
    AutoLock lock(mutex);

    shared_ptr<AutoscanList> rm_id_list(new AutoscanList());

    for (int i = 0; i < list->size(); i++)
    {
        if (list->get(i) != nullptr && (list->get(i)->getLocation().startsWith(parent)))
        {
            shared_ptr<AutoscanDirectory> dir = list->get(i);
            if (dir == nullptr)
                continue;
            if (dir->persistent() && !persistent)
            {
                continue;
            }
            shared_ptr<AutoscanDirectory> copy(new AutoscanDirectory());
            dir->copyTo(copy);
            rm_id_list->add(copy);
            copy->setScanID(dir->getScanID());
            dir->setScanID(INVALID_SCAN_ID);
            if (i == list->size()-1)
            {
                list->removeUnordered(i);
            }
            else
            {
                list->set(nullptr, i);
            }
        }
    }

    return rm_id_list;
}

void AutoscanList::notifyAll(Timer::Subscriber *sub)
{
    if (sub == nullptr) return;
    AutoLock lock(mutex);
    
    shared_ptr<Timer> timer = Timer::getInstance();
    for (int i = 0; i < list->size(); i++)
    {
        if (list->get(i) == nullptr)
            continue;
        sub->timerNotify(list->get(i)->getTimerParameter());
    }
}

void AutoscanDirectory::setLocation(String location)
{
    if (this->location == nullptr)
        this->location = location;
    else
        throw _Exception(_("UNALLOWED LOCATION CHANGE!"));

}

void AutoscanDirectory::setScanID(int id) 
{
    scanID = id; 
    timer_parameter->setID(id);
} 

String AutoscanDirectory::mapScanmode(scan_mode_t scanmode)
{
    String scanmode_str = nullptr;
    switch (scanmode)
    {
        case TimedScanMode: scanmode_str = _("timed"); break;
        case InotifyScanMode: scanmode_str = _("inotify"); break;
    }
    if (scanmode_str == nullptr)
        throw Exception(_("illegal scanmode given to mapScanmode(): ") + scanmode);
    return scanmode_str;
}

scan_mode_t AutoscanDirectory::remapScanmode(String scanmode)
{
    if (scanmode == "timed")
        return TimedScanMode;
    if (scanmode == "inotify")
        return InotifyScanMode;
    else
        throw _Exception(_("illegal scanmode (") + scanmode + ") given to remapScanmode()");
}

String AutoscanDirectory::mapScanlevel(scan_level_t scanlevel)
{
    String scanlevel_str = nullptr;
    switch (scanlevel)
    {
        case BasicScanLevel: scanlevel_str = _("basic"); break;
        case FullScanLevel: scanlevel_str = _("full"); break;
    }
    if (scanlevel_str == nullptr)
        throw Exception(_("illegal scanlevel given to mapScanlevel(): ") + scanlevel);
    return scanlevel_str;
}

scan_level_t AutoscanDirectory::remapScanlevel(String scanlevel)
{
    if (scanlevel == "basic")
        return BasicScanLevel;
    else if (scanlevel == "full")
        return FullScanLevel;
    else
        throw _Exception(_("illegal scanlevel (") + scanlevel + ") given to remapScanlevel()");
}

void AutoscanDirectory::copyTo(shared_ptr<AutoscanDirectory> copy)
{
    copy->location = location;
    copy->mode = mode;
    copy->level = level;
    copy->recursive = recursive;
    copy->hidden = hidden;
    copy->persistent_flag = persistent_flag;
    copy->interval = interval;
    copy->taskCount = taskCount;
    copy->scanID = scanID;
    copy->objectID = objectID;
    copy->storageID = storageID;
    copy->last_mod_previous_scan = last_mod_previous_scan;
    copy->last_mod_current_scan = last_mod_current_scan;
    copy->timer_parameter = timer_parameter;
}

shared_ptr<Timer::Parameter> AutoscanDirectory::getTimerParameter()
{
    return timer_parameter; 
}
