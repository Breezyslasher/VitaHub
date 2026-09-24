/**
 * Vita Music Assistant - Settings Tab implementation
 */

#include "view/settings_tab.hpp"
#include "app/application.hpp"
#include "app/ma_client.hpp"
#include "app/webrtc_client.hpp"
#include "app/ma_types.hpp"
#include "activity/player_activity.hpp"
#include "utils/http_client.hpp"
#include <set>
#include <chrono>
#include <thread>
#include <cctype>

#ifdef __vita__
#include <psp2/net/netctl.h>
#include <psp2/net/net.h>
#endif

namespace vita_ma {

SettingsTab::SettingsTab() {
    this->setAxis(brls::Axis::COLUMN);
    this->setJustifyContent(brls::JustifyContent::FLEX_START);
    this->setAlignItems(brls::AlignItems::STRETCH);
    this->setGrow(1.0f);

    // Create scrolling container
    m_scrollView = new brls::ScrollingFrame();
    m_scrollView->setGrow(1.0f);

    m_contentBox = new brls::Box();
    m_contentBox->setAxis(brls::Axis::COLUMN);
    m_contentBox->setPadding(20);
    m_contentBox->setGrow(1.0f);

    // Create all sections
    createAccountSection();
    createUISection();
    createLayoutSection();
    createContentDisplaySection();
    createPlaybackSection();
    createAudioSection();
    createPlayerSection();
    createRemoteAccessSection();
    createDebugSection();
    createAboutSection();

    m_scrollView->setContentView(m_contentBox);
    this->addView(m_scrollView);
}

void SettingsTab::createAccountSection() {
    Application& app = Application::getInstance();

    // Section header
    auto* header = new brls::Header();
    header->setTitle("Account");
    m_contentBox->addView(header);

    // User info cell
    m_userLabel = new brls::Label();
    m_userLabel->setText("User: " + (app.getUsername().empty() ? "Not logged in" : app.getUsername()));
    m_userLabel->setFontSize(18);
    m_userLabel->setMarginLeft(16);
    m_userLabel->setMarginBottom(8);
    m_contentBox->addView(m_userLabel);

    // Server info cell
    m_serverLabel = new brls::Label();
    m_serverLabel->setText("Server: " + (app.getServerUrl().empty() ? "Not connected" : app.getServerUrl()));
    m_serverLabel->setFontSize(18);
    m_serverLabel->setMarginLeft(16);
    m_serverLabel->setMarginBottom(16);
    m_contentBox->addView(m_serverLabel);

    // Logout button
    auto* logoutCell = new brls::DetailCell();
    logoutCell->setText("Logout");
    logoutCell->setDetailText("Sign out from current account");
    logoutCell->registerClickAction([this](brls::View* view) {
        onLogout();
        return true;
    });
    m_contentBox->addView(logoutCell);
}

void SettingsTab::createUISection() {
    Application& app = Application::getInstance();
    AppSettings& settings = app.getSettings();

    // Section header
    auto* header = new brls::Header();
    header->setTitle("User Interface");
    m_contentBox->addView(header);

    // Theme selector
    m_themeSelector = new brls::SelectorCell();
    m_themeSelector->init("Theme", {"System", "Light", "Dark"}, static_cast<int>(settings.theme),
        [this](int index) {
            onThemeChanged(index);
        });
    m_contentBox->addView(m_themeSelector);

    // Debug logging toggle
    m_debugLogToggle = new brls::BooleanCell();
    m_debugLogToggle->init("Debug Logging", settings.debugLogging, [&settings](bool value) {
        settings.debugLogging = value;
        Application::getInstance().applyLogLevel();
        Application::getInstance().saveSettings();
    });
    m_contentBox->addView(m_debugLogToggle);

    // Show debug tab toggle
    m_showDebugTabToggle = new brls::BooleanCell();
    m_showDebugTabToggle->init("Show Debug Tab", settings.showDebugTab, [&settings](bool value) {
        settings.showDebugTab = value;
        Application::getInstance().saveSettings();
    });
    m_contentBox->addView(m_showDebugTabToggle);

    // Info label for debug tab setting
    auto* debugInfoLabel = new brls::Label();
    debugInfoLabel->setText("Debug tab change requires app restart");
    debugInfoLabel->setFontSize(14);
    debugInfoLabel->setMarginLeft(16);
    debugInfoLabel->setMarginTop(8);
    m_contentBox->addView(debugInfoLabel);
}

void SettingsTab::createLayoutSection() {
    Application& app = Application::getInstance();
    AppSettings& settings = app.getSettings();

    // Section header
    auto* header = new brls::Header();
    header->setTitle("Layout");
    m_contentBox->addView(header);

    // Collapse sidebar toggle
    m_collapseSidebarToggle = new brls::BooleanCell();
    m_collapseSidebarToggle->init("Collapse Sidebar", settings.collapseSidebar, [&settings](bool value) {
        settings.collapseSidebar = value;
        Application::getInstance().saveSettings();
        // Note: Requires app restart to take effect
    });
    m_contentBox->addView(m_collapseSidebarToggle);

    // Manage sidebar order
    m_sidebarOrderCell = new brls::DetailCell();
    m_sidebarOrderCell->setText("Sidebar Order");
    m_sidebarOrderCell->setDetailText(settings.sidebarOrder.empty() ? "Default" : "Custom");
    m_sidebarOrderCell->registerClickAction([this](brls::View* view) {
        onManageSidebarOrder();
        return true;
    });
    m_contentBox->addView(m_sidebarOrderCell);

    // Info label
    auto* infoLabel = new brls::Label();
    infoLabel->setText("Layout changes require app restart");
    infoLabel->setFontSize(14);
    infoLabel->setMarginLeft(16);
    infoLabel->setMarginTop(8);
    m_contentBox->addView(infoLabel);
}

void SettingsTab::createContentDisplaySection() {
    Application& app = Application::getInstance();
    AppSettings& settings = app.getSettings();

    // Section header
    auto* header = new brls::Header();
    header->setTitle("Content Display");
    m_contentBox->addView(header);

    // Show playlists toggle
    m_playlistsToggle = new brls::BooleanCell();
    m_playlistsToggle->init("Show Playlists", settings.showPlaylists, [&settings](bool value) {
        settings.showPlaylists = value;
        Application::getInstance().saveSettings();
    });
    m_contentBox->addView(m_playlistsToggle);

    // Hide titles toggle
    m_hideTitlesToggle = new brls::BooleanCell();
    m_hideTitlesToggle->init("Hide Titles in Grid", settings.hideTitlesInGrid, [&settings](bool value) {
        settings.hideTitlesInGrid = value;
        Application::getInstance().saveSettings();
    });
    m_contentBox->addView(m_hideTitlesToggle);

    // Info label
    auto* contentInfoLabel = new brls::Label();
    contentInfoLabel->setText("Hides empty sections automatically");
    contentInfoLabel->setFontSize(14);
    contentInfoLabel->setMarginLeft(16);
    contentInfoLabel->setMarginTop(8);
    m_contentBox->addView(contentInfoLabel);
}

void SettingsTab::createPlaybackSection() {
    Application& app = Application::getInstance();
    AppSettings& settings = app.getSettings();

    // Section header
    auto* header = new brls::Header();
    header->setTitle("Playback");
    m_contentBox->addView(header);

    // Auto-play next toggle
    m_autoPlayToggle = new brls::BooleanCell();
    m_autoPlayToggle->init("Auto-Play Next Track", settings.autoPlayNext, [&settings](bool value) {
        settings.autoPlayNext = value;
        Application::getInstance().saveSettings();
    });
    m_contentBox->addView(m_autoPlayToggle);

    // Seek interval selector
    m_seekIntervalSelector = new brls::SelectorCell();
    m_seekIntervalSelector->init("Seek Interval",
        {"5 seconds", "10 seconds", "15 seconds", "30 seconds", "60 seconds"},
        settings.seekInterval == 5 ? 0 :
        settings.seekInterval == 10 ? 1 :
        settings.seekInterval == 15 ? 2 :
        settings.seekInterval == 30 ? 3 : 4,
        [this](int index) {
            onSeekIntervalChanged(index);
        });
    m_contentBox->addView(m_seekIntervalSelector);

    // Controls auto-hide selector
    m_controlsAutoHideSelector = new brls::SelectorCell();
    m_controlsAutoHideSelector->init("Controls Auto-Hide",
        {"Never", "3 seconds", "5 seconds", "10 seconds", "15 seconds"},
        settings.controlsAutoHideSeconds == 0 ? 0 :
        settings.controlsAutoHideSeconds == 3 ? 1 :
        settings.controlsAutoHideSeconds == 5 ? 2 :
        settings.controlsAutoHideSeconds == 10 ? 3 : 4,
        [this](int index) {
            onControlsAutoHideChanged(index);
        });
    m_contentBox->addView(m_controlsAutoHideSelector);

    // Music section
    auto* musicHeader = new brls::Header();
    musicHeader->setTitle("Music");
    m_contentBox->addView(musicHeader);

    // Default track action selector
    m_trackActionSelector = new brls::SelectorCell();
    m_trackActionSelector->init("Default Track Action",
        {"Play Next", "Play Now (Replace Current)", "Add to Bottom of Queue", "Play Now (Clear Queue)", "Ask Each Time"},
        static_cast<int>(settings.trackDefaultAction),
        [](int index) {
            Application& app = Application::getInstance();
            app.getSettings().trackDefaultAction = static_cast<TrackDefaultAction>(index);
            app.saveSettings();
        });
    m_contentBox->addView(m_trackActionSelector);

    // Background music toggle
    m_backgroundMusicToggle = new brls::BooleanCell();
    m_backgroundMusicToggle->init("Background Music", settings.backgroundMusic, [&settings](bool value) {
        settings.backgroundMusic = value;
        Application::getInstance().saveSettings();
    });
    m_contentBox->addView(m_backgroundMusicToggle);

    // Info label for music settings
    auto* musicInfoLabel = new brls::Label();
    musicInfoLabel->setText("Background music lets you leave player to add more songs");
    musicInfoLabel->setFontSize(14);
    musicInfoLabel->setMarginLeft(16);
    musicInfoLabel->setMarginTop(8);
    m_contentBox->addView(musicInfoLabel);
}

void SettingsTab::createAudioSection() {
    Application& app = Application::getInstance();
    AppSettings& settings = app.getSettings();

    // Section header
    auto* header = new brls::Header();
    header->setTitle("Audio");
    m_contentBox->addView(header);

    // Audio quality selector
    m_qualitySelector = new brls::SelectorCell();
    m_qualitySelector->init("Audio Quality",
        {"Lossless (FLAC)", "High (320 kbps)", "Normal (192 kbps)", "Low (96 kbps)"},
        static_cast<int>(settings.audioQuality),
        [this](int index) {
            onQualityChanged(index);
        });
    m_contentBox->addView(m_qualitySelector);

    // Connection timeout selector
    m_connectionTimeoutSelector = new brls::SelectorCell();
    m_connectionTimeoutSelector->init("Connection Timeout",
        {"30 seconds", "60 seconds", "120 seconds", "180 seconds", "300 seconds"},
        settings.connectionTimeout == 30 ? 0 :
        settings.connectionTimeout == 60 ? 1 :
        settings.connectionTimeout == 120 ? 2 :
        settings.connectionTimeout == 300 ? 4 : 3,
        [this](int index) {
            onConnectionTimeoutChanged(index);
        });
    m_contentBox->addView(m_connectionTimeoutSelector);
}

void SettingsTab::createPlayerSection() {
    Application& app = Application::getInstance();
    AppSettings& settings = app.getSettings();

    // Section header
    auto* header = new brls::Header();
    header->setTitle("Player");
    m_contentBox->addView(header);

    // Local playback toggle
    m_localPlaybackToggle = new brls::BooleanCell();
    m_localPlaybackToggle->init("Local Playback", settings.localPlayback, [&settings](bool value) {
        settings.localPlayback = value;
        Application::getInstance().saveSettings();
    });
    m_contentBox->addView(m_localPlaybackToggle);

    auto* localPlaybackInfo = new brls::Label();
    localPlaybackInfo->setText("Play audio on this Vita. Disable to use Vita as a remote control only.");
    localPlaybackInfo->setFontSize(14);
    localPlaybackInfo->setMarginLeft(16);
    localPlaybackInfo->setMarginTop(4);
    m_contentBox->addView(localPlaybackInfo);

    // Player name setting - opens on-screen keyboard for free-form input
    m_playerNameCell = new brls::DetailCell();
    m_playerNameCell->setText("Player Name");
    m_playerNameCell->setDetailText(settings.sendspinPlayerName);
    m_playerNameCell->registerClickAction([this](brls::View*) {
        auto& currentName = Application::getInstance().getSettings().sendspinPlayerName;
        brls::Application::getImeManager()->openForText([this](std::string newName) {
            if (newName.empty()) return;
            Application& a = Application::getInstance();
            a.getSettings().sendspinPlayerName = newName;
            a.saveSettings();
            m_playerNameCell->setDetailText(newName);
            brls::Application::notify("Player name changed. Reconnect to apply.");
        }, "Player Name", "Enter the name shown in Music Assistant", 64, currentName);
        return true;
    });
    m_contentBox->addView(m_playerNameCell);

    auto* nameInfo = new brls::Label();
    nameInfo->setText("Name shown in Music Assistant. Requires reconnect to take effect.");
    nameInfo->setFontSize(14);
    nameInfo->setMarginLeft(16);
    nameInfo->setMarginTop(4);
    m_contentBox->addView(nameInfo);

    // Target player selector - initially shows "Loading..." until player list arrives
    m_playerSelector = new brls::SelectorCell();
    m_playerSelector->init("Target Player",
        {"This Vita (Local)"},
        0,
        [this](int index) {
            onPlayerSelected(index);
        });
    m_contentBox->addView(m_playerSelector);

    auto* playerInfo = new brls::Label();
    playerInfo->setText("Choose which Music Assistant player to control.");
    playerInfo->setFontSize(14);
    playerInfo->setMarginLeft(16);
    playerInfo->setMarginTop(4);
    m_contentBox->addView(playerInfo);

    // Load available players from the server
    loadPlayerList();
}

void SettingsTab::loadPlayerList() {
    auto& client = MAClient::instance();
    if (!client.isConnected()) {
        brls::Logger::debug("SettingsTab: Not connected, skipping player list load");
        return;
    }

    client.getPlayers([this](bool success, const Json& result) {
        if (!success || result.type() != Json::ARRAY) {
            brls::Logger::error("SettingsTab: Failed to load player list");
            return;
        }

        std::vector<PlayerInfo> players;
        for (size_t i = 0; i < result.size(); i++) {
            const Json& p = result[i];
            PlayerInfo info;
            if (p.has("player_id")) info.playerId = p["player_id"].str();
            if (p.has("name")) info.name = p["name"].str();
            if (p.has("type")) info.type = p["type"].str();
            if (p.has("powered")) info.powered = p["powered"].boolVal();
            if (p.has("available")) info.available = p["available"].boolVal();
            if (!info.playerId.empty()) {
                players.push_back(info);
            }
        }

        brls::sync([this, players]() {
            m_players = players;

            // Identify our own registered Sendspin player so we can drop it from
            // the list - it's represented by the "This Vita" entry (local
            // playback), so it must not appear twice.
            std::string ownClientId = App::instance().getPlayerId();
            std::string ownName = Application::getInstance().getSettings().sendspinPlayerName;
            if (ownName.empty()) ownName = "PS Vita";

            // Build a filtered player list (our own player removed) so option
            // indices map cleanly onto m_players in onPlayerSelected.
            std::vector<PlayerInfo> filtered;
            for (size_t i = 0; i < m_players.size(); i++) {
                bool isOwn = (!ownClientId.empty() &&
                              (m_players[i].playerId == ownClientId ||
                               m_players[i].playerId.find(ownClientId) != std::string::npos)) ||
                             m_players[i].name == ownName;
                if (isOwn) continue;
                filtered.push_back(m_players[i]);
            }
            m_players = filtered;

            // "This Vita (Local)" first, then the remaining MA players.
            std::vector<std::string> options;
            options.push_back("This Vita");
            int selectedIndex = 0;
            const auto& currentId = Application::getInstance().getSettings().selectedPlayerId;

            for (size_t i = 0; i < m_players.size(); i++) {
                std::string label = m_players[i].name;
                if (!m_players[i].available) label += " (offline)";
                options.push_back(label);

                if (m_players[i].playerId == currentId) {
                    selectedIndex = static_cast<int>(i + 1);
                }
            }

            brls::Logger::info("SettingsTab: Loaded {} players", m_players.size());

            m_playerSelector->init("Target Player", options, selectedIndex,
                [this](int index) { onPlayerSelected(index); });
        });
    });
}

void SettingsTab::onPlayerSelected(int index) {
    Application& app = Application::getInstance();
    AppSettings& settings = app.getSettings();

    if (index == 0) {
        // "This Vita" - local playback on the instant local path.
        settings.selectedPlayerId.clear();
        brls::Logger::info("SettingsTab: Selected local Vita player");
    } else {
        int playerIndex = index - 1;
        if (playerIndex >= 0 && playerIndex < static_cast<int>(m_players.size())) {
            settings.selectedPlayerId = m_players[playerIndex].playerId;
            brls::Logger::info("SettingsTab: Selected player '{}' ({})",
                m_players[playerIndex].name, m_players[playerIndex].playerId);
        }
    }
    app.saveSettings();
}

void SettingsTab::createRemoteAccessSection() {
    Application& app = Application::getInstance();
    AppSettings& settings = app.getSettings();

    // Section header
    auto* header = new brls::Header();
    header->setTitle("Remote Access");
    m_contentBox->addView(header);

    // Remote ID - opens the on-screen keyboard to enter the server's Remote ID
    m_remoteIdCell = new brls::DetailCell();
    m_remoteIdCell->setText("Remote ID");
    m_remoteIdCell->setDetailText(settings.remoteId.empty() ? "Not set" : settings.remoteId);
    m_remoteIdCell->registerClickAction([this](brls::View*) {
        std::string current = Application::getInstance().getSettings().remoteId;
        brls::Application::getImeManager()->openForText([this](std::string value) {
            // Canonicalize (strips hyphen grouping / extracts from a pasted URL)
            std::string id = WebRTCClient::normalizeRemoteId(value);

            if (!id.empty() && !MAClient::isRemoteId(id)) {
                brls::Application::notify("Invalid Remote ID - copy it from the server's Remote Access settings");
                return;
            }
            Application& a = Application::getInstance();
            a.getSettings().remoteId = id;
            a.saveSettings();
            m_remoteIdCell->setDetailText(id.empty() ? "Not set" : id);
            brls::Application::notify(id.empty() ? "Remote ID cleared"
                                                 : "Remote ID saved");
        }, "Remote ID", "Remote ID from server settings", 80, current);
        return true;
    });
    m_contentBox->addView(m_remoteIdCell);

    // Connect Now - establishes the WebRTC connection using the saved id + token
    m_remoteConnectCell = new brls::DetailCell();
    m_remoteConnectCell->setText("Connect Now");
    m_remoteConnectCell->setDetailText("Connect to your server remotely");
    m_remoteConnectCell->registerClickAction([this](brls::View*) {
        onRemoteConnect();
        return true;
    });
    m_contentBox->addView(m_remoteConnectCell);

    // Info label
    auto* infoLabel = new brls::Label();
    infoLabel->setText("Remote access uses WebRTC to reach your Music Assistant server from "
                       "anywhere. Enable Remote Access on the server, sign in once with your "
                       "server URL, then set the Remote ID here and press Connect Now.");
    infoLabel->setFontSize(14);
    infoLabel->setMarginLeft(16);
    infoLabel->setMarginTop(8);
    m_contentBox->addView(infoLabel);
}

void SettingsTab::onRemoteConnect() {
    Application& app = Application::getInstance();
    const std::string remoteId = app.getSettings().remoteId;
    const std::string token = app.getAuthToken();

    if (remoteId.empty()) {
        brls::Application::notify("Set a Remote ID first");
        return;
    }
    if (!MAClient::isRemoteId(remoteId)) {
        brls::Application::notify("Invalid Remote ID - copy it from the server's Remote Access settings");
        return;
    }
    if (token.empty()) {
        brls::Application::notify("Sign in with your server URL once before connecting remotely");
        return;
    }

    brls::Application::notify("Connecting remotely (pairing can take ~30s)...");
    std::string username = app.getUsername();

    std::thread([remoteId, token, username]() {
        bool ok = MAClient::instance().connect(remoteId, token);

        brls::sync([remoteId, token, username, ok]() {
            if (ok) {
                Application& a = Application::getInstance();
                // Keep the saved server URL (direct address); remember the
                // remote route instead.
                a.getSettings().remoteId = remoteId;
                a.getSettings().lastConnectionRemote = true;
                a.setAuthToken(token);
                a.setUsername(username);
                a.getSettings().remoteAccessEnabled = true;
                a.saveSettings();
                a.connectSendspin();
                brls::Application::notify("Connected remotely");
            } else {
                std::string err = WebRTCClient::instance().getLastError();
                brls::Application::notify("Remote connection failed: " +
                                          (err.empty() ? "check the Remote ID and that the server is online" : err));
            }
        });
    }).detach();
}

void SettingsTab::createDebugSection() {
    // Section header
    auto* header = new brls::Header();
    header->setTitle("Debug");
    m_contentBox->addView(header);

    // Network test button
    auto* networkTestCell = new brls::DetailCell();
    networkTestCell->setText("Network Test");
    networkTestCell->setDetailText("View network info and test server connection");
    networkTestCell->registerClickAction([this](brls::View* view) {
        onNetworkTest();
        return true;
    });
    m_contentBox->addView(networkTestCell);

    // Info label
    auto* infoLabel = new brls::Label();
    infoLabel->setText("Place test.mp3 or test.mp4 in ux0:data/VitaMA/");
    infoLabel->setFontSize(14);
    infoLabel->setMarginLeft(16);
    infoLabel->setMarginTop(8);
    infoLabel->setMarginBottom(16);
    m_contentBox->addView(infoLabel);
}

void SettingsTab::createAboutSection() {
    // Section header
    auto* header = new brls::Header();
    header->setTitle("About");
    m_contentBox->addView(header);

    // Version info
    auto* versionCell = new brls::DetailCell();
    versionCell->setText("Version");
    versionCell->setDetailText("Beta 0.2.4");
    m_contentBox->addView(versionCell);

    // App description
    auto* descLabel = new brls::Label();
    descLabel->setText("Vita Music Assistant - Music Client for PlayStation Vita");
    descLabel->setFontSize(16);
    descLabel->setMarginLeft(16);
    descLabel->setMarginTop(8);
    m_contentBox->addView(descLabel);

    // Credit
    auto* creditLabel = new brls::Label();
    creditLabel->setText("UI powered by Borealis");
    creditLabel->setFontSize(14);
    creditLabel->setMarginLeft(16);
    creditLabel->setMarginTop(4);
    creditLabel->setMarginBottom(20);
    m_contentBox->addView(creditLabel);
}

void SettingsTab::onLogout() {
    brls::Dialog* dialog = new brls::Dialog("Are you sure you want to logout?");

    dialog->addButton("Cancel", []() {});

    dialog->addButton("Logout", [this]() {
        // Clear credentials
        MAClient::instance().disconnect();
        Application::getInstance().setAuthToken("");
        Application::getInstance().setServerUrl("");
        Application::getInstance().setUsername("");
        Application::getInstance().saveSettings();

        // Go back to login
        Application::getInstance().pushLoginActivity();
    });

    dialog->open();
}

void SettingsTab::onThemeChanged(int index) {
    Application& app = Application::getInstance();
    AppSettings& settings = app.getSettings();

    settings.theme = static_cast<AppTheme>(index);
    app.applyTheme();
    app.saveSettings();
}

void SettingsTab::onQualityChanged(int index) {
    Application& app = Application::getInstance();
    AppSettings& settings = app.getSettings();

    settings.audioQuality = static_cast<AudioQuality>(index);
    app.saveSettings();
}

void SettingsTab::onConnectionTimeoutChanged(int index) {
    Application& app = Application::getInstance();
    AppSettings& settings = app.getSettings();

    switch (index) {
        case 0: settings.connectionTimeout = 30; break;
        case 1: settings.connectionTimeout = 60; break;
        case 2: settings.connectionTimeout = 120; break;
        case 3: settings.connectionTimeout = 180; break;
        case 4: settings.connectionTimeout = 300; break;
    }

    app.saveSettings();
}

void SettingsTab::onSeekIntervalChanged(int index) {
    Application& app = Application::getInstance();
    AppSettings& settings = app.getSettings();

    switch (index) {
        case 0: settings.seekInterval = 5; break;
        case 1: settings.seekInterval = 10; break;
        case 2: settings.seekInterval = 15; break;
        case 3: settings.seekInterval = 30; break;
        case 4: settings.seekInterval = 60; break;
    }

    app.saveSettings();
}

void SettingsTab::onControlsAutoHideChanged(int index) {
    Application& app = Application::getInstance();
    AppSettings& settings = app.getSettings();

    switch (index) {
        case 0: settings.controlsAutoHideSeconds = 0; break;
        case 1: settings.controlsAutoHideSeconds = 3; break;
        case 2: settings.controlsAutoHideSeconds = 5; break;
        case 3: settings.controlsAutoHideSeconds = 10; break;
        case 4: settings.controlsAutoHideSeconds = 15; break;
    }

    app.saveSettings();
}

void SettingsTab::onManageSidebarOrder() {
    Application& app = Application::getInstance();
    AppSettings& settings = app.getSettings();

    // Default sidebar items (Settings is always last and not movable)
    std::vector<std::pair<std::string, std::string>> defaultItems = {
        {"home", "Home"},
        {"library", "Library"},
        {"music", "Music"},
        {"search", "Search"}
    };

    // Parse current order or use default
    std::vector<std::pair<std::string, std::string>> currentOrder;
    if (!settings.sidebarOrder.empty()) {
        std::string order = settings.sidebarOrder;
        size_t pos = 0;
        while ((pos = order.find(',')) != std::string::npos) {
            std::string key = order.substr(0, pos);
            for (const auto& item : defaultItems) {
                if (item.first == key) {
                    currentOrder.push_back(item);
                    break;
                }
            }
            order.erase(0, pos + 1);
        }
        if (!order.empty()) {
            for (const auto& item : defaultItems) {
                if (item.first == order) {
                    currentOrder.push_back(item);
                    break;
                }
            }
        }
        // Add any missing items at the end
        for (const auto& item : defaultItems) {
            bool found = false;
            for (const auto& cur : currentOrder) {
                if (cur.first == item.first) {
                    found = true;
                    break;
                }
            }
            if (!found) currentOrder.push_back(item);
        }
    } else {
        currentOrder = defaultItems;
    }

    // Create dialog content
    brls::Box* outerBox = new brls::Box();
    outerBox->setAxis(brls::Axis::COLUMN);
    outerBox->setWidth(450);
    outerBox->setHeight(380);

    auto* title = new brls::Label();
    title->setText("Reorder sidebar items:");
    title->setFontSize(20);
    title->setMarginBottom(15);
    title->setMarginLeft(20);
    title->setMarginTop(20);
    outerBox->addView(title);

    // Use shared state
    auto orderCopy = std::make_shared<std::vector<std::pair<std::string, std::string>>>(currentOrder);
    auto labels = std::make_shared<std::vector<brls::Label*>>();

    // Scrolling frame for items
    brls::ScrollingFrame* scrollFrame = new brls::ScrollingFrame();
    scrollFrame->setGrow(1.0f);

    brls::Box* content = new brls::Box();
    content->setAxis(brls::Axis::COLUMN);
    content->setPaddingLeft(20);
    content->setPaddingRight(20);

    // Helper to update all labels
    auto updateLabels = [orderCopy, labels]() {
        for (size_t i = 0; i < labels->size() && i < orderCopy->size(); i++) {
            (*labels)[i]->setText(std::to_string(i + 1) + ". " + (*orderCopy)[i].second);
        }
    };

    // Create rows for each item
    for (size_t i = 0; i < orderCopy->size(); i++) {
        auto* row = new brls::Box();
        row->setAxis(brls::Axis::ROW);
        row->setJustifyContent(brls::JustifyContent::SPACE_BETWEEN);
        row->setAlignItems(brls::AlignItems::CENTER);
        row->setHeight(50);
        row->setMarginBottom(8);

        auto* label = new brls::Label();
        label->setText(std::to_string(i + 1) + ". " + (*orderCopy)[i].second);
        label->setFontSize(18);
        label->setGrow(1.0f);
        row->addView(label);
        labels->push_back(label);

        auto* btnBox = new brls::Box();
        btnBox->setAxis(brls::Axis::ROW);

        // Up button (move item up)
        auto* upBtn = new brls::Button();
        upBtn->setText(i > 0 ? "^" : " ");
        upBtn->setWidth(45);
        upBtn->setHeight(35);
        upBtn->setMarginRight(8);
        if (i > 0) {
            size_t idx = i;
            upBtn->registerClickAction([orderCopy, labels, idx, updateLabels](brls::View* view) {
                if (idx > 0 && idx < orderCopy->size()) {
                    std::swap((*orderCopy)[idx], (*orderCopy)[idx - 1]);
                    updateLabels();
                }
                return true;
            });
        }
        btnBox->addView(upBtn);

        // Down button (move item down)
        auto* downBtn = new brls::Button();
        downBtn->setText(i < orderCopy->size() - 1 ? "v" : " ");
        downBtn->setWidth(45);
        downBtn->setHeight(35);
        if (i < orderCopy->size() - 1) {
            size_t idx = i;
            downBtn->registerClickAction([orderCopy, labels, idx, updateLabels](brls::View* view) {
                if (idx < orderCopy->size() - 1) {
                    std::swap((*orderCopy)[idx], (*orderCopy)[idx + 1]);
                    updateLabels();
                }
                return true;
            });
        }
        btnBox->addView(downBtn);

        row->addView(btnBox);
        content->addView(row);
    }

    scrollFrame->setContentView(content);
    outerBox->addView(scrollFrame);

    // Note about Settings
    auto* noteLabel = new brls::Label();
    noteLabel->setText("Settings always appears last. Restart required.");
    noteLabel->setFontSize(14);
    noteLabel->setMarginLeft(20);
    noteLabel->setMarginBottom(10);
    outerBox->addView(noteLabel);

    brls::Dialog* dialog = new brls::Dialog(outerBox);

    dialog->addButton("Cancel", []() {});

    dialog->addButton("Reset", [orderCopy, defaultItems, this]() {
        Application& app = Application::getInstance();
        AppSettings& settings = app.getSettings();
        settings.sidebarOrder = "";
        app.saveSettings();
        if (m_sidebarOrderCell) {
            m_sidebarOrderCell->setDetailText("Default");
        }
    });

    dialog->addButton("Save", [orderCopy, this]() {
        Application& app = Application::getInstance();
        AppSettings& settings = app.getSettings();

        std::string newOrder;
        for (const auto& item : *orderCopy) {
            if (!newOrder.empty()) newOrder += ",";
            newOrder += item.first;
        }

        settings.sidebarOrder = newOrder;
        app.saveSettings();

        if (m_sidebarOrderCell) {
            m_sidebarOrderCell->setDetailText("Custom");
        }
    });

    dialog->open();
}

void SettingsTab::onNetworkTest() {
    // Show a toast while tests run
    brls::Application::notify("Running network test...");

    // Run the network tests on a detached thread to avoid blocking the UI
    std::thread([this]() {
        // ── 1. WiFi Check ──
        std::string ipAddress = "-";
        std::string dnsInfo = "-";
        std::string signalStr = "-";
        std::string ssid = "-";
        bool wifiConnected = false;

#ifdef __vita__
        SceNetCtlInfo info;

        int ret = sceNetCtlInetGetInfo(SCE_NETCTL_INFO_GET_IP_ADDRESS, &info);
        if (ret >= 0) {
            ipAddress = std::string(info.ip_address);
            wifiConnected = true;
        }

        ret = sceNetCtlInetGetInfo(SCE_NETCTL_INFO_GET_SSID, &info);
        if (ret >= 0) {
            ssid = std::string(info.ssid);
        }

        ret = sceNetCtlInetGetInfo(SCE_NETCTL_INFO_GET_RSSI_PERCENTAGE, &info);
        if (ret >= 0) {
            signalStr = std::to_string(info.rssi_percentage) + "%";
        }

        ret = sceNetCtlInetGetInfo(SCE_NETCTL_INFO_GET_PRIMARY_DNS, &info);
        if (ret >= 0) {
            dnsInfo = std::string(info.primary_dns);
            ret = sceNetCtlInetGetInfo(SCE_NETCTL_INFO_GET_SECONDARY_DNS, &info);
            if (ret >= 0) {
                dnsInfo += " / " + std::string(info.secondary_dns);
            }
        }
#else
        ipAddress = "127.0.0.1";
        dnsInfo = "8.8.8.8";
        signalStr = "100%";
        ssid = "Desktop";
        wifiConnected = true;
#endif

        // ── 2. Internet Check (latency) ──
        std::string internetStatus = "Skipped (no WiFi)";
        if (wifiConnected) {
            HttpClient netClient;
            netClient.setTimeout(10);

            auto start = std::chrono::steady_clock::now();
            std::string response;
            bool ok = netClient.get("http://connectivitycheck.gstatic.com/generate_204", response);
            auto end = std::chrono::steady_clock::now();
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

            if (ok) {
                internetStatus = "Reachable (" + std::to_string(ms) + "ms)";
            } else {
                internetStatus = "Unreachable (" + std::to_string(ms) + "ms)";
            }
        }

        // ── 3. MA Server Check (latency) ──
        Application& app = Application::getInstance();
        std::string serverUrl = app.getServerUrl();
        std::string maStatus;
        std::string maLatency = "-";

        if (serverUrl.empty()) {
            maStatus = "Not configured";
        } else if (!wifiConnected) {
            maStatus = "Skipped (no WiFi)";
        } else {
            HttpClient maClient;
            maClient.setTimeout(10);
            maClient.setDefaultHeader("Authorization", "Bearer " + app.getAuthToken());
            maClient.setDefaultHeader("Accept", "application/json");

            auto start = std::chrono::steady_clock::now();
            std::string response;
            bool ok = maClient.get(serverUrl + "/info", response);
            auto end = std::chrono::steady_clock::now();
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

            maLatency = std::to_string(ms) + "ms";
            if (ok) {
                maStatus = "Connected (" + std::to_string(ms) + "ms)";
            } else {
                maStatus = "Failed (" + std::to_string(ms) + "ms)";
            }
        }

        // ── Build dialog on main thread ──
        // Capture results by value for the lambda
        brls::sync([=]() {
            brls::Box* content = new brls::Box();
            content->setAxis(brls::Axis::COLUMN);
            content->setWidth(700);
            content->setHeight(420);
            content->setPadding(25);

            auto* titleLabel = new brls::Label();
            titleLabel->setText("Network Test Results");
            titleLabel->setFontSize(22);
            titleLabel->setMarginBottom(15);
            content->addView(titleLabel);

            // Helper to create info rows (item #11 style)
            auto addRow = [&content](const std::string& label, const std::string& value) {
                auto* row = new brls::Box();
                row->setAxis(brls::Axis::ROW);
                row->setMarginBottom(8);
                auto* lblA = new brls::Label();
                lblA->setText(label);
                lblA->setFontSize(16);
                lblA->setWidth(220);
                row->addView(lblA);
                auto* lblB = new brls::Label();
                lblB->setText(value);
                lblB->setFontSize(16);
                row->addView(lblB);
                content->addView(row);
            };

            // Helper for section headers
            auto addHeader = [&content](const std::string& text) {
                auto* lbl = new brls::Label();
                lbl->setText(text);
                lbl->setFontSize(16);
                lbl->setMarginBottom(6);
                lbl->setMarginTop(4);
                content->addView(lbl);
            };

            // WiFi section
            addHeader("-- WiFi --");
            addRow("Status:", wifiConnected ? "Connected" : "Not Connected");
            addRow("Network:", ssid);
            addRow("IP Address:", ipAddress);
            addRow("DNS:", dnsInfo);
            addRow("Signal:", signalStr);

            // Internet section
            addHeader("-- Internet --");
            addRow("Connectivity:", internetStatus);

            // MA server section
            addHeader("-- Music Assistant --");
            addRow("Server:", serverUrl.empty() ? "Not configured" : serverUrl);
            addRow("Connection:", maStatus);

            auto* dialog = new brls::Dialog(content);
            dialog->addButton("Close", []() {});
            dialog->open();
        });
    }).detach();
}

} // namespace vita_ma
