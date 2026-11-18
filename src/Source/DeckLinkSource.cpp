/*
 * This file is part of vimix - video live mixer
 *
 * **Copyright** (C) 2019-2023 Bruno Herbelin <bruno.herbelin@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
**/

#include <algorithm>
#include <map>
#include <sstream>
#include <thread>
#include <chrono>

#include <gst/gst.h>

#include "defines.h"
#include "Log.h"
#include "Scene/Decorations.h"
#include "Stream.h"
#include "Toolkit/GstToolkit.h"
#include "Visitor/Visitor.h"

#include "DeckLinkSource.h"

#ifndef NDEBUG
#define DECKLINK_DEBUG
#endif

//
// Video modes and connections, with the names of the GStreamer decklink plugin
// (see 'gst-inspect-1.0 decklinkvideosrc')
//
const std::vector<DeckLinkMode> &DeckLink::modes()
{
    // { gst_name, label, width, height, fps_numerator, fps_denominator }
    static const std::vector<DeckLinkMode> modes_ = {
        // SD formats
        { "ntsc",       "NTSC",           720,  486,  30000, 1001 },
        { "ntsc2398",   "NTSC 23.98",     720,  486,  24000, 1001 },
        { "pal",        "PAL",            720,  576,  25,    1    },
        { "ntsc-p",     "NTSC-P",         720,  486,  30000, 1001 },
        { "pal-p",      "PAL-P",          720,  576,  25,    1    },
        // HD 720 formats
        { "720p50",     "720p 50",        1280, 720,  50,    1    },
        { "720p5994",   "720p 59.94",     1280, 720,  60000, 1001 },
        { "720p60",     "720p 60",        1280, 720,  60,    1    },
        // HD 1080 interlaced formats
        { "1080i50",    "1080i 50",       1920, 1080, 25,    1    },
        { "1080i5994",  "1080i 59.94",    1920, 1080, 30000, 1001 },
        { "1080i60",    "1080i 60",       1920, 1080, 30,    1    },
        // HD 1080 progressive formats
        { "1080p2398",  "1080p 23.98",    1920, 1080, 24000, 1001 },
        { "1080p24",    "1080p 24",       1920, 1080, 24,    1    },
        { "1080p25",    "1080p 25",       1920, 1080, 25,    1    },
        { "1080p2997",  "1080p 29.97",    1920, 1080, 30000, 1001 },
        { "1080p30",    "1080p 30",       1920, 1080, 30,    1    },
        { "1080p50",    "1080p 50",       1920, 1080, 50,    1    },
        { "1080p5994",  "1080p 59.94",    1920, 1080, 60000, 1001 },
        { "1080p60",    "1080p 60",       1920, 1080, 60,    1    },
        // 2K DCI formats
        { "2kdcip2398", "2K DCI 23.98",   2048, 1080, 24000, 1001 },
        { "2kdcip24",   "2K DCI 24",      2048, 1080, 24,    1    },
        { "2kdcip25",   "2K DCI 25",      2048, 1080, 25,    1    },
        // 4K UHD formats
        { "2160p2398",  "4K UHD 23.98",   3840, 2160, 24000, 1001 },
        { "2160p24",    "4K UHD 24",      3840, 2160, 24,    1    },
        { "2160p25",    "4K UHD 25",      3840, 2160, 25,    1    },
        { "2160p2997",  "4K UHD 29.97",   3840, 2160, 30000, 1001 },
        { "2160p30",    "4K UHD 30",      3840, 2160, 30,    1    },
        { "2160p50",    "4K UHD 50",      3840, 2160, 50,    1    },
        { "2160p5994",  "4K UHD 59.94",   3840, 2160, 60000, 1001 },
        { "2160p60",    "4K UHD 60",      3840, 2160, 60,    1    },
        // 4K DCI formats
        { "4kdcip2398", "4K DCI 23.98",   4096, 2160, 24000, 1001 },
        { "4kdcip24",   "4K DCI 24",      4096, 2160, 24,    1    },
        { "4kdcip25",   "4K DCI 25",      4096, 2160, 25,    1    },
    };
    return modes_;
}

