/*MT*
    
    MediaTomb - http://www.mediatomb.cc/
    
    transcode_ext_handler.cc - this file is part of MediaTomb.
    
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

/// \file transcode_ext_handler.cc

#ifdef EXTERNAL_TRANSCODING

#include "transcode_ext_handler.h"
#include "server.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <upnp-1.8/ixml.h>
#include <unistd.h>
#include <cstring>
#include <cstdio>
#include <csignal>
#include <climits>
#include "common.h"
#include "storage.h"
#include "cds_objects.h"
#include "process.h"
#include "update_manager.h"
#include "session_manager.h"
#include "process_io_handler.h"
#include "buffered_io_handler.h"
#include "dictionary.h"
#include "metadata_handler.h"
#include "tools.h"
#include "file_io_handler.h"
#include "transcoding_process_executor.h"
#include "io_handler_chainer.h"
#include "play_hook.h"

#ifdef HAVE_CURL
    #include "curl_io_handler.h"
#endif

#ifdef HAVE_LIBDVDNAV
    #include "dvd_io_handler.h"
    #include "metadata/dvd_handler.h"
    #include "fd_io_handler.h"
    #include "mpegremux_processor.h"
#endif

using namespace zmm;

TranscodeExternalHandler::TranscodeExternalHandler() : TranscodeHandler()
{
}

shared_ptr<IOHandler> TranscodeExternalHandler::open(shared_ptr<TranscodingProfile> profile, 
                                              String location, 
                                              shared_ptr<CdsObject> obj,
                                              String range)
{
    bool isURL = false;
//    bool is_srt = false;

    log_debug("start transcoding file: %s\n", location.c_str());
    char fifo_template[]="mt_transcode_XXXXXX";

    if (profile == nullptr)
        throw _Exception(_("Transcoding of file ") + location +
                           "requested but no profile given");
    
    isURL = (IS_CDS_ITEM_INTERNAL_URL(obj->getObjectType()) ||
            IS_CDS_ITEM_EXTERNAL_URL(obj->getObjectType()));

    String mimeType = profile->getTargetMimeType();

    if (IS_CDS_ITEM(obj->getObjectType()))
    {
        shared_ptr<CdsItem> it = dynamic_pointer_cast<CdsItem>(obj);
        shared_ptr<Dictionary> mappings = ConfigManager::getInstance()->getDictionaryOption(
                CFG_IMPORT_MAPPINGS_MIMETYPE_TO_CONTENTTYPE_LIST);

        if (mappings->get(mimeType) == CONTENT_TYPE_PCM)
        {
            String freq = it->getResource(0)->getAttribute(MetadataHandler::getResAttrName(R_SAMPLEFREQUENCY));
            String nrch = it->getResource(0)->getAttribute(MetadataHandler::getResAttrName(R_NRAUDIOCHANNELS));

            if (string_ok(freq)) 
                mimeType = mimeType + _(";rate=") + freq;
            if (string_ok(nrch))
                mimeType = mimeType + _(";channels=") + nrch;
        }
    }

    //info->content_type = ixmlCloneDOMString(mimeType.c_str());
#ifdef EXTEND_PROTOCOLINFO
    String header;
    header = header + _("TimeSeekRange.dlna.org: npt=") + range;

    log_debug("Adding TimeSeekRange response HEADERS: %s\n", header.c_str());
    header = getDLNAtransferHeader(mimeType, header);
    if (string_ok(header))
        info->http_header = ixmlCloneDOMString(header.c_str());
#endif
   
    //info->file_length = UNKNOWN_CONTENT_LENGTH;
    //info->force_chunked = (int)profile->getChunked();

    shared_ptr<ConfigManager> cfg = ConfigManager::getInstance();
   
    String fifo_name = normalizePath(tempName(cfg->getOption(CFG_SERVER_TMPDIR),
                                     fifo_template));
    String arguments;
    String temp;
    String command;
    shared_ptr<Array<StringBase> > arglist;
    shared_ptr<Array<ProcListItem> > proc_list = nullptr;

#ifdef SOPCAST
    service_type_t service = OS_None;
    if (obj->getFlag(OBJECT_FLAG_ONLINE_SERVICE))
    {
        service = (service_type_t)(obj->getAuxData(_(ONLINE_SERVICE_AUX_ID)).toInt());
    }
    
    if (service == OS_SopCast)
    {
        shared_ptr<Array<StringBase> > sop_args;
        int p1 = find_local_port(45000,65500);
        int p2 = find_local_port(45000,65500);
        sop_args = parseCommandLine(location + " " + String::from(p1) + " " +
                   String::from(p2), nullptr, nullptr);
        shared_ptr<ProcessExecutor> spsc(new ProcessExecutor(_("sp-sc-auth"), 
                                                      sop_args));
        proc_list = shared_ptr<Array<ProcListItem> >(new Array<ProcListItem>(1));
        shared_ptr<ProcListItem> pr_item(new ProcListItem(dynamic_pointer_cast<Executor>(spsc)));
        proc_list->append(pr_item);
        location = _("http://localhost:") + String::from(p2) + "/tv.asf";
#warning check if socket is ready 
        sleep(15); 
    }
#warning check if we can use "accept url" with sopcast
    else
    {
#endif
        if (isURL && (!profile->acceptURL()))
        {
#ifdef HAVE_CURL
            String url = location;
            strcpy(fifo_template, "mt_transcode_XXXXXX");
            location = normalizePath(tempName(cfg->getOption(CFG_SERVER_TMPDIR), fifo_template));
            log_debug("creating reader fifo: %s\n", location.c_str());
            if (mkfifo(location.c_str(), O_RDWR) == -1)
            {
                log_error("Failed to create fifo for the remote content "
                          "reading thread: %s\n", strerror(errno));
                throw _Exception(_("Could not create reader fifo!\n"));
            }

            try
            {
                chmod(location.c_str(), S_IWUSR | S_IRUSR);

                shared_ptr<IOHandler> c_ioh(new CurlIOHandler(url, nullptr, 
                   cfg->getIntOption(CFG_EXTERNAL_TRANSCODING_CURL_BUFFER_SIZE),
                   cfg->getIntOption(CFG_EXTERNAL_TRANSCODING_CURL_FILL_SIZE)));

                shared_ptr<IOHandler> p_ioh(new ProcessIOHandler(location, nullptr));
                shared_ptr<Executor> ch(new IOHandlerChainer(c_ioh, p_ioh, 16384));
                proc_list = shared_ptr<Array<ProcListItem> >(new Array<ProcListItem>(1));
                shared_ptr<ProcListItem> pr_item(new ProcListItem(ch));
                proc_list->append(pr_item);
            }
            catch (const Exception & ex)
            {
                unlink(location.c_str());
                throw ex;
            }
#else
            throw _Exception(_("MediaTomb was compiled without libcurl support,"
                               "data proxying is not available"));
#endif

        }
#ifdef SOPCAST
    }
#endif

#ifdef HAVE_LIBDVDNAV
    if (obj->getFlag(OBJECT_FLAG_DVD_IMAGE))
    {
        strcpy(fifo_template, "mt_transcode_XXXXXX");
        location = normalizePath(tempName(cfg->getOption(CFG_SERVER_TMPDIR), fifo_template));
        log_debug("creating reader fifo: %s\n", location.c_str());
        if (mkfifo(location.c_str(), O_RDWR) == -1)
        {
            log_error("Failed to create fifo for the DVD image "
                    "reading thread: %s\n", strerror(errno));
            throw _Exception(_("Could not create reader fifo!\n"));
        }

       
        try
        {
            String tmp = obj->getResource(0)->getParameter(DVDHandler::renderKey(DVD_Title));
            if (!string_ok(tmp))
                throw _Exception(_("DVD Image requested but title parameter is missing!"));
            int title = tmp.toInt();
            if (title < 0)
                throw _Exception(_("DVD Image - requested invalid title!"));

            tmp = obj->getResource(0)->getParameter(DVDHandler::renderKey(DVD_Chapter));
            if (!string_ok(tmp))
                throw _Exception(_("DVD Image requested but chapter parameter is missing!"));
            int chapter = tmp.toInt();
            if (chapter < 0)
                throw _Exception(_("DVD Image - requested invalid chapter!"));

            // actually we are retrieving the audio stream id here
            tmp = obj->getResource(0)->getParameter(DVDHandler::renderKey(DVD_AudioStreamID));
            if (!string_ok(tmp))
                throw _Exception(_("DVD Image requested but audio track parameter is missing!"));
            int audio_track = tmp.toInt();
            if (audio_track < 0)
                throw _Exception(_("DVD Image - requested invalid audio stream ID!"));

            chmod(location.c_str(), S_IWUSR | S_IRUSR);
            
            shared_ptr<IOHandler> dvd_ioh(new DVDIOHandler(obj->getLocation(), title, chapter, audio_track));

            int from_dvd_fd[2];
            if (pipe(from_dvd_fd) == -1)
                throw _Exception(_("Failed to create DVD input pipe!"));

            int from_remux_fd[2];
            if (pipe(from_remux_fd) == -1)
            {
                close(from_dvd_fd[0]);
                close(from_dvd_fd[1]);
                throw _Exception(_("Failed to create remux output pipe!"));
            }

            shared_ptr<IOHandler> fd_writer(new FDIOHandler(from_dvd_fd[1]));
            shared_ptr<Executor> from_dvd(new IOHandlerChainer(dvd_ioh,
                                                        fd_writer, 16384));

            shared_ptr<IOHandler> fd_reader(new FDIOHandler(from_remux_fd[0]));

            shared_ptr<MPEGRemuxProcessor> remux(new MPEGRemuxProcessor(from_dvd_fd[0],
                                          from_remux_fd[1],
                                          (unsigned char)audio_track));

            dynamic_pointer_cast<FDIOHandler>(fd_reader)->addReference(dynamic_pointer_cast<Object>(remux));
            dynamic_pointer_cast<FDIOHandler>(fd_reader)->addReference(dynamic_pointer_cast<Object>(from_dvd));
            dynamic_pointer_cast<FDIOHandler>(fd_reader)->addReference(dynamic_pointer_cast<Object>(fd_writer));
            dynamic_pointer_cast<FDIOHandler>(fd_reader)->closeOther(fd_writer);
            

            shared_ptr<IOHandler> p_ioh(new ProcessIOHandler(location, nullptr));
            shared_ptr<Executor> ch(new IOHandlerChainer(fd_reader, p_ioh, 16384));
            proc_list = shared_ptr<Array<ProcListItem> >(new Array<ProcListItem>(2));
            shared_ptr<ProcListItem> pr_item(new ProcListItem(ch));
            proc_list->append(pr_item);
            shared_ptr<ProcListItem> pr2_item(new ProcListItem(from_dvd));
            proc_list->append(pr2_item);
        }
        catch (const Exception & ex)
        {
            unlink(location.c_str());
            throw ex;
        }
    }
#endif

    String check;
    if (profile->getCommand().startsWith(_(_DIR_SEPARATOR)))
    {
        if (!check_path(profile->getCommand()))
            throw _Exception(_("Could not find transcoder: ") + 
                    profile->getCommand());

        check = profile->getCommand();
    }
    else
    {
        check = find_in_path(profile->getCommand());

        if (!string_ok(check))
            throw _Exception(_("Could not find transcoder ") + 
                        profile->getCommand() + " in $PATH");

    }

    int err = 0;
    if (!is_executable(check, &err))
        throw _Exception(_("Transcoder ") + profile->getCommand() + 
                " is not executable: " + strerror(err));

    log_debug("creating fifo: %s\n", fifo_name.c_str());
    if (mkfifo(fifo_name.c_str(), O_RDWR) == -1) 
    {
        log_error("Failed to create fifo for the transcoding process!: %s\n", strerror(errno));
        throw _Exception(_("Could not create fifo!\n"));
    }
        
    chmod(fifo_name.c_str(), S_IWUSR | S_IRUSR);
   
    arglist = parseCommandLine(profile->getArguments(), location, fifo_name, range);

    log_debug("Command: %s\n", profile->getCommand().c_str());
    log_debug("Arguments: %s\n", profile->getArguments().c_str());
    shared_ptr<TranscodingProcessExecutor> main_proc(new TranscodingProcessExecutor(profile->getCommand(), arglist));
    main_proc->removeFile(fifo_name);
    if (isURL && (!profile->acceptURL()))
    {
        main_proc->removeFile(location);
    }
#ifdef HAVE_LIBDVDNAV
    if (obj->getFlag(OBJECT_FLAG_DVD_IMAGE))
    {
        main_proc->removeFile(location);
    }
#endif    
    shared_ptr<IOHandler> io_handler(new BufferedIOHandler(shared_ptr<IOHandler> (new ProcessIOHandler(fifo_name, dynamic_pointer_cast<Executor>(main_proc), proc_list)), profile->getBufferSize(), profile->getBufferChunkSize(), profile->getBufferInitialFillSize()));

    io_handler->open(UPNP_READ);
    PlayHook::getInstance()->trigger(obj);
    return io_handler;
}

#endif//EXTERNAL_TRANSCODING
