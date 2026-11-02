#pragma once

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace Ayu::ChannelPopular {

inline std::optional<std::int32_t> FloodWaitSeconds(std::string_view error) {
	for (const auto prefix : {
			std::string_view("FLOOD_WAIT_"),
			std::string_view("FLOOD_PREMIUM_WAIT_"),
			std::string_view("SLOWMODE_WAIT_") }) {
		if (!error.starts_with(prefix)) {
			continue;
		}
		auto seconds = std::int32_t(0);
		const auto last = error.data() + error.size();
		const auto result = std::from_chars(
			error.data() + prefix.size(),
			last,
			seconds);
		return (result.ec == std::errc()
			&& result.ptr == last
			&& seconds >= 0)
			? std::optional(std::max(std::int32_t(1), seconds))
			: std::nullopt;
	}
	return std::nullopt;
}

struct ReactionCount {
	std::int64_t count = 0;
	bool paid = false;
};

inline std::int64_t CountReactions(const std::vector<ReactionCount> &values) {
	auto result = std::int64_t(0);
	for (const auto &value : values) {
		if (!value.paid && value.count > 0) {
			result += std::min(
				value.count,
				std::numeric_limits<std::int64_t>::max() - result);
		}
	}
	return result;
}

struct Entry {
	std::int32_t id = 0;
	std::int64_t date = 0;
	std::int64_t reactions = 0;
	std::string summary;
};

class Index final {
public:
	explicit Index(std::int64_t cutoff = 0) : _cutoff(cutoff) {
	}

	bool upsert(Entry entry) {
		if (entry.id <= 0 || entry.date < _cutoff) {
			return false;
		}
		entry.reactions = std::max(std::int64_t(0), entry.reactions);
		const auto existing = _entries.find(entry.id);
		const auto inserted = existing == _entries.end();
		if (!inserted) {
			const auto &old = existing->second;
			_order.erase({ old.reactions, old.date, old.id });
		}
		_order.emplace(entry.reactions, entry.date, entry.id);
		_entries.insert_or_assign(entry.id, std::move(entry));
		return inserted;
	}

	void erase(std::int32_t id) {
		const auto existing = _entries.find(id);
		if (existing != _entries.end()) {
			const auto &entry = existing->second;
			_order.erase({ entry.reactions, entry.date, entry.id });
			_entries.erase(existing);
		}
	}

	[[nodiscard]] bool contains(std::int32_t id) const {
		return _entries.contains(id);
	}

	[[nodiscard]] const Entry *find(std::int32_t id) const {
		const auto i = _entries.find(id);
		return (i == _entries.end()) ? nullptr : &i->second;
	}

	[[nodiscard]] std::size_t size() const {
		return _entries.size();
	}

	[[nodiscard]] std::int64_t cutoff() const {
		return _cutoff;
	}

	[[nodiscard]] std::vector<std::int32_t> ranked(
			std::int64_t minimum,
			std::size_t limit = std::numeric_limits<std::size_t>::max()) const {
		auto result = std::vector<std::int32_t>();
		result.reserve(std::min(_entries.size(), limit));
		for (const auto &[reactions, date, id] : _order) {
			if (reactions < minimum || result.size() == limit) {
				break;
			}
			result.push_back(id);
		}
		return result;
	}

private:
	std::int64_t _cutoff = 0;
	std::unordered_map<std::int32_t, Entry> _entries;
	std::set<
		std::tuple<std::int64_t, std::int64_t, std::int32_t>,
		std::greater<>> _order;

};

struct PageProgress {
	std::int32_t nextOffset = 0;
	bool complete = false;
	bool stalled = false;
};

inline PageProgress AdvancePage(
		std::int32_t previousOffset,
		const std::vector<std::int32_t> &ids,
		bool reachedCutoff) {
	auto next = std::int32_t(0);
	for (const auto id : ids) {
		if (id > 0 && (!next || id < next)) {
			next = id;
		}
	}
	const auto exhausted = !next;
	return {
		exhausted ? previousOffset : next,
		reachedCutoff || exhausted,
		!reachedCutoff && !exhausted
			&& previousOffset > 0 && next >= previousOffset,
	};
}

}
