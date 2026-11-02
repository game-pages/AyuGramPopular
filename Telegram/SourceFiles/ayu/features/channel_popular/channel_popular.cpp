#include "ayu/features/channel_popular/channel_popular.h"

#include "ayu/features/channel_popular/channel_popular_cache.h"
#include "ayu/features/channel_popular/channel_popular_index.h"
#include "ayu/ayu_settings.h"
#include "base/call_delayed.h"
#include "base/timer.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "data/data_changes.h"
#include "data/data_channel.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "mtproto/mtproto_response.h"
#include "mtproto/sender.h"
#include "storage/cache/storage_cache_database.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/scroll_area.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/painter.h"
#include "ui/ui_utility.h"
#include "window/section_memento.h"
#include "window/section_widget.h"
#include "window/window_adaptive.h"
#include "window/window_session_controller.h"

#include <QtCore/QDateTime>
#include <QtCore/QtEndian>
#include <QtGui/QKeyEvent>
#include <climits>

#include "styles/style_ayu_styles.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_info.h"
#include "styles/style_widgets.h"

namespace Ayu::ChannelPopular {
namespace {

constexpr auto kPageSize = 100;
constexpr auto kVisiblePageSize = 50;
constexpr auto kPageDelay = crl::time(500);
constexpr auto kRenderDelay = crl::time(300);
constexpr auto kSummaryLength = 180;
constexpr auto kCheckpointDelay = crl::time(5000);
constexpr auto kThirtyDays = std::int64_t(30 * 24 * 60 * 60);

class Query final : private MTP::Sender
, public std::enable_shared_from_this<Query> {
public:
	explicit Query(not_null<ChannelData*> channel);
	~Query();

	void open(bool allHistory);
	void start(bool allHistory);
	void resume();
	void stop();
	void remove(std::int32_t id);

	[[nodiscard]] const Index &index() const;
	[[nodiscard]] bool allHistory() const;
	[[nodiscard]] bool running() const;
	[[nodiscard]] bool complete() const;
	[[nodiscard]] QString status() const;
	[[nodiscard]] rpl::producer<> updates() const;

private:
	void requestPage();
	void cacheLoaded(std::uint64_t generation, std::optional<CacheSnapshot> snapshot);
	void cacheChanged();
	void saveCache();
	[[nodiscard]] CacheScope cacheScope() const;
	[[nodiscard]] Storage::Cache::Key cacheKey() const;
	void received(const MTPmessages_Messages &result);
	void failed(const MTP::Error &error);
	void messageUpdated(const Data::MessageUpdate &update);

	const not_null<ChannelData*> _channel;
	Index _index;
	base::Timer _timer;
	base::Timer _saveTimer;
	mtpRequestId _requestId = 0;
	std::int32_t _offset = 0;
	bool _allHistory = false;
	bool _running = false;
	bool _complete = false;
	bool _loading = false;
	bool _cacheDirty = false;
	bool _cacheError = false;
	std::int64_t _cachedAt = 0;
	std::uint64_t _generation = 0;
	std::uint64_t _writeSequence = 0;
	QString _error;
	std::int64_t _retryAt = 0;
	rpl::event_stream<> _updates;
	rpl::lifetime _lifetime;

};

class Row final : public Ui::AbstractButton {
public:
	Row(QWidget *parent, Entry entry);
	QString accessibilityName() override;

protected:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *event) override;

private:
	Entry _entry;

};

class Memento;

class Widget final : public Window::SectionWidget {
public:
	Widget(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		not_null<ChannelData*> channel,
		Window::Column column,
		std::shared_ptr<Query> query);
	~Widget();

	Dialogs::RowDescriptor activeChat() const override;
	bool showInternal(
		not_null<Window::SectionMemento*> memento,
		const Window::SectionShow &params) override;
	std::shared_ptr<Window::SectionMemento> createMemento() override;
	bool floatPlayerHandleWheelEvent(QEvent *event) override;
	QRect floatPlayerAvailableRect() override;

protected:
	void resizeEvent(QResizeEvent *event) override;
	void paintEvent(QPaintEvent *event) override;
	void keyPressEvent(QKeyEvent *event) override;

private:
	void close();
	void updateLayout();
	void updateStatus();
	void rebuildRows();
	void minimumChanged();
	void showMessage(std::int32_t id);

