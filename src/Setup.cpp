#include "Setup.h"
#include "ChatWidget.hpp"
#include "ObsData.hpp"

#include <chatterino-embed/App.hpp>
#include <chatterino-embed/AppBuilder.hpp>
#include <chatterino-embed/Split.hpp>

#include <filesystem>
#include <obs-module.h>
#include <obs-frontend-api.h>
#include <plugin-support.h>

#include <QWidget>
#include <QObject>
#include <QStandardPaths>
#include <QMainWindow>
#include <QUuid>
#include <QSSLSocket>
#include <QPluginLoader>
#include <QScopedPointer>
#include <QPointer>

namespace {

using namespace chatterino::obs;

QString stdPathToQString(const std::filesystem::path &path)
{
#ifdef Q_OS_WIN
	return QString::fromStdWString(path.native());
#else
	return QString::fromStdString(path.native());
#endif
}

std::filesystem::path qStringToStdPath(const QString &path)
{
	const auto *ptr = reinterpret_cast<const char16_t *>(path.utf16());
	return {ptr, ptr + path.size()};
}

QString getAppDir()
{
	auto appdata =
		qStringToStdPath(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).parent_path();
	return stdPathToQString(appdata / "Chatterino2");
}

class PluginState {
public:
	PluginState(chatterino::embed::App *app) : app(app) {}

public:
	void load(obs_data_t *rootData)
	{
		OwnedObsData obj(obs_data_get_obj(rootData, "chatterino-splits"));
		if (!obj) {
			return;
		}

		for (obs_data_item_t *item = obs_data_first(obj); item != nullptr; obs_data_item_next(&item)) {
			const char *id = obs_data_item_get_name(item);
			const char *data = obs_data_item_get_string(item);
			if (!id || !data) {
				continue;
			}
			auto *chat = this->addChatByID(QString::fromUtf8(id), /*forceVisible=*/false);
			if (!chat) {
				continue;
			}
			auto *split = new chatterino::embed::Split(chat);
			split->deserializeData(QByteArrayView(data));
			chat->setSplit(split);
		}
	}

	void save(obs_data_t *rootData)
	{
		OwnedObsData obj(obs_data_create());
		for (const auto &[key, val] : this->widgets) {
			if (!val) {
				continue;
			}
			QByteArray ser = val->split()->serializeData();
			ser.append('\0'); // gotta love C strings
			obs_data_set_string(obj, key.toStdString().c_str(), ser.constData());
		}
		obs_data_set_obj(rootData, "chatterino-splits", obj);
	}

	void addChat()
	{
		auto uuid = QUuid::createUuid();
		auto id = "chatterino-obs:" + uuid.toString();
		auto *chat = this->addChatByID(id, true);
		chat->setSplit(new chatterino::embed::Split(chat));
	}

private:
	ChatWidget *addChatByID(const QString &id, bool forceVisible)
	{
		auto *chat = new ChatWidget(id, static_cast<QMainWindow *>(obs_frontend_get_main_window()));
		if (!obs_frontend_add_dock_by_id(id.toStdString().c_str(), "Chatterino", chat)) {
			chat->deleteLater();
			return nullptr;
		}

		if (forceVisible) {
			QMetaObject::invokeMethod(
				chat, [chat] { chat->parentWidget()->setVisible(true); }, Qt::QueuedConnection);
		}
		this->widgets.emplace_back(id, chat);
		return chat;
	}

	QScopedPointer<chatterino::embed::App> app;
	std::vector<std::pair<QString, QPointer<ChatWidget>>> widgets;
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

void ensureState()
{
	if (!GLOBAL_STATE) {
		chatterino::embed::AppBuilder builder(nullptr);
		builder.setRootDirectory(getAppDir());
		builder.setSaveSettingsOnExit(false);
		GLOBAL_STATE = std::make_unique<PluginState>(builder.createApp());
	}
}

} // namespace

namespace chatterino::obs {

}

namespace {
extern "C" void chatterino_obs_init()
{
#ifdef Q_OS_WIN
	ensureTLS();
#endif
	ensureState();

	obs_frontend_add_save_callback(
		+[](obs_data_t *save_data, bool saving, void *user) {
			if (!GLOBAL_STATE) {
				return;
			}

			if (saving) {
				GLOBAL_STATE->save(save_data);
			} else {
				GLOBAL_STATE->load(save_data);
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
