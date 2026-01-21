#ifndef SOURCERECORDING_H
#define SOURCERECORDING_H

#include <map>
#include <set>
#include <vector>
#include <string>
#include <cstdint>

#include <gst/gst.h>

// Forward declare OpenGL types
typedef unsigned int GLuint;

class FrameGrabber;
class FrameBuffer;

/**
 * @brief Info structure for an active source recording
 *
 * Supports two recording paths:
 * - GPU path: Uses GPUVideoRecorder with texture-to-texture transfer (no CPU copies)
 * - CPU path: Uses VideoRecorder with PBO double-buffering for efficient pixel readback
 */
struct SourceRecordingInfo
{
    FrameGrabber *grabber;
    GstCaps *caps;
    FrameBuffer *capture_fb;  // Framebuffer for rendering source with mask applied
    guint width;
    guint height;
    bool use_alpha;
    bool use_gpu_path;        // True if using GPUVideoRecorder (texture-based)

    // PBO double-buffering for CPU path (like FrameGrabbing)
    GLuint pbo[2];            // Pixel buffer objects for async read
    int pbo_index;            // Current PBO being written to
    int pbo_next_index;       // PBO ready for reading
    guint pbo_size;           // Size of each PBO buffer

    SourceRecordingInfo()
        : grabber(nullptr), caps(nullptr), capture_fb(nullptr),
          width(0), height(0), use_alpha(false), use_gpu_path(false),
          pbo_index(0), pbo_next_index(0), pbo_size(0)
    {
        pbo[0] = 0;
        pbo[1] = 0;
    }
};

/**
 * @brief Manager for per-source video recording
 *
 * This class manages individual video recordings for each source,
 * allowing users to record webcams, media files, and other sources
 * independently from the main output recording.
 */
class SourceRecordingManager
{
public:
    /**
     * @brief Get the singleton instance
     */
    static SourceRecordingManager& manager();

    /**
     * @brief Start recording a specific source
     * @param source_id The ID of the source to record
     * @param basename The base filename for the recording
     * @return true if recording started successfully
     */
    bool startRecording(uint64_t source_id, const std::string &basename);

    /**
     * @brief Stop recording a specific source
     * @param source_id The ID of the source to stop recording
     */
    void stopRecording(uint64_t source_id);

    /**
     * @brief Stop all active source recordings
     */
    void stopAllRecordings();

    /**
     * @brief Pause or resume recording for a specific source
     * @param source_id The ID of the source
     * @param pause true to pause, false to resume
     */
    void pauseRecording(uint64_t source_id, bool pause);

    /**
     * @brief Check if a source is currently being recorded
     * @param source_id The ID of the source to check
     * @return true if the source is being recorded
     */
    bool isRecording(uint64_t source_id) const;

    /**
     * @brief Check if a source recording is paused
     * @param source_id The ID of the source to check
     * @return true if the source recording is paused
     */
    bool isPaused(uint64_t source_id) const;

    /**
     * @brief Get list of all sources currently being recorded
     * @return Vector of source IDs
     */
    std::vector<uint64_t> getRecordingSources() const;

    /**
     * @brief Get the recorder instance for a specific source
     * @param source_id The ID of the source
     * @return Pointer to FrameGrabber or nullptr if not recording
     */
    FrameGrabber* getRecorder(uint64_t source_id) const;

    /**
     * @brief Capture frames from all recording sources
     * Called from main loop to grab frames from each active source
     */
    void grabSourceFrames();

private:
    SourceRecordingManager() = default;
    ~SourceRecordingManager() = default;
    SourceRecordingManager(const SourceRecordingManager&) = delete;
    SourceRecordingManager& operator=(const SourceRecordingManager&) = delete;

    // Map of source ID to recording info (includes grabber and cached caps)
    std::map<uint64_t, SourceRecordingInfo> active_recordings_;

    // Set of paused source recordings
    std::set<uint64_t> paused_recordings_;
};

#endif // SOURCERECORDING_H
