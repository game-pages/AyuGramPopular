#include "channel_popular_cache.h"

#include <cassert>
#include <iostream>
#include <random>

int main() {
	using namespace Ayu::ChannelPopular;
	assert(FloodWaitSeconds("FLOOD_WAIT_30") == 30);
	assert(FloodWaitSeconds("FLOOD_PREMIUM_WAIT_60") == 60);
	assert(FloodWaitSeconds("SLOWMODE_WAIT_4") == 4);
	assert(FloodWaitSeconds("FLOOD_WAIT_0") == 1);
	assert(!FloodWaitSeconds("FLOOD_WAIT_-2"));
	assert(!FloodWaitSeconds("FLOOD_WAIT_10junk"));
	assert(!FloodWaitSeconds("FLOOD_WAIT_"));
	assert(!FloodWaitSeconds("FLOOD_WAIT_999999999999"));
	assert(!FloodWaitSeconds("CHANNEL_PRIVATE"));
	assert(CountReactions({ { 30 }, { 15 }, { 500, true } }) == 45);
	assert(CountReactions({}) == 0);
	assert(CountReactions({ { -5 }, { 9 } }) == 9);
	const auto maximum = std::numeric_limits<std::int64_t>::max();
	assert(CountReactions({ { maximum }, { 1 } }) == maximum);
	auto index = Index(100);
	assert(!index.upsert({ 1, 99, 900, "outside range" }));
	assert(!index.upsert({ -1, 150, 900, "local message" }));
	assert(index.upsert({ 1, 100, 50, "boundary" }));
	assert(index.upsert({ 2, 200, 49, "below threshold" }));
	assert(index.upsert({ 3, 201, 50, "newer" }));
	assert(index.upsert({ 4, 201, 50, "same date" }));
	assert((index.ranked(50) == std::vector<std::int32_t>{ 4, 3, 1 }));
	assert((index.ranked(50, 2) == std::vector<std::int32_t>{ 4, 3 }));
	assert(index.ranked(0, 0).empty());
	assert(index.ranked(51).empty());
	assert(!index.upsert({ 2, 200, 100, "updated" }));
	assert(index.size() == 4);
	assert(index.ranked(50).front() == 2);
	index.erase(2);
	assert(!index.contains(2));
	assert(index.find(2) == nullptr);
	assert(index.upsert({ 5, 210, 0, "no reactions" }));
	assert(index.ranked(0).size() == 4);
	auto otherChannel = Index();
	otherChannel.upsert({ 1, 100, 5, "other channel" });
	assert(otherChannel.find(1)->reactions == 5);
	assert(index.find(1)->reactions == 50);
	assert(AdvancePage(0, { 30, 29, 27 }, false).nextOffset == 27);
	assert(!AdvancePage(30, { 30, 29, 27 }, false).complete);
	assert(!AdvancePage(27, { 27 }, false).complete);
	assert(AdvancePage(27, { 27 }, false).stalled);
	assert(AdvancePage(27, {}, false).complete);
	assert(AdvancePage(30, { 29, 27 }, true).complete);
	const auto lastId = std::numeric_limits<std::int32_t>::max();
	assert(AdvancePage(0, { lastId }, false).nextOffset == lastId);
	assert(!AdvancePage(0, { lastId }, false).complete);
	auto generator = std::mt19937(90210);
	auto stress = Index();
	auto reference = std::unordered_map<std::int32_t, Entry>();
	for (auto step = 0; step < 2000; ++step) {
		const auto id = std::int32_t(generator() % 300 + 1);
		if (generator() % 4 == 0) {
			stress.erase(id);
			reference.erase(id);
		} else {
			const auto entry = Entry{
				id,
				std::int64_t(generator() % 1000),
				std::int64_t(generator() % 100),
				"updated message",
			};
			stress.upsert(entry);
			reference.insert_or_assign(id, entry);
		}
		if (step % 20 == 0) {
			const auto minimum = std::int64_t(generator() % 100);
			auto expected = std::vector<std::int32_t>();
			for (const auto &[key, entry] : reference) {
				if (entry.reactions >= minimum) {
					expected.push_back(key);
				}
			}
			std::sort(expected.begin(), expected.end(), [&](auto a, auto b) {
				const auto &left = reference.at(a);
				const auto &right = reference.at(b);
				return std::tie(left.reactions, left.date, left.id)
					> std::tie(right.reactions, right.date, right.id);
			});
			expected.resize(std::min(expected.size(), std::size_t(51)));
			assert(stress.ranked(minimum, 51) == expected);
			assert(stress.size() == reference.size());
		}
	}
	auto snapshot = CacheSnapshot{
		{ 1234, 5678, false },
		10000,
		10030,
		10,
		false,
		{ { 30, 9000, 500, "Unicode: 缓存 👍" }, { 20, 8000, 50, "boundary" } },
	};
	const auto encoded = EncodeCache(snapshot);
	assert(!encoded.empty());
	const auto restored = DecodeCache(encoded, snapshot.scope, 10000);
	assert(restored && restored->offset == 10 && !restored->complete);
	assert(restored->retryAfter == 10030 && restored->updatedAt == 10000);
	assert(restored->entries.size() == 2);
	assert(restored->entries[0].summary == snapshot.entries[0].summary);
	assert(restored->entries[0].reactions == 500);
	assert(!DecodeCache(encoded, { 1, 5678, false }, 10000));
	assert(!DecodeCache(encoded, { 1234, 1, false }, 10000));
	assert(!DecodeCache(encoded, { 1234, 5678, true }, 10000));
	assert(DecodeCache(encoded, snapshot.scope, 10000 + kCacheLifetime - 1));
	assert(!DecodeCache(encoded, snapshot.scope, 10000 + kCacheLifetime));
	assert(!DecodeCache(encoded, snapshot.scope, 9999));
	assert(!DecodeCache(encoded, snapshot.scope, -1));
	for (auto length = std::size_t(0); length < encoded.size(); ++length) {
		assert(!DecodeCache(std::string_view(encoded).substr(0, length), snapshot.scope, 10000));
	}
	assert(!DecodeCache(encoded + "junk", snapshot.scope, 10000));
	auto corrupt = encoded;
	corrupt[9] = char(2);
	assert(!DecodeCache(corrupt, snapshot.scope, 10000));
	corrupt = encoded;
	for (auto i = 10 + 7 * 8; i < 10 + 8 * 8; ++i) {
		corrupt[i] = char(0xFF);
	}
	assert(!DecodeCache(corrupt, snapshot.scope, 10000));
	snapshot.offset = 31;
	assert(!DecodeCache(EncodeCache(snapshot), snapshot.scope, 10000));
	snapshot.offset = 10;
	snapshot.entries[0].date = 10001;
	assert(DecodeCache(EncodeCache(snapshot), snapshot.scope, 10000));
	snapshot.entries[0].date = 9000;
	snapshot.entries.push_back(snapshot.entries[0]);
	assert(!DecodeCache(EncodeCache(snapshot), snapshot.scope, 10000));
	snapshot.entries.pop_back();
	snapshot.entries[0].summary.resize(kMaxCacheSummary + 1);
	assert(EncodeCache(snapshot).empty());
	snapshot.entries.clear();
	snapshot.offset = 0;
	snapshot.complete = true;
	assert(DecodeCache(EncodeCache(snapshot), snapshot.scope, 10000)->complete);
	snapshot.complete = false;
	assert(!DecodeCache(EncodeCache(snapshot), snapshot.scope, 10000)->complete);
	for (auto step = 0; step < 2000; ++step) {
		auto random = std::string(generator() % 512, char(0));
		for (auto &character : random) {
			character = char(generator());
		}
		if (step % 2 == 0 && random.size() >= 10) {
			random.replace(0, 10, encoded.substr(0, 10));
		}
		DecodeCache(random, snapshot.scope, 10000);
	}
	std::cout << "Channel popular index and cache: all tests passed\n";
}
