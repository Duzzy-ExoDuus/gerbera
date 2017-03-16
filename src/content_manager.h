/*MT*
    
    MediaTomb - http://www.mediatomb.cc/
    
    content_manager.h - this file is part of MediaTomb.
    
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

/// \file content_manager.h
#ifndef __CONTENT_MANAGER_H__
#define __CONTENT_MANAGER_H__

#include <memory>
#include <unordered_set>
#include <mutex>
#include <condition_variable>

#include "common.h"
#include "cds_objects.h"
#include "storage.h"
#include "dictionary.h"
#include "autoscan.h"
#include "timer.h"
#include "generic_task.h"

#ifdef HAVE_JS
    // this is somewhat not nice, the playlist header needs the cm header and
    // vice versa
    class PlaylistParserScript;
    #include "scripting/playlist_parser_script.h"
#ifdef HAVE_LIBDVDNAV
    class DVDImportScript;
    #include "scripting/dvd_image_import_script.h"
#endif

#endif
#include "layout/layout.h"
#ifdef HAVE_INOTIFY
    #include "autoscan_inotify.h"
#endif

#ifdef EXTERNAL_TRANSCODING
    #include "transcoding/transcoding.h"
#endif

#ifdef ONLINE_SERVICES 
    #include "online_service.h"
#ifdef YOUTUBE
    #include "cached_url.h"
    #include "reentrant_array.h"
    #define  MAX_CACHED_URLS            20
    #define URL_CACHE_CHECK_INTERVAL    300
    //#define URL_CACHE_CHECK_INTERVAL 30
    //#define URL_CACHE_LIFETIME          60
    #define URL_CACHE_LIFETIME          600
#endif
#endif//ONLINE_SERVICES

#if defined (EXTERNAL_TRANSCODING) || defined(SOPCAST)
    #include "executor.h"
#endif

class CMAddFileTask : public GenericTask
{
protected:
    zmm::String path;
    zmm::String rootpath;
    bool recursive;
    bool hidden;
public:
    CMAddFileTask(zmm::String path, zmm::String rootpath, bool recursive=false,
                  bool hidden=false, bool cancellable = true);
    zmm::String getPath();
    zmm::String getRootPath();
    virtual void run();
};

class CMRemoveObjectTask : public GenericTask
{
protected:
    int objectID;
    bool all;
public:
    CMRemoveObjectTask(int objectID, bool all);
    virtual void run();
};

class CMLoadAccountingTask : public GenericTask
{
public:
    CMLoadAccountingTask();
    virtual void run();
};

class CMRescanDirectoryTask : public GenericTask
{
protected: 
    int objectID;
    int scanID;
    scan_mode_t scanMode;
public:
    CMRescanDirectoryTask(int objectID, int scanID, scan_mode_t scanMode,
                          bool cancellable);
    virtual void run();
};

class CMAccounting : public zmm::Object
{
public:
    CMAccounting();
public:
    int totalFiles;
};

#ifdef ONLINE_SERVICES
class CMFetchOnlineContentTask : public GenericTask
{
protected:
    std::shared_ptr<OnlineService> service;
    std::shared_ptr<Layout> layout;
    bool unscheduled_refresh;

public:
    CMFetchOnlineContentTask(std::shared_ptr<OnlineService> service, 
                             std::shared_ptr<Layout> layout,
                             bool cancellable, bool unscheduled_refresh);
    virtual void run();
};
#endif

/*
class DirCacheEntry : public zmm::Object
{
public:
    DirCacheEntry();
public:
    int end;
    int id;
};

class DirCache : public zmm::Object
{
protected:
    std::shared_ptr<zmm::StringBuffer> buffer;
    int size; // number of entries
    int capacity; // capacity of entries
    std::shared_ptr<zmm::Array<DirCacheEntry> > entries;
public:
    DirCache();
    void push(zmm::String name);
    void pop();
    void setPath(zmm::String path);
    void clear();
    zmm::String getPath();
    int createContainers();
};
*/

