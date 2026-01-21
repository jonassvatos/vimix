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

//  Desktop OpenGL function loader
#include <glad/glad.h>

#include <gst/gst.h>

#include "SourceRecording.h"
#include "Recorder.h"
#include "FrameGrabbing.h"
#include "FrameGrabber.h"
#include "FrameBuffer.h"
#include "Settings.h"
#include "Mixer.h"
#include "Source/Source.h"
#include "MediaPlayer.h"
#include "Toolkit/GstToolkit.h"
#include "Log.h"

#ifdef USE_GST_OPENGL_SYNC_HANDLER
#include "GPUVideoRecorder.h"
#endif

SourceRecordingManager& SourceRecordingManager::manager()
{
    static SourceRecordingManager _instance;
    return _instance;
}

bool SourceRecordingManager::startRecording(uint64_t source_id, const std::string &basename)
{
    // Check if source exists
    Source *source = Mixer::manager().findSource(source_id);
    if (!source) {
        Log::Warning("Cannot record: source %lu not found", source_id);
        return false;
    }

    // Check if already recording this source
    if (isRecording(source_id)) {
        Log::Warning("Source '%s' is already being recorded", source->name().c_str());
        return false;
    }

    // Get source framebuffer to determine dimensions
    FrameBuffer *fb = source->frame();
    if (!fb) {
        Log::Warning("Cannot record: source '%s' has no framebuffer", source->name().c_str());
        return false;
    }

    // Create recording info
    SourceRecordingInfo info;
    info.caps = nullptr;  // Will be created on first frame
    info.width = 0;
    info.height = 0;
    info.use_alpha = true;  // Always use alpha for mask support
    info.use_gpu_path = false;
    info.pbo[0] = 0;
    info.pbo[1] = 0;
    info.pbo_index = 0;
    info.pbo_next_index = 0;
    info.pbo_size = 0;

    // Decide which recorder to use based on settings and availability
#ifdef USE_GST_OPENGL_SYNC_HANDLER
    // Try GPU path if gpu_decoding is enabled and hardware encoder is available
    if (Settings::application.render.gpu_decoding &&
        GPUVideoRecorder::hasProfile(Settings::application.record.profile)) {
        info.grabber = new GPUVideoRecorder(basename);
        info.use_gpu_path = true;
        Log::Info("Source recording using GPU path (hardware encoder)");
    }
    else
#endif
    {
        // Fall back to CPU path with VideoRecorder
        info.grabber = new VideoRecorder(basename);
        info.use_gpu_path = false;
        Log::Info("Source recording using CPU path (software encoder)");
    }

    if (!info.grabber) {
        Log::Warning("Failed to create recorder for source '%s'", source->name().c_str());
        return false;
    }

    // Track in our map
    active_recordings_[source_id] = info;

    Log::Info("Started recording source '%s'", source->name().c_str());
    return true;
}

void SourceRecordingManager::stopRecording(uint64_t source_id)
{
    auto it = active_recordings_.find(source_id);
    if (it == active_recordings_.end()) {
        return;
    }

    SourceRecordingInfo &info = it->second;

    // Stop the recorder - this sends EOS to finalize the file
    // The recorder will be cleaned up in grabSourceFrames() when it finishes
    if (info.grabber)
        info.grabber->stop();

    // Remove from paused state
    paused_recordings_.erase(source_id);

    Source *source = Mixer::manager().findSource(source_id);
    if (source) {
        Log::Info("Stopping recording for source '%s'...", source->name().c_str());
    } else {
        Log::Info("Stopping recording for source %lu...", source_id);
    }

    // Note: The actual cleanup and adding to recentRecordings happens in
    // grabSourceFrames() when the grabber is detected as finished
}

void SourceRecordingManager::stopAllRecordings()
{
    // Copy the map keys to avoid iterator invalidation
    std::vector<uint64_t> source_ids;
    for (auto &pair : active_recordings_) {
        source_ids.push_back(pair.first);
    }

    // Stop each recording
    for (uint64_t source_id : source_ids) {
        stopRecording(source_id);
    }

    Log::Info("Stopped all source recordings");
}

