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
#include <sstream>
#include <thread>
#include <chrono>
#include <glm/gtc/matrix_transform.hpp>

#include <gst/gst.h>

#include "Log.h"
#include "Scene/Decorations.h"
#include "Stream.h"
#include "Visitor/Visitor.h"

#include "DeckLinkSource.h"

#ifndef NDEBUG
#define DECKLINK_DEBUG
#endif

// Static member initialization
std::vector<DeckLinkMode> DeckLink::modes_;
std::vector<DeckLinkConnection> DeckLink::connections_;

void DeckLink::initModes()
{
    if (!modes_.empty())
        return;

    // Common DeckLink modes - based on GStreamer decklink plugin modes
    // Mode numbers correspond to BMDDisplayMode enum values
    modes_ = {
        // NTSC formats
        { 0,  "NTSC",          720,  486,  30000, 1001 },
        { 1,  "NTSC 2398",     720,  486,  24000, 1001 },
        { 2,  "PAL",           720,  576,  25,    1    },
        { 3,  "NTSC-P",        720,  486,  30000, 1001 },
        { 4,  "PAL-P",         720,  576,  25,    1    },
        // HD 720 formats
        { 5,  "HD 720p50",     1280, 720,  50,    1    },
        { 6,  "HD 720p5994",   1280, 720,  60000, 1001 },
        { 7,  "HD 720p60",     1280, 720,  60,    1    },
        // HD 1080 interlaced formats
        { 8,  "HD 1080i50",    1920, 1080, 25,    1    },
        { 9,  "HD 1080i5994",  1920, 1080, 30000, 1001 },
        { 10, "HD 1080i60",    1920, 1080, 30,    1    },
        // HD 1080 progressive formats
        { 11, "HD 1080p2398",  1920, 1080, 24000, 1001 },
        { 12, "HD 1080p24",    1920, 1080, 24,    1    },
        { 13, "HD 1080p25",    1920, 1080, 25,    1    },
        { 14, "HD 1080p2997",  1920, 1080, 30000, 1001 },
        { 15, "HD 1080p30",    1920, 1080, 30,    1    },
        { 16, "HD 1080p50",    1920, 1080, 50,    1    },
        { 17, "HD 1080p5994",  1920, 1080, 60000, 1001 },
        { 18, "HD 1080p60",    1920, 1080, 60,    1    },
        // 2K formats
        { 19, "2K DCI 2398",   2048, 1080, 24000, 1001 },
        { 20, "2K DCI 24",     2048, 1080, 24,    1    },
        { 21, "2K DCI 25",     2048, 1080, 25,    1    },
        // 4K UHD formats
        { 22, "4K UHD 2398",   3840, 2160, 24000, 1001 },
        { 23, "4K UHD 24",     3840, 2160, 24,    1    },
        { 24, "4K UHD 25",     3840, 2160, 25,    1    },
        { 25, "4K UHD 2997",   3840, 2160, 30000, 1001 },
        { 26, "4K UHD 30",     3840, 2160, 30,    1    },
        { 27, "4K UHD 50",     3840, 2160, 50,    1    },
        { 28, "4K UHD 5994",   3840, 2160, 60000, 1001 },
        { 29, "4K UHD 60",     3840, 2160, 60,    1    },
        // 4K DCI formats
        { 30, "4K DCI 2398",   4096, 2160, 24000, 1001 },
        { 31, "4K DCI 24",     4096, 2160, 24,    1    },
        { 32, "4K DCI 25",     4096, 2160, 25,    1    },
    };
}

void DeckLink::initConnections()
{
    if (!connections_.empty())
        return;

    // Connection types - based on GStreamer decklink plugin connection enum
    connections_ = {
        { 0, "Auto" },
        { 1, "SDI" },
        { 2, "HDMI" },
        { 3, "Optical SDI" },
        { 4, "Component" },
        { 5, "Composite" },
        { 6, "S-Video" },
    };
}

int DeckLink::numModes()
{
    initModes();
    return static_cast<int>(modes_.size());
}

std::string DeckLink::modeName(int index)
{
    initModes();
    if (index >= 0 && index < static_cast<int>(modes_.size()))
        return modes_[index].name;
    return "";
}

DeckLinkMode DeckLink::mode(int index)
{
    initModes();
    if (index >= 0 && index < static_cast<int>(modes_.size()))
        return modes_[index];
    return { 0, "Unknown", 1920, 1080, 30, 1 };
}

int DeckLink::numConnections()
{
    initConnections();
    return static_cast<int>(connections_.size());
}

std::string DeckLink::connectionName(int index)
{
    initConnections();
    if (index >= 0 && index < static_cast<int>(connections_.size()))
        return connections_[index].name;
    return "Auto";
}