class ContentManager : public Timer::Subscriber, public Singleton<ContentManager, std::recursive_mutex>
{
public:
    ContentManager();
    virtual void init();
    virtual ~ContentManager();
    void shutdown();
    
    virtual void timerNotify(std::shared_ptr<Timer::Parameter> parameter);
    
    bool isBusy() { return working; }
    
    std::shared_ptr<CMAccounting> getAccounting();

    /// \brief Returns the task that is currently being executed.
    std::shared_ptr<GenericTask> getCurrentTask();

    /// \brief Returns the list of all enqueued tasks, including the current or nullptr if no tasks are present.
    std::shared_ptr<zmm::Array<GenericTask> > getTasklist();

    /// \brief Find a task identified by the task ID and invalidate it.
    void invalidateTask(unsigned int taskID, task_owner_t taskOwner = ContentManagerTask);

    
    /* the functions below return true if the task has been enqueued */
    
    /* sync/async methods */
    void loadAccounting(bool async=true);

    /// \brief Adds a file or directory to the database.
    /// \param path absolute path to the file
    /// \param recursive recursive add (process subdirecotories)
    /// \param async queue task or perform a blocking call
    /// \param hidden true allows to import hidden files, false ignores them
    /// \param queue for immediate processing or in normal order
    /// \return object ID of the added file - only in blockign mode, when used in async mode this function will return INVALID_OBJECT_ID
    int addFile(zmm::String path, bool recursive=true, bool async=true, 
                bool hidden=false, bool lowPriority=false, 
                bool cancellable=true);

    int ensurePathExistence(zmm::String path);
    void removeObject(int objectID, bool async=true, bool all=false);
    void rescanDirectory(int objectID, int scanID, scan_mode_t scanMode,
                         zmm::String descPath = nullptr, bool cancellable = true);

    /// \brief Updates an object in the database using the given parameters.
    /// \param objectID ID of the object to update
    /// \param parameters key value pairs of fields to be updated
    void updateObject(int objectID, std::shared_ptr<Dictionary> parameters);

    std::shared_ptr<CdsObject> createObjectFromFile(zmm::String path, 
                                             bool magic=true, 
                                             bool allow_fifo=false);

#ifdef ONLINE_SERVICES
    /// \brief Creates a layout based from data that is obtained from an
    /// online service (like YouTube, SopCast, etc.)
    void fetchOnlineContent(service_type_t service, bool lowPriority=true, 
                            bool cancellable=true, 
                            bool unscheduled_refresh = false);

    void cleanupOnlineServiceObjects(std::shared_ptr<OnlineService> service);

#ifdef YOUTUBE
    /// \brief Adds a URL to the cache.
    void cacheURL(std::shared_ptr<CachedURL> url);
    /// \brief Retrieves an URL from the cache.
    zmm::String getCachedURL(int objectID);
#endif
#endif//ONLINE_SERVICES

    /// \brief Adds a virtual item.
    /// \param obj item to add
    /// \param allow_fifo flag to indicate that it is ok to add a fifo,
    /// otherwise only regular files or directories are allowed.
    ///
    /// This function makes sure that the file is first added to
    /// PC-Directory, however without the scripting execution.
    /// It then adds the user defined virtual item to the database.
    void addVirtualItem(std::shared_ptr<CdsObject> obj, bool allow_fifo=false);

    /// \brief Adds an object to the database.
    /// \param obj object to add
    ///
    /// parentID of the object must be set before this method.
    /// The ID of the object provided is ignored and generated by this method    
    void addObject(std::shared_ptr<CdsObject> obj);