	const not_null<ChannelData*> _channel;
	const Window::Column _column;
	std::shared_ptr<Query> _query;
	object_ptr<Ui::LinkButton> _close;
	object_ptr<Ui::LinkButton> _range;
	object_ptr<Ui::LinkButton> _refresh;
	object_ptr<Ui::LinkButton> _resume;
	object_ptr<Ui::LinkButton> _cancel;
	object_ptr<Ui::InputField> _minimum;
	object_ptr<Ui::FlatLabel> _status;
	object_ptr<Ui::ScrollArea> _scroll;
	QPointer<Ui::VerticalLayout> _rows;
	base::Timer _renderTimer;
	base::Timer _statusTimer;
	std::int64_t _minimumValue = 0;
	std::size_t _visibleLimit = kVisiblePageSize;

};

class Memento final : public Window::SectionMemento {
public:
	Memento(not_null<ChannelData*> channel, std::shared_ptr<Query> query);

	object_ptr<Window::SectionWidget> createWidget(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		Window::Column column,
		const QRect &geometry) override;
	[[nodiscard]] not_null<ChannelData*> channel() const;

private:
	const not_null<ChannelData*> _channel;
	std::shared_ptr<Query> _query;

};

[[nodiscard]] std::int64_t ReactionTotal(const MTPMessageReactions *data) {
	auto values = std::vector<ReactionCount>();
	if (data) {
		for (const auto &reaction : data->data().vresults().v) {
			const auto &value = reaction.data();
			values.push_back({
				value.vcount().v,
				value.vreaction().type() == mtpc_reactionPaid,
			});
		}
	}
	return CountReactions(values);
}

[[nodiscard]] QString MessageSummary(const MTPDmessage &message) {
	const auto text = qs(message.vmessage()).simplified();
	if (!text.isEmpty()) {
		return text.left(kSummaryLength);
	}
	return message.vmedia()
		? tr::ayu_PopularMedia(tr::now)
		: tr::ayu_PopularNoText(tr::now);
}

Query::Query(not_null<ChannelData*> channel)
: MTP::Sender(&channel->session().mtp())
, _channel(channel)
, _timer([=] { requestPage(); })
, _saveTimer([=] { saveCache(); }) {
	using Flag = Data::MessageUpdate::Flag;
	_channel->session().changes().messageUpdates(
		Flag::Edited | Flag::Destroyed
	) | rpl::filter([=](const Data::MessageUpdate &update) {
		return update.item->history()->peer == _channel;
	}) | rpl::on_next([=](const Data::MessageUpdate &update) {
		messageUpdated(update);
	}, _lifetime);
}

Query::~Query() = default;

CacheScope Query::cacheScope() const {
	return {
		_channel->session().uniqueId(),
		peerToChannel(_channel->id).bare,
		_allHistory,
	};
}

Storage::Cache::Key Query::cacheKey() const {
	const auto scope = cacheScope();
	return { scope.account, (scope.channel << 1) | scope.allHistory };
}

void Query::open(bool allHistory) {
	stop();
	_allHistory = allHistory;
	_index = Index(allHistory
		? 0
		: QDateTime::currentSecsSinceEpoch() - kThirtyDays);
	_offset = 0;
	_complete = false;
	_loading = true;
	_cachedAt = 0;
	_cacheError = false;
	_cacheDirty = false;
	_error.clear();
	const auto generation = _generation;
	const auto scope = cacheScope();
	const auto weak = weak_from_this();
	const auto sessionGuard = base::make_weak(&_channel->session());
	_channel->owner().channelPopularCache().get(cacheKey(), [=](QByteArray value) {
		auto snapshot = std::optional<CacheSnapshot>();
		if (value.size() >= 4
			&& qFromBigEndian<quint32>(value.constData()) <= kMaxCacheBytes) {
			const auto decoded = qUncompress(value);
			snapshot = DecodeCache(
				std::string_view(decoded.constData(), decoded.size()),
				scope,
				QDateTime::currentSecsSinceEpoch());
		}
		crl::on_main(sessionGuard, [
			weak,
			generation,
			snapshot = std::move(snapshot)
		]() mutable {
			if (const auto strong = weak.lock()) {
				strong->cacheLoaded(generation, std::move(snapshot));
			}
		});
	});
	_updates.fire({});
}

void Query::cacheLoaded(
		std::uint64_t generation,
		std::optional<CacheSnapshot> snapshot) {
	if (generation != _generation || !_loading) {
		return;
	}
	_loading = false;
	if (!snapshot) {
		start(_allHistory);
		return;
	}
	for (auto &entry : snapshot->entries) {
		_index.upsert(std::move(entry));
	}
	_offset = snapshot->offset;
	_complete = snapshot->complete;
	_cachedAt = snapshot->updatedAt;
	const auto wait = snapshot->retryAfter - QDateTime::currentSecsSinceEpoch();
	_retryAt = std::max<std::int64_t>(
		_retryAt,
		(wait > 0) ? crl::now() + wait * 1000 : crl::time(0));
	if (!_complete) {
		resume();
	}
	_updates.fire({});
}

void Query::cacheChanged() {
	_cacheDirty = true;
	if (!_saveTimer.isActive()) {
		_saveTimer.callOnce(kCheckpointDelay);
	}
}

void Query::saveCache() {
	_saveTimer.cancel();
	if (!_cacheDirty || !_cachedAt) {
		return;
	}
	_cacheDirty = false;
	const auto sequence = ++_writeSequence;
	auto snapshot = CacheSnapshot();
	snapshot.scope = cacheScope();
	snapshot.updatedAt = _cachedAt;
	snapshot.retryAfter = (_retryAt > crl::now())
		? QDateTime::currentSecsSinceEpoch() + (_retryAt - crl::now() + 999) / 1000
		: 0;
	snapshot.offset = _offset;
	snapshot.complete = _complete;
	const auto ids = _index.ranked(0);
	if (ids.size() <= kMaxCacheEntries) {
		snapshot.entries.reserve(ids.size());
		for (const auto id : ids) {
			snapshot.entries.push_back(*_index.find(id));
		}
	}
	const auto encoded = (ids.size() <= kMaxCacheEntries)
		? EncodeCache(snapshot)
		: std::string();
	const auto value = encoded.empty()
		? QByteArray()
		: qCompress(reinterpret_cast<const uchar*>(encoded.data()), encoded.size(), 1);
	if (value.isEmpty()
		|| value.size() > Storage::Cache::Database::Settings().maxDataSize) {
		_cacheError = true;
		return;
	}
	const auto weak = weak_from_this();
	const auto generation = _generation;
	const auto sessionGuard = base::make_weak(&_channel->session());
	_channel->owner().channelPopularCache().put(
		cacheKey(),
		QByteArray(value),
		[=](Storage::Cache::Error error) {
			crl::on_main(sessionGuard, [=] {
				if (const auto strong = weak.lock()) {
					if (generation == strong->_generation
						&& sequence == strong->_writeSequence) {
						strong->_cacheError = error.type != Storage::Cache::Error::Type::None;
						if (strong->_cacheError) {
							strong->_cacheDirty = true;
						}
						strong->_updates.fire({});
					}
				}
			});
		});
}

void Query::start(bool allHistory) {
	stop();
	_allHistory = allHistory;
	_index = Index(allHistory
		? 0
		: QDateTime::currentSecsSinceEpoch() - kThirtyDays);
	_offset = 0;
	_complete = false;
	_cachedAt = 0;
	_cacheError = false;
	_cacheDirty = false;
	_error.clear();
	resume();
}

void Query::resume() {
	if (_running || _loading || _complete) {
		return;
	}
	_running = true;
	_error.clear();
	_timer.callOnce(crl::time(1));
	_updates.fire({});
}

void Query::stop() {
	++_generation;
	_loading = false;
	_timer.cancel();
	if (const auto id = base::take(_requestId)) {
		request(id).cancel();
	}
	_running = false;
	saveCache();
	_updates.fire({});
}

void Query::remove(std::int32_t id) {
	_index.erase(id);
	cacheChanged();
	_updates.fire({});
}

const Index &Query::index() const {
	return _index;
}

bool Query::allHistory() const {
	return _allHistory;
}

bool Query::running() const {
	return _running || _loading;
}

bool Query::complete() const {
	return _complete;
}

QString Query::status() const {
	const auto state = [&]() -> QString {
		if (_loading) {
			return tr::ayu_PopularLoadingCache(tr::now);
		} else if (!_error.isEmpty()) {
			return _error;
		} else if (_running && _retryAt > crl::now()) {
			return tr::ayu_PopularWaiting(
				tr::now,
				lt_count,
				(_retryAt - crl::now() + 999) / 1000);
		}
		return (_complete
			? tr::ayu_PopularComplete
			: _running
			? tr::ayu_PopularScanning
			: tr::ayu_PopularStopped)(
				tr::now,
				lt_count,
				_index.size());
	}();
	auto result = state;
	if (_cachedAt) {
		result += u"\n"_q + tr::ayu_PopularCached(
			tr::now,
			lt_date,
			QDateTime::fromSecsSinceEpoch(_cachedAt)
				.toLocalTime().toString(u"yyyy-MM-dd HH:mm"_q));
	}
	if (_cacheError) {
		result += u"\n"_q + tr::ayu_PopularCacheFailed(tr::now);
	}
	return result;
}

rpl::producer<> Query::updates() const {
	return _updates.events();
}

void Query::requestPage() {
	if (!_running || _requestId) {
		return;
	}
	const auto now = crl::now();
	if (_retryAt > now) {
		_timer.callOnce(std::min(
			crl::time(_retryAt - now),
			crl::time(INT_MAX)));
		return;
	}
	_retryAt = 0;
	const auto generation = _generation;
	_requestId = request(MTPmessages_GetHistory(
		_channel->input(),
		MTP_int(_offset),
		MTP_int(0),
		MTP_int(0),
		MTP_int(kPageSize),
		MTP_int(0),
		MTP_int(0),
		MTP_long(0)
	)).done([=](const MTPmessages_Messages &result) {
		if (generation != _generation) {
			return;
		}
		_requestId = 0;
		received(result);
	}).fail([=](const MTP::Error &error) {
		if (generation != _generation) {
			return;
		}
		_requestId = 0;
		failed(error);
	}).handleFloodErrors().send();
}

void Query::received(const MTPmessages_Messages &result) {
	auto ids = std::vector<std::int32_t>();
	auto reachedCutoff = false;
	auto valid = false;
	result.match([&](const MTPDmessages_messagesNotModified &) {
	}, [&](const auto &data) {
		valid = true;
		for (const auto &message : data.vmessages().v) {
			message.match([&](const MTPDmessage &value) {
				ids.push_back(value.vid().v);
				if (value.vdate().v < _index.cutoff()) {
					reachedCutoff = true;
					return;
				}
				_index.upsert({
					value.vid().v,
					value.vdate().v,
					ReactionTotal(value.vreactions()),
					MessageSummary(value).toStdString(),
				});
			}, [&](const auto &value) {
				ids.push_back(value.vid().v);
			});
		}
	});
	if (!valid) {
		_running = false;
		_error = tr::ayu_PopularFailed(tr::now);
	} else {
		const auto progress = AdvancePage(_offset, ids, reachedCutoff);
		if (progress.stalled) {
			_running = false;
			_error = tr::ayu_PopularFailed(tr::now);
		} else {
			_offset = progress.nextOffset;
			_complete = progress.complete;
			_running = !_complete;
			if (_running) {
				_timer.callOnce(kPageDelay);
			}
		}
	}
	if (valid) {
		_cachedAt = QDateTime::currentSecsSinceEpoch();
		cacheChanged();
		if (!_running) {
			saveCache();
		}
	}
	_updates.fire({});
}

void Query::failed(const MTP::Error &error) {
	const auto wait = FloodWaitSeconds(error.type().toStdString());
	if (wait) {
		_retryAt = crl::now() + crl::time(*wait) * 1000;
		_timer.callOnce(crl::time(1));
		cacheChanged();
		saveCache();
	} else {
		_running = false;
		_error = MTP::IgnoreError(error)
			? QString()
			: tr::ayu_PopularFailed(tr::now);
	}
	_updates.fire({});
}

void Query::messageUpdated(const Data::MessageUpdate &update) {
	const auto item = update.item;
	const auto id = std::int32_t(item->id.bare);
	const auto existing = _index.find(id);
	if (!existing) {
		return;
	}
	if (update.flags & Data::MessageUpdate::Flag::Destroyed) {
		remove(id);
		return;
	}
	auto entry = *existing;
	const auto summary = item->originalText().text.simplified();
	if (!summary.isEmpty()) {
		entry.summary = summary.left(kSummaryLength).toStdString();
	}
	if (entry.reactions == existing->reactions
		&& entry.summary == existing->summary) {
		return;
	}
	_index.upsert(std::move(entry));
	cacheChanged();
	_updates.fire({});
}

Row::Row(QWidget *parent, Entry entry)
: AbstractButton(parent)
, _entry(std::move(entry)) {
	setPointerCursor(true);
	setFocusPolicy(Qt::StrongFocus);
}

QString Row::accessibilityName() {
	return tr::ayu_PopularScore(tr::now, lt_count, _entry.reactions)
		+ u" · "_q
		+ QDateTime::fromSecsSinceEpoch(_entry.date)
			.toLocalTime().toString(u"yyyy-MM-dd HH:mm"_q)
		+ u" · "_q
		+ QString::fromStdString(_entry.summary);
}

int Row::resizeGetHeight(int newWidth) {
	return st::channelPopularRowHeight;
}

void Row::paintEvent(QPaintEvent *event) {
	auto p = Painter(this);
	p.fillRect(event->rect(), isOver() ? st::windowBgOver : st::windowBg);
	const auto padding = st::channelPopularPadding;
	const auto textWidth = std::max(0, width() - 2 * padding);
	p.setFont(st::normalFont);
	p.setPen(st::windowFg);
	const auto summary = QString::fromStdString(_entry.summary);
	const auto elided = st::normalFont->elided(summary, textWidth);
	p.drawTextLeft(padding, padding, width(), elided);
	p.setFont(st::normalFont);
	p.setPen(st::windowSubTextFg);
	const auto date = QDateTime::fromSecsSinceEpoch(_entry.date)
		.toLocalTime().toString(u"yyyy-MM-dd HH:mm"_q);
	p.drawTextLeft(
		padding,
		padding + st::channelPopularTextStep,
		width(),
		date);
	p.setPen(st::windowActiveTextFg);
	p.drawTextLeft(
		padding,
		padding + 2 * st::channelPopularTextStep,
		width(),
		tr::ayu_PopularScore(tr::now, lt_count, _entry.reactions));
	p.fillRect(0, height() - st::lineWidth, width(), st::lineWidth, st::windowBgOver);
}

Widget::Widget(
	QWidget *parent,
	not_null<Window::SessionController*> controller,
	not_null<ChannelData*> channel,
	Window::Column column,
	std::shared_ptr<Query> query)
: Window::SectionWidget(parent, controller)
, _channel(channel)
, _column(column)
, _query(std::move(query))
, _close(this, tr::lng_close(tr::now))
, _range(this, QString())
, _refresh(this, tr::ayu_PopularRefresh(tr::now))
, _resume(this, tr::ayu_PopularResume(tr::now))
, _cancel(this, tr::ayu_PopularCancel(tr::now))
, _minimum(
	this,
	st::defaultInputField,
	tr::ayu_PopularMinimum(),
	QString::number(AyuSettings::getInstance().channelPopularMinimum()))
, _status(this, QString(), st::defaultFlatLabel)
, _scroll(this, st::historyScroll, false)
, _renderTimer([=] { rebuildRows(); })
, _statusTimer([=] { updateStatus(); })
, _minimumValue(AyuSettings::getInstance().channelPopularMinimum()) {
	setAccessibleName(tr::ayu_PopularTitle(tr::now));
	_rows = _scroll->setOwnedWidget(object_ptr<Ui::VerticalLayout>(this));
	_minimum->setMaxLength(10);
	_minimum->changes() | rpl::on_next([=] {
		minimumChanged();
	}, lifetime());
	_close->setClickedCallback([=] { close(); });
	_range->setClickedCallback([=] {
		_visibleLimit = kVisiblePageSize;
		_query->open(!_query->allHistory());
		_scroll->scrollToY(0);
		rebuildRows();
	});
	_refresh->setClickedCallback([=] {
		_query->start(_query->allHistory());
	});
	_resume->setClickedCallback([=] { _query->resume(); });
	_cancel->setClickedCallback([=] { _query->stop(); });
	_query->updates() | rpl::on_next([=] {
		if (!_renderTimer.isActive()) {
			_renderTimer.callOnce(kRenderDelay);
		}
		updateStatus();
	}, lifetime());
	controller->activeChatValue() | rpl::filter([=](Dialogs::Key key) {
		const auto peer = key.peer();
		return peer && peer != _channel;
	}) | rpl::on_next([=] {
		_query->stop();
		if (_column == Window::Column::Third) {
			base::call_delayed(crl::time(1), this, [=] {
				if (controller->activeChatCurrent().peer() != _channel) {
					controller->closeThirdSection();
				}
			});
		}
	}, lifetime());
	Lang::Updated() | rpl::on_next([=] {
		setAccessibleName(tr::ayu_PopularTitle(tr::now));
		_close->setText(tr::lng_close(tr::now));
		_cancel->setText(tr::ayu_PopularCancel(tr::now));
		_refresh->setText(tr::ayu_PopularRefresh(tr::now));
		_resume->setText(tr::ayu_PopularResume(tr::now));
		rebuildRows();
		update();
	}, lifetime());
	_statusTimer.callEach(crl::time(1000));
	rebuildRows();
}

Widget::~Widget() {
	lifetime().destroy();
	_query->stop();
}

Dialogs::RowDescriptor Widget::activeChat() const {
	return { _channel->owner().history(_channel), FullMsgId() };
}

bool Widget::showInternal(
		not_null<Window::SectionMemento*> memento,
		const Window::SectionShow &params) {
	const auto popular = dynamic_cast<Memento*>(memento.get());
	return popular && popular->channel() == _channel;
}

std::shared_ptr<Window::SectionMemento> Widget::createMemento() {
	_query->stop();
	return std::make_shared<Memento>(_channel, _query);
}

bool Widget::floatPlayerHandleWheelEvent(QEvent *event) {
	return _scroll->viewportEvent(event);
}

QRect Widget::floatPlayerAvailableRect() {
	return mapToGlobal(_scroll->geometry());
}

void Widget::close() {
	_query->stop();
	if (_column == Window::Column::Third) {
		controller()->closeThirdSection();
	} else {
		controller()->showBackFromStack();
	}
}

void Widget::resizeEvent(QResizeEvent *event) {
	Window::SectionWidget::resizeEvent(event);
	updateLayout();
}

void Widget::paintEvent(QPaintEvent *event) {
	Window::SectionWidget::paintEvent(event);
	if (animatingShow()) {
		return;
	}
	auto p = Painter(this);
	p.fillRect(event->rect(), st::windowBg);
	p.setFont(st::semiboldFont);
	p.setPen(st::windowFg);
	p.drawTextLeft(
		st::channelPopularPadding,
		st::channelPopularPadding,
		width(),
		tr::ayu_PopularTitle(tr::now));
}

void Widget::keyPressEvent(QKeyEvent *event) {
	if (event->key() == Qt::Key_Escape) {
		close();
	} else {
		Window::SectionWidget::keyPressEvent(event);
	}
}

void Widget::updateLayout() {
	if (width() <= 0 || height() <= 0) {
		return;
	}
	const auto padding = st::channelPopularPadding;
	const auto available = std::max(0, width() - 2 * padding);
	_close->moveToRight(padding, padding);
	_range->moveToLeft(padding, st::channelPopularRangeTop);
	_minimum->resizeToWidth(available);
	_minimum->moveToLeft(padding, st::channelPopularInputTop);
	const auto actionsTop = st::channelPopularInputTop
		+ _minimum->height() + padding;
	_refresh->moveToLeft(padding, actionsTop);
	_resume->moveToLeft(2 * padding + _refresh->width(), actionsTop);
	_cancel->moveToRight(padding, actionsTop);
	const auto statusTop = actionsTop + _refresh->height() + padding;
	_status->resizeToWidth(available);
	_status->moveToLeft(padding, statusTop);
	const auto scrollTop = statusTop + _status->height() + padding;
	_scroll->setGeometry(0, scrollTop, width(), std::max(0, height() - scrollTop));
	_rows->resizeToWidth(width());
}

void Widget::updateStatus() {
	_range->setText((_query->allHistory()
		? tr::ayu_PopularRangeAll
		: tr::ayu_PopularRangeThirty)(tr::now));
	_resume->setVisible(!_query->running() && !_query->complete());
	_cancel->setVisible(_query->running());
	_status->setText(_query->status());
	updateLayout();
}

void Widget::rebuildRows() {
	const auto scrollTop = _scroll->scrollTop();
	auto previous = _scroll->takeWidget<Ui::VerticalLayout>();
	_rows = _scroll->setOwnedWidget(object_ptr<Ui::VerticalLayout>(this));
	previous.destroyDelayed();
	const auto ranked = _query->index().ranked(_minimumValue, _visibleLimit + 1);
	const auto count = std::min(_visibleLimit, ranked.size());
	for (auto i = std::size_t(0); i < count; ++i) {
		const auto id = ranked[i];
		const auto entry = _query->index().find(id);
		const auto row = _rows->add(object_ptr<Row>(_rows, *entry));
		row->setClickedCallback([=] { showMessage(id); });
	}
	if (count < ranked.size()) {
		const auto more = _rows->add(object_ptr<Ui::FlatButton>(
			_rows,
			tr::ayu_PopularMore(tr::now),
			st::historyComposeButton));
		more->setClickedCallback([=] {
			_visibleLimit += kVisiblePageSize;
			rebuildRows();
		});
	} else if (ranked.empty()) {
		_rows->add(object_ptr<Ui::FlatLabel>(
			_rows,
			tr::ayu_PopularEmpty(tr::now),
			st::defaultFlatLabel),
			style::margins(
				st::channelPopularPadding,
				st::channelPopularPadding,
				st::channelPopularPadding,
				st::channelPopularPadding));
	}
	updateStatus();
	_scroll->scrollToY(scrollTop);
}

void Widget::minimumChanged() {
	auto valid = false;
	const auto text = _minimum->getLastText().trimmed();
	const auto value = text.isEmpty() ? 0 : text.toLongLong(&valid);
	if ((!text.isEmpty() && !valid) || value < 0 || value > INT_MAX) {
		_minimum->showErrorNoFocus();
		return;
	}
	_minimum->hideError();
	_minimumValue = value;
	AyuSettings::getInstance().setChannelPopularMinimum(int(value));
	_visibleLimit = kVisiblePageSize;
	_scroll->scrollToY(0);
	rebuildRows();
}

void Widget::showMessage(std::int32_t id) {
	if (_column != Window::Column::Third) {
		_query->stop();
	}
	controller()->showPeerHistory(
		_channel,
		Window::SectionShow::Way::Forward,
		MsgId(id));
}

Memento::Memento(not_null<ChannelData*> channel, std::shared_ptr<Query> query)
: _channel(channel)
, _query(std::move(query)) {
}

object_ptr<Window::SectionWidget> Memento::createWidget(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		Window::Column column,
		const QRect &geometry) {
	if (&controller->session() != &_channel->session()) {
		return nullptr;
	}
	auto result = object_ptr<Widget>(parent, controller, _channel, column, _query);
	result->setGeometry(geometry);
	Ui::SendPendingMoveResizeEvents(result.data());
	return result;
}

not_null<ChannelData*> Memento::channel() const {
	return _channel;
}

}

void Show(
		not_null<Window::SessionController*> controller,
		not_null<ChannelData*> channel) {
	if (!channel->isBroadcast()) {
		return;
	}
	const auto query = std::make_shared<Query>(channel);
	query->open(false);
	const auto memento = std::make_shared<Memento>(channel, query);
	const auto side = controller->canShowThirdSectionWithoutResize();
	if (side) {
		Core::App().settings().setThirdSectionInfoEnabled(true);
	}
	const auto params = side
		? Window::SectionShow().withThirdColumn()
		: Window::SectionShow();
	controller->showSection(memento, params);
}

}