const std::vector<DeckLinkConnection> &DeckLink::connections()
{
    static const std::vector<DeckLinkConnection> connections_ = {
        { "auto",        "Auto" },
        { "sdi",         "SDI" },
        { "hdmi",        "HDMI" },
        { "optical-sdi", "Optical SDI" },
        { "component",   "Component" },
        { "composite",   "Composite" },
        { "svideo",      "S-Video" },
    };
    return connections_;
}

int DeckLink::numModes()
{
    return static_cast<int>(modes().size());
}

int DeckLink::modeIndex(const std::string &gst_name)
{
    const std::vector<DeckLinkMode> &m = modes();
    for (size_t i = 0; i < m.size(); ++i) {
        if (m[i].gst_name == gst_name)
            return static_cast<int>(i);
    }
    return -1;
}

DeckLinkMode DeckLink::mode(int index)
{
    if (index < 0 || index >= numModes())
        index = modeIndex(default_mode);
    return modes()[index];
}

std::string DeckLink::modeLabel(const std::string &gst_name)
{
    int i = modeIndex(gst_name);
    return i < 0 ? gst_name : modes()[i].label;
}

int DeckLink::numConnections()
{
    return static_cast<int>(connections().size());
}

int DeckLink::connectionIndex(const std::string &gst_name)
{
    const std::vector<DeckLinkConnection> &c = connections();
    for (size_t i = 0; i < c.size(); ++i) {
        if (c[i].gst_name == gst_name)
            return static_cast<int>(i);
    }
    return -1;
}

DeckLinkConnection DeckLink::connection(int index)
{
    if (index < 0 || index >= numConnections())
        index = connectionIndex(default_connection);
    return connections()[index];
}

std::string DeckLink::connectionLabel(const std::string &gst_name)
{
    int i = connectionIndex(gst_name);
    return i < 0 ? gst_name : connections()[i].label;
}

bool DeckLink::available()
{
    return GstToolkit::has_feature("decklinkvideosrc");
}

//
// List of DeckLink inputs
//
bool DeckLinkHandle::sameInput(int64_t pid, int number) const
{
    // the persistent id identifies an input whatever its device number
    if (pid >= 0)
        return persistent_id == pid;
    return number >= 0 && device_number == number;
}

DeckLink::DeckLink(): probing_(true), initialized_(false)
{
    // list inputs in background: loading the DeckLink driver can take a while
    std::thread(launchProbe, this).detach();
}

void DeckLink::launchProbe(DeckLink *d)
{
    d->probe();
}

void DeckLink::reload()
{
    // ignore if already listing inputs
    bool expected = false;
    if (probing_.compare_exchange_strong(expected, true))
        std::thread(launchProbe, this).detach();
}