void SourceRecordingManager::pauseRecording(uint64_t source_id, bool pause)
{
    auto it = active_recordings_.find(source_id);
    if (it == active_recordings_.end()) {
        return;
    }

    if (pause)
        paused_recordings_.insert(source_id);
    else
        paused_recordings_.erase(source_id);

    Source *source = Mixer::manager().findSource(source_id);
    if (source) {
        Log::Info("%s recording source '%s'",
                  pause ? "Paused" : "Resumed",
                  source->name().c_str());
    }
}

bool SourceRecordingManager::isRecording(uint64_t source_id) const
{
    return active_recordings_.find(source_id) != active_recordings_.end();
}

bool SourceRecordingManager::isPaused(uint64_t source_id) const
{
    return paused_recordings_.find(source_id) != paused_recordings_.end();
}

std::vector<uint64_t> SourceRecordingManager::getRecordingSources() const
{
    std::vector<uint64_t> sources;
    for (auto &pair : active_recordings_) {
        sources.push_back(pair.first);
    }
    return sources;
}

FrameGrabber* SourceRecordingManager::getRecorder(uint64_t source_id) const
{
    auto it = active_recordings_.find(source_id);
    if (it != active_recordings_.end()) {
        return it->second.grabber;
    }
    return nullptr;
}

