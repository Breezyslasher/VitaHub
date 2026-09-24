/**
 * VitaABS - Settings Tab implementation
 *
 * Master/detail layout (rail on the left, section pane on the right),
 * adapted from VitaPlex's settings redesign. Every cell, change handler
 * and persistence path from the previous single-column version is kept
 * verbatim — only the parent layout changed.
 */

#include "view/settings_tab.hpp"
#include "app/application.hpp"
#include "app/audiobookshelf_client.hpp"
#include "app/downloads_manager.hpp"
#include "utils/app_update.hpp"

// Version macros come from CMakeLists.txt (VITAABS_VERSION numeric,
// VITAABS_DISPLAY_VERSION human-readable); guard for odd build setups.
#ifndef VITAABS_DISPLAY_VERSION
#define VITAABS_DISPLAY_VERSION "0.0.0"
#endif

namespace vitaabs {

// ─── design tokens ────────────────────────────────────────────────────
// Everything except the brand accent comes from borealis' active theme,
// so this page matches the rest of the app in both Light and Dark. The
// page background is deliberately NOT painted: leaving it unset lets the
// frame's own "brls/background" show through, exactly like every other
// tab. The rail uses ViewBackground::SIDEBAR — the same background the
// app's real sidebar paints, resolved live at draw time.
namespace tok {
    static inline NVGcolor text()     { return brls::Application::getTheme()["brls/text"]; }
    static inline NVGcolor muted()    { return brls::Application::getTheme()["brls/text_disabled"]; }
    static inline NVGcolor hairline() { return brls::Application::getTheme()["brls/sidebar/separator"]; }
    // Audiobookshelf bronze — a brand colour, so it stays fixed across
    // themes (as the in-app updater's accent does).
    static inline NVGcolor accent()   { return nvgRGB(0xcd, 0x9d, 0x49); }
    // Selected rail row: a wash of the accent rather than a fixed grey,
    // so it reads correctly on both the light and dark sidebar.
    static inline NVGcolor raised()   { return nvgRGBA(0xcd, 0x9d, 0x49, 46); }
}

// Per-section metadata. The rail rows + detail header pull from this
// table — keep its order in sync with the SectionId enum in the header.
struct SectionMeta {
    const char* name;      // rail label + detail title
    const char* icon;      // file under BRLS_RESOURCES "/icons/"
    const char* subtitle;  // one-liner shown under the detail title
};

static const SectionMeta kSections[] = {
    /* SEC_ACCOUNT   */ { "Account",         "account.png",
                          "Server URLs, connection, and sign-out." },
    /* SEC_INTERFACE */ { "Interface",       "theme-light-dark.png",
                          "Theme and diagnostic logging." },
    /* SEC_CONTENT   */ { "Content Display", "show.png",
                          "What appears in libraries and on the Home tab." },
    /* SEC_PLAYBACK  */ { "Playback",        "play.png",
                          "Resume, seek, speed, and auto-complete." },
    /* SEC_AUDIO     */ { "Audio",           "music.png",
                          "Volume boost and chapter list." },
    /* SEC_DOWNLOADS */ { "Downloads",       "download.png",
                          "Storage, cleanup, and offline behaviour." },
    /* SEC_ABOUT     */ { "About",           "information.png",
                          "Updates and app information." },
};

// ─── responsive sizing ────────────────────────────────────────────────
// Derive the rail width from the viewport so the tab still fits on a
// Vita (960×544 logical) without devouring the detail pane.
static int railWidthForViewport() {
    float vw = brls::Application::contentWidth;
    if (vw >= 1280) return 280;
    if (vw >= 1024) return 240;
    if (vw >= 800)  return 220;
    if (vw >= 560)  return 180;
    return 160;   // really narrow — phone portrait; UI still functional
}

// ============================================================================
// Constructor & master/detail plumbing
// ============================================================================

SettingsTab::SettingsTab() {
    static_assert(sizeof(kSections) / sizeof(kSections[0]) == SEC_COUNT,
                  "kSections / SectionId out of sync");

    this->setAxis(brls::Axis::ROW);
    this->setJustifyContent(brls::JustifyContent::FLEX_START);
    this->setAlignItems(brls::AlignItems::STRETCH);
    this->setGrow(1.0f);
    // No background here on purpose — the frame's "brls/background"
    // shows through, so the page sits on the same field as every other
    // tab instead of a colour of its own.

    // ─── Rail (left) ────────────────────────────────────────────────
    m_railContainer = new brls::Box();
    m_railContainer->setAxis(brls::Axis::COLUMN);
    m_railContainer->setAlignItems(brls::AlignItems::STRETCH);
    m_railContainer->setWidth(railWidthForViewport());
    // Same background the app's real sidebar uses, resolved from the
    // theme at draw time — so the rail reads as a continuation of it.
    m_railContainer->setBackground(brls::ViewBackground::SIDEBAR);

    // Rail header — "Settings" and the signed-in user.
    auto* railHeader = new brls::Box();
    railHeader->setAxis(brls::Axis::COLUMN);
    railHeader->setPaddingLeft(18);
    railHeader->setPaddingRight(14);
    railHeader->setPaddingTop(18);
    railHeader->setPaddingBottom(14);

    m_railTitle = new brls::Label();
    m_railTitle->setText("Settings");
    m_railTitle->setFontSize(22);
    railHeader->addView(m_railTitle);

    m_railSubtitle = new brls::Label();
    {
        const auto& app = Application::getInstance();
        m_railSubtitle->setText(app.getUsername().empty()
                                    ? std::string("Not signed in")
                                    : app.getUsername());
    }
    m_railSubtitle->setFontSize(13);
    m_railSubtitle->setMarginTop(2);
    railHeader->addView(m_railSubtitle);

    // Thin divider under the rail header.
    m_railHairline = new brls::Box();
    m_railHairline->setHeight(1);
    railHeader->addView(m_railHairline);

    m_railContainer->addView(railHeader);

    // Scrollable list of rail rows — every section fits on desktop,
    // overflow scrolls on a Vita-sized viewport.
    m_railScroll = new brls::ScrollingFrame();
    m_railScroll->setGrow(1.0f);
    // Not focusable: a focusable ScrollingFrame defaults to
    // ScrollingBehavior::NATURAL, whose per-frame focus fixup steals
    // focus from the row the user is on. Descend straight onto a row.
    m_railScroll->setFocusable(false);

    m_railBox = new brls::Box();
    m_railBox->setAxis(brls::Axis::COLUMN);
    m_railBox->setAlignItems(brls::AlignItems::STRETCH);
    m_railBox->setPaddingTop(6);
    m_railBox->setPaddingBottom(6);

    m_railScroll->setContentView(m_railBox);
    m_railContainer->addView(m_railScroll);

    this->addView(m_railContainer);

    // ─── Detail (right) ────────────────────────────────────────────
    m_detailContainer = new brls::Box();
    m_detailContainer->setAxis(brls::Axis::COLUMN);
    m_detailContainer->setAlignItems(brls::AlignItems::STRETCH);
    m_detailContainer->setGrow(1.0f);
    m_detailContainer->setPaddingLeft(24);
    m_detailContainer->setPaddingRight(24);
    m_detailContainer->setPaddingTop(20);
    m_detailContainer->setPaddingBottom(12);

    // Section header — title/subtitle column, updated in showSection().
    m_detailHeader = new brls::Box();
    m_detailHeader->setAxis(brls::Axis::ROW);
    m_detailHeader->setAlignItems(brls::AlignItems::CENTER);
    m_detailHeader->setMarginBottom(14);

    auto* headerTextCol = new brls::Box();
    headerTextCol->setAxis(brls::Axis::COLUMN);
    headerTextCol->setGrow(1.0f);

    m_detailTitle = new brls::Label();
    m_detailTitle->setFontSize(26);
    headerTextCol->addView(m_detailTitle);

    m_detailSubtitle = new brls::Label();
    m_detailSubtitle->setFontSize(13);
    m_detailSubtitle->setMarginTop(3);
    headerTextCol->addView(m_detailSubtitle);

    m_detailHeader->addView(headerTextCol);
    m_detailContainer->addView(m_detailHeader);

    // Hairline under the section header.
    m_detailHairline = new brls::Box();
    m_detailHairline->setHeight(1);
    m_detailHairline->setMarginBottom(10);
    m_detailContainer->addView(m_detailHairline);

    // Scrolling holder for the active section box.
    m_detailScroll = new brls::ScrollingFrame();
    m_detailScroll->setGrow(1.0f);
    m_detailScroll->setFocusable(false);   // see m_railScroll above

    m_detailContent = new brls::Box();
    m_detailContent->setAxis(brls::Axis::COLUMN);
    m_detailContent->setAlignItems(brls::AlignItems::STRETCH);

    m_detailScroll->setContentView(m_detailContent);
    m_detailContainer->addView(m_detailScroll);

    this->addView(m_detailContainer);

    // ─── Section boxes ─────────────────────────────────────────────
    // Build every section's Box up-front and stash it; showSection()
    // attaches one at a time. Order must match SectionId so
    // m_sectionBoxes[id] resolves correctly.
    m_sectionBoxes.resize(SEC_COUNT, nullptr);
    m_sectionBoxes[SEC_ACCOUNT]   = createAccountSection();
    m_sectionBoxes[SEC_INTERFACE] = createUISection();
    m_sectionBoxes[SEC_CONTENT]   = createContentDisplaySection();
    m_sectionBoxes[SEC_PLAYBACK]  = createPlaybackSection();
    m_sectionBoxes[SEC_AUDIO]     = createAudioSection();
    m_sectionBoxes[SEC_DOWNLOADS] = createDownloadsSection();
    m_sectionBoxes[SEC_ABOUT]     = createAboutSection();

    // Stage every section's box but do NOT add any of them to the
    // detail content yet — showSection() adds exactly one at a time.
    // The reason is borealis' Box::getDefaultFocus walks children and
    // their descendants for the first focusable view, *without*
    // checking Visibility::GONE. If we added every section box and only
    // toggled visibility, RIGHT from the rail would land on the first
    // focusable cell of section[0] (Account) regardless of which
    // section the user actually selected — focus on an invisible cell.
    //
    // Keeping the unused section boxes detached (no parent) makes them
    // invisible to the focus walker and to Yoga; addView re-parents
    // the one we want to show, removeView(_, /*free=*/false) detaches
    // the previous one without destroying it.
    for (brls::Box* sec : m_sectionBoxes) {
        if (!sec) continue;
        sec->setVisibility(brls::Visibility::VISIBLE);
    }

    // ─── Rail rows ─────────────────────────────────────────────────
    m_railRows.resize(SEC_COUNT, nullptr);
    for (int id = 0; id < SEC_COUNT; id++) {
        brls::Box* row = makeRailRow(kSections[id].icon, kSections[id].name, id);
        m_railRows[id] = row;
        m_railBox->addView(row);
    }

    // Rail footer — the single, always-visible version readout. This is
    // the app's ONE version display: the About section deliberately has
    // no version row and the update cell no longer repeats it, which is
    // what used to make the version look duplicated.
    m_railBox->addView(makeRailInfoRow("information.png",
                                       VITAABS_DISPLAY_VERSION));

    // Default landing — Account on first open.
    m_activeSection = SEC_ACCOUNT;
    showSection(m_activeSection);

    m_themeVariant = brls::Application::getThemeVariant();
    applyThemeColors();
}

// Tint every piece of chrome this page owns from the active theme.
// Labels bake their colour at construction (brls::Label reads
// "brls/text" in its constructor), so a theme switch has to re-apply
// them — draw() below does that when the variant changes.
void SettingsTab::applyThemeColors() {
    if (m_railTitle)      m_railTitle->setTextColor(tok::text());
    if (m_railSubtitle)   m_railSubtitle->setTextColor(tok::muted());
    if (m_detailTitle)    m_detailTitle->setTextColor(tok::text());
    if (m_detailSubtitle) m_detailSubtitle->setTextColor(tok::muted());
    if (m_versionLabel)   m_versionLabel->setTextColor(tok::muted());

    if (m_railHairline)   m_railHairline->setBackgroundColor(tok::hairline());
    if (m_detailHairline) m_detailHairline->setBackgroundColor(tok::hairline());

    for (brls::Box* row : m_railRows) {
        if (!row) continue;
        if (auto* label = dynamic_cast<brls::Label*>(row->getView("rail/label")))
            label->setTextColor(tok::text());
    }

    paintRailRowSelection();
}

void SettingsTab::draw(NVGcontext* vg, float x, float y, float width, float height,
                       brls::Style style, brls::FrameContext* ctx) {
    // Picking the theme up here (rather than only in the constructor)
    // means changing it from this page's own Interface section repaints
    // the page on the very next frame.
    brls::ThemeVariant variant = brls::Application::getThemeVariant();
    if (variant != m_themeVariant) {
        m_themeVariant = variant;
        applyThemeColors();
    }

    brls::Box::draw(vg, x, y, width, height, style, ctx);
}

SettingsTab::~SettingsTab() {
    // Each section box that's NOT the currently-attached one has no
    // parent; brls::Box::~Box only deletes children, so those orphans
    // would leak. Delete them here. The attached one is owned by
    // m_detailContent and will be freed by the base class destructor.
    for (brls::Box* sec : m_sectionBoxes) {
        if (sec && sec != m_attachedSection) {
            delete sec;
        }
    }
}

// Make a fresh column box with the spacing the detail pane expects.
// Returns a Box ready to hold cells; the caller owns it until the
// constructor passes it to m_detailContent.
brls::Box* SettingsTab::makeSectionBox() {
    auto* box = new brls::Box();
    box->setAxis(brls::Axis::COLUMN);
    box->setAlignItems(brls::AlignItems::STRETCH);
    box->setMarginBottom(20);
    return box;
}

// One rail row: icon + label, focusable, clickable, with a bronze left
// accent bar and raised background when selected. The bar is a 4px
// ABSOLUTE child so it can sit flush with the row's edge without
// disturbing the row's content layout.
brls::Box* SettingsTab::makeRailRow(const std::string& iconPath,
                                    const std::string& title,
                                    int sectionId) {
    auto* row = new brls::Box();
    row->setAxis(brls::Axis::ROW);
    row->setAlignItems(brls::AlignItems::CENTER);
    row->setHeight(46);
    row->setMarginLeft(8);
    row->setMarginRight(8);
    row->setMarginTop(2);
    row->setMarginBottom(2);
    row->setCornerRadius(10);
    row->setPaddingLeft(12);
    row->setPaddingRight(10);
    row->setFocusable(true);

    // Bronze left-edge bar (4px). Hidden until paintRailRowSelection()
    // toggles it on for the active row.
    auto* leftBar = new brls::Box();
    leftBar->setPositionType(brls::PositionType::ABSOLUTE);
    leftBar->setPositionLeft(0);
    leftBar->setPositionTop(8);
    leftBar->setWidth(4);
    leftBar->setHeight(30);
    leftBar->setCornerRadius(2);
    leftBar->setBackgroundColor(tok::accent());
    leftBar->setVisibility(brls::Visibility::INVISIBLE);
    leftBar->setId("rail/selected-bar");
    row->addView(leftBar);

    // Icon — borealis Image with FIT scaling so non-square assets keep
    // their aspect on the small chip.
    auto* icon = new brls::Image();
    icon->setWidth(20);
    icon->setHeight(20);
    icon->setScalingType(brls::ImageScalingType::FIT);
    icon->setMarginRight(12);
    icon->setImageFromRes("icons/" + iconPath);
    icon->setId("rail/icon");
    row->addView(icon);

    auto* label = new brls::Label();
    label->setText(title);
    label->setFontSize(15);
    label->setTextColor(tok::text());
    label->setGrow(1.0f);
    label->setId("rail/label");   // applyThemeColors() re-tints via this id
    row->addView(label);

    // Right chevron — `right.png` is small enough to read as a hint
    // without crowding the row.
    auto* chevron = new brls::Image();
    chevron->setWidth(14);
    chevron->setHeight(14);
    chevron->setScalingType(brls::ImageScalingType::FIT);
    chevron->setImageFromRes("icons/right.png");
    chevron->setId("rail/chevron");
    row->addView(chevron);

    row->registerClickAction([this, sectionId](brls::View*) {
        showSection(sectionId);
        return true;
    });
    row->addGestureRecognizer(new brls::TapGestureRecognizer(row));

    // Make focus also select (one-press navigation feels right on a TV).
    row->getFocusEvent()->subscribe([this, sectionId](brls::View*) {
        if (m_activeSection != sectionId) {
            showSection(sectionId);
        }
    });

    return row;
}

// Static rail footer entry — same icon + label scaffolding as a regular
// rail row but with focusable=false, no click handler, no chevron, and
// muted text. Used for the version readout at the bottom of the rail.
brls::Box* SettingsTab::makeRailInfoRow(const std::string& iconPath,
                                        const std::string& title) {
    auto* row = new brls::Box();
    row->setAxis(brls::Axis::ROW);
    row->setAlignItems(brls::AlignItems::CENTER);
    row->setHeight(40);
    row->setMarginLeft(8);
    row->setMarginRight(8);
    row->setMarginTop(6);
    row->setMarginBottom(4);
    row->setPaddingLeft(12);
    row->setPaddingRight(10);
    // Explicitly non-focusable — Box defaults to false but the
    // surrounding rows are focusable=true so spell it out here too
    // to keep the contrast obvious to readers.
    row->setFocusable(false);

    auto* icon = new brls::Image();
    icon->setWidth(18);
    icon->setHeight(18);
    icon->setScalingType(brls::ImageScalingType::FIT);
    icon->setMarginRight(10);
    icon->setImageFromRes("icons/" + iconPath);
    row->addView(icon);

    m_versionLabel = new brls::Label();
    m_versionLabel->setText(title);
    m_versionLabel->setFontSize(13);
    m_versionLabel->setTextColor(tok::muted());
    m_versionLabel->setGrow(1.0f);
    row->addView(m_versionLabel);

    return row;
}

void SettingsTab::showSection(int sectionId) {
    if (sectionId < 0 || sectionId >= SEC_COUNT) return;
    brls::Box* target = m_sectionBoxes[sectionId];
    if (!target) return;

    // Swap which section box owns the detail content holder. We rely on
    // m_attachedSection rather than getParent() because brls
    // removeView(_, /*free=*/false) doesn't clear the view's parent
    // pointer, so getParent() lies after a detach. Borealis' focus
    // walker only sees attached children, so detaching the previous
    // section's box stops RIGHT-from-rail from landing on its cells.
    if (m_attachedSection != target) {
        if (m_attachedSection) {
            m_detailContent->removeView(m_attachedSection, /*free=*/false);
        }
        m_detailContent->addView(target);
        m_attachedSection = target;

        // Re-point lastFocusedView along the ancestor chain so a later
        // RIGHT-from-rail walk doesn't tunnel through the stale
        // lastFocusedView (which still pointed into the detached
        // section's cells) and re-focus an invisible widget.
        m_detailContent->setLastFocusedView(target);
        if (m_detailScroll) m_detailScroll->setLastFocusedView(m_detailContent);
        if (m_detailContainer) m_detailContainer->setLastFocusedView(m_detailScroll);

        // The scroll frame keeps its offset across the content swap, so a
        // new section opened after scrolling deep into a long one started
        // mid-list (or past its end, if shorter). Every section switch
        // starts reading from the top.
        if (m_detailScroll) m_detailScroll->setContentOffsetY(0.0f, false);
    }

    // Header text.
    if (m_detailTitle)    m_detailTitle->setText(kSections[sectionId].name);
    if (m_detailSubtitle) m_detailSubtitle->setText(kSections[sectionId].subtitle);

    m_activeSection = sectionId;
    paintRailRowSelection();
}

void SettingsTab::paintRailRowSelection() {
    for (int i = 0; i < (int)m_railRows.size(); i++) {
        brls::Box* row = m_railRows[i];
        if (!row) continue;
        bool active = (i == m_activeSection);
        row->setBackgroundColor(active ? tok::raised() : nvgRGBA(0, 0, 0, 0));
        if (auto* bar = row->getView("rail/selected-bar")) {
            bar->setVisibility(active ? brls::Visibility::VISIBLE
                                      : brls::Visibility::INVISIBLE);
        }
    }
}

// ============================================================================
// Per-section builders. Each returns a fresh Box of cells — the wiring of
// each cell's change handler, persistence, and getter/setter is preserved
// from the previous single-column implementation verbatim.
// ============================================================================

brls::Box* SettingsTab::createAccountSection() {
    Application& app = Application::getInstance();
    AppSettings& settings = app.getSettings();
    brls::Box* box = makeSectionBox();

    // User info cell
    m_userLabel = new brls::Label();
    m_userLabel->setText("User: " + (app.getUsername().empty() ? "Not logged in" : app.getUsername()));
    m_userLabel->setFontSize(18);
    m_userLabel->setMarginLeft(16);
    m_userLabel->setMarginBottom(8);
    box->addView(m_userLabel);

    // Current server info
    m_serverLabel = new brls::Label();
    std::string serverInfo = "Server: ";
    if (app.getServerUrl().empty()) {
        serverInfo += "Not connected";
    } else {
        serverInfo += app.getServerUrl();
        serverInfo += app.isUsingLocalUrl() ? " (Local)" : " (Remote)";
    }
    m_serverLabel->setText(serverInfo);
    m_serverLabel->setFontSize(18);
    m_serverLabel->setMarginLeft(16);
    m_serverLabel->setMarginBottom(16);
    box->addView(m_serverLabel);

    // Local URL setting
    auto* localUrlCell = new brls::DetailCell();
    localUrlCell->setText("Local URL");
    localUrlCell->setDetailText(app.getLocalServerUrl().empty() ? "Not set" : app.getLocalServerUrl());
    localUrlCell->registerClickAction([localUrlCell](brls::View* view) {
        Application& app = Application::getInstance();
        brls::Application::getImeManager()->openForText([localUrlCell](std::string text) {
            Application::getInstance().setLocalServerUrl(text);
            Application::getInstance().saveSettings();
            localUrlCell->setDetailText(text.empty() ? "Not set" : text);
        }, "Enter Local Server URL", "http://192.168.1.100:13378", 256, app.getLocalServerUrl());
        return true;
    });
    box->addView(localUrlCell);

    // Remote URL setting
    auto* remoteUrlCell = new brls::DetailCell();
    remoteUrlCell->setText("Remote URL");
    remoteUrlCell->setDetailText(app.getRemoteServerUrl().empty() ? "Not set" : app.getRemoteServerUrl());
    remoteUrlCell->registerClickAction([remoteUrlCell](brls::View* view) {
        Application& app = Application::getInstance();
        brls::Application::getImeManager()->openForText([remoteUrlCell](std::string text) {
            Application::getInstance().setRemoteServerUrl(text);
            Application::getInstance().saveSettings();
            remoteUrlCell->setDetailText(text.empty() ? "Not set" : text);
        }, "Enter Remote Server URL", "https://abs.example.com", 256, app.getRemoteServerUrl());
        return true;
    });
    box->addView(remoteUrlCell);

    // URL selector (local vs remote)
    auto* urlSelector = new brls::SelectorCell();
    urlSelector->init("Use Server",
        {"Local URL", "Remote URL"},
        app.isUsingLocalUrl() ? 0 : 1,
        [this](int index) {
            Application::getInstance().setUseLocalUrl(index == 0);
            Application::getInstance().saveSettings();
            // Update the server label
            Application& app = Application::getInstance();
            std::string serverInfo = "Server: ";
            if (app.getServerUrl().empty()) {
                serverInfo += "Not connected";
            } else {
                serverInfo += app.getServerUrl();
                serverInfo += app.isUsingLocalUrl() ? " (Local)" : " (Remote)";
            }
            if (m_serverLabel) {
                m_serverLabel->setText(serverInfo);
            }
        });
    box->addView(urlSelector);

    // Auto-switch URL toggle
    auto* autoSwitchToggle = new brls::BooleanCell();
    autoSwitchToggle->init("Auto-Switch URL on Failure", settings.autoSwitchUrl, [&settings](bool value) {
        settings.autoSwitchUrl = value;
        Application::getInstance().saveSettings();
    });
    box->addView(autoSwitchToggle);

    // Info label
    auto* urlInfoLabel = new brls::Label();
    urlInfoLabel->setText("Auto-switch tries the other URL when connection fails");
    urlInfoLabel->setFontSize(14);
    urlInfoLabel->setMarginLeft(16);
    urlInfoLabel->setMarginTop(4);
    urlInfoLabel->setMarginBottom(16);
    box->addView(urlInfoLabel);

    // Connection timeout selector
    auto* timeoutSelector = new brls::SelectorCell();
    int timeoutIndex = 1; // default to 30s
    if (settings.connectionTimeout <= 10) timeoutIndex = 0;
    else if (settings.connectionTimeout <= 30) timeoutIndex = 1;
    else if (settings.connectionTimeout <= 60) timeoutIndex = 2;
    else timeoutIndex = 3;
    timeoutSelector->init("Connection Timeout",
        {"10 seconds", "30 seconds", "60 seconds", "120 seconds"},
        timeoutIndex,
        [&settings](int index) {
            int timeouts[] = {10, 30, 60, 120};
            settings.connectionTimeout = timeouts[index];
            Application::getInstance().saveSettings();
        });
    box->addView(timeoutSelector);

    // Logout button
    auto* logoutCell = new brls::DetailCell();
    logoutCell->setText("Logout");
    logoutCell->setDetailText("Sign out from current account");
    logoutCell->registerClickAction([this](brls::View* view) {
        onLogout();
        return true;
    });
    box->addView(logoutCell);

    return box;
}

brls::Box* SettingsTab::createUISection() {
    Application& app = Application::getInstance();
    AppSettings& settings = app.getSettings();
    brls::Box* box = makeSectionBox();

    // Theme selector
    m_themeSelector = new brls::SelectorCell();
    m_themeSelector->init("Theme", {"System", "Light", "Dark"}, static_cast<int>(settings.theme),
        [this](int index) {
            onThemeChanged(index);
        });
    box->addView(m_themeSelector);

    // Debug logging toggle
    m_debugLogToggle = new brls::BooleanCell();
    m_debugLogToggle->init("Debug Logging", settings.debugLogging, [&settings](bool value) {
        settings.debugLogging = value;
        Application::getInstance().applyLogLevel();
        Application::getInstance().saveSettings();
    });
    box->addView(m_debugLogToggle);

    return box;
}

brls::Box* SettingsTab::createContentDisplaySection() {
    Application& app = Application::getInstance();
    AppSettings& settings = app.getSettings();
    brls::Box* box = makeSectionBox();

    // Show collections toggle
    m_collectionsToggle = new brls::BooleanCell();
    m_collectionsToggle->init("Show Collections", settings.showCollections, [&settings](bool value) {
        settings.showCollections = value;
        Application::getInstance().saveSettings();
    });
    box->addView(m_collectionsToggle);

    // Show series toggle
    auto* seriesToggle = new brls::BooleanCell();
    seriesToggle->init("Show Series", settings.showSeries, [&settings](bool value) {
        settings.showSeries = value;
        Application::getInstance().saveSettings();
    });
    box->addView(seriesToggle);

    // Show authors toggle
    auto* authorsToggle = new brls::BooleanCell();
    authorsToggle->init("Show Authors", settings.showAuthors, [&settings](bool value) {
        settings.showAuthors = value;
        Application::getInstance().saveSettings();
    });
    box->addView(authorsToggle);

    // Show progress toggle
    auto* progressToggle = new brls::BooleanCell();
    progressToggle->init("Show Progress Bars", settings.showProgress, [&settings](bool value) {
        settings.showProgress = value;
        Application::getInstance().saveSettings();
    });
    box->addView(progressToggle);

    // Show only downloaded toggle
    auto* downloadedOnlyToggle = new brls::BooleanCell();
    downloadedOnlyToggle->init("Show Only Downloaded", settings.showOnlyDownloaded, [&settings](bool value) {
        settings.showOnlyDownloaded = value;
        Application::getInstance().saveSettings();
    });
    box->addView(downloadedOnlyToggle);

    // Info label for downloaded only
    auto* downloadedInfoLabel = new brls::Label();
    downloadedInfoLabel->setText("When enabled, library shows only downloaded content");
    downloadedInfoLabel->setFontSize(14);
    downloadedInfoLabel->setMarginLeft(16);
    downloadedInfoLabel->setMarginTop(4);
    box->addView(downloadedInfoLabel);

    // Home Tab sub-header — the Home settings stay grouped under Content
    // Display, as they were before the master/detail split.
    auto* homeHeader = new brls::Header();
    homeHeader->setTitle("Home Tab");
    box->addView(homeHeader);

    // Show Home Tab toggle
    auto* homeTabToggle = new brls::BooleanCell();
    homeTabToggle->init("Show Home Tab", settings.showHomeTab, [&settings](bool value) {
        settings.showHomeTab = value;
        Application::getInstance().saveSettings();
        brls::Application::notify("Restart app to apply changes");
    });
    box->addView(homeTabToggle);

    // Max Recent Episodes selector
    auto* maxEpisodesSelector = new brls::SelectorCell();
    int maxEpisodesIndex = 0;
    if (settings.maxRecentEpisodes == 5) maxEpisodesIndex = 0;
    else if (settings.maxRecentEpisodes == 10) maxEpisodesIndex = 1;
    else if (settings.maxRecentEpisodes == 15) maxEpisodesIndex = 2;
    else if (settings.maxRecentEpisodes == 20) maxEpisodesIndex = 3;
    else if (settings.maxRecentEpisodes == 0) maxEpisodesIndex = 4;
    else maxEpisodesIndex = 1; // Default to 10

    maxEpisodesSelector->init("Max Recent Episodes",
        {"5", "10", "15", "20", "Unlimited"},
        maxEpisodesIndex,
        [&settings](int index) {
            switch (index) {
                case 0: settings.maxRecentEpisodes = 5; break;
                case 1: settings.maxRecentEpisodes = 10; break;
                case 2: settings.maxRecentEpisodes = 15; break;
                case 3: settings.maxRecentEpisodes = 20; break;
                case 4: settings.maxRecentEpisodes = 0; break; // 0 = unlimited
            }
            Application::getInstance().saveSettings();
        });
    box->addView(maxEpisodesSelector);

    // Info label for max episodes
    auto* maxEpisodesInfoLabel = new brls::Label();
    maxEpisodesInfoLabel->setText("Number of recently added episodes shown on Home tab");
    maxEpisodesInfoLabel->setFontSize(14);
    maxEpisodesInfoLabel->setMarginLeft(16);
    maxEpisodesInfoLabel->setMarginTop(4);
    box->addView(maxEpisodesInfoLabel);

    return box;
}

brls::Box* SettingsTab::createPlaybackSection() {
    Application& app = Application::getInstance();
    AppSettings& settings = app.getSettings();
    brls::Box* box = makeSectionBox();

    // Resume playback toggle
    m_resumeToggle = new brls::BooleanCell();
    m_resumeToggle->init("Resume Playback", settings.resumePlayback, [&settings](bool value) {
        settings.resumePlayback = value;
        Application::getInstance().saveSettings();
    });
    box->addView(m_resumeToggle);

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
    box->addView(m_seekIntervalSelector);

    // Playback speed selector
    auto* speedSelector = new brls::SelectorCell();
    speedSelector->init("Playback Speed",
        {"0.5x", "0.75x", "1.0x", "1.25x", "1.5x", "1.75x", "2.0x"},
        static_cast<int>(settings.playbackSpeed),
        [&settings](int index) {
            settings.playbackSpeed = static_cast<PlaybackSpeed>(index);
            Application::getInstance().saveSettings();
        });
    box->addView(speedSelector);

    // Podcast auto-complete threshold selector
    auto* podcastCompleteSelector = new brls::SelectorCell();
    podcastCompleteSelector->init("Podcast Auto-Complete",
        {"Disabled", "Last 10 sec", "Last 30 sec", "Last 60 sec", "90%", "95%", "99%"},
        static_cast<int>(settings.podcastAutoComplete),
        [&settings](int index) {
            settings.podcastAutoComplete = static_cast<AutoCompleteThreshold>(index);
            Application::getInstance().saveSettings();
        });
    box->addView(podcastCompleteSelector);

    // Prevent sleep toggle
    auto* sleepToggle = new brls::BooleanCell();
    sleepToggle->init("Prevent Screen Sleep", settings.preventSleep, [&settings](bool value) {
        settings.preventSleep = value;
        Application::getInstance().saveSettings();
    });
    box->addView(sleepToggle);

    // Show download progress in player
    auto* downloadProgressToggle = new brls::BooleanCell();
    downloadProgressToggle->init("Show Download Progress", settings.showDownloadProgress, [&settings](bool value) {
        settings.showDownloadProgress = value;
        Application::getInstance().saveSettings();
    });
    box->addView(downloadProgressToggle);

    return box;
}

brls::Box* SettingsTab::createAudioSection() {
    Application& app = Application::getInstance();
    AppSettings& settings = app.getSettings();
    brls::Box* box = makeSectionBox();

    // Volume boost toggle
    auto* boostToggle = new brls::BooleanCell();
    boostToggle->init("Volume Boost", settings.boostVolume, [&settings](bool value) {
        settings.boostVolume = value;
        Application::getInstance().saveSettings();
    });
    box->addView(boostToggle);

    // Show chapter list toggle
    auto* chapterToggle = new brls::BooleanCell();
    chapterToggle->init("Show Chapter List", settings.showChapterList, [&settings](bool value) {
        settings.showChapterList = value;
        Application::getInstance().saveSettings();
    });
    box->addView(chapterToggle);

    return box;
}

brls::Box* SettingsTab::createDownloadsSection() {
    Application& app = Application::getInstance();
    AppSettings& settings = app.getSettings();
    brls::Box* box = makeSectionBox();

    // Auto-start downloads toggle
    m_autoStartDownloadsToggle = new brls::BooleanCell();
    m_autoStartDownloadsToggle->init("Auto-Start Downloads", settings.autoStartDownloads, [&settings](bool value) {
        settings.autoStartDownloads = value;
        Application::getInstance().saveSettings();
    });
    box->addView(m_autoStartDownloadsToggle);

    // Download on play toggle
    auto* downloadOnPlayToggle = new brls::BooleanCell();
    downloadOnPlayToggle->init("Download on Play", settings.downloadOnPlay, [&settings](bool value) {
        settings.downloadOnPlay = value;
        Application::getInstance().saveSettings();
    });
    box->addView(downloadOnPlayToggle);

    // Info label for download on play
    auto* downloadOnPlayInfo = new brls::Label();
    downloadOnPlayInfo->setText("When enabled, pressing play also queues for download");
    downloadOnPlayInfo->setFontSize(14);
    downloadOnPlayInfo->setMarginLeft(16);
    downloadOnPlayInfo->setMarginTop(4);
    downloadOnPlayInfo->setMarginBottom(8);
    box->addView(downloadOnPlayInfo);

    // Delete after finish toggle
    m_deleteAfterWatchToggle = new brls::BooleanCell();
    m_deleteAfterWatchToggle->init("Delete After Finishing", settings.deleteAfterFinish, [&settings](bool value) {
        settings.deleteAfterFinish = value;
        Application::getInstance().saveSettings();
    });
    box->addView(m_deleteAfterWatchToggle);

    // Refresh downloads list button
    auto* refreshDownloadsCell = new brls::DetailCell();
    refreshDownloadsCell->setText("Refresh Downloads List");
    refreshDownloadsCell->setDetailText("Scan folder for untracked files");
    refreshDownloadsCell->registerClickAction([this](brls::View* view) {
        // Reload state first, then scan for untracked files
        DownloadsManager::getInstance().loadState();
        int newFiles = DownloadsManager::getInstance().scanDownloadsFolder();
        // Also update metadata for items that are missing it
        int updatedMetadata = DownloadsManager::getInstance().updateMissingMetadata();
        auto downloads = DownloadsManager::getInstance().getDownloads();
        if (m_clearDownloadsCell) {
            m_clearDownloadsCell->setDetailText(std::to_string(downloads.size()) + " items");
        }
        std::string message;
        if (newFiles > 0 || updatedMetadata > 0) {
            message = "Found " + std::to_string(newFiles) + " new, updated " +
                      std::to_string(updatedMetadata) + " metadata (" +
                      std::to_string(downloads.size()) + " total)";
        } else {
            message = "Downloads list refreshed (" + std::to_string(downloads.size()) + " items)";
        }
        brls::Application::notify(message);
        return true;
    });
    box->addView(refreshDownloadsCell);

    // Clear all downloads
    m_clearDownloadsCell = new brls::DetailCell();
    m_clearDownloadsCell->setText("Clear All Downloads");
    auto downloads = DownloadsManager::getInstance().getDownloads();
    m_clearDownloadsCell->setDetailText(std::to_string(downloads.size()) + " items");
    m_clearDownloadsCell->registerClickAction([this](brls::View* view) {
        brls::Dialog* dialog = new brls::Dialog("Delete all downloaded content?");

        dialog->addButton("Cancel", [dialog]() {
            dialog->close();
        });

        dialog->addButton("Delete All", [dialog, this]() {
            auto downloads = DownloadsManager::getInstance().getDownloads();
            for (const auto& item : downloads) {
                DownloadsManager::getInstance().deleteDownload(item.itemId);
            }
            if (m_clearDownloadsCell) {
                m_clearDownloadsCell->setDetailText("0 items");
            }
            dialog->close();
            brls::Application::notify("All downloads deleted");
        });

        dialog->open();
        return true;
    });
    box->addView(m_clearDownloadsCell);

    // Downloads storage path info
    auto* pathLabel = new brls::Label();
    pathLabel->setText("Storage: " + DownloadsManager::getInstance().getDownloadsPath());
    pathLabel->setFontSize(14);
    pathLabel->setMarginLeft(16);
    pathLabel->setMarginTop(8);
    box->addView(pathLabel);

    return box;
}

brls::Box* SettingsTab::createAboutSection() {
    brls::Box* box = makeSectionBox();

    // No "Version" row here, and the update cell below no longer repeats
    // the version either — the rail footer is the single place the app
    // reports it. Showing it in all three was the duplication.

    // In-app updates: manual check now, plus the startup check toggle.
    auto* checkUpdatesCell = new brls::DetailCell();
    checkUpdatesCell->setText("Check for Updates");
    checkUpdatesCell->setDetailText("Check now");
    checkUpdatesCell->registerClickAction([](brls::View*) {
        app_update::checkForUpdates(true);
        return true;
    });
    box->addView(checkUpdatesCell);

    auto* autoUpdateToggle = new brls::BooleanCell();
    autoUpdateToggle->init("Check for Updates on Startup",
        Application::getInstance().getSettings().autoCheckUpdates,
        [](bool value) {
            AppSettings& s = Application::getInstance().getSettings();
            s.autoCheckUpdates = value;
            Application::getInstance().saveSettings();
        });
    box->addView(autoUpdateToggle);

    // App description
    auto* descLabel = new brls::Label();
    descLabel->setText("VitaABS - Audiobookshelf client for PS Vita, PS4, Switch, Android and desktop");
    descLabel->setFontSize(16);
    descLabel->setMarginLeft(16);
    descLabel->setMarginTop(8);
    box->addView(descLabel);

    // Credit
    auto* creditLabel = new brls::Label();
    creditLabel->setText("UI powered by Borealis");
    creditLabel->setFontSize(14);
    creditLabel->setMarginLeft(16);
    creditLabel->setMarginTop(4);
    creditLabel->setMarginBottom(20);
    box->addView(creditLabel);

    return box;
}

// ============================================================================
// Handlers — unchanged from the single-column implementation.
// ============================================================================

void SettingsTab::onLogout() {
    brls::Dialog* dialog = new brls::Dialog("Are you sure you want to logout?");

    dialog->addButton("Cancel", [dialog]() {
        dialog->close();
    });

    dialog->addButton("Logout", [dialog, this]() {
        dialog->close();

        // Clear credentials
        AudiobookshelfClient::getInstance().logout();
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

} // namespace vitaabs
