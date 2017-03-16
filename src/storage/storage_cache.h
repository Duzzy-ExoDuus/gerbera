/*MT*
    
    MediaTomb - http://www.mediatomb.cc/
    
    storage_cache.h - this file is part of MediaTomb.
    
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

/// \file storage_cache.h

#ifndef __STORAGE_CACHE_H__
#define __STORAGE_CACHE_H__

#include <memory>
#include <unordered_map>
#include <mutex>

#include "zmm/zmmf.h"
#include "common.h"
#include "cache_object.h"

#define STORAGE_CACHE_CAPACITY 29989u
#define STORAGE_CACHE_MAXFILL 9973u

class StorageCache : public zmm::Object
{
public:
    StorageCache();
    
    std::shared_ptr<CacheObject> getObject(int id);
    std::shared_ptr<CacheObject> getObjectDefinitely(int id);
    bool removeObject(int id);
    void clear();
    
    std::shared_ptr<zmm::Array<CacheObject> > getObjects(zmm::String location);
    void checkLocation(std::shared_ptr<CacheObject> obj);
    
    // a child was added to the specified object - update numChildren accordingly,
    // if the object has cached information
    void addChild(int id);
    
    bool flushed();
    
    std::mutex & getMutex() { return mutex; }
    
private:
    
    int capacity;
    unsigned int maxfill;
    bool hasBeenFlushed;
    
    void ensureFillLevelOk();
    
    std::shared_ptr<std::unordered_map<int,std::shared_ptr<CacheObject> > > idHash;
    std::shared_ptr<std::unordered_map<zmm::String, std::shared_ptr<zmm::Array<CacheObject> > > > locationHash;
    std::mutex mutex;
    using AutoLock = std::lock_guard<std::mutex>;
};

#endif // __STORAGE_CACHE_H__