void DeckLink::probe()
{
    std::vector< DeckLinkHandle > inputs;
    bool plugin = available();

    if (plugin) {
        // the DeckLink device provider lists inputs without opening them
        GstDeviceProvider *provider = gst_device_provider_factory_get_by_name("decklinkdeviceprovider");
        if (provider) {
            GList *devices = gst_device_provider_get_devices(provider);
            for (GList *it = devices; it != nullptr; it = it->next) {
                GstDevice *device = GST_DEVICE(it->data);
                if ( !gst_device_has_classes(device, "Video/Source") )
                    continue;

                GstStructure *props = gst_device_get_properties(device);
                if (props == nullptr)
                    continue;

                DeckLinkHandle input;
                guint number = 0;
                if ( gst_structure_get_uint(props, "device-number", &number) )
                    input.device_number = static_cast<int>(number);
                gint64 pid = 0;
                if ( gst_structure_get_int64(props, "persistent-id", &pid) )
                    input.persistent_id = pid;
                gboolean detection = FALSE;
                gst_structure_get_boolean(props, "supports-format-detection", &detection);
                const gchar *display = gst_structure_get_string(props, "display-name");
                const gchar *model = gst_structure_get_string(props, "model-name");
                const gchar *serial = gst_structure_get_string(props, "serial-number");

                input.name = display ? display : (model ? model : "DeckLink");

                std::ostringstream oss;
                oss << "- DeckLink input -" << std::endl;
                if (model)
                    oss << "Model : " << model << std::endl;
                if (serial)
                    oss << "Serial : " << serial << std::endl;
                oss << "Device number : " << input.device_number << std::endl;
                if (input.persistent_id >= 0)
                    oss << "Persistent id : " << input.persistent_id << std::endl;
                oss << "Format detection : " << (detection ? "yes" : "no") << std::endl;
                input.properties = oss.str();

                gst_structure_free(props);

                if (input.device_number >= 0)
                    inputs.push_back(input);
            }
            g_list_free_full(devices, (GDestroyNotify) gst_object_unref);
            gst_object_unref(provider);
        }
    }

    // identical cards can have the same name: distinguish them with their device number
    std::map<std::string, int> names;
    for (const auto &i : inputs)
        names[i.name]++;
    for (auto &i : inputs) {
        if (names[i.name] > 1)
            i.name += " #" + std::to_string(i.device_number);
    }

    // replace the list of inputs, keeping those in use
    access_.lock();
    for (auto h = handles_.begin(); h != handles_.end(); ++h) {
        auto i = std::find_if(inputs.begin(), inputs.end(), [&h](const DeckLinkHandle &in) {
            return h->sameInput(in.persistent_id, in.device_number);
        });
        if (i != inputs.end()) {
            // listed again: keep its stream and the sources connected to it
            i->stream = h->stream;
            i->video_mode = h->video_mode;
            i->connection = h->connection;
            i->connected_sources = h->connected_sources;
        }
        else if (h->stream != nullptr || !h->connected_sources.empty())
            // not listed anymore but still in use: its sources keep a reference to it
            inputs.push_back(*h);
    }
    handles_.swap(inputs);
    int num = static_cast<int>(handles_.size());
    access_.unlock();

    // done
    bool first = false;
    {
        std::lock_guard<std::mutex> lock(initialization_mutex_);
        first = !initialized_;
        initialized_ = true;
    }
    initialization_.notify_all();
    probing_ = false;

    if (!plugin) {
        if (first)
            Log::Info("Blackmagic DeckLink GStreamer plugin not available.");
    }
    else
        Log::Info("Found %d Blackmagic DeckLink video input(s).", num);
}

void DeckLink::waitInitialized()
{
    std::unique_lock<std::mutex> lock(initialization_mutex_);
    // do not wait forever if the driver does not answer
    initialization_.wait_for(lock, std::chrono::seconds(10), [this]{ return initialized_; });
}

std::vector< DeckLinkHandle >::iterator DeckLink::find(int64_t persistent_id, int device_number)
{
    return std::find_if(handles_.begin(), handles_.end(), [&](const DeckLinkHandle &h) {
        return h.sameInput(persistent_id, device_number);
    });
}

int DeckLink::numDevices()
{
    std::lock_guard<std::mutex> lock(access_);
    return static_cast<int>(handles_.size());
}

int DeckLink::index(int64_t persistent_id, int device_number)
{
    std::lock_guard<std::mutex> lock(access_);
    auto h = find(persistent_id, device_number);
    if (h == handles_.end())
        return -1;
    return static_cast<int>(std::distance(handles_.begin(), h));
}

std::string DeckLink::name(int index)
{
    std::lock_guard<std::mutex> lock(access_);
    if (index >= 0 && index < static_cast<int>(handles_.size()))
        return handles_[index].name;
    return "";
}

std::string DeckLink::properties(int index)
{
    std::lock_guard<std::mutex> lock(access_);
    if (index >= 0 && index < static_cast<int>(handles_.size()))
        return handles_[index].properties;
    return "";
}

int64_t DeckLink::persistentId(int index)
{
    std::lock_guard<std::mutex> lock(access_);
    if (index >= 0 && index < static_cast<int>(handles_.size()))
        return handles_[index].persistent_id;
    return -1;
}

int DeckLink::deviceNumber(int index)
{
    std::lock_guard<std::mutex> lock(access_);
    if (index >= 0 && index < static_cast<int>(handles_.size()))
        return handles_[index].device_number;
    return -1;
}