    /// \brief Adds a virtual container chain specified by path.
    /// \param container path separated by '/'. Slashes in container
    /// titles must be escaped.
    /// \param lastClass upnp:class of the last container in the chain, if nullptr
    /// then the default class will be taken
    /// \param lastRefID reference id of the last container in the chain,
    /// INVALID_OBJECT_ID indicates that the id will not be set. 
    /// \return ID of the last container in the chain.
    int addContainerChain(zmm::String chain, zmm::String lastClass = nullptr,
            int lastRefID = INVALID_OBJECT_ID, std::shared_ptr<Dictionary> lastMetadata = nullptr);
    
    /// \brief Adds a virtual container specified by parentID and title
    /// \param parentID the id of the parent.
    /// \param title the title of the container.
    /// \param upnpClass the upnp class of the container.
    void addContainer(int parentID, zmm::String title, zmm::String upnpClass);
    
    /// \brief Updates an object in the database.
    /// \param obj the object to update
    void updateObject(std::shared_ptr<CdsObject> obj, bool send_updates = true);

    /// \brief Updates an object in the database using the given parameters.
    /// \param objectID ID of the object to update
    ///
    /// Note: no actions should be performed on the object given as the parameter.
    /// Only the returned object should be processed. This method does not save
    /// the returned object in the database. To do so updateObject must be called
    std::shared_ptr<CdsObject> convertObject(std::shared_ptr<CdsObject> obj, int objectType);

    /// \brief Gets an AutocsanDirectrory from the watch list.
    std::shared_ptr<AutoscanDirectory> getAutoscanDirectory(int scanID, scan_mode_t scanMode);

    /// \brief Get an AutoscanDirectory given by location on disk from the watch list.
    std::shared_ptr<AutoscanDirectory> getAutoscanDirectory(zmm::String location);
    /// \brief Removes an AutoscanDirectrory (found by scanID) from the watch list.
    void removeAutoscanDirectory(int scanID, scan_mode_t scanMode);

    /// \brief Removes an AutoscanDirectrory (found by location) from the watch list.
    void removeAutoscanDirectory(zmm::String location);
    
    /// \brief Removes an AutoscanDirectory (by objectID) from the watch list.
    void removeAutoscanDirectory(int objectID);
 
    /// \brief Update autoscan parameters for an existing autoscan directory 
    /// or add a new autoscan directory
    void setAutoscanDirectory(std::shared_ptr<AutoscanDirectory> dir);

    /// \brief handles the removal of a persistent autoscan directory
    void handlePeristentAutoscanRemove(int scanID, scan_mode_t scanMode);

    /// \brief handles the recreation of a persistent autoscan directory
    void handlePersistentAutoscanRecreate(int scanID, scan_mode_t scanMode);

    /// \brief returns an array of autoscan directories for the given scan mode
    std::shared_ptr<zmm::Array<AutoscanDirectory> > getAutoscanDirectories(scan_mode_t scanMode);

    /// \brief returns an array of all autoscan directories 
    std::shared_ptr<zmm::Array<AutoscanDirectory> > getAutoscanDirectories();


    /// \brief instructs ContentManager to reload scripting environment
    void reloadLayout();

#if defined(EXTERNAL_TRANSCODING) || defined(SOPCAST)
    /// \brief register executor
    ///
    /// When an external process is launched we will register the executor
    /// the content manager. This will ensure that we can kill all processes
    /// when we shutdown the server.
    /// 
    /// \param exec the Executor object of the process
    void registerExecutor(std::shared_ptr<Executor> exec);

    /// \brief unregister process
    /// 
    /// When the the process io handler receives a close on a stream that is
    /// currently being processed by an external process, it will kill it.
    /// The handler will then remove the executor from the list.
    void unregisterExecutor(std::shared_ptr<Executor> exec);
#endif

#ifdef HAVE_MAGIC
    zmm::String getMimeTypeFromBuffer(const void *buffer, size_t length);
#endif
protected:
    void initLayout();
    void destroyLayout();

#ifdef HAVE_JS
    void initJS();
    void destroyJS();
#endif
    
    std::shared_ptr<RExp> reMimetype;

    bool ignore_unknown_extensions;
    bool extension_map_case_sensitive;

