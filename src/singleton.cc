/*MT*
    
    MediaTomb - http://www.mediatomb.cc/
    
    singleton.cc - this file is part of MediaTomb.
    
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

/// \file singleton.cc

#include "singleton.h"

using namespace zmm;

shared_ptr<SingletonManager> SingletonManager::instance = nullptr;
std::mutex SingletonManager::mutex{};

shared_ptr<SingletonManager> SingletonManager::getInstance()
{
    if (instance == nullptr)
    {
        AutoLock lock(mutex);
        if (instance == nullptr) // check again, because there is a very small chance
                             // that 2 threads tried to lock() concurrently
        {
            instance = zmm::shared_ptr<SingletonManager>(new SingletonManager());
        }
    }
    return instance;
}

SingletonManager::SingletonManager() : Object()
{
    singletonStack = shared_ptr<ObjectStack<Singleton<Object> > >(new ObjectStack<Singleton<Object> >(SINGLETON_CUR_MAX));
}

void SingletonManager::registerSingleton(shared_ptr<Singleton<Object> > object)
{
    AutoLock lock(mutex);
#ifdef TOMBDEBUG
    if (singletonStack->size() >= SINGLETON_CUR_MAX)
    {
        printf("%d singletons are active (SINGLETON_CUR_MAX=%d) and tried to add another singleton - check this!\n", singletonStack->size(), SINGLETON_CUR_MAX);
        print_backtrace();
        abort();
    }
#endif
    log_debug("registering new singleton... - %d -> %d\n", singletonStack->size(), singletonStack->size() + 1);
    singletonStack->push(object);
}

void SingletonManager::shutdown(bool complete)
{
    log_debug("start (%d objects)\n", singletonStack->size());
    AutoLock lock(mutex);
    
    shared_ptr<ObjectStack<Singleton<Object> > > singletonStackReactivate(new ObjectStack<Singleton<Object> >(SINGLETON_CUR_MAX));
    
    shared_ptr<Singleton<Object> > object;
    while((object = singletonStack->pop()) != nullptr)
    {
        //log_debug("destoying... \n");
        //_print_backtrace(stdout);
        object->shutdown();
        object->inactivateSingleton();
        singletonStackReactivate->push(object);
        //object->destroyMutex();
    }
    while((object = singletonStackReactivate->pop()) != nullptr)
        object->reactivateSingleton();
    if (complete && instance != nullptr)
        instance = nullptr;
    log_debug("end\n");
}
