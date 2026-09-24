/**
 * Vita Music Assistant - Music Assistant API Client
 * Additional data types for queue management and player info.
 * Base types (MediaType, MusicItem, Hub) are defined in app.h.
 */

#pragma once

#include "app.h"
#include <string>
#include <vector>
#include <functional>
#include <memory>

namespace vita_ma {

// Queue item
struct MAQueueItem {
    std::string queueItemId;
    std::string uri;
    std::string name;
    std::string artistName;
    std::string albumName;
    std::string imageUrl;
    std::string imageProvider;
    int duration = 0;
    int trackNumber = 0;
    MediaType mediaType = MediaType::UNKNOWN;
};

// Queue state
struct QueueState {
    std::string queueId;
    std::string currentItemId;
    int currentIndex = 0;
    int elapsed = 0;           // seconds
    int duration = 0;          // seconds
    bool shuffleEnabled = false;
    std::string repeatMode;    // "off", "one", "all"
    std::string state;         // "playing", "paused", "idle"
    std::vector<MAQueueItem> items;
};

// Player info from MA server
struct PlayerInfo {
    std::string playerId;
    std::string name;
    std::string type;
    bool powered = false;
    bool available = true;
    int volume = 100;
    bool muted = false;
};

} // namespace vita_ma
