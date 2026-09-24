/**
 * Vita Music Assistant - Music Queue Manager Implementation
 */

#include "app/music_queue.hpp"
#include <borealis.hpp>
#include <algorithm>
#include <chrono>
#include <fstream>

#ifdef __vita__
#include <psp2/io/fcntl.h>
#endif

namespace vita_ma {

static const std::string QUEUE_STATE_FILE = "ux0:data/VitaMA/queue_state.txt";

MusicQueue::MusicQueue() {
    // Seed random number generator
    auto seed = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    m_rng.seed(static_cast<unsigned int>(seed));
}

MusicQueue& MusicQueue::getInstance() {
    static MusicQueue instance;
    return instance;
}

void MusicQueue::clear() {
    m_queue.clear();
    m_shuffleOrder.clear();
    m_currentIndex = -1;
    m_shufflePosition = -1;
    notifyQueueChanged();
}

QueueItem MusicQueue::mediaItemToQueueItem(const MusicItem& item, int index) {
    QueueItem qi;
    qi.ratingKey = item.itemId;
    qi.uri = item.uri;
    qi.title = item.name;
    qi.artist = item.artistName;
    qi.album = item.albumName;
    qi.thumb = item.imageUrl;
    qi.thumbProvider = item.imageProvider;
    qi.duration = item.duration;  // Already in seconds
    qi.index = index;
    return qi;
}

void MusicQueue::insertTrackAfterCurrent(const MusicItem& item) {
    int insertPos = m_currentIndex + 1;
    if (insertPos < 0) insertPos = 0;
    if (insertPos > (int)m_queue.size()) insertPos = (int)m_queue.size();

    m_queue.insert(m_queue.begin() + insertPos, mediaItemToQueueItem(item, insertPos));

    // Update indices for items after the insertion point
    for (int i = insertPos; i < (int)m_queue.size(); i++) {
        m_queue[i].index = i;
    }

    // Update shuffle order incrementally: bump indices >= insertPos, then insert
    // the new track right after the current shuffle position (play next behavior)
    if (m_shuffleEnabled) {
        for (auto& idx : m_shuffleOrder) {
            if (idx >= insertPos) idx++;
        }
        // Insert right after current shuffle position so it plays next
        int shuffleInsert = m_shufflePosition + 1;
        if (shuffleInsert > (int)m_shuffleOrder.size()) shuffleInsert = (int)m_shuffleOrder.size();
        m_shuffleOrder.insert(m_shuffleOrder.begin() + shuffleInsert, insertPos);
    }

    notifyQueueChanged();
    brls::Logger::info("MusicQueue: Inserted track after current at position {}", insertPos);
}

void MusicQueue::addTracks(const std::vector<MusicItem>& items) {
    int startIndex = (int)m_queue.size();
    m_queue.reserve(m_queue.size() + items.size());
    for (size_t i = 0; i < items.size(); i++) {
        m_queue.push_back(mediaItemToQueueItem(items[i], startIndex + (int)i));
    }

    // Append new indices to the end of shuffle order, then Fisher-Yates
    // shuffle only the unplayed tail portion — O(n) instead of O(n^2)
    if (m_shuffleEnabled && !m_queue.empty()) {
        // Append new track indices
        m_shuffleOrder.reserve(m_shuffleOrder.size() + items.size());
        for (size_t i = 0; i < items.size(); i++) {
            m_shuffleOrder.push_back(startIndex + (int)i);
        }
        // Shuffle the unplayed tail (everything after current shuffle position)
        int tailStart = m_shufflePosition + 1;
        int tailSize = (int)m_shuffleOrder.size() - tailStart;
        for (int i = tailSize - 1; i > 0; i--) {
            int j = m_rng() % (i + 1);
            std::swap(m_shuffleOrder[tailStart + i], m_shuffleOrder[tailStart + j]);
        }
    }

    notifyQueueChanged();
}

void MusicQueue::setQueue(const std::vector<MusicItem>& items, int startIndex) {
    clear();

    m_queue.reserve(items.size());
    for (size_t i = 0; i < items.size(); i++) {
        m_queue.push_back(mediaItemToQueueItem(items[i], (int)i));
    }

    if (m_shuffleEnabled) {
        generateShuffleOrder();
        // Find the start index in shuffle order
        m_shufflePosition = 0;
        for (size_t i = 0; i < m_shuffleOrder.size(); i++) {
            if (m_shuffleOrder[i] == startIndex) {
                // Move this to the front of remaining shuffle
                std::swap(m_shuffleOrder[0], m_shuffleOrder[i]);
                break;
            }
        }
        m_currentIndex = m_shuffleOrder[0];
    } else {
        m_currentIndex = (startIndex >= 0 && startIndex < (int)m_queue.size())
                        ? startIndex : 0;
    }

    notifyQueueChanged();
    brls::Logger::info("MusicQueue: Set queue with {} tracks, starting at {}",
                       m_queue.size(), m_currentIndex);
}

bool MusicQueue::playNext() {
    if (m_queue.empty()) return false;

    int nextIndex = -1;

    if (m_repeatMode == RepeatMode::ONE) {
        // Repeat same track
        nextIndex = m_currentIndex;
    } else if (m_shuffleEnabled) {
        // Use shuffle order
        m_shufflePosition++;
        if (m_shufflePosition >= (int)m_shuffleOrder.size()) {
            if (m_repeatMode == RepeatMode::ALL) {
                // Reshuffle and start over
                reshuffle();
                m_shufflePosition = 0;
            } else {
                // End of queue - stop
                m_shufflePosition = (int)m_shuffleOrder.size() - 1;
                return false;
            }
        }
        nextIndex = m_shuffleOrder[m_shufflePosition];
    } else {
        // Normal sequential order
        nextIndex = m_currentIndex + 1;
        if (nextIndex >= (int)m_queue.size()) {
            if (m_repeatMode == RepeatMode::ALL) {
                nextIndex = 0;
            } else {
                // End of queue - stop
                return false;
            }
        }
    }

    m_currentIndex = nextIndex;
    brls::Logger::info("MusicQueue: Next track {} - {}", m_currentIndex, m_queue[m_currentIndex].title);
    return true;
}

bool MusicQueue::playPrevious() {
    if (m_queue.empty()) return false;

    int prevIndex = -1;

    if (m_shuffleEnabled) {
        m_shufflePosition--;
        if (m_shufflePosition < 0) {
            if (m_repeatMode == RepeatMode::ALL) {
                m_shufflePosition = (int)m_shuffleOrder.size() - 1;
            } else {
                m_shufflePosition = 0;
                return false;
            }
        }
        prevIndex = m_shuffleOrder[m_shufflePosition];
    } else {
        prevIndex = m_currentIndex - 1;
        if (prevIndex < 0) {
            if (m_repeatMode == RepeatMode::ALL) {
                prevIndex = (int)m_queue.size() - 1;
            } else {
                return false;
            }
        }
    }

    m_currentIndex = prevIndex;
    brls::Logger::info("MusicQueue: Previous track {} - {}", m_currentIndex, m_queue[m_currentIndex].title);
    return true;
}

const QueueItem* MusicQueue::getCurrentTrack() const {
    if (m_currentIndex < 0 || m_currentIndex >= (int)m_queue.size()) {
        return nullptr;
    }
    return &m_queue[m_currentIndex];
}

void MusicQueue::setShuffle(bool enabled) {
    if (m_shuffleEnabled == enabled) return;

    m_shuffleEnabled = enabled;

    if (enabled && !m_queue.empty()) {
        // Build shuffle order: current track first, then all others shuffled
        m_shuffleOrder.clear();
        m_shuffleOrder.push_back(m_currentIndex);

        // Collect all other indices
        std::vector<int> others;
        for (int i = 0; i < (int)m_queue.size(); i++) {
            if (i != m_currentIndex) {
                others.push_back(i);
            }
        }

        // Fisher-Yates shuffle the remaining tracks
        for (int i = (int)others.size() - 1; i > 0; i--) {
            int j = m_rng() % (i + 1);
            std::swap(others[i], others[j]);
        }

        // Append shuffled tracks after current
        m_shuffleOrder.insert(m_shuffleOrder.end(), others.begin(), others.end());
        m_shufflePosition = 0;
    } else {
        m_shuffleOrder.clear();
        m_shufflePosition = -1;
    }

    brls::Logger::info("MusicQueue: Shuffle {}", enabled ? "enabled" : "disabled");
    notifyQueueChanged();
}

void MusicQueue::reshuffle() {
    if (!m_shuffleEnabled || m_queue.empty()) return;

    generateShuffleOrder();
    m_shufflePosition = -1;

    brls::Logger::debug("MusicQueue: Reshuffled queue");
}

void MusicQueue::generateShuffleOrder() {
    m_shuffleOrder.clear();
    m_shuffleOrder.reserve(m_queue.size());

    for (int i = 0; i < (int)m_queue.size(); i++) {
        m_shuffleOrder.push_back(i);
    }

    // Fisher-Yates shuffle
    for (int i = (int)m_shuffleOrder.size() - 1; i > 0; i--) {
        int j = m_rng() % (i + 1);
        std::swap(m_shuffleOrder[i], m_shuffleOrder[j]);
    }
}

void MusicQueue::setRepeatMode(RepeatMode mode) {
    m_repeatMode = mode;

    const char* modeStr = "OFF";
    if (mode == RepeatMode::ONE) modeStr = "ONE";
    else if (mode == RepeatMode::ALL) modeStr = "ALL";

    brls::Logger::info("MusicQueue: Repeat mode set to {}", modeStr);
    notifyQueueChanged();
}

void MusicQueue::cycleRepeatMode() {
    switch (m_repeatMode) {
        case RepeatMode::OFF:
            setRepeatMode(RepeatMode::ALL);
            break;
        case RepeatMode::ALL:
            setRepeatMode(RepeatMode::ONE);
            break;
        case RepeatMode::ONE:
            setRepeatMode(RepeatMode::OFF);
            break;
    }
}

void MusicQueue::onTrackEnded() {
    brls::Logger::debug("MusicQueue: Track ended, checking for next");

    const QueueItem* nextTrack = nullptr;

    if (playNext()) {
        nextTrack = getCurrentTrack();
    }

    if (m_trackEndedCallback) {
        m_trackEndedCallback(nextTrack);
    }
}

void MusicQueue::notifyQueueChanged() {
    if (m_queueChangedCallback) {
        m_queueChangedCallback();
    }
}

void MusicQueue::saveState() {
    std::ofstream file(QUEUE_STATE_FILE);
    if (!file.is_open()) {
        brls::Logger::warning("MusicQueue: Could not save queue state");
        return;
    }

    // Save settings
    file << "shuffle=" << (m_shuffleEnabled ? 1 : 0) << "\n";
    file << "repeat=" << (int)m_repeatMode << "\n";
    file << "current=" << m_currentIndex << "\n";
    file << "count=" << m_queue.size() << "\n";

    // Save queue items (just item IDs for now)
    for (const auto& item : m_queue) {
        file << "track=" << item.ratingKey << "\n";
    }

    file.close();
    brls::Logger::debug("MusicQueue: State saved");
}

// See the TODO in music_queue.hpp.

} // namespace vita_ma
