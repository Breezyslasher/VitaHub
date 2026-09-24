/**
 * Vita Music Assistant - Main Activity
 * Main navigation with tabs for Home, Library, Search, Settings
 */

#pragma once

#include <borealis.hpp>
#include <memory>

namespace vita_ma {

class MainActivity : public brls::Activity {
public:
    MainActivity();
    ~MainActivity() override;

    brls::View* createContentView() override;

    void onContentAvailable() override;

private:
    void loadLibrariesToSidebar();
    // Build the sidebar tabs. The Podcasts/Audiobooks/Radio categories are only
    // added when the library actually has items of that type.
    void buildSidebar(bool hasPodcasts, bool hasAudiobooks, bool hasRadios);

    BRLS_BIND(brls::TabFrame, tabFrame, "main/tab_frame");

    // Guards the async library-count probes against a torn-down activity.
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

} // namespace vita_ma