    std::shared_ptr<Dictionary> extension_mimetype_map;
    std::shared_ptr<Dictionary> mimetype_upnpclass_map;
    std::shared_ptr<Dictionary> mimetype_contenttype_map;

    std::shared_ptr<AutoscanList> autoscan_timed;
#ifdef HAVE_INOTIFY
    std::shared_ptr<AutoscanList> autoscan_inotify;
    std::shared_ptr<AutoscanInotify> inotify;
#endif
 
#if defined(EXTERNAL_TRANSCODING) || defined(SOPCAST)
    std::shared_ptr<zmm::Array<Executor> > process_list;
#endif

    void _loadAccounting();

    int addFileInternal(zmm::String path, zmm::String rootpath, 
                        bool recursive=true,
                        bool async=true, bool hidden=false,
                        bool lowPriority=false, 
                        unsigned int parentTaskID = 0,
                        bool cancellable = true);
    int _addFile(zmm::String path, zmm::String rootpath, bool recursive=false, bool hidden=false, std::shared_ptr<GenericTask> task=nullptr);
    //void _addFile2(zmm::String path, bool recursive=0);
    void _removeObject(int objectID, bool all);
    
    void _rescanDirectory(int containerID, int scanID, scan_mode_t scanMode, scan_level_t scanLevel, std::shared_ptr<GenericTask> task=nullptr);
    /* for recursive addition */
    void addRecursive(zmm::String path, bool hidden, std::shared_ptr<GenericTask> task);
    //void addRecursive2(std::shared_ptr<DirCache> dirCache, zmm::String filename, bool recursive);
    
    zmm::String extension2mimetype(zmm::String extension);
    zmm::String mimetype2upnpclass(zmm::String mimeType);

    void invalidateAddTask(std::shared_ptr<GenericTask> t, zmm::String path);
    
    std::shared_ptr<Layout> layout;

#ifdef ONLINE_SERVICES 
    std::shared_ptr<OnlineServiceList> online_services;

    void fetchOnlineContentInternal(std::shared_ptr<OnlineService> service,
                                    bool lowPriority=true,
                                    bool cancellable=true,
                                    unsigned int parentTaskID = 0,
                                    bool unscheduled_refresh = false);

#ifdef YOUTUBE
    std::mutex urlcache_mutex;
    using AutoLockYT = std::lock_guard<std::mutex>;
    std::shared_ptr<ReentrantArray<CachedURL> > cached_urls;
    /// \brief Removes old URLs from the cache.
    void checkCachedURLs();
#endif

#endif //ONLINE_SERVICES 

#ifdef HAVE_JS
    std::shared_ptr<PlaylistParserScript> playlist_parser_script;
#ifdef HAVE_LIBDVDNAV
    std::shared_ptr<DVDImportScript> dvd_import_script;
#endif
#endif

    bool layout_enabled;
    
    void setLastModifiedTime(time_t lm);
    
    inline void signal() { cond.notify_one(); }
    static void *staticThreadProc(void *arg);
    void threadProc();
    
    void addTask(std::shared_ptr<GenericTask> task, bool lowPriority = false);
    
    std::shared_ptr<CMAccounting> acct;
    
    pthread_t taskThread;
    std::condition_variable_any cond;
    
    bool working;
    
    bool shutdownFlag;
    
    std::shared_ptr<zmm::ObjectQueue<GenericTask> > taskQueue1; // priority 1
    std::shared_ptr<zmm::ObjectQueue<GenericTask> > taskQueue2; // priority 2
    std::shared_ptr<GenericTask> currentTask;

    unsigned int taskID;

    friend void CMAddFileTask::run();
    friend void CMRemoveObjectTask::run();
    friend void CMRescanDirectoryTask::run();
#ifdef ONLINE_SERVICES
    friend void CMFetchOnlineContentTask::run();
#endif
    friend void CMLoadAccountingTask::run();
};

#endif // __CONTENT_MANAGER_H__
