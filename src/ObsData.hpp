#pragma once

#include <obs-data.h>
#include <QUtf8StringView>

namespace chatterino::obs {

struct OwnedObsData {
	OwnedObsData() = default;
	explicit OwnedObsData(obs_data_t *data) : data(data) {}

	OwnedObsData(const OwnedObsData &) = delete;
	OwnedObsData(OwnedObsData &&other) : data(other.data) { other.data = nullptr; }
	OwnedObsData &operator=(const OwnedObsData &) = delete;
	OwnedObsData &operator=(OwnedObsData &&other)
	{
		this->data = other.data;
		other.data = nullptr;
		return *this;
	}

	~OwnedObsData()
	{
		if (this->data) {
			obs_data_release(this->data);
		}
	}
	operator obs_data_t *() { return this->data; }

private:
	obs_data_t *data = nullptr;
};

} // namespace chatterino::obs
