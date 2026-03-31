#include "ChatWidget.hpp"

#include <chatterino-embed/Split.hpp>

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <QFrame>
#include <QDockWidget>

namespace chatterino::obs {

using namespace Qt::Literals;

ChatWidget::ChatWidget(QString id, QWidget *parent) : QFrame(parent), id_(std::move(id)), split_(nullptr) {}

void ChatWidget::setSplit(embed::Split *split)
{
	this->split_ = split;
	this->syncTitle();

	if (!this->split_) {
		return;
	}

	this->split_->setSizePolicy({QSizePolicy::Expanding, QSizePolicy::Expanding});

	QObject::connect(this->split_, &embed::Split::closeRequested, this,
			 [this] { obs_frontend_remove_dock(this->id_.toStdString().c_str()); });

	QObject::connect(this->split_, &embed::Split::channelChanged, this, &ChatWidget::syncTitle);
}

embed::Split *ChatWidget::split()
{
	return this->split_;
}

QStringView ChatWidget::id() const
{
	return this->id_;
}

void ChatWidget::resizeEvent(QResizeEvent *event)
{
	if (this->split_) {
		this->split_->setGeometry({QPoint{}, this->size()});
	}
}

void ChatWidget::syncTitle()
{
	auto *parent = qobject_cast<QDockWidget *>(this->parentWidget());
	if (parent) {
		QString name;

		if (this->split_) {
			name = this->split_->channelName();
		}
		if (name.isEmpty()) {
			name = u"Chatterino"_s;
		}

		parent->setWindowTitle(name);
	}
}

} // namespace chatterino::obs