//
// DeckLink source
//
DeckLinkSource::DeckLinkSource(uint64_t id) : StreamSource(id),
    persistent_id_(-1), device_number_(-1),
    video_mode_(DeckLink::default_mode), connection_(DeckLink::default_connection),
    unplugged_(false)
{
    // set symbol
    symbol_ = new Symbol(Symbol::TELEVISION, glm::vec3(0.75f, 0.75f, 0.01f));
    symbol_->scale_.y = 1.5f;
}

DeckLinkSource::~DeckLinkSource()
{
    unsetDevice();
}

void DeckLinkSource::unsetDevice()
{
    std::lock_guard<std::mutex> lock(DeckLink::manager().access_);

    // unregister this source from the input it is connected to
    for (auto &h : DeckLink::manager().handles_) {
        auto sit = std::find(h.connected_sources.begin(), h.connected_sources.end(), this);
        if (sit == h.connected_sources.end())
            continue;

        h.connected_sources.erase(sit);
        if (h.connected_sources.empty())
            // this was the last source: its stream is deleted with it
            // (by ~StreamSource) and the input should not keep a reference to it
            h.stream = nullptr;
        else
            // other sources still use this stream:
            // avoid deleting it in the ~StreamSource destructor
            stream_ = nullptr;
        break;
    }
}

void DeckLinkSource::reconnect()
{
    // remember settings
    int64_t pid = persistent_id_;
    int number = device_number_;
    std::string mode = video_mode_;
    std::string connection = connection_;
    // disconnect
    unsetDevice();
    if (stream_) {
        delete stream_;
        stream_ = nullptr;
    }
    // connect
    setDevice(pid, number, mode, connection);
}

std::string DeckLinkSource::deviceName() const
{
    int i = DeckLink::manager().index(persistent_id_, device_number_);
    if (i >= 0)
        return DeckLink::manager().name(i);
    return "DeckLink " + std::to_string(device_number_);
}

int DeckLinkSource::sharingSources() const
{
    std::lock_guard<std::mutex> lock(DeckLink::manager().access_);
    for (const auto &h : DeckLink::manager().handles_) {
        if (std::find(h.connected_sources.begin(), h.connected_sources.end(), this) != h.connected_sources.end())
            return static_cast<int>(h.connected_sources.size()) - 1;
    }
    return 0;
}