int DeckLink::connectionValue(int index)
{
    initConnections();
    if (index >= 0 && index < static_cast<int>(connections_.size()))
        return connections_[index].value;
    return 0;
}

bool DeckLink::available()
{
    return GstToolkit::has_feature("decklinkvideosrc");
}

DeckLink::DeckLink(): initialized_(false)
{
    // Initialize modes and connections
    initModes();
    initConnections();

    // Probe for devices
    probe();
}

void DeckLink::probe()
{
    // Check if decklink plugin is available
    if (!available()) {
        Log::Info("Blackmagic DeckLink GStreamer plugin not available.");
        initialized_.store(true);
        return;
    }

    // lock before change
    access_.lock();
    handles_.clear();

    // Try to probe DeckLink devices (up to 8 devices)
    for (int device_num = 0; device_num < 8; ++device_num) {

        // Create a test pipeline to check if device exists
        std::ostringstream pipeline;
        pipeline << "decklinkvideosrc device-number=" << device_num << " mode=16"; // mode 16 = 1080p50

        GstToolkit::PipelineConfigSet confs = GstToolkit::getPipelineConfigs(pipeline.str());

        // If we get valid configs, the device exists
        if (!confs.empty()) {
            DeckLinkHandle dev;
            dev.device_number = device_num;
            dev.name = "DeckLink " + std::to_string(device_num);
            dev.pipeline = "decklinkvideosrc device-number=" + std::to_string(device_num);
            dev.configs = confs;

            // Properties string for display
            std::ostringstream props;
            props << "DeckLink Device #" << device_num << std::endl;
            props << "Driver: Blackmagic DeckLink" << std::endl;
            dev.properties = props.str();

#ifdef DECKLINK_DEBUG
            Log::Info("Found DeckLink device %d", device_num);
#endif
            handles_.push_back(dev);
        }
        else {
            // Stop probing when we don't find a device
            break;
        }
    }

    // unlock access
    access_.unlock();

    initialized_.store(true);

    if (handles_.empty()) {
        Log::Info("No Blackmagic DeckLink devices found.");
    }
    else {
        Log::Info("Found %d Blackmagic DeckLink device(s).", (int)handles_.size());
    }
}

void DeckLink::reload()
{
    probe();
}

int DeckLink::numDevices()
{
    // Wait for initialization
    while (!initialized_.load())
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

    access_.lock();
    int ret = static_cast<int>(handles_.size());
    access_.unlock();

    return ret;
}

bool DeckLink::exists(int device_number)
{
    access_.lock();
    bool found = false;
    for (const auto& h : handles_) {
        if (h.device_number == device_number) {
            found = true;
            break;
        }
    }
    access_.unlock();
    return found;
}

int DeckLink::index(int device_number)
{
    access_.lock();
    int idx = -1;
    for (size_t i = 0; i < handles_.size(); ++i) {
        if (handles_[i].device_number == device_number) {
            idx = static_cast<int>(i);
            break;
        }
    }
    access_.unlock();
    return idx;
}

std::string DeckLink::name(int index)
{
    std::string ret = "";
    access_.lock();
    if (index >= 0 && index < static_cast<int>(handles_.size()))
        ret = handles_[index].name;
    access_.unlock();
    return ret;
}

std::string DeckLink::description(int index)
{
    std::string ret = "";
    access_.lock();
    if (index >= 0 && index < static_cast<int>(handles_.size()))
        ret = handles_[index].pipeline;
    access_.unlock();
    return ret;
}

std::string DeckLink::properties(int index)
{
    std::string ret = "";
    access_.lock();
    if (index >= 0 && index < static_cast<int>(handles_.size()))
        ret = handles_[index].properties;
    access_.unlock();
    return ret;
}

// Helper struct for finding handles
struct hasDeckLinkDeviceNumber
{
    inline bool operator()(const DeckLinkHandle &elem) const {
       return (elem.device_number == _num);
    }
    explicit hasDeckLinkDeviceNumber(int num) : _num(num) { }
private:
    int _num;
};

struct hasDeckLinkConnectedSource
{
    inline bool operator()(const DeckLinkHandle &elem) const {
        auto sit = std::find(elem.connected_sources.begin(), elem.connected_sources.end(), s_);
        return sit != elem.connected_sources.end();
    }
    explicit hasDeckLinkConnectedSource(DeckLinkSource *s) : s_(s) { }
private:
    DeckLinkSource *s_;
};