void SourceRecordingManager::grabSourceFrames()
{
    if (active_recordings_.empty()) {
        return;
    }

    // Collect finished grabbers to remove after iteration
    std::vector<uint64_t> finished_recordings;

    // Iterate through all active recordings
    for (auto &pair : active_recordings_) {
        uint64_t source_id = pair.first;
        SourceRecordingInfo &info = pair.second;
        FrameGrabber *grabber = info.grabber;

        if (!grabber)
            continue;

        // Check if grabber has finished
        if (grabber->finished()) {
            finished_recordings.push_back(source_id);
            Source *source = Mixer::manager().findSource(source_id);
            if (source) {
                Log::Info("Recording finished for source '%s'", source->name().c_str());
            }
            continue;
        }

        // Skip if paused
        if (isPaused(source_id)) {
            continue;
        }

        // Get the source
        Source *source = Mixer::manager().findSource(source_id);
        if (!source)
            continue;

        // Get source framebuffer to check dimensions
        FrameBuffer *source_fb = source->frame();
        if (!source_fb || !source_fb->texture())
            continue;

        // Get dimensions from source framebuffer
        guint width = source_fb->width();
        guint height = source_fb->height();
        bool use_alpha = true;  // Always use alpha for mask support
        guint size = width * height * 4;  // RGBA

        if (size == 0)
            continue;

        // Create or update capture framebuffer and caps if dimensions changed
        if (info.caps == nullptr || info.width != width || info.height != height || info.use_alpha != use_alpha) {
            // Free old caps if any
            if (info.caps)
                gst_caps_unref(info.caps);

            // Delete old capture framebuffer if any
            if (info.capture_fb)
                delete info.capture_fb;

            // Create new capture framebuffer with alpha support
            info.capture_fb = new FrameBuffer(glm::vec3(width, height, 0.f), FrameBuffer::FrameBuffer_alpha);

            // Create new caps
            info.caps = gst_caps_new_simple("video/x-raw",
                                            "format", G_TYPE_STRING, "RGBA",
                                            "width",  G_TYPE_INT, width,
                                            "height", G_TYPE_INT, height,
                                            NULL);
            info.width = width;
            info.height = height;
            info.use_alpha = use_alpha;

            // For CPU path: Setup PBOs for double-buffered reading
            if (!info.use_gpu_path) {
                // Delete old PBOs if any
                if (info.pbo[0] != 0)
                    glDeleteBuffers(2, info.pbo);

                // Create new PBOs
                glGenBuffers(2, info.pbo);
                glBindBuffer(GL_PIXEL_PACK_BUFFER, info.pbo[0]);
                glBufferData(GL_PIXEL_PACK_BUFFER, size, NULL, GL_STREAM_READ);
                glBindBuffer(GL_PIXEL_PACK_BUFFER, info.pbo[1]);
                glBufferData(GL_PIXEL_PACK_BUFFER, size, NULL, GL_STREAM_READ);
                glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

                info.pbo_size = size;
                info.pbo_index = 0;
                info.pbo_next_index = 0;
            }
        }

        // Render the source with mask applied to the capture framebuffer
        source->renderWithMask(info.capture_fb);

#ifdef USE_GST_OPENGL_SYNC_HANDLER
        // GPU PATH: Pass texture ID directly to GPUVideoRecorder
        if (info.use_gpu_path) {
            // GPUVideoRecorder handles the texture-to-texture transfer internally
            grabber->addFrame(info.capture_fb->texture(), info.caps);
        }
        else
#endif
        {
            // CPU PATH: Use PBO double-buffering for efficient pixel readback
            // This mirrors the approach in FrameGrabbing::grabFrame()

            // Start async read into current PBO
            glBindBuffer(GL_PIXEL_PACK_BUFFER, info.pbo[info.pbo_index]);

            // Initiate async read from framebuffer to PBO
            glBindFramebuffer(GL_READ_FRAMEBUFFER, info.capture_fb->opengl_id());
            glPixelStorei(GL_PACK_ALIGNMENT, 4);
            glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, 0);
            glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);

            // If we have a previous frame ready in the other PBO, read it
            if (info.pbo_next_index != info.pbo_index) {
                // Switch to the PBO that has completed data
                glBindBuffer(GL_PIXEL_PACK_BUFFER, info.pbo[info.pbo_next_index]);

                // Allocate GStreamer buffer
                GstBuffer *buffer = gst_buffer_new_and_alloc(info.pbo_size);
                if (buffer) {
                    // Map gst buffer for writing
                    GstMapInfo map;
                    if (gst_buffer_map(buffer, &map, GST_MAP_WRITE)) {
                        // Map PBO for reading (this waits for the async read to complete)
                        unsigned char *ptr = (unsigned char *)glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY);
                        if (ptr) {
                            // Copy from PBO to GStreamer buffer
                            memmove(map.data, ptr, info.pbo_size);
                            glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
                        }
                        gst_buffer_unmap(buffer, &map);

                        // Add frame to grabber
                        if (gst_buffer_get_size(buffer) > 0) {
                            grabber->addFrame(buffer, info.caps);
                        }
                    }
                    // Free the buffer (grabber has taken a ref if needed)
                    gst_buffer_unref(buffer);
                }
            }

            glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

            // Alternate PBO indices for double-buffering
            info.pbo_next_index = info.pbo_index;
            info.pbo_index = (info.pbo_index + 1) % 2;
        }
    }

    // Remove finished recordings and add to recent recordings
    for (uint64_t source_id : finished_recordings) {
        auto it = active_recordings_.find(source_id);
        if (it != active_recordings_.end()) {
            SourceRecordingInfo &info = it->second;

            // Get filename from recorder and add to recent recordings
            // Handle both VideoRecorder and GPUVideoRecorder
            std::string filename;

#ifdef USE_GST_OPENGL_SYNC_HANDLER
            if (info.use_gpu_path) {
                GPUVideoRecorder *gpu_recorder = dynamic_cast<GPUVideoRecorder*>(info.grabber);
                if (gpu_recorder) {
                    filename = gpu_recorder->filename();
                }
            }
            else
#endif
            {
                VideoRecorder *recorder = dynamic_cast<VideoRecorder*>(info.grabber);
                if (recorder) {
                    filename = recorder->filename();
                }
            }

            if (!filename.empty()) {
                // Validate the recording
                std::string uri = GstToolkit::filename_to_uri(filename);
                MediaInfo media = MediaPlayer::UriDiscoverer(uri);
                if (media.valid && !media.isimage) {
                    Settings::application.recentRecordings.push(filename);
                    Log::Notify("Source Recording %s is ready.", filename.c_str());
                }
                else {
                    Settings::application.recentRecordings.remove(filename);
                    Log::Warning("Source Recording %s is invalid.", filename.c_str());
                }
            }

            // Free caps
            if (info.caps)
                gst_caps_unref(info.caps);

            // Delete capture framebuffer
            if (info.capture_fb)
                delete info.capture_fb;

            // Delete PBOs (CPU path only)
            if (!info.use_gpu_path && info.pbo[0] != 0)
                glDeleteBuffers(2, info.pbo);

            // Delete grabber
            delete info.grabber;
            active_recordings_.erase(it);
        }
        paused_recordings_.erase(source_id);
    }
}
