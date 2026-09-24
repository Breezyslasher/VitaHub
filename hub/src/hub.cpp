#include "hub/hub.hpp"

#include "hub/hub_settings.hpp"
#include "hub/theme.hpp"
#include "utils/app_update.hpp"
#include "vitahub/bridge.hpp"

#include <functional>
#include <set>

#ifndef VITAHUB_DISPLAY_VERSION
#define VITAHUB_DISPLAY_VERSION "dev"
#endif

namespace vitahub {

namespace {

Module* s_current = nullptr;

// A service's status line ("Signed in to ..."). Every live instance is
// tracked so the hub can refresh them when it comes back on screen, without
// rebuilding pages (the focus stack may still point into them).
class StatusLabel : public brls::Label {
  public:
    explicit StatusLabel(Module* module) : m_module(module) {
        instances().insert(this);
        refresh();
    }
    ~StatusLabel() override { instances().erase(this); }

    void refresh() { setText(m_module->status()); }

    static void refreshAll() {
        for (StatusLabel* l : instances()) l->refresh();
    }

  private:
    static std::set<StatusLabel*>& instances() {
        static std::set<StatusLabel*> s;
        return s;
    }
    Module* m_module;
};

brls::Label* makeLabel(const std::string& text, float size, NVGcolor color, bool wrap = false) {
    auto* label = new brls::Label();
    label->setText(text);
    label->setFontSize(size);
    label->setTextColor(color);
    label->setSingleLine(!wrap);
    label->setIsWrapping(wrap);
    return label;
}

void makeClickable(brls::View* view, std::function<void()> onClick) {
    view->setFocusable(true);
    view->registerClickAction([onClick](brls::View*) {
        onClick();
        return true;
    });
    view->addGestureRecognizer(new brls::TapGestureRecognizer(view));
}

brls::Box* makePage() {
    auto* page = new brls::Box(brls::Axis::COLUMN);
    page->setPadding(30, 40, 30, 40);
    return page;
}

brls::ScrollingFrame* scrollable(brls::Box* page) {
    auto* frame = new brls::ScrollingFrame();
    frame->setContentView(page);
    frame->setGrow(1.0f);
    return frame;
}

brls::Header* makeHeader(const std::string& title) {
    auto* header = new brls::Header();
    header->setTitle(title);
    return header;
}

// Pop activities one per frame until only `depth` remain, then run `done`.
void popDownTo(size_t depth, std::function<void()> done) {
    if (brls::Application::getActivitiesStack().size() <= depth) {
        if (done) done();
        return;
    }
    brls::Application::popActivity(brls::TransitionAnimation::NONE, [depth, done]() {
        brls::sync([depth, done]() { popDownTo(depth, done); });
    });
}

// ---------------------------------------------------------------------------
// Pages
// ---------------------------------------------------------------------------

brls::View* createHomePage() {
    auto* page = makePage();

    auto* title = makeLabel("VitaHub", 32, theme::text);
    page->addView(title);
    auto* subtitle = makeLabel("Your media, books, manga and music in one place.", 16, theme::muted);
    subtitle->setMargins(4, 0, 20, 0);
    page->addView(subtitle);

    if (modules().empty()) {
        page->addView(makeLabel("This build contains no services.", 18, theme::muted));
    }
    for (Module* m : modules()) {
        auto* card = createServiceCard(m, false);
        card->setMargins(0, 0, 14, 0);
        page->addView(card);
    }

    auto* hint = makeLabel("Every service also has a \"VitaHub\" entry in its own sidebar to switch "
                           "services without coming back here.",
                           13, theme::dim, true);
    hint->setMargins(10, 0, 0, 0);
    page->addView(hint);
    return scrollable(page);
}

brls::View* createServicePage(Module* m) {
    const ModuleInfo& info = m->info();
    auto* page = makePage();

    auto* top = new brls::Box(brls::Axis::ROW);
    top->setAlignItems(brls::AlignItems::CENTER);
    auto* icon = new brls::Image();
    icon->setImageFromRes(info.icon);
    icon->setWidth(96);
    icon->setHeight(96);
    icon->setCornerRadius(16);
    top->addView(icon);

    auto* names = new brls::Box(brls::Axis::COLUMN);
    names->setMargins(0, 0, 0, 20);
    names->addView(makeLabel(info.name, 30, theme::text));
    names->addView(makeLabel(info.kind, 16, info.accent));
    top->addView(names);
    page->addView(top);

    auto* description = makeLabel(info.description, 16, theme::muted, true);
    description->setMargins(20, 0, 10, 0);
    page->addView(description);

    auto* statusRow = new brls::Box(brls::Axis::ROW);
    statusRow->setMargins(0, 0, 6, 0);
    statusRow->addView(makeLabel("Status:  ", 16, theme::muted));
    auto* status = new StatusLabel(m);
    status->setFontSize(16);
    status->setTextColor(theme::text);
    statusRow->addView(status);
    page->addView(statusRow);

    auto* version = new brls::DetailCell();
    version->setText("Imported from");
    version->setDetailText(info.version);
    version->setFocusable(false);
    page->addView(version);

    auto* openButton = new brls::Button();
    openButton->setStyle(&brls::BUTTONSTYLE_PRIMARY);
    openButton->setText("Open " + info.name);
    openButton->setMargins(20, 0, 0, 0);
    openButton->registerClickAction([m](brls::View*) {
        openModule(m);
        return true;
    });
    page->addView(openButton);

    return scrollable(page);
}

brls::View* createSettingsPage() {
    auto* page = makePage();
    HubSettings& s = hubSettings();

    page->addView(makeHeader("Appearance"));
    auto* unified = new brls::BooleanCell();
    unified->init("Unified look across services", s.unifiedTheme, [](bool on) {
        hubSettings().unifiedTheme = on;
        saveHubSettings();
    });
    page->addView(unified);
    page->addView(makeLabel("One dark palette everywhere, tinted with each service's colour. "
                            "Turn off to give every service the look of its standalone app.",
                            13, theme::dim, true));

    page->addView(makeHeader("Startup"));
    auto* launch = new brls::SelectorCell();
    launch->init("When VitaHub starts", {"Show the VitaHub home", "Reopen the last service"},
                 static_cast<int>(s.launchTarget), [](int selected) {
                     hubSettings().launchTarget = static_cast<LaunchTarget>(selected);
                     saveHubSettings();
                 });
    page->addView(launch);

    page->addView(makeHeader("Updates"));
    auto* check = new brls::DetailCell();
    check->setText("Check for updates");
    check->setDetailText(std::string("Installed: ") + VITAHUB_DISPLAY_VERSION);
    check->registerClickAction([](brls::View*) {
        app_update::checkForUpdates(true);
        return true;
    });
    page->addView(check);
    auto* autoCheck = new brls::BooleanCell();
    autoCheck->init("Check when VitaHub starts", s.autoCheckUpdates, [](bool on) {
        hubSettings().autoCheckUpdates = on;
        saveHubSettings();
    });
    page->addView(autoCheck);
    page->addView(makeLabel("VitaHub updates itself from its GitHub releases, and every service "
                            "updates with it.",
                            13, theme::dim, true));

    page->addView(makeHeader("Diagnostics"));
    auto* fps = new brls::BooleanCell();
    fps->init("Show FPS counter", s.showFps, [](bool on) {
        hubSettings().showFps = on;
        brls::Application::setFPSStatus(on);
        saveHubSettings();
    });
    page->addView(fps);

    page->addView(makeHeader("About"));
    auto* about = new brls::DetailCell();
    about->setText("VitaHub");
    about->setDetailText(VITAHUB_DISPLAY_VERSION);
    about->setFocusable(false);
    page->addView(about);
    for (Module* m : modules()) {
        auto* cell = new brls::DetailCell();
        cell->setText(m->info().name);
        cell->setDetailText(m->info().version);
        cell->setFocusable(false);
        page->addView(cell);
    }
    page->addView(makeLabel("Each service keeps its own settings, sign-in and downloads; "
                            "change them from inside the service.",
                            13, theme::dim, true));

    auto* quit = new brls::Button();
    quit->setText("Quit VitaHub");
    quit->setMargins(20, 0, 0, 0);
    quit->registerClickAction([](brls::View*) {
        brls::Application::quit();
        return true;
    });
    page->addView(quit);
    return scrollable(page);
}

// Standalone, a service's first screen is the app's root, and some have no
// Circle action at all (there is nothing to go back to). Inside VitaHub,
// Circle there should lead back to the hub.
void addBackToHub() {
    auto stack = brls::Application::getActivitiesStack();
    if (stack.size() <= hubStackDepth()) return;
    brls::View* root = stack[hubStackDepth()]->getContentView();
    if (!root) return;
    for (const auto& action : root->getActions()) {
        if (*action == brls::BUTTON_B) return;  // the module handles Circle itself
    }
    root->registerAction("VitaHub", brls::BUTTON_B, [](brls::View*) {
        returnToHub();
        return true;
    });
}

}  // namespace

// ---------------------------------------------------------------------------
// Service card
// ---------------------------------------------------------------------------

brls::Box* createServiceCard(Module* m, bool compact) {
    const ModuleInfo& info = m->info();
    const float iconSize = compact ? 48.0f : 72.0f;

    auto* card = new brls::Box(brls::Axis::ROW);
    card->setAlignItems(brls::AlignItems::CENTER);
    card->setPadding(compact ? 10 : 14);
    card->setCornerRadius(12);
    card->setHighlightCornerRadius(12);
    card->setBackgroundColor(theme::surface);
    card->setBorderColor(m == s_current ? info.accent : theme::line);
    card->setBorderThickness(m == s_current ? 2.0f : 1.0f);

    auto* icon = new brls::Image();
    icon->setImageFromRes(info.icon);
    icon->setWidth(iconSize);
    icon->setHeight(iconSize);
    icon->setCornerRadius(compact ? 8 : 12);
    card->addView(icon);

    auto* text = new brls::Box(brls::Axis::COLUMN);
    text->setMargins(0, 0, 0, 16);
    text->setGrow(1.0f);
    text->setShrink(1.0f);
    text->addView(makeLabel(info.name, compact ? 20 : 24, theme::text));
    text->addView(makeLabel(info.kind, 14, info.accent));
    if (!compact) {
        auto* status = new StatusLabel(m);
        status->setFontSize(13);
        status->setTextColor(theme::muted);
        status->setSingleLine(true);
        text->addView(status);
    }
    card->addView(text);

    auto* accentBar = new brls::Rectangle(info.accent);
    accentBar->setWidth(6);
    accentBar->setHeight(iconSize * 0.8f);
    accentBar->setCornerRadius(3);
    card->addView(accentBar);

    makeClickable(card, [m]() {
        if (m == s_current) return;  // already there: the services tab is inside it
        switchToModule(m->info().id.c_str());
    });
    return card;
}

// ---------------------------------------------------------------------------
// Hub activity
// ---------------------------------------------------------------------------

brls::View* HubActivity::createContentView() {
    auto* root = new brls::Box(brls::Axis::COLUMN);
    root->setWidthPercentage(100);
    root->setHeightPercentage(100);
    m_tabs = new brls::TabFrame();
    m_tabs->setWidthPercentage(100);
    m_tabs->setHeightPercentage(100);
    root->addView(m_tabs);
    return root;
}

void HubActivity::onContentAvailable() {
    if (brls::View* sidebar = m_tabs->getView("brls/tab_frame/sidebar")) sidebar->setWidth(260);
    m_tabs->addTab("Home", createHomePage);
    for (Module* m : modules()) {
        m_tabs->addTab(m->info().name, [m]() { return createServicePage(m); });
    }
    m_tabs->addSeparator();
    m_tabs->addTab("Settings", createSettingsPage);
}

void HubActivity::onResume() {
    StatusLabel::refreshAll();
    if (!s_current) return;
    s_current = nullptr;
    theme::restoreDefaults();
    theme::applyUnified(theme::hubAccent);
}

// ---------------------------------------------------------------------------
// Navigation
// ---------------------------------------------------------------------------

Module* currentModule() { return s_current; }

void paintModuleTheme(const Module& module) {
    if (hubSettings().unifiedTheme) theme::applyUnified(module.info().accent);
}

void openModule(Module* module) {
    if (!module) return;
    brls::Logger::info("VitaHub: opening {}", module->info().id);

    // Start every module from borealis' stock theme so nothing the previous
    // module painted leaks in; the module then applies its own theme
    // (inside open()) and paintModuleTheme() layers the unified look on top.
    theme::restoreDefaults();
    s_current = module;
    const size_t depthBefore = brls::Application::getActivitiesStack().size();
    module->open();
    if (brls::Application::getActivitiesStack().size() == depthBefore) {
        // The module didn't push anything (it failed to start): stay on the hub.
        s_current = nullptr;
        theme::restoreDefaults();
        theme::applyUnified(theme::hubAccent);
        return;
    }
    addBackToHub();

    hubSettings().lastModule = module->info().id;
    saveHubSettings();
}

void returnToHub() {
    popDownTo(hubStackDepth(), []() {
        s_current = nullptr;
        theme::restoreDefaults();
        theme::applyUnified(theme::hubAccent);
    });
}

void switchToModule(const char* id) {
    Module* target = findModule(id ? id : "");
    if (!target) return;
    popDownTo(hubStackDepth(), [target]() {
        s_current = nullptr;
        openModule(target);
    });
}

std::size_t hubStackDepth() { return 1; }

brls::View* createServicesTab(const char* currentModuleId) {
    auto* page = makePage();
    page->addView(makeLabel("VitaHub", 28, theme::text));
    auto* hint = makeLabel("Switch to another service. Each one picks up where you left it.", 15,
                           theme::muted, true);
    hint->setMargins(4, 0, 16, 0);
    page->addView(hint);

    for (Module* m : modules()) {
        auto* card = createServiceCard(m, true);
        card->setMargins(0, 0, 10, 0);
        if (m->info().id == (currentModuleId ? currentModuleId : "")) {
            card->setFocusable(false);
            card->setAlpha(0.6f);
        }
        page->addView(card);
    }

    auto* home = new brls::DetailCell();
    home->setText("VitaHub home");
    home->setDetailText("Service list and hub settings");
    home->setMargins(10, 0, 0, 0);
    home->registerClickAction([](brls::View*) {
        returnToHub();
        return true;
    });
    page->addView(home);
    return scrollable(page);
}

}  // namespace vitahub
