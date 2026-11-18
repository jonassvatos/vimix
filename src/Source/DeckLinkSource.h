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
#include <string_view>
#include <vector>
#include <list>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <cstdint>

#include "StreamSource.h"

/**
 * @brief The DeckLinkSource class
 *
 * Captures the video input of a Blackmagic Design DeckLink card
 * with the GStreamer 'decklinkvideosrc' element.
 *
 * An input is identified by its persistent id when the card provides one
 * (stable across reboots and PCIe slots), otherwise by its device number.
 * Video mode and connection are GStreamer names (e.g. "1080p50", "hdmi").
 *
 * One input carries one signal: all sources capturing the same input
 * share its stream, and therefore its video mode and connection.
 */
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
    void setDevice(int64_t persistent_id, int device_number,
                   const std::string &video_mode, const std::string &connection);
    inline int64_t persistentId() const { return persistent_id_; }
    inline int deviceNumber() const { return device_number_; }
    inline std::string videoMode() const { return video_mode_; }
    inline std::string connection() const { return connection_; }
    std::string deviceName() const;
    // number of other sources capturing the same input
    int sharingSources() const;
    void reconnect();

    glm::ivec2 icon() const override;
    std::string_view type() const override;
    std::string info() const override;

protected:
    void unplug() { unplugged_ = true; }

private:
    int64_t persistent_id_;
    int device_number_;
    std::string video_mode_;
    std::string connection_;
    std::atomic<bool> unplugged_;
    void unsetDevice();
};

struct DeckLinkHandle {

    int64_t persistent_id;     // -1 if not provided
    int device_number;
    std::string name;
    std::string properties;

    // settings of the open stream, shared by all connected sources
    std::string video_mode;
    std::string connection;
    Stream *stream;
    std::list<DeckLinkSource *> connected_sources;

    DeckLinkHandle() : persistent_id(-1), device_number(-1), stream(nullptr) {}
    bool sameInput(int64_t pid, int number) const;
};

// Video modes of DeckLink inputs
struct DeckLinkMode {
    std::string gst_name;  // GStreamer mode name (e.g., "1080p60")
    std::string label;     // Display name
    int width;
    int height;
    int fps_numerator;
    int fps_denominator;
};

// Connection types of DeckLink inputs
struct DeckLinkConnection {
    std::string gst_name;  // GStreamer connection name (e.g., "hdmi")
    std::string label;     // Display name
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

    // list of DeckLink video inputs (empty until the first listing is done)
    int numDevices ();
    std::string name (int index);
    std::string properties (int index);
    int64_t persistentId (int index);
    int deviceNumber (int index);
    int index (int64_t persistent_id, int device_number);
    // list inputs again, in background
    void reload ();

    // video modes, identified by their GStreamer name
    static int numModes ();
    static DeckLinkMode mode (int index);
    static int modeIndex (const std::string &gst_name);
    static std::string modeLabel (const std::string &gst_name);
    static constexpr const char *default_mode = "1080p50";

    // connections, identified by their GStreamer name
    static int numConnections ();
    static DeckLinkConnection connection (int index);
    static int connectionIndex (const std::string &gst_name);
    static std::string connectionLabel (const std::string &gst_name);
    static constexpr const char *default_connection = "auto";

    // check for decklink GStreamer plugin availability
    static bool available ();

private:
    static void launchProbe (DeckLink *d);
    void probe ();
    void waitInitialized ();
    // NB: access_ must be locked
    std::vector< DeckLinkHandle >::iterator find (int64_t persistent_id, int device_number);

    std::mutex access_;
    std::vector< DeckLinkHandle > handles_;

    std::atomic<bool> probing_;
    std::mutex initialization_mutex_;
    std::condition_variable initialization_;
    bool initialized_;

    static const std::vector<DeckLinkMode> &modes ();
    static const std::vector<DeckLinkConnection> &connections ();
};


#endif // DECKLINKSOURCE_H
