#include "Setup.h"
#include "common/Args.hpp"
#include "common/Channel.hpp"
#include "providers/twitch/TwitchIrcServer.hpp"
#include "singletons/Paths.hpp"
#include "singletons/Resources.hpp"
#include "singletons/Settings.hpp"
#include "singletons/Updates.hpp"
#include "widgets/splits/Split.hpp"
#include "common/network/NetworkManager.hpp"
#include "providers/NetworkConfigurationProvider.hpp"
#include "providers/IvrApi.hpp"
#include "providers/twitch/api/Helix.hpp"
#include "util/FilesystemHelpers.hpp"
#include "common/Env.hpp"
#include "Application.hpp"

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <plugin-support.h>

#include <QWidget>
#include <QDockWidget>
#include <QObject>
#include <QFrame>
#include <QVBoxLayout>
#include <QStandardPaths>
#include <QLabel>
#include <QMainWindow>
#include <QUuid>
#include <QSizePolicy>
#include <QSSLSocket>
#include <QPluginLoader>
#include <QPointer>

namespace {

class ChatWidget : public QFrame {
public:
	ChatWidget(QWidget *parent) : QFrame(parent), split(new chatterino::Split(this))
	{
		this->split->setChannel(chatterino::Channel::getEmpty());
		this->split->setSizePolicy({QSizePolicy::Expanding, QSizePolicy::Expanding});
	}

protected:
	void resizeEvent(QResizeEvent *event) override { this->split->setGeometry({QPoint{}, this->size()}); }

private:
	chatterino::Split *split;
};

chatterino::Args makeArgs()
{
	chatterino::Args args;
	args.dontSaveSettings = true;
	args.dontLoadMainWindow = true;
	args.isFramelessEmbed = true;
	return args;
}

QString getAppDir()
{
	auto appdata = chatterino::qStringToStdPath(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
			       .parent_path();
	return chatterino::stdPathToQString(appdata / "Chatterino2");
}

class PluginState {
public:
	PluginState()
		: paths(getAppDir()),
		  args(makeArgs()),
		  settings(this->args, this->paths.settingsDirectory),
		  updates(this->paths, this->settings),
		  app(this->settings, this->paths, this->args, this->updates)
	{
	}

public:
	void load() {}

	void save() {}

	void addChat()
	{
		auto *chat = new ChatWidget(static_cast<QMainWindow *>(obs_frontend_get_main_window()));
		auto uuid = QUuid::createUuid();
		std::string id = "chatterino-obs:" + uuid.toString().toStdString();
		if (!obs_frontend_add_dock_by_id(id.c_str(), "Chatterino", chat)) {
			return;
		}
		QMetaObject::invokeMethod(
			chat,
			[chat] {
				obs_log(LOG_INFO, "Visibling: %s", chat->parentWidget()->metaObject()->className());
				chat->parentWidget()->setVisible(true);
			},
			Qt::QueuedConnection);
		this->widgets.emplace_back(chat);
	}

public:
	chatterino::Paths paths;
	chatterino::Args args;
	chatterino::Settings settings;
	chatterino::Updates updates;
	chatterino::Application app;

private:
	std::vector<QPointer<ChatWidget>> widgets;
};

#ifdef Q_OS_WIN
QString obsModuleDataPath(const char *name)
{
	char *res = obs_module_file(name);
	if (!res) {
		return {};
	}
	auto str = QString::fromUtf8(res);
	bfree(res);
	return str;
}

void tryLoadTLS(const char *path)
{
	auto qPath = obsModuleDataPath(path);
	if (qPath.isEmpty()) {
		obs_log(LOG_INFO, "%s not found in data, skipping", path);
		return;
	}
	QPluginLoader loader(qPath);
	if (loader.isLoaded()) {
		obs_log(LOG_INFO, "%s is already loaded", path);
		return;
	}
	if (!loader.load()) {
		obs_log(LOG_INFO, "%s failed to load: %s", path, loader.errorString().toStdString().c_str());
		return;
	}
	if (!loader.instance()) {
		obs_log(LOG_INFO, "%s no instance.", path);
		return;
	}
	obs_log(LOG_INFO, "%s loaded (instance: %s)", path, loader.instance()->metaObject()->className());
}

void ensureTLS()
{
	if (!QSslSocket::availableBackends().empty()) {
		return;
	}
#ifdef NDEBUG
	tryLoadTLS("qschannelbackend.dll");
	tryLoadTLS("qcertonlybackend.dll");
	tryLoadTLS("qopensslbackend.dll");
#else
	tryLoadTLS("qschannelbackendd.dll");
	tryLoadTLS("qcertonlybackendd.dll");
	tryLoadTLS("qopensslbackendd.dll");
#endif

	std::string backends = QSslSocket::availableBackends().join(", ").toStdString();
	obs_log(LOG_INFO, "TLS backends: %s", backends.c_str());
}
#endif

std::unique_ptr<PluginState> GLOBAL_STATE;
} // namespace

namespace chatterino::obs {

}

namespace {
extern "C" void chatterino_obs_init()
{
#ifdef Q_OS_WIN
	ensureTLS();
#endif

	chatterino::initResources();
	chatterino::NetworkConfigurationProvider::applyFromEnv(chatterino::Env::get());
	chatterino::IvrApi::initialize();
	chatterino::Helix::initialize();
	GLOBAL_STATE.reset(new PluginState);

	chatterino::NetworkManager::init();
	GLOBAL_STATE->app.initialize(GLOBAL_STATE->settings, GLOBAL_STATE->paths);
	GLOBAL_STATE->app.getTwitch()->connect();
	obs_log(LOG_INFO, "SETTINGS: %s", GLOBAL_STATE->paths.settingsDirectory.toStdString().c_str());

	GLOBAL_STATE->settings.enableBTTVChannelEmotes.connect(
		[] {
			dynamic_cast<chatterino::TwitchIrcServer *>(GLOBAL_STATE->app.getTwitch())
				->reloadAllBTTVChannelEmotes();
		},
		false);
	GLOBAL_STATE->settings.enableFFZChannelEmotes.connect(
		[] {
			dynamic_cast<chatterino::TwitchIrcServer *>(GLOBAL_STATE->app.getTwitch())
				->reloadAllFFZChannelEmotes();
		},
		false);
	GLOBAL_STATE->settings.enableSevenTVChannelEmotes.connect(
		[] {
			dynamic_cast<chatterino::TwitchIrcServer *>(GLOBAL_STATE->app.getTwitch())
				->reloadAllSevenTVChannelEmotes();
		},
		false);

	obs_frontend_add_save_callback(
		+[](obs_data_t *save_data, bool saving, void *user) {
			if (!GLOBAL_STATE) {
				return;
			}

			if (saving) {
				GLOBAL_STATE->save();
			} else {
				GLOBAL_STATE->load();
			}
		},
		nullptr);

	QAction *action =
		static_cast<QAction *>(obs_frontend_add_tools_menu_qaction(obs_module_text("New Chatterino Window")));
	QAction::connect(action, &QAction::triggered, [] {
		if (!GLOBAL_STATE) {
			return;
		}
		GLOBAL_STATE->addChat();
	});
}
} // namespace
