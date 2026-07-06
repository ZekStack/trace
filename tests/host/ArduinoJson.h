#pragma once

#include <cstddef>
#include <cstring>
#include <string>

class JsonDocument {
  public:
	void setSerialized(const char *value) {
		_serialized = value != nullptr ? value : "";
	}

	const std::string &serialized() const {
		return _serialized;
	}

  private:
	std::string _serialized = "{}";
};

inline size_t measureJson(const JsonDocument &doc) {
	return doc.serialized().size();
}

inline size_t measureJsonPretty(const JsonDocument &doc) {
	return doc.serialized().size();
}

inline size_t serializeJson(const JsonDocument &doc, char *buffer, size_t size) {
	if (buffer == nullptr || size == 0) {
		return 0;
	}
	const size_t copyLength = doc.serialized().size() < size - 1 ? doc.serialized().size() : size - 1;
	memcpy(buffer, doc.serialized().data(), copyLength);
	buffer[copyLength] = '\0';
	return copyLength;
}

inline size_t serializeJsonPretty(const JsonDocument &doc, char *buffer, size_t size) {
	return serializeJson(doc, buffer, size);
}