void DeckLinkSource::setDevice(int64_t persistent_id, int device_number,
                               const std::string &video_mode, const std::string &connection)
{
    // validate settings
    std::string mode = video_mode;
    if (DeckLink::modeIndex(mode) < 0) {
        Log::Warning("Unknown DeckLink video mode '%s', using %s.", video_mode.c_str(), DeckLink::default_mode);
        mode = DeckLink::default_mode;
    }
    std::string conn = connection;
    if (DeckLink::connectionIndex(conn) < 0)
        conn = DeckLink::default_connection;

    // nothing to do if already capturing this input with these settings
    if (stream_ != nullptr && !unplugged_ && persistent_id == persistent_id_
        && device_number == device_number_ && mode == video_mode_ && conn == connection_)
        return;

    // wait for the first listing of inputs
    DeckLink::manager().waitInitialized();

    // leave the current input
    unsetDevice();

    // if the stream referenced in this source remains after unsetDevice, no other source uses it
    if (stream_) {
        delete stream_;
        stream_ = nullptr;
    }

    // set new settings
    persistent_id_ = persistent_id;
    device_number_ = device_number;
    video_mode_ = mode;
    connection_ = conn;
    unplugged_ = false;

    std::lock_guard<std::mutex> lock(DeckLink::manager().access_);

    auto h = DeckLink::manager().find(persistent_id_, device_number_);
    if (h == DeckLink::manager().handles_.end()) {
        unplugged_ = true;
        if (persistent_id_ >= 0)
            Log::Warning("No DeckLink input with persistent id %lld.", (long long) persistent_id_);
        else
            Log::Warning("No DeckLink input number %d.", device_number_);
        return;
    }

    // keep the identity of the input up to date (device numbers depend on detection order)
    persistent_id_ = h->persistent_id;
    device_number_ = h->device_number;

    if (h->stream != nullptr) {
        // the input is already captured by other sources;
        // one input carries one signal: use the same settings
        if (h->video_mode != video_mode_ || h->connection != connection_) {
            Log::Notify("DeckLink '%s' is already captured in %s (%s); the new source uses the same settings.",
                        h->name.c_str(),
                        DeckLink::modeLabel(h->video_mode).c_str(),
                        DeckLink::connectionLabel(h->connection).c_str());
            video_mode_ = h->video_mode;
            connection_ = h->connection;
        }
        // share the stream
        stream_ = h->stream;
        // reinit to adapt to new stream
        init();
    }
    else {
        DeckLinkMode m = DeckLink::mode( DeckLink::modeIndex(video_mode_) );

        // GStreamer pipeline; the persistent id has priority over the device number
        std::ostringstream pipeline;
        pipeline << "decklinkvideosrc";
        if (h->persistent_id >= 0)
            pipeline << " persistent-id=" << h->persistent_id;
        else
            pipeline << " device-number=" << h->device_number;
        pipeline << " mode=" << m.gst_name;
        pipeline << " connection=" << connection_;
        pipeline << " ! video/x-raw,format=UYVY";
        pipeline << ",width=" << m.width;
        pipeline << ",height=" << m.height;
        pipeline << ",framerate=" << m.fps_numerator << "/" << m.fps_denominator;

        // always convert; on GPU if GLMemory can be used
        bool glmemory = Stream::glMemoryAvailable();
        if (glmemory)
            pipeline << " ! queue ! glupload ! glcolorconvert";
        else
            pipeline << " ! queue ! videoconvert";

#ifdef DECKLINK_DEBUG
        Log::Info("DeckLink pipeline: %s", pipeline.str().c_str());
#endif

        // delete and reset render buffer to enforce re-init of StreamSource
        if (renderbuffer_)
            delete renderbuffer_;
        renderbuffer_ = nullptr;

        // new stream
        stream_ = h->stream = new Stream;
        h->video_mode = video_mode_;
        h->connection = connection_;

        // open gstreamer
        h->stream->open(pipeline.str(), m.width, m.height, glmemory);
        h->stream->play(true);

        Log::Info("DeckLink '%s' opened in %s (%s).", h->name.c_str(), m.label.c_str(),
                  DeckLink::connectionLabel(connection_).c_str());
    }

    // reference this source in the input
    h->connected_sources.push_back(this);

    // will be ready after init and one frame rendered
    ready_ = false;
}

void DeckLinkSource::setActive(bool on)
{
    bool was_active = active_;

    // try to activate (may fail if source is cloned)
    Source::setActive(on);

    // change status of stream (only if status changed)
    if (stream_ && active_ != was_active) {

        std::lock_guard<std::mutex> lock(DeckLink::manager().access_);

        // the stream is active if any of the sources of the input is active
        for (const auto &h : DeckLink::manager().handles_) {
            if (std::find(h.connected_sources.begin(), h.connected_sources.end(), this) == h.connected_sources.end())
                continue;

            bool streamactive = false;
            for (auto sit = h.connected_sources.begin(); sit != h.connected_sources.end(); ++sit) {
                if ((*sit)->active_)
                    streamactive = true;
            }

            // option to automatically replay when the source is disabled
            if (streamactive && replay_on_disable_)
                stream_->rewind();

            stream_->enable(streamactive);
            break;
        }
    }
}

void DeckLinkSource::accept(Visitor& v)
{
    StreamSource::accept(v);
    v.visit(*this);
}

Source::Failure DeckLinkSource::failed() const
{
    return (unplugged_ || StreamSource::failed()) ? FAIL_CRITICAL : FAIL_NONE;
}

glm::ivec2 DeckLinkSource::icon() const
{
    return glm::ivec2(ICON_VI_SOURCE_DECKLINK);
}

std::string_view DeckLinkSource::type() const
{
    return SOURCE_TYPE_DECKLINK;
}

std::string DeckLinkSource::info() const
{
    return "DeckLink";
}
