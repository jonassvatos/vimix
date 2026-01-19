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
#ifndef DECKLINKSOURCE_H
#define DECKLINKSOURCE_H

#include <string>
#include <vector>
#include <atomic>

#include "Toolkit/GstToolkit.h"
#include "StreamSource.h"

class DeckLinkSource : public StreamSource
{
    friend class DeckLink;

public:
    DeckLinkSource(uint64_t id = 0);
    ~DeckLinkSource();

    // Source interface
    Failure failed() const override;
    void accept (Visitor& v) override;
    void setActive (bool on) override;

    // StreamSource interface
    Stream *stream() const override { return stream_; }

    // specific interface
    void setDevice(int device_number, int mode_number, int connection);
    inline int deviceNumber() const { return device_number_; }
    inline int modeNumber() const { return mode_number_; }
    inline int connection() const { return connection_; }
    std::string deviceName() const;
    void reconnect();

    glm::ivec2 icon() const override;
    inline std::string info() const override;

protected:
    void unplug() { unplugged_ = true; }

private:
    int device_number_;
    int mode_number_;
    int connection_;
    std::atomic<bool> unplugged_;
    void unsetDevice();
};

struct DeckLinkHandle {
    int device_number;
    std::string name;
    std::string pipeline;
    std::string properties;
    GstToolkit::PipelineConfigSet configs;

    Stream *stream;
    std::list<DeckLinkSource *> connected_sources;

    DeckLinkHandle() {
        device_number = 0;
        stream = nullptr;
    }
};

// Mode presets for DeckLink devices
struct DeckLinkMode {
    std::string gst_mode;  // GStreamer mode name (e.g., "1080p60")
    std::string name;      // Display name
    int width;
    int height;
    int fps_numerator;
    int fps_denominator;
};

// Connection types for DeckLink devices
struct DeckLinkConnection {
    int value;
    std::string name;
};

class DeckLink
{
    friend class DeckLinkSource;

    DeckLink();
    DeckLink(DeckLink const& copy) = delete;
    DeckLink& operator=(DeckLink const& copy) = delete;

public:

    static DeckLink& manager()
    {
        // The only instance
        static DeckLink _instance;
        return _instance;
    }

    // device enumeration
    int numDevices () ;
    std::string name (int index) ;
    std::string description (int index) ;
    std::string properties (int index) ;
    int  index  (int device_number);
    bool exists (int device_number);
    void reload ();

    // mode enumeration
    static int numModes();
    static std::string modeName(int index);
    static DeckLinkMode mode(int index);

    // connection enumeration
    static int numConnections();
    static std::string connectionName(int index);
    static int connectionValue(int index);

    // check for decklink GStreamer plugin availability
    static bool available();

private:
    void probe();
    std::mutex access_;
    std::vector< DeckLinkHandle > handles_;
    std::atomic<bool> initialized_;

    static std::vector<DeckLinkMode> modes_;
    static std::vector<DeckLinkConnection> connections_;
    static void initModes();
    static void initConnections();
};


#endif // DECKLINKSOURCE_H