DeckLinkSource::DeckLinkSource(uint64_t id) : StreamSource(id),
    device_number_(-1), mode_number_(16), connection_(0), unplugged_(false)
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
    // lock before accessing handles_
    DeckLink::manager().access_.lock();

    // unregister this source from a DeckLink handler
    auto h = std::find_if(DeckLink::manager().handles_.begin(),
                          DeckLink::manager().handles_.end(),
                          hasDeckLinkConnectedSource(this));
    if (h != DeckLink::manager().handles_.end())
    {
        // remove this pointer from the list of connected sources
        h->connected_sources.remove(this);
        // if this is the last source connected to the device handler
        // the stream will be removed by the ~StreamSource destructor
        // and the device handler should not keep reference to it
        if (h->connected_sources.empty())
            // cancel the reference to the stream
            h->stream = nullptr;
        else
        // else this means another DeckLinkSource is using this stream
        // and we should avoid to delete the stream in the ~StreamSource destructor
            stream_ = nullptr;
    }

    // unlock before changing device number
    DeckLink::manager().access_.unlock();

    device_number_ = -1;
}

void DeckLinkSource::reconnect()
{
    // remember settings
    int d = device_number_;
    int m = mode_number_;
    int c = connection_;
    // disconnect
    unsetDevice();
    // connect
    setDevice(d, m, c);
}

std::string DeckLinkSource::deviceName() const
{
    int idx = DeckLink::manager().index(device_number_);
    if (idx >= 0)
        return DeckLink::manager().name(idx);
    return "DeckLink " + std::to_string(device_number_);
}

void DeckLinkSource::setDevice(int device_number, int mode_number, int connection)
{
    if (device_number_ == device_number && mode_number_ == mode_number && connection_ == connection)
        return;

    // if changing device
    if (device_number_ >= 0)
        unsetDevice();

    // if the stream referenced in this source remains after unsetDevice
    if (stream_) {
        delete stream_;
        stream_ = nullptr;
    }

    // set new device settings
    device_number_ = device_number;
    mode_number_ = mode_number;
    connection_ = connection;

    // lock before accessing handles_
    DeckLink::manager().access_.lock();

    // find the device handle
    auto h = std::find_if(DeckLink::manager().handles_.begin(),
                          DeckLink::manager().handles_.end(),
                          hasDeckLinkDeviceNumber(device_number_));

    // found a device handle
    if (h != DeckLink::manager().handles_.end()) {

        // find if a DeckLinkHandle with this device already has a stream that is open
        if (h->stream != nullptr) {
            // just use it!
            stream_ = h->stream;
            // reinit to adapt to new stream
            init();
        }
        else {
            // Get mode information
            DeckLinkMode m = DeckLink::mode(mode_number_);

            // Build GStreamer pipeline
            std::ostringstream pipeline;
            pipeline << "decklinkvideosrc device-number=" << device_number_;
            pipeline << " mode=" << mode_number_;
            pipeline << " connection=" << connection_;
            pipeline << " ! video/x-raw";
            pipeline << ",width=" << m.width;
            pipeline << ",height=" << m.height;
            pipeline << ",framerate=" << m.fps_numerator << "/" << m.fps_denominator;
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

            // open gstreamer
            h->stream->open(pipeline.str(), m.width, m.height);
            h->stream->play(true);

            Log::Info("DeckLink %d opened with mode %s (%dx%d)",
                      device_number_, m.name.c_str(), m.width, m.height);
        }

        // reference this source in the handle
        h->connected_sources.push_back(this);

        // will be ready after init and one frame rendered
        ready_ = false;
    }
    else {
        unplugged_ = true;
        Log::Warning("No DeckLink device number %d", device_number_);
    }

    // unlock after accessing handles_
    DeckLink::manager().access_.unlock();
}

void DeckLinkSource::setActive(bool on)
{
    bool was_active = active_;

    // try to activate (may fail if source is cloned)
    Source::setActive(on);

    if (stream_) {
        // change status of stream (only if status changed)
        if (active_ != was_active) {

            // lock before accessing handles_
            DeckLink::manager().access_.lock();

            // activate a source if any of the handled device source is active
            auto h = std::find_if(DeckLink::manager().handles_.begin(),
                                  DeckLink::manager().handles_.end(),
                                  hasDeckLinkConnectedSource(this));
            if (h != DeckLink::manager().handles_.end()) {
                bool streamactive = false;
                for (auto sit = h->connected_sources.begin(); sit != h->connected_sources.end(); ++sit) {
                    if ((*sit)->active_)
                        streamactive = true;
                }

                // option to automatically replay when the source is disabled
                if (streamactive && replay_on_disable_)
                    stream_->rewind();

                stream_->enable(streamactive);
            }

            // unlock after accessing handles_
            DeckLink::manager().access_.unlock();
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
    return glm::ivec2(ICON_SOURCE_DECKLINK);
}

std::string DeckLinkSource::info() const
{
    return "DeckLink";
}
