#include <Geode/Geode.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <chrono>
#include <cstdio>

using namespace geode::prelude;

// Popup where the player types a date (YYYY-MM-DD) and presses Go.
// Daily number for a date = (current daily number) - (days between that date and today).
// This relies on one daily per day with no gaps, and on the server accepting old daily numbers.
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
        m_status->setPosition(m_mainLayer->getContentSize().width / 2, 40.f);
        m_mainLayer->addChild(m_status);

        auto btn = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("Go"), this, menu_selector(DatePopup::onGo)
        );
        m_buttonMenu->addChildAtPosition(btn, Anchor::Bottom, {0.f, 22.f});

        // Make sure the current daily number is available.
        auto glm = GameLevelManager::get();
        if (!glm->hasDailyStateBeenLoaded(GJTimedLevelType::Daily)) {
            glm->getGJDailyLevelState(GJTimedLevelType::Daily);
        }
        return true;
    }

    void onGo(CCObject*) {
        int y = 0, m = 0, d = 0;
        if (std::sscanf(m_input->getString().c_str(), "%d-%d-%d", &y, &m, &d) != 3) {
            m_status->setString("Use the format YYYY-MM-DD");
            return;
        }

        namespace ch = std::chrono;
        ch::year_month_day ymd{ch::year{y}, ch::month{static_cast<unsigned>(m)}, ch::day{static_cast<unsigned>(d)}};
        if (!ymd.ok()) {
            m_status->setString("That date doesn't exist");
            return;
        }

        auto glm = GameLevelManager::get();
        int currentDaily = glm->getDailyID(GJTimedLevelType::Daily);
        if (currentDaily <= 0) {
            m_status->setString("Daily info not loaded yet, try again");
            glm->getGJDailyLevelState(GJTimedLevelType::Daily);
            return;
        }

        auto target = ch::sys_days{ymd};
        auto today = ch::floor<ch::days>(ch::system_clock::now());
        int daysAgo = (today - target).count();
        int dailyNumber = currentDaily - daysAgo;

        if (daysAgo < 0 || dailyNumber < 1) {
            m_status->setString("Date is in the future or before dailies started");
            return;
        }

        log::info("Date {} -> daily number {} (current {})", m_input->getString(), dailyNumber, currentDaily);
        m_status->setString(fmt::format("Requesting daily #{}...", dailyNumber).c_str());

        glm->m_levelDownloadDelegate = this;
        // -1 is the id the game uses for "the daily"; dailyID carries the number.
        // Whether the server honours an old number here is unverified, so the result is logged.
        glm->downloadLevel(-1, false, dailyNumber);
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