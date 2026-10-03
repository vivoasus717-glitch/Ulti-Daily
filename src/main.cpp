#include <Geode/Geode.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <chrono>
#include <cstdio>

using namespace geode::prelude;

// Popup where the player types a date (YYYY-MM-DD).
//  - "Open" downloads that day's daily level directly.
//  - "List" opens GD's own level-list view of past dailies, starting near that date.
// Daily number for a date = (current daily number) - (days between that date and today).
class DatePopup : public Popup, public LevelDownloadDelegate {
protected:
    TextInput* m_input = nullptr;
    CCLabelBMFont* m_status = nullptr;

    bool init() {
        if (!Popup::init(260.f, 150.f)) return false;

        this->setTitle("Daily by date");

        m_input = TextInput::create(200.f, "YYYY-MM-DD", "bigFont.fnt");
        m_input->setCommonFilter(CommonFilter::Any);
        m_input->setMaxCharCount(10);
        m_input->setPosition(m_mainLayer->getContentSize() / 2 + CCPoint{0.f, 10.f});
        m_mainLayer->addChild(m_input);

        m_status = CCLabelBMFont::create("", "chatFont.fnt");
        m_status->setScale(0.7f);
        m_status->setPosition(m_mainLayer->getContentSize().width / 2, 48.f);
        m_mainLayer->addChild(m_status);

        auto openBtn = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("Open", "goldFont.fnt", "GJ_button_01.png", 0.8f),
            this, menu_selector(DatePopup::onOpen)
        );
        m_buttonMenu->addChildAtPosition(openBtn, Anchor::Bottom, {-85.f, 22.f});

        auto listBtn = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("List", "goldFont.fnt", "GJ_button_01.png", 0.8f),
            this, menu_selector(DatePopup::onList)
        );
        m_buttonMenu->addChildAtPosition(listBtn, Anchor::Bottom, {0.f, 22.f});

        auto era21Btn = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("2.1", "goldFont.fnt", "GJ_button_02.png", 0.8f),
            this, menu_selector(DatePopup::onEra21)
        );
        m_buttonMenu->addChildAtPosition(era21Btn, Anchor::Bottom, {85.f, 22.f});

        // Make sure the current daily number is available.
        auto glm = GameLevelManager::get();
        if (!glm->hasDailyStateBeenLoaded(GJTimedLevelType::Daily)) {
            glm->getGJDailyLevelState(GJTimedLevelType::Daily);
        }
        return true;
    }

    // Parses the typed date. On success fills daysAgo (0 = today) and the daily number for that date.
    bool parseDate(int& daysAgo, int& dailyNumber) {
        int y = 0, m = 0, d = 0;
        if (std::sscanf(m_input->getString().c_str(), "%d-%d-%d", &y, &m, &d) != 3) {
            m_status->setString("Use the format YYYY-MM-DD");
            return false;
        }

        namespace ch = std::chrono;
        ch::year_month_day ymd{ch::year{y}, ch::month{static_cast<unsigned>(m)}, ch::day{static_cast<unsigned>(d)}};
        if (!ymd.ok()) {
            m_status->setString("That date doesn't exist");
            return false;
        }

        auto glm = GameLevelManager::get();
        int currentDaily = glm->getDailyID(GJTimedLevelType::Daily);
        if (currentDaily <= 0) {
            m_status->setString("Daily info not loaded yet, try again");
            glm->getGJDailyLevelState(GJTimedLevelType::Daily);
            return false;
        }

        auto target = ch::sys_days{ymd};
        auto today = ch::floor<ch::days>(ch::system_clock::now());
        daysAgo = (today - target).count();
        dailyNumber = currentDaily - daysAgo;

        if (daysAgo < 0 || dailyNumber < 1) {
            m_status->setString("Date is in the future or before dailies started");
            return false;
        }
        return true;
    }

    void onOpen(CCObject*) {
        int daysAgo = 0, dailyNumber = 0;
        if (!parseDate(daysAgo, dailyNumber)) return;

        log::info("Open: date {} -> daily number {}", m_input->getString(), dailyNumber);
        m_status->setString(fmt::format("Requesting daily #{}...", dailyNumber).c_str());

        auto glm = GameLevelManager::get();
        glm->m_levelDownloadDelegate = this;
        // -1 is the id the game uses for "the daily"; dailyID carries the number.
        glm->downloadLevel(-1, false, dailyNumber);
    }

    void onList(CCObject*) {
        int daysAgo = 0, dailyNumber = 0;
        if (!parseDate(daysAgo, dailyNumber)) return;

        log::info("List: date {} -> daily #{}", m_input->getString(), dailyNumber);
        openDailyList(daysAgo);
    }

    // Opens GD's past-dailies list at the page containing the daily from `daysAgo` days back.
    // The list is newest first, 10 per page, starting from yesterday,
    // so the entry for "daysAgo" days back is at index daysAgo - 1.
    void openDailyList(int daysAgo) {
        int index = daysAgo > 0 ? daysAgo - 1 : 0;
        int page = index / 10;

        log::info("Opening past dailies list at page {}", page);

        auto search = GJSearchObject::create(SearchType::DailySafe);
        search->m_page = page;
        CCDirector::get()->pushScene(CCTransitionFade::create(0.5f, LevelBrowserLayer::scene(search)));
    }

    // 2.1 era: dailies started with 2.1 (Jan 2017); 2.2 launched on 22 Dec 2023.
    // Opens the list at the last daily before 2.2 (21 Dec 2023); page back for older ones.
    void onEra21(CCObject*) {
        namespace ch = std::chrono;
        auto end = ch::sys_days{ch::year{2023} / ch::December / 21};
        auto today = ch::floor<ch::days>(ch::system_clock::now());
        int daysAgo = (today - end).count();
        if (daysAgo < 1) daysAgo = 1;

        m_status->setString("2.1 dailies: Jan 2017 - Dec 2023, newest first");
        openDailyList(daysAgo);
    }

    void levelDownloadFinished(GJGameLevel* level) override {
        log::info("Downloaded level {} (id {})", std::string(level->m_levelName), level->m_levelID.value());
        GameLevelManager::get()->m_levelDownloadDelegate = nullptr;
        CCDirector::get()->replaceScene(CCTransitionFade::create(0.5f, LevelInfoLayer::scene(level, false)));
    }

    void levelDownloadFailed(int err) override {
        log::warn("Daily download failed: {}", err);
        GameLevelManager::get()->m_levelDownloadDelegate = nullptr;
        if (m_status) m_status->setString("Server did not return that daily");
    }

    ~DatePopup() {
        auto glm = GameLevelManager::get();
        if (glm && glm->m_levelDownloadDelegate == this) glm->m_levelDownloadDelegate = nullptr;
    }

public:
    static DatePopup* create() {
        auto ret = new DatePopup();
        if (ret->init()) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }
};

class $modify(DailyMenuLayer, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;

        auto menu = this->getChildByID("bottom-menu");
        if (!menu) return true;

        auto btn = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("Daily by date", "bigFont.fnt", "GJ_button_01.png", 0.6f),
            this, menu_selector(DailyMenuLayer::onDailyByDate)
        );
        btn->setID("daily-by-date"_spr);
        menu->addChild(btn);
        menu->updateLayout();
        return true;
    }

    void onDailyByDate(CCObject*) {
        DatePopup::create()->show();
    }
};