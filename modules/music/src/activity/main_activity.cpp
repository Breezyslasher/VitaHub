/**
 * Vita Music Assistant - Main Activity implementation
 */

#include "activity/main_activity.hpp"

#ifdef VITAHUB
#include "vitahub/bridge.hpp"
#endif
#include "view/home_tab.hpp"
#include "view/library_tab.hpp"
#include "view/search_tab.hpp"
#include "view/settings_tab.hpp"
#include "view/debug_tab.hpp"
#include "app/application.hpp"
#include "app/ma_client.hpp"
#include "app/ma_types.hpp"
#include "app/music_queue.hpp"
#include "activity/player_activity.hpp"
#include "utils/async.hpp"

#include <algorithm>
#include <atomic>
#include <memory>

namespace vita_ma {

// Helper to calculate text width (approximate based on character count)
static int calculateTextWidth(const std::string& text) {
    const int charWidth = 12;
    const int padding = 50;
    return static_cast<int>(text.length()) * charWidth + padding;
}

MainActivity::MainActivity() {
    brls::Logger::debug("MainActivity created");
}

brls::View* MainActivity::createContentView() {
    return brls::View::createFromXMLResource("music/activity/main.xml");
}

void MainActivity::onContentAvailable() {
    brls::Logger::debug("MainActivity content available");

    if (!tabFrame) return;

    AppSettings& settings = Application::getInstance().getSettings();

    // Dynamic sidebar width (independent of which tabs get added).
    int sidebarWidth = 200;
    std::vector<std::string> standardTabs = {"Home", "Library", "Music", "Search", "Settings"};
    for (const auto& tab : standardTabs) {
        sidebarWidth = std::max(sidebarWidth, calculateTextWidth(tab));
    }
    sidebarWidth = std::min(sidebarWidth, 350);
    brls::View* sidebar = tabFrame->getView("brls/tab_frame/sidebar");
    if (sidebar) {
        sidebar->setWidth(settings.collapseSidebar ? 160 : sidebarWidth);
    }

    // Probe whether the library holds any Podcasts / Audiobooks / Radio items,
    // then build the sidebar so empty categories are omitted. Each probe fetches
    // a single item; an item_id in the raw response means the category is
    // non-empty. Building after all three land keeps the category order intact
    // (TabFrame only appends).
    struct ProbeState {
        std::atomic<int> pending{3};
        std::atomic<bool> podcasts{false};
        std::atomic<bool> audiobooks{false};
        std::atomic<bool> radios{false};
        std::atomic<bool> built{false};
    };
    auto state = std::make_shared<ProbeState>();
    auto aliveWeak = std::weak_ptr<bool>(m_alive);

    auto finish = [this, state, aliveWeak]() {
        if (state->pending.fetch_sub(1) != 1) return;  // wait for all three probes
        brls::sync([this, state, aliveWeak]() {
            auto alive = aliveWeak.lock();
            if (!alive || !*alive) return;
            if (state->built.exchange(true)) return;
            buildSidebar(state->podcasts.load(), state->audiobooks.load(),
                         state->radios.load());
        });
    };

    auto probe = [finish](const std::string& media, std::atomic<bool>* flag) {
        MAClient::instance().getLibraryItemsRaw(media,
            [finish, flag](bool ok, const std::string& raw) {
                if (ok && raw.find("\"item_id\"") != std::string::npos) flag->store(true);
                finish();
            }, "", 1, 0);
    };
    probe("podcasts", &state->podcasts);
    probe("audiobooks", &state->audiobooks);
    probe("radios", &state->radios);
}

void MainActivity::buildSidebar(bool hasPodcasts, bool hasAudiobooks, bool hasRadios) {
    if (!tabFrame) return;
    AppSettings& settings = Application::getInstance().getSettings();

    // Add tabs based on the sidebar order setting.
    std::vector<std::string> order;
    std::string orderStr = settings.sidebarOrder;
    if (!orderStr.empty()) {
        size_t pos = 0;
        while ((pos = orderStr.find(',')) != std::string::npos) {
            order.push_back(orderStr.substr(0, pos));
            orderStr.erase(0, pos + 1);
        }
        if (!orderStr.empty()) order.push_back(orderStr);
    } else {
        order = {"home", "library", "search"};  // default order for a music-only app
    }

    for (const std::string& item : order) {
        if (item == "home") {
            tabFrame->addTab("Home", []() { return new HomeTab(); });
        } else if (item == "library") {
            // One sidebar entry per library category. The Podcasts/Audiobooks/
            // Radio categories are only shown when the library has such items.
            tabFrame->addTab("Artists",   []() { return new LibraryTab(MusicCategory::ARTISTS); });
            tabFrame->addTab("Albums",    []() { return new LibraryTab(MusicCategory::ALBUMS); });
            tabFrame->addTab("Tracks",    []() { return new LibraryTab(MusicCategory::TRACKS); });
            tabFrame->addTab("Playlists", []() { return new LibraryTab(MusicCategory::PLAYLISTS); });
            if (hasPodcasts)
                tabFrame->addTab("Podcasts",   []() { return new LibraryTab(MusicCategory::PODCASTS); });
            if (hasAudiobooks)
                tabFrame->addTab("Audiobooks", []() { return new LibraryTab(MusicCategory::AUDIOBOOKS); });
            if (hasRadios)
                tabFrame->addTab("Radio",      []() { return new LibraryTab(MusicCategory::RADIOS); });
        } else if (item == "search") {
            tabFrame->addTab("Search", []() { return new SearchTab(); });
        }
    }

    // Debug and Settings always at the bottom.
    tabFrame->addSeparator();
    if (settings.showDebugTab) {
        tabFrame->addTab("Debug", []() { return new DebugTab(); });
    }
    tabFrame->addTab("Settings", []() { return new SettingsTab(); });
#ifdef VITAHUB
    tabFrame->addTab("VitaHub", []() { return vitahub::createServicesTab("music"); });
#endif

    tabFrame->focusTab(0);

    // BUTTON_B on the root content view opens/returns to the player.
    brls::View* rootBox = tabFrame->getParent();
    if (rootBox) {
        rootBox->registerAction("", brls::ControllerButton::BUTTON_B, [](brls::View* view) {
            auto* playerActivity = PlayerActivity::createResumeQueue();
            brls::Application::pushActivity(playerActivity);
            return true;
        });
    }
}

MainActivity::~MainActivity() {
    if (m_alive) *m_alive = false;
}

} // namespace vita_ma
