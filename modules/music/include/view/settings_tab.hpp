/**
 * Vita Music Assistant - Settings Tab
 * Application settings and user info
 */

#pragma once

#include <borealis.hpp>
#include "app/ma_types.hpp"

namespace vita_ma {

class SettingsTab : public brls::Box {
public:
    SettingsTab();

private:
    void createAccountSection();
    void createUISection();
    void createLayoutSection();
    void createContentDisplaySection();
    void createPlaybackSection();
    void createAudioSection();
    void createRemoteAccessSection();
    void createPlayerSection();
    void createAboutSection();
    void createDebugSection();

    void loadPlayerList();
    void onPlayerSelected(int index);

    void onLogout();
    void onNetworkTest();
    void onThemeChanged(int index);
    void onQualityChanged(int index);
    void onSeekIntervalChanged(int index);
    void onControlsAutoHideChanged(int index);
    void onConnectionTimeoutChanged(int index);
    void onManageSidebarOrder();
    void onRemoteConnect();

    brls::ScrollingFrame* m_scrollView = nullptr;
    brls::Box* m_contentBox = nullptr;

    // Account section
    brls::Label* m_userLabel = nullptr;
    brls::Label* m_serverLabel = nullptr;

    // UI section
    brls::SelectorCell* m_themeSelector = nullptr;
    brls::BooleanCell* m_debugLogToggle = nullptr;
    brls::BooleanCell* m_showDebugTabToggle = nullptr;

    // Layout section
    brls::BooleanCell* m_collapseSidebarToggle = nullptr;
    brls::DetailCell* m_sidebarOrderCell = nullptr;

    // Content display section
    brls::BooleanCell* m_playlistsToggle = nullptr;
    brls::BooleanCell* m_hideTitlesToggle = nullptr;

    // Playback section
    brls::BooleanCell* m_autoPlayToggle = nullptr;
    brls::SelectorCell* m_seekIntervalSelector = nullptr;
    brls::SelectorCell* m_controlsAutoHideSelector = nullptr;

    // Audio section
    brls::SelectorCell* m_qualitySelector = nullptr;
    brls::SelectorCell* m_connectionTimeoutSelector = nullptr;

    // Music section
    brls::SelectorCell* m_trackActionSelector = nullptr;
    brls::BooleanCell* m_backgroundMusicToggle = nullptr;

    // Player section
    brls::BooleanCell* m_localPlaybackToggle = nullptr;
    brls::DetailCell* m_playerNameCell = nullptr;
    brls::SelectorCell* m_playerSelector = nullptr;
    std::vector<PlayerInfo> m_players;  // Cached player list from MA

    // Remote access section
    brls::DetailCell* m_remoteIdCell = nullptr;
    brls::DetailCell* m_remoteConnectCell = nullptr;
};

} // namespace vita_ma
