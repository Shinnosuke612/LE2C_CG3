// 役割: AssimpへWindowsのUnicodeパスを安全に渡すI/Oアダプタを定義する。
#pragma once

#include "../utility/StringUtility.h"

#include <assimp/IOStream.hpp>
#include <assimp/IOSystem.hpp>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <utility>

class AssimpUnicodeStream final : public Assimp::IOStream {
public:
	explicit AssimpUnicodeStream(
		const std::filesystem::path& path,
		const char* mode
	) {
		std::ios_base::openmode openMode = std::ios_base::binary;
		if (std::strchr(mode, 'r')) {
			openMode |= std::ios_base::in;
		}
		if (std::strchr(mode, 'w')) {
			openMode |= std::ios_base::out | std::ios_base::trunc;
		}
		if (std::strchr(mode, 'a')) {
			openMode |= std::ios_base::out | std::ios_base::app;
		}
		writable_ = (openMode & std::ios_base::out) != std::ios_base::openmode{};
		file_.open(path, openMode);

		std::error_code error;
		fileSize_ = std::filesystem::file_size(path, error);
		if (error) {
			fileSize_ = 0;
		}
	}

	bool IsOpen() const {
		return file_.is_open();
	}

	size_t Read(void* buffer, size_t size, size_t count) override {
		if (size == 0 || count == 0 || !file_.is_open()) {
			return 0;
		}
		file_.read(static_cast<char*>(buffer), static_cast<std::streamsize>(size * count));
		return static_cast<size_t>(file_.gcount()) / size;
	}

	size_t Write(const void* buffer, size_t size, size_t count) override {
		if (size == 0 || count == 0 || !file_.is_open()) {
			return 0;
		}
		file_.write(
			static_cast<const char*>(buffer),
			static_cast<std::streamsize>(size * count)
		);
		return file_.good() ? count : 0;
	}

	aiReturn Seek(size_t offset, aiOrigin origin) override {
		if (!file_.is_open()) {
			return aiReturn_FAILURE;
		}

		std::ios_base::seekdir direction = std::ios_base::beg;
		switch (origin) {
		case aiOrigin_CUR:
			direction = std::ios_base::cur;
			break;
		case aiOrigin_END:
			direction = std::ios_base::end;
			break;
		case aiOrigin_SET:
		default:
			break;
		}

		file_.clear();
		file_.seekg(static_cast<std::streamoff>(offset), direction);
		if (writable_) {
			file_.seekp(static_cast<std::streamoff>(offset), direction);
		}
		return file_.fail() ? aiReturn_FAILURE : aiReturn_SUCCESS;
	}

	size_t Tell() const override {
		if (!file_.is_open()) {
			return 0;
		}
		const std::streampos position = file_.tellg();
		return position == std::streampos(-1)
			? 0
			: static_cast<size_t>(position);
	}

	size_t FileSize() const override {
		return static_cast<size_t>(fileSize_);
	}

	void Flush() override {
		file_.flush();
	}

private:
	mutable std::fstream file_;
	uintmax_t fileSize_ = 0;
	bool writable_ = false;
};

class AssimpUnicodeIOSystem final : public Assimp::IOSystem {
public:
	explicit AssimpUnicodeIOSystem(std::filesystem::path rootDirectory)
		: rootDirectory_(std::move(rootDirectory)) {
	}

	bool Exists(const char* file) const override {
		std::error_code error;
		return std::filesystem::exists(
			ResolvePath(file),
			error
		);
	}

	char getOsSeparator() const override {
		return '/';
	}

	Assimp::IOStream* Open(const char* file, const char* mode = "rb") override {
		AssimpUnicodeStream* stream = new AssimpUnicodeStream(
			ResolvePath(file),
			mode ? mode : "rb"
		);
		if (!stream->IsOpen()) {
			delete stream;
			return nullptr;
		}
		return stream;
	}

	void Close(Assimp::IOStream* file) override {
		delete file;
	}

private:
	std::filesystem::path ResolvePath(const char* file) const {
		const std::filesystem::path requestedPath =
			StringUtility::ToPath(file ? file : "");
		if (requestedPath.is_absolute() || CurrentDirectory().empty()) {
			return requestedPath.is_absolute()
				? requestedPath
				: (rootDirectory_ / requestedPath).lexically_normal();
		}
		return (
			StringUtility::ToPath(CurrentDirectory()) / requestedPath
		).lexically_normal();
	}

	std::filesystem::path rootDirectory_;
};
