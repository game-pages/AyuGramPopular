#pragma once

#include "ayu/features/channel_popular/channel_popular_index.h"

#include <string_view>

namespace Ayu::ChannelPopular {

constexpr auto kCacheLifetime = std::int64_t(30 * 24 * 60 * 60);
constexpr auto kMaxCacheBytes = std::size_t(64 * 1024 * 1024);
constexpr auto kMaxCacheEntries = std::size_t(1'000'000);
constexpr auto kMaxCacheSummary = std::size_t(1024);

struct CacheScope {
	std::uint64_t account = 0;
	std::uint64_t channel = 0;
	bool allHistory = false;

	friend bool operator==(const CacheScope &, const CacheScope &) = default;
};

struct CacheSnapshot {
	CacheScope scope;
	std::int64_t updatedAt = 0;
	std::int64_t retryAfter = 0;
	std::int32_t offset = 0;
	bool complete = false;
	std::vector<Entry> entries;
};

inline bool CacheFresh(std::int64_t updatedAt, std::int64_t now) {
	return updatedAt > 0 && updatedAt <= now
		&& now - updatedAt < kCacheLifetime;
}

inline std::string EncodeCache(const CacheSnapshot &snapshot) {
	if (!snapshot.scope.account || !snapshot.scope.channel
		|| snapshot.updatedAt <= 0 || snapshot.retryAfter < 0
		|| snapshot.offset < 0 || snapshot.entries.size() > kMaxCacheEntries) {
		return {};
	}
	auto result = std::string("AYPOPULAR\x01", 10);
	const auto write = [&](std::uint64_t value) {
		for (auto shift = 0; shift != 64; shift += 8) {
			result.push_back(char((value >> shift) & 0xFF));
		}
	};
	write(snapshot.scope.account);
	write(snapshot.scope.channel);
	write(snapshot.scope.allHistory);
	write(snapshot.updatedAt);
	write(snapshot.retryAfter);
	write(snapshot.offset);
	write(snapshot.complete);
	write(snapshot.entries.size());
	for (const auto &entry : snapshot.entries) {
		if (entry.id <= 0 || entry.date < 0 || entry.reactions < 0
			|| entry.summary.size() > kMaxCacheSummary
			|| result.size() + 32 + entry.summary.size() > kMaxCacheBytes) {
			return {};
		}
		write(entry.id);
		write(entry.date);
		write(entry.reactions);
		write(entry.summary.size());
		result.append(entry.summary);
	}
	return result;
}

inline std::optional<CacheSnapshot> DecodeCache(
		std::string_view data,
		CacheScope expected,
		std::int64_t now) {
	constexpr auto headerSize = std::size_t(10 + 8 * 8);
	if (now <= 0 || data.size() < headerSize || data.size() > kMaxCacheBytes
		|| data.substr(0, 10) != std::string_view("AYPOPULAR\x01", 10)) {
		return std::nullopt;
	}
	auto position = std::size_t(10);
	const auto read = [&]() {
		auto result = std::uint64_t(0);
		for (auto shift = 0; shift != 64; shift += 8) {
			result |= std::uint64_t(std::uint8_t(data[position++])) << shift;
		}
		return result;
	};
	auto result = CacheSnapshot();
	result.scope.account = read();
	result.scope.channel = read();
	const auto allHistory = read();
	const auto updatedAt = read();
	const auto retryAfter = read();
	const auto offset = read();
	const auto complete = read();
	const auto count = read();
	if (allHistory > 1 || complete > 1 || offset > INT32_MAX
		|| updatedAt > std::uint64_t(now)
		|| retryAfter > std::uint64_t(now) + INT32_MAX
		|| count > kMaxCacheEntries || count > (data.size() - position) / 32) {
		return std::nullopt;
	}
	result.scope.allHistory = allHistory;
	result.updatedAt = std::int64_t(updatedAt);
	result.retryAfter = std::int64_t(retryAfter);
	result.offset = std::int32_t(offset);
	result.complete = complete;
	if (result.scope != expected || !CacheFresh(result.updatedAt, now)
		|| (count && !result.offset)) {
		return std::nullopt;
	}
	result.entries.reserve(count);
	auto ids = std::unordered_map<std::int32_t, bool>();
	for (auto i = std::uint64_t(0); i < count; ++i) {
		if (data.size() - position < 32) {
			return std::nullopt;
		}
		const auto id = read();
		const auto date = read();
		const auto reactions = read();
		const auto length = read();
		if (!id || id > INT32_MAX || date > INT32_MAX
			|| reactions > INT64_MAX || length > kMaxCacheSummary
			|| length > data.size() - position
			|| !ids.emplace(std::int32_t(id), true).second) {
			return std::nullopt;
		}
		result.entries.push_back({
			std::int32_t(id),
			std::int64_t(date),
			std::int64_t(reactions),
			std::string(data.substr(position, length)),
		});
		position += length;
	}
	if (position != data.size()) {
		return std::nullopt;
	}
	for (const auto &entry : result.entries) {
		if (entry.id < result.offset) {
			return std::nullopt;
		}
	}
	return result;
}

}
