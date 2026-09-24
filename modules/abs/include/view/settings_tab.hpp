/**
 * VitaABS - Settings Tab
 * Application settings and user info — master/detail layout.
 *
 * The tab is split into two panes: a vertical "rail" on the left that
 * lists the section categories, and a "detail" pane on the right that
 * holds the cells of the currently-selected section. Every existing
 * cell, change handler, and persistence path is preserved; only the
 * parent layout changes. Per-section builder methods now return their
 * own brls::Box instead of appending into one big m_contentBox, and
 * the detail pane swaps which one is visible based on the rail
 * selection.
 */

#pragma once

#include <borealis.hpp>
#include <string>
#include <vector>

namespace vitaabs {

class SettingsTab : public brls::Box {
public:
    SettingsTab();
    // Section boxes that aren't currently attached to m_detailContent
    // have no parent, so brls::Box::~Box's "delete every child" sweep
    // won't reach them. The destructor walks m_sectionBoxes and frees
    // the detached ones explicitly.
    ~SettingsTab() override;

private:
    // Section IDs. Order matches kSections in the cpp and the order in
    // which the rail rows are added. Keep them dense and contiguous —
    // showSection() indexes m_sectionBoxes / m_railRows by these.
    enum SectionId : int {
        SEC_ACCOUNT = 0,
        SEC_INTERFACE,
        SEC_CONTENT,
        SEC_PLAYBACK,
        SEC_AUDIO,
        SEC_DOWNLOADS,
        SEC_ABOUT,
        SEC_COUNT
    };

    // Per-section builders. Each one returns a freshly-built brls::Box
    // (COLUMN of cells) instead of appending into a shared container —
    // the constructor stitches all of them into m_sectionBoxes and
    // attaches one at a time.
    brls::Box* createAccountSection();
    brls::Box* createUISection();
    brls::Box* createContentDisplaySection();
    brls::Box* createPlaybackSection();
    brls::Box* createAudioSection();
    brls::Box* createDownloadsSection();
    brls::Box* createAboutSection();

    // Master/detail plumbing — see settings_tab.cpp for the layout.
    brls::Box* makeSectionBox();
    brls::Box* makeRailRow(const std::string& iconPath,
                           const std::string& title,
                           int sectionId);
    // Static, non-focusable row used for the rail footer — shows the
    // version next to an icon and is skipped by focus navigation so
    // the user can't accidentally land on it.
    brls::Box* makeRailInfoRow(const std::string& iconPath,
                               const std::string& title);
    void showSection(int sectionId);
    // Refresh the left-bar / background / text colour on every rail row
    // so the visually-selected one matches m_activeSection. Called by
    // showSection and by any handler that programmatically changes it.
    void paintRailRowSelection();

    // The page paints with borealis' active theme so it matches the rest
    // of the app in both Light and Dark. Colours are read once here and
    // re-applied from draw() whenever the variant changes, so switching
    // the theme from this very page repaints it immediately.
    void draw(NVGcontext* vg, float x, float y, float width, float height,
              brls::Style style, brls::FrameContext* ctx) override;
    void applyThemeColors();

    void onLogout();
    void onThemeChanged(int index);
    void onSeekIntervalChanged(int index);

    // Layout panes
    brls::Box*            m_railContainer   = nullptr;  // left column wrapper
    brls::ScrollingFrame* m_railScroll      = nullptr;
    brls::Box*            m_railBox         = nullptr;  // holds the rail rows
    brls::Box*            m_detailContainer = nullptr;
    brls::Box*            m_detailHeader    = nullptr;
    brls::Label*          m_detailTitle     = nullptr;
    brls::Label*          m_detailSubtitle  = nullptr;
    brls::ScrollingFrame* m_detailScroll    = nullptr;
    brls::Box*            m_detailContent   = nullptr;  // holds the active section's Box

    // Theme-coloured chrome, re-tinted by applyThemeColors().
    brls::Label* m_railTitle      = nullptr;
    brls::Label* m_railSubtitle   = nullptr;
    brls::Box*   m_railHairline   = nullptr;
    brls::Box*   m_detailHairline = nullptr;
    brls::Label* m_versionLabel   = nullptr;
    brls::ThemeVariant m_themeVariant = brls::ThemeVariant::DARK;

    std::vector<brls::Box*> m_railRows;      // one per section, indexed by SectionId
    std::vector<brls::Box*> m_sectionBoxes;  // one per section, indexed by SectionId
    // Which section box is currently parented to m_detailContent. brls
    // Box::removeView with free=false doesn't reset the view's parent
    // pointer, so getParent() can't be used to tell whether we already
    // attached a particular section. Track it explicitly here.
    brls::Box* m_attachedSection = nullptr;
    int        m_activeSection   = SEC_ACCOUNT;

    // Account section
    brls::Label* m_userLabel   = nullptr;
    brls::Label* m_serverLabel = nullptr;

    // Interface section
    brls::SelectorCell* m_themeSelector  = nullptr;
    brls::BooleanCell*  m_debugLogToggle = nullptr;

    // Content display section
    brls::BooleanCell* m_collectionsToggle = nullptr;

    // Playback section
    brls::BooleanCell*  m_resumeToggle         = nullptr;
    brls::SelectorCell* m_seekIntervalSelector = nullptr;

    // Downloads section
    brls::BooleanCell* m_autoStartDownloadsToggle = nullptr;
    brls::BooleanCell* m_deleteAfterWatchToggle   = nullptr;
    brls::DetailCell*  m_clearDownloadsCell       = nullptr;
};

} // namespace vitaabs
