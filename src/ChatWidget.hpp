#pragma once

#include <QFrame>

namespace chatterino {

namespace embed {
class Split;
}

namespace obs {

class ChatWidget : public QFrame {
public:
	ChatWidget(QString id, QWidget *parent);

	void setSplit(embed::Split *split);

	embed::Split *split();
	QStringView id() const;

protected:
	void resizeEvent(QResizeEvent *event) override;

private:
	void syncTitle();

	QString id_;
	embed::Split *split_;
};

} // namespace obs
} // namespace chatterino
