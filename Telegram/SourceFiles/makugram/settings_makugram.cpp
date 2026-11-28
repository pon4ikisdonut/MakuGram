/*
This file is part of MakuGram,
a custom Telegram Desktop client fork.
*/
#include "makugram/settings_makugram.h"
#include "makugram/currency_converter.h"
#include "makugram/makugram_updater.h"
#include "makugram/makugram_lang.h"
#include "makugram/tg_ws_proxy.h"
#include "boxes/connection_box.h"

#include "makugram/makugram_settings.h"
#include "apiwrap.h"
#include "base/event_filter.h"
#include "core/click_handler_types.h"
#include "data/data_changes.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "settings/sections/settings_main.h"
#include "settings/settings_builder.h"
#include "settings/settings_common_session.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "ui/basic_click_handlers.h"
#include "ui/boxes/confirm_box.h"
#include "ui/empty_userpic.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include <QtCore/QDateTime>
#include <QtCore/QHash>
#include <QtCore/QPointer>
#include <QtCore/QRegularExpression>
#include <QtCore/QSet>
#include <QtGui/QPainterPath>

namespace Settings {
namespace {

using namespace Builder;
using namespace MakuGram;

constexpr auto kBoxContentWidth = 540;
constexpr auto kBoxMaxHeight = 690;

[[nodiscard]] [[maybe_unused]] int CheckboxTextSkip() {
	return st::defaultBoxCheckbox.checkPosition.x()
		+ st::defaultCheck.diameter
		+ st::defaultBoxCheckbox.textPosition.x();
}

[[nodiscard]] const style::RoundButton &TelegramButtonStyle() {
	static auto result = [] {
		auto st = st::defaultActiveButton;
		st.height = 46;
		st.textTop = 13;
		st.style = st::semiboldTextStyle;
		return st;
	}();
	return result;
}

[[nodiscard]] const style::RoundButton &AddPlusButtonStyle() {
	static auto result = [] {
		auto st = st::defaultActiveButton;
		st.height = 36;
		st.width = 36;
		st.textTop = 7;
		st.style = st::semiboldTextStyle;
		return st;
	}();
	return result;
}

[[nodiscard]] const style::RoundButton &YouTubeButtonStyle() {
	static auto ytBg = style::complex_color([] { return QColor(230, 33, 23); });
	static auto ytBgOver = style::complex_color([] { return QColor(200, 25, 16); });
	static auto ripple = style::complex_color([] { return QColor(255, 255, 255, 50); });
	static auto result = [] {
		auto st = st::defaultActiveButton;
		st.height = 46;
		st.textTop = 13;
		st.style = st::semiboldTextStyle;
		st.textFg = st::activeButtonFg;
		st.textFgOver = st::activeButtonFg;
		st.textBg = ytBg.color();
		st.textBgOver = ytBgOver.color();
		st.ripple.color = ripple.color();
		return st;
	}();
	return result;
}

[[nodiscard]] const style::RoundButton &BoostyButtonStyle() {
	static auto boostyBg = style::complex_color([] { return QColor(241, 95, 44); });
	static auto boostyBgOver = style::complex_color([] { return QColor(220, 80, 32); });
	static auto ripple = style::complex_color([] { return QColor(255, 255, 255, 50); });
	static auto result = [] {
		auto st = st::defaultActiveButton;
		st.height = 46;
		st.textTop = 13;
		st.style = st::semiboldTextStyle;
		st.textFg = st::activeButtonFg;
		st.textFgOver = st::activeButtonFg;
		st.textBg = boostyBg.color();
		st.textBgOver = boostyBgOver.color();
		st.ripple.color = ripple.color();
		return st;
	}();
	return result;
}

class MakuGramSection : public Section<MakuGramSection> {
public:
	MakuGramSection(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;

private:
	void setupContent();
};

void AddCheckboxRow(
		not_null<Ui::GenericBox*> box,
		rpl::producer<QString> title,
		bool checked,
		Fn<void(bool)> onChange,
		rpl::producer<QString> about = nullptr,
		int bottomSkip = -1) {
	const auto cb = box->addRow(
		object_ptr<Ui::Checkbox>(
			box,
			std::move(title),
			checked,
			st::defaultBoxCheckbox),
		style::margins(
			st::boxRowPadding.left(),
			4,
			st::boxRowPadding.right(),
			about ? 0 : (bottomSkip >= 0 ? bottomSkip : st::boxMediumSkip)),
		style::al_justify);
	cb->setCursor(style::cur_pointer);
	cb->setAllowTextLines(0);
	cb->setCheckAlignment(style::al_topleft);
	cb->checkedChanges(
	) | rpl::on_next([=](bool checked) {
		onChange(checked);
	}, cb->lifetime());

	if (about) {
		const auto label = box->addRow(
			object_ptr<Ui::FlatLabel>(
				box,
				std::move(about),
				st::boxDividerLabel),
			style::margins(
				st::boxRowPadding.left(),
				9,
				st::boxRowPadding.right(),
				bottomSkip >= 0 ? bottomSkip : st::boxMediumSkip),
			style::al_justify);
		label->setCursor(style::cur_pointer);
		base::install_event_filter(label, [=](not_null<QEvent*> e) {
			if (e->type() == QEvent::MouseButtonPress) {
				cb->setChecked(!cb->checked());
				return base::EventFilterResult::Cancel;
			}
			return base::EventFilterResult::Continue;
		});
	}
}

void ShowSpamFilterBox(not_null<Window::SessionController*> controller) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setWidth(kBoxContentWidth);
		box->setMaxHeight(kBoxMaxHeight);
		box->setTitle(Tr::Value(
			QString::fromUtf8("Локальный спам-фильтр"),
			u"Local Spam Filter"_q));

		box->addRow(
			object_ptr<Ui::FlatLabel>(
				box,
				Tr::Value(
					QString::fromUtf8(
						"Введите скрываемые слова через запятую или с новой строки.\n"
						"Поддерживается звёздочка * (например: кошк* скроет \"кошка\", \"кошку\", \"кошки\" и т.д.):"),
					u"Enter filtered words comma-separated or one per line.\n"
					"Asterisk * wildcard is supported (e.g.: cat* will hide \"cat\", \"cats\", etc.):"_q),
				st::boxLabel),
			st::boxRowPadding);

		const auto field = box->addRow(
			object_ptr<Ui::InputField>(
				box,
				st::supportInfoField,
				Ui::InputField::Mode::MultiLine,
				Tr::Value(
					QString::fromUtf8("Введите слова или шаблоны (*)..."),
					u"Enter words or patterns (*)..."_q),
				MakuGram::Settings::instance().spamKeywords()),
			st::boxRowPadding);
		field->setFocus();
		field->setMinHeight(130);

		const auto excludeSearch = box->addRow(
			object_ptr<Ui::Checkbox>(
				box,
				Tr::Value(
					QString::fromUtf8("Исключить скрываемые слова из поиска"),
					u"Exclude hidden words from search"_q),
				MakuGram::Settings::instance().filterSpamInSearch(),
				st::defaultBoxCheckbox),
			style::margins(
				st::boxRowPadding.left(),
				st::boxMediumSkip,
				st::boxRowPadding.right(),
				2),
			style::al_justify);
		excludeSearch->setCursor(style::cur_pointer);
		excludeSearch->setAllowTextLines(0);
		excludeSearch->setCheckAlignment(style::al_topleft);

		const auto excludeAbout = box->addRow(
			object_ptr<Ui::FlatLabel>(
				box,
				Tr::Value(
					QString::fromUtf8("Сообщения, содержащие скрываемые слова, не будут отображаться в результатах глобального поиска и поиска по чату."),
					u"Messages containing hidden words will not appear in global and chat search results."_q),
				st::boxDividerLabel),
			style::margins(
				st::boxRowPadding.left(),
				9,
				st::boxRowPadding.right(),
				st::boxMediumSkip),
			style::al_justify);
		excludeAbout->setCursor(style::cur_pointer);
		base::install_event_filter(excludeAbout, [=](not_null<QEvent*> e) {
			if (e->type() == QEvent::MouseButtonPress) {
				excludeSearch->setChecked(!excludeSearch->checked());
				return base::EventFilterResult::Cancel;
			}
			return base::EventFilterResult::Continue;
		});

		box->addButton(tr::lng_settings_save(), [=] {
			MakuGram::Settings::instance().setSpamKeywords(field->getLastText());
			MakuGram::Settings::instance().setFilterSpamInSearch(excludeSearch->checked());
			box->closeBox();
			controller->showToast(Tr::Get(
				QString::fromUtf8("Настройки сохранены"),
				u"Settings saved"_q));
		});
		box->addButton(tr::lng_cancel(), [=] {
			box->closeBox();
		});
	}));
}

struct HiddenUsersBoxConfig {
	rpl::producer<QString> title;
	rpl::producer<QString> description;
	bool showEnableToggle = false;
	rpl::producer<QString> enableToggleTitle;
	Fn<bool()> getEnabled;
	Fn<void(bool)> setEnabled;
	Fn<std::vector<HiddenUserEntry>()> getEntries;
	Fn<void(const QString &)> addEntry;
	Fn<void(const QString &)> removeEntry;
	Fn<rpl::producer<bool>()> changes;
};

[[nodiscard]] QString NormalizeUserInputIdentifier(const QString &raw) {
	auto clean = raw.trimmed();
	if (clean.startsWith(u"https://t.me/"_q, Qt::CaseInsensitive)) {
		clean = clean.mid(13);
	} else if (clean.startsWith(u"http://t.me/"_q, Qt::CaseInsensitive)) {
		clean = clean.mid(12);
	} else if (clean.startsWith(u"t.me/"_q, Qt::CaseInsensitive)) {
		clean = clean.mid(5);
	}
	if (clean.startsWith('@')) {
		clean = clean.mid(1);
	}
	const auto slash = clean.indexOf('/');
	if (slash > 0) {
		clean = clean.left(slash);
	}
	clean = clean.trimmed();
	if (clean.isEmpty()) {
		return QString();
	}
	bool ok = false;
	if (const auto id = clean.toULongLong(&ok); ok && id > 0) {
		return QString::number(id);
	}
	static const auto kUsernameRx = QRegularExpression(u"^[a-zA-Z0-9_]{3,64}$"_q);
	if (kUsernameRx.match(clean).hasMatch()) {
		return '@' + clean.toLower();
	}
	return QString();
}

[[nodiscard]] PeerData *ResolveLocalPeer(
		not_null<Main::Session*> session,
		const QString &identifier) {
	auto clean = identifier.trimmed().toLower();
	if (clean.startsWith('@')) {
		clean.remove(0, 1);
	}
	bool ok = false;
	if (const auto rawId = clean.toULongLong(&ok); ok && rawId > 0) {
		if (const auto user = session->data().userLoaded(UserId(rawId))) {
			return user;
		}
		return session->data().peerLoaded(PeerId(rawId));
	}
	return session->data().peerByUsername(clean);
}

void ShowHiddenUsersManagerBox(
		not_null<Window::SessionController*> controller,
		HiddenUsersBoxConfig config) {
	const auto sharedConfig = std::make_shared<HiddenUsersBoxConfig>(std::move(config));
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setWidth(kBoxContentWidth);
		box->setMaxHeight(kBoxMaxHeight);
		box->setTitle(std::move(sharedConfig->title));

		box->addRow(
			object_ptr<Ui::FlatLabel>(
				box,
				std::move(sharedConfig->description),
				st::boxDividerLabel),
			style::margins(
				st::boxRowPadding.left(),
				4,
				st::boxRowPadding.right(),
				st::boxMediumSkip),
			style::al_justify);

		if (sharedConfig->showEnableToggle) {
			AddCheckboxRow(
				box,
				std::move(sharedConfig->enableToggleTitle),
				sharedConfig->getEnabled(),
				[=](bool checked) {
					sharedConfig->setEnabled(checked);
				});
		}

		const auto inputRow = box->addRow(
			object_ptr<Ui::RpWidget>(box),
			style::margins(
				st::boxRowPadding.left(),
				0,
				st::boxRowPadding.right(),
				st::boxMediumSkip));
		const auto field = Ui::CreateChild<Ui::InputField>(
			inputRow,
			st::defaultInputField,
			Tr::Value(
				QString::fromUtf8("Введите @username или user_id..."),
				u"Enter @username or user_id..."_q));
		field->setFocus();

		const auto addBtn = Ui::CreateChild<Ui::RoundButton>(
			inputRow,
			rpl::single(u"+"_q),
			AddPlusButtonStyle());
		addBtn->setFullRadius(true);

		constexpr auto kAddBtnSize = 36;
		const auto layoutInputRow = [=] {
			const auto w = inputRow->width();
			if (w <= 0) {
				return;
			}
			const auto fieldWidth = std::max(80, w - kAddBtnSize - 10);
			field->resizeToWidth(fieldWidth);
			const auto rowHeight = std::max(field->height(), kAddBtnSize);
			inputRow->resize(w, rowHeight);
			field->moveToLeft(0, std::max(0, (rowHeight - field->height()) / 2));
			addBtn->resize(kAddBtnSize, kAddBtnSize);
			addBtn->moveToRight(0, std::max(0, (rowHeight - kAddBtnSize) / 2));
		};
		inputRow->widthValue() | rpl::on_next([=](int) {
			layoutInputRow();
		}, inputRow->lifetime());
		field->heightValue() | rpl::on_next([=](int) {
			layoutInputRow();
		}, inputRow->lifetime());
		layoutInputRow();

		const auto cardsContainer = box->addRow(
			object_ptr<Ui::VerticalLayout>(box),
			QMargins());

		const auto weakBox = QPointer<Ui::GenericBox>(box);
		const auto weakCards = QPointer<Ui::VerticalLayout>(cardsContainer);
		const auto resolveAttempted = std::make_shared<QSet<QString>>();
		const auto rebuildPtr = std::make_shared<Fn<void()>>();

		const auto session = &controller->session();
		const auto requestResolveUsername = [=](const QString &usernameNoAt) {
			const auto lower = usernameNoAt.toLower();
			if (lower.isEmpty() || resolveAttempted->contains(lower)) {
				return;
			}
			resolveAttempted->insert(lower);
			session->api().request(MTPcontacts_ResolveUsername(
				MTP_flags(0),
				MTP_string(lower),
				MTP_string(QString())
			)).done([=](const MTPcontacts_ResolvedPeer &result) {
				result.match([&](const MTPDcontacts_resolvedPeer &data) {
					session->data().processUsers(data.vusers());
					session->data().processChats(data.vchats());
				});
				if (weakBox && rebuildPtr && *rebuildPtr) {
					(*rebuildPtr)();
				}
			}).send();
		};

		*rebuildPtr = [=] {
			const auto container = weakCards.data();
			if (!container) {
				return;
			}
			container->clear();
			const auto entries = sharedConfig->getEntries();
			if (entries.empty()) {
				container->add(
					object_ptr<Ui::FlatLabel>(
						container,
						Tr::Value(
							QString::fromUtf8("Список пуст. Добавьте пользователя по @username или user_id выше."),
							u"The list is empty. Add a user by @username or user_id above."_q),
						st::boxDividerLabel),
					style::margins(
						st::boxRowPadding.left(),
						8,
						st::boxRowPadding.right(),
						12));
				if (container->width() > 0) {
					container->resizeToWidth(container->width());
				}
				return;
			}

			struct CardState {
				Ui::PeerUserpicView userpic;
			};

			for (const auto &entry : entries) {
				const auto idText = entry.identifier;
				auto cleanKey = idText.trimmed().toLower();
				if (cleanKey.startsWith('@')) {
					cleanKey.remove(0, 1);
				}
				bool ok = false;
				const auto numericId = cleanKey.toULongLong(&ok);
				const auto isNumeric = (ok && numericId > 0);
				if (!isNumeric && !ResolveLocalPeer(session, idText)) {
					requestResolveUsername(cleanKey);
				}

				const auto card = container->add(
					object_ptr<Ui::RpWidget>(container),
					style::margins(
						st::boxRowPadding.left(),
						0,
						st::boxRowPadding.right(),
						8));
				const auto state = card->lifetime().make_state<CardState>();
				constexpr auto kCardHeight = 66;
				card->resize(card->width(), kCardHeight);

				const auto removeBtn = Ui::CreateChild<Ui::IconButton>(
					card,
					st::boxTitleClose);
				removeBtn->setCursor(style::cur_pointer);
				removeBtn->setClickedCallback([=] {
					sharedConfig->removeEntry(idText);
				});

				card->sizeValue() | rpl::on_next([=](QSize s) {
					removeBtn->moveToRight(
						6,
						std::max(0, (s.height() - removeBtn->height()) / 2));
				}, card->lifetime());

				card->paintRequest() | rpl::on_next([=] {
					auto p = QPainter(card);
					auto hq = PainterHighQualityEnabler(p);

					QPainterPath path;
					path.addRoundedRect(
						QRectF(0, 0, card->width(), card->height()),
						st::roundRadiusLarge,
						st::roundRadiusLarge);
					p.fillPath(path, st::boxDividerBg);

					constexpr auto kAvatarSize = 42;
					constexpr auto kAvatarLeft = 12;
					const auto avatarTop = (card->height() - kAvatarSize) / 2;
					const auto peer = ResolveLocalPeer(session, idText);

					if (peer) {
						peer->paintRawUserpic(p, state->userpic, {
							.position = QPoint(kAvatarLeft, avatarTop),
							.size = kAvatarSize,
							.shape = Ui::PeerUserpicShape::Circle,
						});
					} else {
						const auto fallbackLetter = cleanKey.isEmpty()
							? u"?"_q
							: cleanKey.left(1).toUpper();
						Ui::EmptyUserpic(
							Ui::EmptyUserpic::UserpicColor(qHash(cleanKey) % 8),
							fallbackLetter).paintCircle(
								p,
								kAvatarLeft,
								avatarTop,
								kAvatarSize,
								kAvatarSize);
					}

					const auto textLeft = kAvatarLeft + kAvatarSize + 12;
					const auto textRight = card->width() - removeBtn->width() - 14;
					const auto textWidth = std::max(40, textRight - textLeft);

					QString titleLine;
					QString subLine;
					if (peer) {
						titleLine = peer->rawName().trimmed();
						if (titleLine.isEmpty()) {
							titleLine = idText;
						}
						const auto uname = peer->rawUsername().trimmed();
						const auto rawPeerId = peer->id.value & PeerId::kChatTypeMask;
						if (!uname.isEmpty()) {
							subLine = u"@%1 · ID: %2"_q.arg(uname).arg(rawPeerId);
						} else {
							subLine = u"ID: %1"_q.arg(rawPeerId);
						}
					} else {
						titleLine = isNumeric
							? Tr::Get(QString::fromUtf8("Пользователь %1").arg(cleanKey), u"User %1"_q.arg(cleanKey))
							: idText;
						subLine = isNumeric
							? u"ID: %1"_q.arg(cleanKey)
							: idText;
					}

					const auto dateValue = (entry.addedAt > 0)
						? QDateTime::fromSecsSinceEpoch(entry.addedAt).toString(u"dd.MM.yyyy"_q)
						: Tr::Get(QString::fromUtf8("ранее"), u"earlier"_q);
					const auto dateLine = Tr::Get(
						QString::fromUtf8("Добавлено: %1").arg(dateValue),
						u"Added: %1"_q.arg(dateValue));

					const auto &titleFont = st::semiboldTextStyle.font;
					const auto &subFont = st::defaultTextStyle.font;
					const auto &dateFont = st::boxDividerLabel.style.font;

					p.setPen(st::windowBoldFg);
					p.setFont(titleFont);
					p.drawText(
						textLeft,
						10 + titleFont->ascent,
						titleFont->elided(titleLine, textWidth));

					p.setPen(st::windowSubTextFg);
					p.setFont(subFont);
					p.drawText(
						textLeft,
						29 + subFont->ascent,
						subFont->elided(subLine, textWidth));

					p.setFont(dateFont);
					p.drawText(
						textLeft,
						47 + dateFont->ascent,
						dateFont->elided(dateLine, textWidth));
				}, card->lifetime());
			}

			if (container->width() > 0) {
				container->resizeToWidth(container->width());
			}
		};

		const auto tryAdd = [=] {
			const auto normalized = NormalizeUserInputIdentifier(field->getLastText());
			if (normalized.isEmpty()) {
				field->showError();
				return;
			}
			auto cleanTarget = normalized.toLower();
			if (cleanTarget.startsWith('@')) {
				cleanTarget.remove(0, 1);
			}
			for (const auto &existing : sharedConfig->getEntries()) {
				auto cleanExisting = existing.identifier.toLower();
				if (cleanExisting.startsWith('@')) {
					cleanExisting.remove(0, 1);
				}
				if (cleanExisting == cleanTarget) {
					controller->showToast(Tr::Get(
						QString::fromUtf8("Пользователь уже есть в списке"),
						u"User is already in the list"_q));
					field->setText(QString());
					return;
				}
			}
			sharedConfig->addEntry(normalized);
			field->setText(QString());
			field->setFocus();
			if (!normalized.startsWith('@')) {
				return;
			}
			if (!ResolveLocalPeer(session, normalized)) {
				requestResolveUsername(normalized.mid(1));
			}
		};

		addBtn->setClickedCallback(tryAdd);
		field->submits() | rpl::on_next(tryAdd, field->lifetime());

		sharedConfig->changes() | rpl::on_next([=] {
			if (*rebuildPtr) {
				(*rebuildPtr)();
			}
		}, box->lifetime());

		const auto updateCards = [=] {
			if (const auto container = weakCards.data()) {
				for (const auto child : container->children()) {
					if (const auto w = qobject_cast<QWidget*>(child)) {
						w->update();
					}
				}
			}
		};

		session->downloaderTaskFinished() | rpl::on_next([=] {
			updateCards();
		}, box->lifetime());

		session->changes().peerUpdates(
			Data::PeerUpdate::Flag::Name
				| Data::PeerUpdate::Flag::Photo
				| Data::PeerUpdate::Flag::Username
		) | rpl::on_next([=] {
			updateCards();
		}, box->lifetime());

		(*rebuildPtr)();

		box->addButton(tr::lng_close(), [=] {
			box->closeBox();
		});
	}));
}

void ShowHiddenUsersBox(not_null<Window::SessionController*> controller) {
	ShowHiddenUsersManagerBox(controller, {
		.title = Tr::Value(
			QString::fromUtf8("Скрытые пользователи"),
			u"Hidden Users"_q),
		.description = Tr::Value(
			QString::fromUtf8(
				"Сообщения от этих пользователей будут отображаться с именем «Скрытый пользователь», с аватаркой удалённого аккаунта, а текст и медиа будут спрятаны под спойлер (без блокировки в Telegram)."),
			u"Messages from these users will appear as 'Hidden User' with a deleted account avatar, and their text and media will be hidden under a spoiler (without blocking them in Telegram)."_q),
		.showEnableToggle = true,
		.enableToggleTitle = Tr::Value(
			QString::fromUtf8("Включить скрытие сообщений указанных пользователей"),
			u"Enable hiding messages from specified users"_q),
		.getEnabled = [] {
			return MakuGram::Settings::instance().hiddenUsersEnabled();
		},
		.setEnabled = [](bool checked) {
			MakuGram::Settings::instance().setHiddenUsersEnabled(checked);
		},
		.getEntries = [] {
			return MakuGram::Settings::instance().hiddenUsersEntries();
		},
		.addEntry = [](const QString &id) {
			MakuGram::Settings::instance().addHiddenUser(id);
		},
		.removeEntry = [](const QString &id) {
			MakuGram::Settings::instance().removeHiddenUser(id);
		},
		.changes = [] {
			return MakuGram::Settings::instance().hiddenUsersChanges();
		},
	});
}

void ShowFullHiddenUsersBox(not_null<Window::SessionController*> controller) {
	ShowHiddenUsersManagerBox(controller, {
		.title = Tr::Value(
			QString::fromUtf8("Полностью скрытые пользователи"),
			u"Full-Hidden Users"_q),
		.description = Tr::Value(
			QString::fromUtf8(
				"Эти пользователи полностью исчезнут из списка чатов, поиска и списка участников, а их сообщения, статусы набора текста и уведомления вообще не будут отображаться (без блокировки в Telegram)."),
			u"These users will completely disappear from the chat list, search, and member lists, and their messages, typing indicators, and notifications will not be displayed at all (without blocking them in Telegram)."_q),
		.showEnableToggle = false,
		.getEntries = [] {
			return MakuGram::Settings::instance().fullHiddenUsersEntries();
		},
		.addEntry = [](const QString &id) {
			MakuGram::Settings::instance().addFullHiddenUser(id);
		},
		.removeEntry = [](const QString &id) {
			MakuGram::Settings::instance().removeFullHiddenUser(id);
		},
		.changes = [] {
			return MakuGram::Settings::instance().fullHiddenUsersChanges();
		},
	});
}

void ShowMessagesBox(not_null<Window::SessionController*> controller) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setWidth(kBoxContentWidth);
		box->setMaxHeight(kBoxMaxHeight);
		box->setTitle(Tr::Value(
			QString::fromUtf8("Сообщения"),
			u"Messages"_q));

		AddCheckboxRow(
			box,
			Tr::Value(
				QString::fromUtf8("Кнопка \"Только непрочитанные\" над чатами"),
				u"\"Unread only\" button above chats"_q),
			MakuGram::Settings::instance().showUnreadFilterButton(),
			[=](bool checked) {
				MakuGram::Settings::instance().setShowUnreadFilterButton(checked);
			},
			Tr::Value(
				QString::fromUtf8("Отображает кнопку над списком чатов для быстрого показа только непрочитанных диалогов в один клик."),
				u"Displays a button above the chat list to quickly show only unread dialogs in one click."_q));

		AddCheckboxRow(
			box,
			Tr::Value(
				QString::fromUtf8("Переслать без автора"),
				u"Forward without author"_q),
			MakuGram::Settings::instance().forwardWithoutAuthor(),
			[=](bool checked) {
				MakuGram::Settings::instance().setForwardWithoutAuthor(checked);
			},
			Tr::Value(
				QString::fromUtf8("Добавляет пункт \"Переслать без автора\" в контекстное меню сообщений."),
				u"Adds a \"Forward without author\" item to the message context menu."_q));

		AddCheckboxRow(
			box,
			Tr::Value(
				QString::fromUtf8("Увеличить лимит выделяемых сообщений с 100 до 1000"),
				u"Increase message selection limit from 100 to 1000"_q),
			MakuGram::Settings::instance().increaseMessageSelectLimit(),
			[=](bool checked) {
				MakuGram::Settings::instance().setIncreaseMessageSelectLimit(checked);
			},
			Tr::Value(
				QString::fromUtf8("Позволяет одновременно выделять до 1000 сообщений в чатах и медиа для их пересылки или удаления."),
				u"Allows selecting up to 1000 messages in chats and media at once for forwarding or deleting."_q));

		AddCheckboxRow(
			box,
			Tr::Value(
				QString::fromUtf8("Точное время сообщений (с секундами)"),
				u"Exact message time (with seconds)"_q),
			MakuGram::Settings::instance().showExactTime(),
			[=](bool checked) {
				MakuGram::Settings::instance().setShowExactTime(checked);
			},
			Tr::Value(
				QString::fromUtf8("Отображать время отправленных сообщений с точностью до секунды (ЧЧ:ММ:СС)."),
				u"Display message sent time with second precision (HH:MM:SS)."_q));

		AddCheckboxRow(
			box,
			Tr::Value(
				QString::fromUtf8("Локальный спам-фильтр по ключевым словам"),
				u"Local keyword spam filter"_q),
			MakuGram::Settings::instance().spamFilterEnabled(),
			[=](bool checked) {
				MakuGram::Settings::instance().setSpamFilterEnabled(checked);
			},
			Tr::Value(
				QString::fromUtf8("Скрывать сообщения со скрываемыми словами в чатах и в личных сообщениях."),
				u"Hide messages containing filtered words in chats and direct messages."_q),
			0);

		const auto spamSetupBtn = box->addRow(
			object_ptr<Ui::RoundButton>(
				box,
				Tr::Value(
					QString::fromUtf8("Настроить скрываемые слова..."),
					u"Configure hidden words..."_q),
				st::defaultBoxButton),
			style::margins(
				st::boxRowPadding.left(),
				9,
				st::boxRowPadding.right(),
				st::boxMediumSkip));
		spamSetupBtn->setClickedCallback([=] {
			ShowSpamFilterBox(controller);
		});

		AddCheckboxRow(
			box,
			Tr::Value(
				QString::fromUtf8("Конвертер валют"),
				u"Currency converter"_q),
			MakuGram::Settings::instance().currencyConverter(),
			[=](bool checked) {
				MakuGram::Settings::instance().setCurrencyConverter(checked);
			},
			Tr::Value(
				QString::fromUtf8("Автоматически подсвечивает суммы в сообщениях (например, \"15 рублей\", \"10M IDR\", \"100$\") и показывает их конвертацию при наведении или клике."),
				u"Automatically detects amounts in messages (e.g. \"15 rubles\", \"10M IDR\", \"100$\") and shows conversion on hover or click."_q),
			0);

		const auto curSetupBtn = box->addRow(
			object_ptr<Ui::RoundButton>(
				box,
				Tr::Value(
					QString::fromUtf8("Выбрать целевые валюты..."),
					u"Select target currencies..."_q),
				st::defaultBoxButton),
			style::margins(
				st::boxRowPadding.left(),
				9,
				st::boxRowPadding.right(),
				st::boxMediumSkip));
		curSetupBtn->setClickedCallback([=] {
			MakuGram::ShowCurrencyTargetsBox(controller);
		});

		box->addButton(tr::lng_close(), [=] {
			box->closeBox();
		});
	}));
}

void ShowPrivacyBox(not_null<Window::SessionController*> controller) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setWidth(kBoxContentWidth);
		box->setMaxHeight(kBoxMaxHeight);
		box->setTitle(Tr::Value(
			QString::fromUtf8("Приватность и безопасность"),
			u"Privacy & Security"_q));

		AddCheckboxRow(
			box,
			Tr::Value(
				QString::fromUtf8("Режим призрака"),
				u"Ghost Mode"_q),
			MakuGram::Settings::instance().ghostMode(),
			[=](bool checked) {
				MakuGram::Settings::instance().setGhostMode(checked);
			},
			Tr::Value(
				QString::fromUtf8("Полная невидимость: скрывает онлайн, статус набора текста и выбора стикеров, запрещает отмечать входящие сообщения и истории прочитанными."),
				u"Complete invisibility: hides online status, typing and sticker choosing, prevents marking messages and stories as read."_q));

		AddCheckboxRow(
			box,
			Tr::Value(
				QString::fromUtf8("Не передавать статус «В сети»"),
				u"Do not send online status"_q),
			MakuGram::Settings::instance().ghostHideOnline(),
			[=](bool checked) {
				MakuGram::Settings::instance().setGhostHideOnline(checked);
			},
			Tr::Value(
				QString::fromUtf8("Скрывает ваше присутствие в сети (для всех отображается статус «был(а) недавно»)."),
				u"Hides your online presence (always shows 'last seen recently')."_q));

		AddCheckboxRow(
			box,
			Tr::Value(
				QString::fromUtf8("Не отмечать сообщения прочитанными"),
				u"Do not mark messages as read"_q),
			MakuGram::Settings::instance().ghostMarkRead(),
			[=](bool checked) {
				MakuGram::Settings::instance().setGhostMarkRead(checked);
			},
			Tr::Value(
				QString::fromUtf8("Входящие сообщения остаются непрочитанными на сервере, собеседник не видит галочки о прочтении."),
				u"Incoming messages remain unread on the server, the sender will not see read checkmarks."_q));

		AddCheckboxRow(
			box,
			Tr::Value(
				QString::fromUtf8("Анонимный просмотр историй"),
				u"Anonymous story viewing"_q),
			MakuGram::Settings::instance().ghostHideStoriesView(),
			[=](bool checked) {
				MakuGram::Settings::instance().setGhostHideStoriesView(checked);
			},
			Tr::Value(
				QString::fromUtf8("Просматривайте истории без отметки и без попадания в список просмотревших."),
				u"View stories without marking them as viewed and without appearing in viewers list."_q));

		AddCheckboxRow(
			box,
			Tr::Value(
				QString::fromUtf8("Не передавать статус набора текста"),
				u"Do not send typing status"_q),
			MakuGram::Settings::instance().hideTypingStatus(),
			[=](bool checked) {
				MakuGram::Settings::instance().setHideTypingStatus(checked);
			},
			Tr::Value(
				QString::fromUtf8("Собеседники не будут видеть, когда вы печатаете текст или записываете голосовое/видеосообщение."),
				u"Others will not see when you are typing or recording voice/video messages."_q));

		AddCheckboxRow(
			box,
			Tr::Value(
				QString::fromUtf8("Разрешить копирование и сохранение контента"),
				u"Allow copying and saving protected content"_q),
			MakuGram::Settings::instance().bypassRestrictedContent(),
			[=](bool checked) {
				MakuGram::Settings::instance().setBypassRestrictedContent(checked);
			},
			Tr::Value(
				QString::fromUtf8("Снимает ограничения на пересылку, сохранение медиа, копирование текста и скриншоты в защищённых каналах и группах (при пересылке защищённых сообщений они копируются с сохранением текста, медиа и указанием автора в конце)."),
				u"Removes restrictions on forwarding, saving media, copying text, and screenshots in protected channels and groups."_q));

		AddCheckboxRow(
			box,
			Tr::Value(
				QString::fromUtf8("Массовое удаление сообщений за период"),
				u"Mass delete messages by period"_q),
			MakuGram::Settings::instance().quickMassDeleteMyMessages(),
			[=](bool checked) {
				MakuGram::Settings::instance().setQuickMassDeleteMyMessages(checked);
			},
			Tr::Value(
				QString::fromUtf8("Добавляет в меню диалога пункт для быстрого удаления сообщений за сегодня, неделю, месяц или всё время."),
				u"Adds an option to chat menu for quickly deleting messages for today, week, month, or all time."_q));

		AddCheckboxRow(
			box,
			Tr::Value(
				QString::fromUtf8("Скрывать сообщения от заблокированных"),
				u"Hide messages from blocked users"_q),
			MakuGram::Settings::instance().hideMessagesFromBlocked(),
			[=](bool checked) {
				MakuGram::Settings::instance().setHideMessagesFromBlocked(checked);
			},
			Tr::Value(
				QString::fromUtf8("Не отображать сообщения заблокированных пользователей в общих чатах и группах."),
				u"Do not display messages from blocked users in public and group chats."_q));

		AddCheckboxRow(
			box,
			Tr::Value(
				QString::fromUtf8("Скрытие сообщений определенных пользователей"),
				u"Hide messages from specific users"_q),
			MakuGram::Settings::instance().hiddenUsersEnabled(),
			[=](bool checked) {
				MakuGram::Settings::instance().setHiddenUsersEnabled(checked);
			},
			Tr::Value(
				QString::fromUtf8("Скрывает никнейм («Скрытый пользователь»), аватарку и прячет текст/медиа под спойлер для пользователей из списка (без блокировки в Telegram)."),
				u"Hides nickname ('Hidden User'), avatar, and puts text/media under spoiler for users from the list (without blocking them in Telegram)."_q),
			0);

		const auto hiddenUsersBtn = box->addRow(
			object_ptr<Ui::RoundButton>(
				box,
				Tr::Value(
					QString::fromUtf8("Настроить скрытых пользователей..."),
					u"Configure hidden users..."_q),
				st::defaultBoxButton),
			style::margins(
				st::boxRowPadding.left(),
				9,
				st::boxRowPadding.right(),
				st::boxMediumSkip));
		hiddenUsersBtn->setClickedCallback([=] {
			ShowHiddenUsersBox(controller);
		});

		AddCheckboxRow(
			box,
			Tr::Value(
				QString::fromUtf8("Полное скрытие пользователей (исчезновение)"),
				u"Full hide users (vanish)"_q),
			MakuGram::Settings::instance().fullHiddenUsersEnabled(),
			[=](bool checked) {
				MakuGram::Settings::instance().setFullHiddenUsersEnabled(checked);
			},
			Tr::Value(
				QString::fromUtf8("Полностью убирает выбранных пользователей из списка чатов, поиска и участников, а их сообщения и уведомления вообще не отображает (без блокировки в Telegram)."),
				u"Completely removes selected users from the chat list, search, and member lists, and hides all their messages and notifications (without blocking them in Telegram)."_q),
			0);

		const auto fullHiddenUsersBtn = box->addRow(
			object_ptr<Ui::RoundButton>(
				box,
				Tr::Value(
					QString::fromUtf8("Настроить полностью скрытых пользователей..."),
					u"Configure full-hidden users..."_q),
				st::defaultBoxButton),
			style::margins(
				st::boxRowPadding.left(),
				9,
				st::boxRowPadding.right(),
				st::boxMediumSkip));
		fullHiddenUsersBtn->setClickedCallback([=] {
			ShowFullHiddenUsersBox(controller);
		});

		AddCheckboxRow(
			box,
			Tr::Value(
				QString::fromUtf8("Спрятать номер телефона из профиля и настроек"),
				u"Hide phone number in profile and settings"_q),
			MakuGram::Settings::instance().hidePhoneNumberInSettings(),
			[=](bool checked) {
				MakuGram::Settings::instance().setHidePhoneNumberInSettings(checked);
			},
			Tr::Value(
				QString::fromUtf8("Скрывает номер телефона в вашем профиле, в шапке настроек и в меню редактирования для защиты от случайной демонстрации."),
				u"Hides your phone number in profile, settings header, and edit menu to protect against accidental exposure."_q));

		box->addButton(tr::lng_close(), [=] {
			box->closeBox();
		});
	}));
}

void ShowNewbiePeriodBox(
		not_null<Window::SessionController*> controller,
		Fn<void()> onSaved = nullptr) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setWidth(kBoxContentWidth);
		box->setMaxHeight(kBoxMaxHeight);
		box->setTitle(Tr::Value(
			QString::fromUtf8("Период статуса «Новичок»"),
			u"Newbie status period"_q));

		box->addRow(
			object_ptr<Ui::FlatLabel>(
				box,
				Tr::Value(
					QString::fromUtf8(
						"Выберите срок пребывания в чате, в течение которого участник считается новичком, или укажите своё количество дней:"),
					u"Choose how long a member is considered a newbie after joining a chat, or enter a custom number of days:"_q),
				st::boxLabel),
			st::boxRowPadding);

		struct Preset {
			int days;
			QString ru;
			QString en;
		};
		const std::vector<Preset> presets = {
			{ 1, QString::fromUtf8("1 день"), u"1 day"_q },
			{ 7, QString::fromUtf8("7 дней (1 неделя)"), u"7 days (1 week)"_q },
			{ 14, QString::fromUtf8("14 дней (2 недели)"), u"14 days (2 weeks)"_q },
			{ 30, QString::fromUtf8("30 дней (1 месяц)"), u"30 days (1 month)"_q },
			{ 90, QString::fromUtf8("90 дней (3 месяца)"), u"90 days (3 months)"_q },
			{ 180, QString::fromUtf8("180 дней (полгода)"), u"180 days (6 months)"_q },
			{ 365, QString::fromUtf8("365 дней (1 год)"), u"365 days (1 year)"_q },
			{ 0, QString::fromUtf8("Свой срок (в днях)"), u"Custom period (days)"_q },
		};

		const auto currentDays = MakuGram::Settings::instance().newbiePeriodDays();
		auto initialGroup = 0;
		for (const auto &p : presets) {
			if (p.days > 0 && p.days == currentDays) {
				initialGroup = p.days;
				break;
			}
		}

		const auto group = std::make_shared<Ui::RadiobuttonGroup>(initialGroup);
		const auto syncing = std::make_shared<bool>(false);

		box->addSkip(st::boxMediumSkip);
		for (const auto &p : presets) {
			const auto radio = box->addRow(
				object_ptr<Ui::Radiobutton>(
					box,
					group,
					p.days,
					Tr::Get(p.ru, p.en),
					st::defaultBoxCheckbox),
				style::margins(
					st::boxRowPadding.left(),
					4,
					st::boxRowPadding.right(),
					4));
			radio->setCursor(style::cur_pointer);
		}

		const auto customField = box->addRow(
			object_ptr<Ui::InputField>(
				box,
				st::defaultInputField,
				Tr::Value(
					QString::fromUtf8("Количество дней (1–36500)"),
					u"Number of days (1–36500)"_q),
				QString::number(currentDays)),
			style::margins(
				st::boxRowPadding.left(),
				st::boxMediumSkip,
				st::boxRowPadding.right(),
				st::boxMediumSkip));

		group->setChangedCallback([=](int value) {
			if (*syncing) {
				return;
			}
			*syncing = true;
			if (value > 0) {
				customField->setText(QString::number(value));
			} else {
				customField->setFocus();
			}
			*syncing = false;
		});

		customField->changes(
		) | rpl::on_next([=] {
			if (*syncing) {
				return;
			}
			*syncing = true;
			bool ok = false;
			const auto typed = customField->getLastText().trimmed().toInt(&ok);
			auto matched = 0;
			if (ok && typed > 0) {
				for (const auto &p : presets) {
					if (p.days > 0 && p.days == typed) {
						matched = p.days;
						break;
					}
				}
			}
			group->setValue(matched);
			*syncing = false;
		}, customField->lifetime());

		const auto saveAndClose = [=] {
			auto days = group->current();
			if (days <= 0) {
				bool ok = false;
				days = customField->getLastText().trimmed().toInt(&ok);
				if (!ok || days < 1) {
					customField->showError();
					return;
				}
			}
			MakuGram::Settings::instance().setNewbiePeriodDays(days);
			if (onSaved) {
				onSaved();
			}
			box->closeBox();
			controller->showToast(Tr::Get(
				QString::fromUtf8("Настройки сохранены"),
				u"Settings saved"_q));
		};

		customField->submits(
		) | rpl::on_next([=] {
			saveAndClose();
		}, customField->lifetime());

		box->addButton(tr::lng_settings_save(), saveAndClose);
		box->addButton(tr::lng_cancel(), [=] {
			box->closeBox();
		});
	}));
}

void ShowModerationBox(not_null<Window::SessionController*> controller) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setWidth(kBoxContentWidth);
		box->setMaxHeight(kBoxMaxHeight);
		box->setTitle(Tr::Value(
			QString::fromUtf8("Модерация чатов"),
			u"Chat Moderation"_q));

		AddCheckboxRow(
			box,
			Tr::Value(
				QString::fromUtf8("Поиск сообщений пользователя в чате"),
				u"Search user messages in chat"_q),
			MakuGram::Settings::instance().searchUserMessagesInChat(),
			[=](bool checked) {
				MakuGram::Settings::instance().setSearchUserMessagesInChat(checked);
			},
			Tr::Value(
				QString::fromUtf8("Добавляет пункт в контекстное меню сообщения для быстрого поиска всех сообщений автора в текущей группе."),
				u"Adds an item to message context menu to quickly find all messages from this author in current group."_q));

		AddCheckboxRow(
			box,
			Tr::Value(
				QString::fromUtf8("Кнопка \"Забанить\" в заявках"),
				u"\"Ban\" button in join requests"_q),
			MakuGram::Settings::instance().quickBanInRequests(),
			[=](bool checked) {
				MakuGram::Settings::instance().setQuickBanInRequests(checked);
			},
			Tr::Value(
				QString::fromUtf8("Добавляет кнопку быстрого бана прямо рядом с кнопками одобрения и отклонения заявок на вступление."),
				u"Adds a quick ban button directly next to approve and decline join requests."_q));

		AddCheckboxRow(
			box,
			Tr::Value(
				QString::fromUtf8("Кнопки \"Принять всех\" и \"Отклонить всех\""),
				u"\"Accept all\" and \"Decline all\" buttons"_q),
			MakuGram::Settings::instance().massJoinRequests(),
			[=](bool checked) {
				MakuGram::Settings::instance().setMassJoinRequests(checked);
			},
			Tr::Value(
				QString::fromUtf8("Отображать кнопки для массового одобрения или отклонения всех заявок на вступление в группу или канал."),
				u"Show buttons to mass approve or decline all join requests in a group or channel."_q));

		AddCheckboxRow(
			box,
			Tr::Value(
				QString::fromUtf8("Бейдж \"Новичок\" в чатах"),
				u"\"Newbie\" badge in chats"_q),
			MakuGram::Settings::instance().showNewbieBadge(),
			[=](bool checked) {
				MakuGram::Settings::instance().setShowNewbieBadge(checked);
			},
			Tr::Value(
				QString::fromUtf8("Отображать бейдж \"Новичок\" рядом с именем участников, состоящих в чате меньше заданного периода (по умолчанию 90 дн.)."),
				u"Show \"Newbie\" badge next to members who joined the chat less than the configured period ago (default: 90 days)."_q),
			0);

		const auto newbieBtnText = [] {
			const auto days = MakuGram::Settings::instance().newbiePeriodDays();
			return Tr::Value(
				QString::fromUtf8("Настроить период новичка (%1 дн.)...").arg(days),
				QString::fromUtf8("Configure newbie period (%1 d)...").arg(days));
		};
		const auto newbiePeriodBtn = box->addRow(
			object_ptr<Ui::RoundButton>(
				box,
				newbieBtnText(),
				st::defaultBoxButton),
			style::margins(
				st::boxRowPadding.left(),
				9,
				st::boxRowPadding.right(),
				st::boxMediumSkip));
		const auto weakNewbieBtn = QPointer<Ui::RoundButton>(newbiePeriodBtn);
		newbiePeriodBtn->setClickedCallback([=] {
			ShowNewbiePeriodBox(controller, [=] {
				if (const auto btn = weakNewbieBtn.data()) {
					btn->setText(newbieBtnText());
				}
			});
		});

		box->addButton(tr::lng_close(), [=] {
			box->closeBox();
		});
	}));
}

void ShowProfileBox(not_null<Window::SessionController*> controller) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setWidth(kBoxContentWidth);
		box->setMaxHeight(kBoxMaxHeight);
		box->setTitle(Tr::Value(
			QString::fromUtf8("Профиль и информация"),
			u"Profile & Info"_q));

		AddCheckboxRow(
			box,
			Tr::Value(
				QString::fromUtf8("Показывать ID и дату регистрации аккаунта"),
				u"Show account ID and registration date"_q),
			MakuGram::Settings::instance().showProfileDetails(),
			[=](bool checked) {
				MakuGram::Settings::instance().setShowProfileDetails(checked);
			},
			Tr::Value(
				QString::fromUtf8("Отображать числовой ID (с копированием в буфер по клику) и примерную дату регистрации в профиле пользователя."),
				u"Display numeric ID (clickable to copy) and estimated registration date in user profile."_q));

		AddCheckboxRow(
			box,
			Tr::Value(
				QString::fromUtf8("Отключить сторис"),
				u"Disable Stories"_q),
			MakuGram::Settings::instance().disableStories(),
			[=](bool checked) {
				MakuGram::Settings::instance().setDisableStories(checked);
			},
			Tr::Value(
				QString::fromUtf8("Полностью скрывает истории пользователей и каналов: ободки на аватарках, верхнюю панель историй, раздел в профиле и в архиве."),
				u"Completely hides user and channel stories: rings on avatars, top stories bar, profile and archive sections."_q));

		box->addButton(tr::lng_close(), [=] {
			box->closeBox();
		});
	}));
}

void ShowMediaBox(not_null<Window::SessionController*> controller) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setWidth(kBoxContentWidth);
		box->setMaxHeight(kBoxMaxHeight);
		box->setTitle(Tr::Value(
			QString::fromUtf8("Медиа"),
			u"Media"_q));

		AddCheckboxRow(
			box,
			Tr::Value(
				QString::fromUtf8("Всегда открывать медиа по размеру экрана"),
				u"Always open media fit to window"_q),
			MakuGram::Settings::instance().alwaysZoomMediaToScreen(),
			[=](bool checked) {
				MakuGram::Settings::instance().setAlwaysZoomMediaToScreen(checked);
			},
			Tr::Value(
				QString::fromUtf8("Автоматически растягивать изображения и медиафайлы до границ окна по соотношению сторон при открытии (аналогично нажатию на колёсико мыши)."),
				u"Automatically fit images and media to window borders on open (like clicking mouse wheel)."_q));

		AddCheckboxRow(
			box,
			Tr::Value(
				QString::fromUtf8("Регулировка громкости колесиком мыши"),
				u"Mouse wheel volume control"_q),
			MakuGram::Settings::instance().videoVolumeWheel(),
			[=](bool checked) {
				MakuGram::Settings::instance().setVideoVolumeWheel(checked);
			},
			Tr::Value(
				QString::fromUtf8("При просмотре видео в полноэкранном режиме колесико мыши регулирует громкость звука вместо переключения на следующее или предыдущее медиа."),
				u"When viewing video in fullscreen, mouse wheel adjusts volume instead of switching to next/previous media."_q));

		AddCheckboxRow(
			box,
			Tr::Value(
				QString::fromUtf8("Отправлять медиа в высоком качестве"),
				u"Send media in high quality"_q),
			MakuGram::Settings::instance().sendMediaHighQuality(),
			[=](bool checked) {
				MakuGram::Settings::instance().setSendMediaHighQuality(checked);
			},
			Tr::Value(
				QString::fromUtf8("Автоматически включает отправку фото и медиа в максимальном HD-качестве (до 2560px) по умолчанию без необходимости каждый раз переключать режим."),
				u"Automatically enables sending photos in max HD quality (up to 2560px) by default without switching every time."_q));

		AddCheckboxRow(
			box,
			Tr::Value(
				QString::fromUtf8("Показывать разрешение"),
				u"Show media resolution"_q),
			MakuGram::Settings::instance().showMediaResolution(),
			[=](bool checked) {
				MakuGram::Settings::instance().setShowMediaResolution(checked);
			},
			Tr::Value(
				QString::fromUtf8("Отображает разрешение изображения или видео в полноэкранном режиме просмотра медиа."),
				u"Displays image or video resolution in fullscreen media viewer."_q));

		AddCheckboxRow(
			box,
			Tr::Value(
				QString::fromUtf8("Добавить выделение медиа в полноэкранном режиме"),
				u"Enable media selection in fullscreen"_q),
			MakuGram::Settings::instance().fullscreenMediaSelection(),
			[=](bool checked) {
				MakuGram::Settings::instance().setFullscreenMediaSelection(checked);
			},
			Tr::Value(
				QString::fromUtf8("Позволяет выделять и пересылать медиа при полноэкранном просмотре, сохраняя выделение при возврате в чат."),
				u"Allows selecting and forwarding media in fullscreen, preserving selection when returning to chat."_q));

		AddCheckboxRow(
			box,
			Tr::Value(
				QString::fromUtf8("Расширить окно отправки медиа"),
				u"Expand media send dialog"_q),
			MakuGram::Settings::instance().expandMediaSendDialog(),
			[=](bool checked) {
				MakuGram::Settings::instance().setExpandMediaSendDialog(checked);
			},
			Tr::Value(
				QString::fromUtf8("Увеличивает ширину окна предпросмотра при отправке фото и видео, а также расширяет поле ввода подписи по высоте для комфортного написания длинного текста."),
				u"Increases preview dialog width when sending media and expands caption input height for writing long text."_q));

		box->addButton(tr::lng_close(), [=] {
			box->closeBox();
		});
	}));
}

void ShowAppearanceBox(not_null<Window::SessionController*> controller) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setWidth(kBoxContentWidth);
		box->setMaxHeight(kBoxMaxHeight);
		box->setTitle(Tr::Value(
			QString::fromUtf8("Внешний вид"),
			u"Appearance"_q));

		AddCheckboxRow(
			box,
			Tr::Value(
				QString::fromUtf8("Компактный список чатов"),
				u"Compact chat list"_q),
			MakuGram::Settings::instance().compactChatList(),
			[=](bool checked) {
				MakuGram::Settings::instance().setCompactChatList(checked);
			},
			Tr::Value(
				QString::fromUtf8("Скрывает строку предпросмотра последнего сообщения и уменьшает высоту строк чатов, умещая в два раза больше диалогов на экране."),
				u"Hides last message preview line and reduces chat row height, fitting twice as many chats on screen."_q));

		AddCheckboxRow(
			box,
			Tr::Value(
				QString::fromUtf8("Отключить анимацию реакций и конфетти"),
				u"Disable reaction animations and confetti"_q),
			MakuGram::Settings::instance().disableReactionAnimations(),
			[=](bool checked) {
				MakuGram::Settings::instance().setDisableReactionAnimations(checked);
			},
			Tr::Value(
				QString::fromUtf8("Отключает летающие большие эмодзи, эффекты и конфетти при установке реакций для максимальной плавности."),
				u"Disables floating emoji effects and confetti when reacting for maximum smoothness."_q));

		box->addButton(tr::lng_close(), [=] {
			box->closeBox();
		});
	}));
}

void ShowStreamerModeBox(not_null<Window::SessionController*> controller) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setWidth(kBoxContentWidth);
		box->setMaxHeight(kBoxMaxHeight);
		box->setTitle(Tr::Value(
			QString::fromUtf8("Режим стримера"),
			u"Streamer Mode"_q));

		AddCheckboxRow(
			box,
			Tr::Value(
				QString::fromUtf8("Включить режим стримера"),
				u"Enable Streamer Mode"_q),
			MakuGram::Settings::instance().streamerMode(),
			[=](bool checked) {
				MakuGram::Settings::instance().setStreamerMode(checked);
			},
			Tr::Value(
				QString::fromUtf8("Скрывает номера телефонов, расчетную дату регистрации и числовой ID профилей, а также размывает все аватарки пользователей и каналов во всем приложении для защиты личных данных при записи видео, демонстрации экрана или стриминге."),
				u"Hides phone numbers, estimated registration date, and profile IDs, and blurs all user and channel avatars across the app to protect personal data during streaming or screen recording."_q));

		box->addButton(tr::lng_close(), [=] {
			box->closeBox();
		});
	}));
}

void ShowProxySettingsBox(not_null<Window::SessionController*> controller) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setWidth(kBoxContentWidth);
		box->setMaxHeight(kBoxMaxHeight);
		box->setTitle(Tr::Value(
			QString::fromUtf8("Прокси и ускорение сети"),
			u"Proxy & Network Acceleration"_q));

		AddCheckboxRow(
			box,
			Tr::Value(
				QString::fromUtf8("Встроенный TG WS Proxy (WebSocket)"),
				u"Built-in TG WS Proxy (WebSocket)"_q),
			MakuGram::Settings::instance().tgWsProxyEnabled(),
			[=](bool checked) {
				MakuGram::Settings::instance().setTgWsProxyEnabled(checked);
				MakuGram::TgWsProxy::instance().applyToProxySettings(checked);
			},
			Tr::Value(
				QString::fromUtf8("Туннелирование MTProto трафика через защищенные WebSocket WSS соединения (Cloudflare) для полного обхода замедлений, фильтрации DPI и блокировок Telegram без использования сторонних VPN."),
				u"Tunnel MTProto traffic over secure WebSocket WSS connections (Cloudflare) to bypass throttling, DPI filtering, and Telegram blocks without external VPN."_q));

		box->addSkip(st::boxMediumSkip);

		const auto buttonWidth = kBoxContentWidth
			- st::boxRowPadding.left()
			- st::boxRowPadding.right();

		const auto updateDomainsBtn = box->addRow(
			object_ptr<Ui::RoundButton>(
				box,
				Tr::Value(
					QString::fromUtf8("Обновить список прокси с GitHub"),
					u"Update proxy list from GitHub"_q),
				st::defaultBoxButton),
			style::margins(
				st::boxRowPadding.left(),
				4,
				st::boxRowPadding.right(),
				4));
		updateDomainsBtn->setFullWidth(buttonWidth);
		updateDomainsBtn->setFullRadius(true);
		updateDomainsBtn->setClickedCallback([=] {
			controller->showToast(Tr::Get(
				QString::fromUtf8("Обновление списка прокси с GitHub..."),
				u"Updating proxy list from GitHub..."_q));
			MakuGram::TgWsProxy::instance().updateDomainsFromGitHub([=](bool success, int count) {
				if (success) {
					controller->showToast(Tr::Get(
						QString::fromUtf8("Список прокси обновлен! Активно доменов: %1").arg(count),
						QString::fromUtf8("Proxy list updated! Active domains: %1").arg(count)));
				} else {
					controller->showToast(Tr::Get(
						QString::fromUtf8("Не удалось обновить список с GitHub (используются кэшированные домены: %1)").arg(count),
						QString::fromUtf8("Failed to update from GitHub (using cached domains: %1)").arg(count)));
				}
			});
		});

		const auto nativeProxyBtn = box->addRow(
			object_ptr<Ui::RoundButton>(
				box,
				Tr::Value(
					QString::fromUtf8("Стандартные настройки прокси Telegram..."),
					u"Standard Telegram Proxy Settings..."_q),
				st::defaultBoxButton),
			style::margins(
				st::boxRowPadding.left(),
				4,
				st::boxRowPadding.right(),
				4));
		nativeProxyBtn->setFullWidth(buttonWidth);
		nativeProxyBtn->setFullRadius(true);
		nativeProxyBtn->setClickedCallback([=] {
			ProxiesBoxController::Show(controller);
		});

		box->addButton(tr::lng_close(), [=] {
			box->closeBox();
		});
	}));
}

void ShowAboutBox(not_null<Window::SessionController*> controller) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setWidth(kBoxContentWidth);
		box->setMaxHeight(kBoxMaxHeight);
		box->setTitle(Tr::Value(
			QString::fromUtf8("О клиенте MakuGramR"),
			u"About MakuGramR"_q));

		box->addRow(
			object_ptr<Ui::FlatLabel>(
				box,
				Tr::Value(
					QString::fromUtf8("MakuGramR (Refresh) — форк MakuGram с расширенными возможностями модерации, приватности, встроенным WS-прокси и режимом невидимки."),
					u"MakuGramR (Refresh) is a fork of MakuGram with advanced moderation, privacy, built-in WS proxy, and ghost mode."_q),
				st::boxLabel),
			st::boxRowPadding);

		box->addSkip(st::boxMediumSkip);

		box->addRow(
			object_ptr<Ui::FlatLabel>(
				box,
				Tr::Value(
					QString::fromUtf8("Версия: %1 (Сборка #%2)\nАвтор (Refresh): pon4ikisdonut\nАвтор (Original MakuGram): Mark Adderly")
						.arg(MakuGram::kVersion)
						.arg(MakuGram::kBuildNumber),
					QString::fromUtf8("Version: %1 (Build #%2)\nAuthor (Refresh): pon4ikisdonut\nAuthor (Original MakuGram): Mark Adderly")
						.arg(MakuGram::kVersion)
						.arg(MakuGram::kBuildNumber)),
				st::boxLabel),
			st::boxRowPadding);

		box->addSkip(st::boxMediumSkip);

		const auto buttonWidth = kBoxContentWidth
			- st::boxRowPadding.left()
			- st::boxRowPadding.right();

		const auto checkUpdateBtn = box->addRow(
			object_ptr<Ui::RoundButton>(
				box,
				Tr::Value(
					QString::fromUtf8("Проверить обновления"),
					u"Check for updates"_q),
				st::defaultBoxButton),
			style::margins(
				st::boxRowPadding.left(),
				4,
				st::boxRowPadding.right(),
				4));
		checkUpdateBtn->setFullWidth(buttonWidth);
		checkUpdateBtn->setFullRadius(true);
		checkUpdateBtn->setClickedCallback([=] {
			MakuGram::CheckForUpdates(controller, /*silent=*/false);
		});

		const auto radugaChatBtn = box->addRow(
			object_ptr<Ui::RoundButton>(
				box,
				Tr::Value(
					QString::fromUtf8("Чат ОПГ «Радуга»"),
					u"Chat OPG «Raduga»"_q),
				TelegramButtonStyle()),
			style::margins(
				st::boxRowPadding.left(),
				4,
				st::boxRowPadding.right(),
				4));
		radugaChatBtn->setFullWidth(buttonWidth);
		radugaChatBtn->setFullRadius(true);
		radugaChatBtn->setClickedCallback([=] {
			UrlClickHandler::Open(
				u"https://t.me/OPG_R4duga"_q,
				QVariant::fromValue(ClickHandlerContext{
					.sessionWindow = base::make_weak(controller),
				}));
		});

		const auto authorTgBtn = box->addRow(
			object_ptr<Ui::RoundButton>(
				box,
				Tr::Value(
					QString::fromUtf8("Telegram автора (Refresh)"),
					u"Author's Telegram (Refresh)"_q),
				TelegramButtonStyle()),
			style::margins(
				st::boxRowPadding.left(),
				4,
				st::boxRowPadding.right(),
				4));
		authorTgBtn->setFullWidth(buttonWidth);
		authorTgBtn->setFullRadius(true);
		authorTgBtn->setClickedCallback([=] {
			UrlClickHandler::Open(
				u"https://t.me/pon4ikisdonut"_q,
				QVariant::fromValue(ClickHandlerContext{
					.sessionWindow = base::make_weak(controller),
				}));
		});

		const auto ghBtn = box->addRow(
			object_ptr<Ui::RoundButton>(
				box,
				Tr::Value(
					QString::fromUtf8("GitHub репозиторий проекта"),
					u"Project GitHub Repository"_q),
				st::defaultBoxButton),
			style::margins(
				st::boxRowPadding.left(),
				4,
				st::boxRowPadding.right(),
				4));
		ghBtn->setFullWidth(buttonWidth);
		ghBtn->setFullRadius(true);
		ghBtn->setClickedCallback([=] {
			UrlClickHandler::Open(u"https://github.com/pon4ikisdonut/MakuGram"_q);
		});

		const auto origGhBtn = box->addRow(
			object_ptr<Ui::RoundButton>(
				box,
				Tr::Value(
					QString::fromUtf8("Репозиторий оригинального MakuGram"),
					u"Original MakuGram Repo"_q),
				st::defaultBoxButton),
			style::margins(
				st::boxRowPadding.left(),
				4,
				st::boxRowPadding.right(),
				4));
		origGhBtn->setFullWidth(buttonWidth);
		origGhBtn->setFullRadius(true);
		origGhBtn->setClickedCallback([=] {
			UrlClickHandler::Open(u"https://github.com/MarkAdderly/MakuGram"_q);
		});

		box->addButton(tr::lng_close(), [=] {
			box->closeBox();
		});
	}));
}

void BuildMakuGramSection(SectionBuilder &builder) {
	const auto controller = builder.controller();
	if (!controller) {
		return;
	}

	static bool updateChecked = false;
	if (!updateChecked) {
		updateChecked = true;
		MakuGram::CheckForUpdates(controller, /*silent=*/true);
	}

	builder.addSubsectionTitle(Tr::Value(
		QString::fromUtf8("Категории настроек"),
		u"Setting Categories"_q));

	builder.addButton({
		.title = Tr::Value(QString::fromUtf8("Сообщения"), u"Messages"_q),
		.icon = { &st::menuIconChats },
		.onClick = [=] { ShowMessagesBox(controller); },
		.keywords = { u"currency"_q, u"messages"_q, u"unread"_q, u"spam"_q, u"time"_q, u"seconds"_q, u"валюта"_q, u"сообщения"_q, u"спам"_q, u"время"_q, u"секунды"_q },
	});

	builder.addButton({
		.title = Tr::Value(QString::fromUtf8("Приватность и безопасность"), u"Privacy & Security"_q),
		.icon = { &st::menuIconLock },
		.onClick = [=] { ShowPrivacyBox(controller); },
		.keywords = { u"privacy"_q, u"ghost"_q, u"offline"_q, u"typing"_q, u"copy"_q, u"delete"_q, u"phone"_q, u"blocked"_q, u"hidden"_q, u"spoiler"_q, u"приватность"_q, u"призрак"_q, u"невидимка"_q, u"офлайн"_q, u"тайпинг"_q, u"копирование"_q, u"удаление"_q, u"телефон"_q, u"номер"_q, u"блок"_q, u"скрытые"_q, u"спойлер"_q },
	});

	builder.addButton({
		.title = Tr::Value(QString::fromUtf8("Модерация чатов"), u"Chat Moderation"_q),
		.icon = { &st::menuIconAdmin },
		.onClick = [=] { ShowModerationBox(controller); },
		.keywords = { u"moderation"_q, u"ban"_q, u"badge"_q, u"requests"_q, u"search"_q, u"newbie"_q, u"period"_q, u"days"_q, u"бан"_q, u"новичок"_q, u"период"_q, u"срок"_q, u"дни"_q, u"заявки"_q, u"модерация"_q, u"поиск"_q },
	});

	builder.addButton({
		.title = Tr::Value(QString::fromUtf8("Профиль и информация"), u"Profile & Info"_q),
		.icon = { &st::menuIconProfile },
		.onClick = [=] { ShowProfileBox(controller); },
		.keywords = { u"id"_q, u"profile"_q, u"stories"_q, u"профиль"_q, u"айди"_q, u"сторис"_q, u"истории"_q },
	});

	builder.addButton({
		.title = Tr::Value(QString::fromUtf8("Внешний вид"), u"Appearance"_q),
		.icon = { &st::menuIconChangeColors },
		.onClick = [=] { ShowAppearanceBox(controller); },
		.keywords = { u"appearance"_q, u"compact"_q, u"reactions"_q, u"вид"_q, u"компактный"_q, u"реакции"_q },
	});

	builder.addButton({
		.title = Tr::Value(QString::fromUtf8("Медиа"), u"Media"_q),
		.icon = { &st::menuIconPhoto },
		.onClick = [=] { ShowMediaBox(controller); },
		.keywords = { u"media"_q, u"zoom"_q, u"volume"_q, u"медиа"_q, u"масштаб"_q, u"экран"_q },
	});

	builder.addButton({
		.title = Tr::Value(QString::fromUtf8("Настройки прокси и сети"), u"Proxy & Network Settings"_q),
		.icon = { &st::menuIconNetwork },
		.onClick = [=] { ShowProxySettingsBox(controller); },
		.keywords = { u"proxy"_q, u"прокси"_q, u"vpn"_q, u"впн"_q, u"network"_q, u"сеть"_q, u"ws"_q, u"websocket"_q },
	});

	builder.addButton({
		.title = Tr::Value(QString::fromUtf8("Режим стримера"), u"Streamer Mode"_q),
		.icon = { &st::menuIconPermissions },
		.onClick = [=] { ShowStreamerModeBox(controller); },
		.keywords = { u"stream"_q, u"streamer"_q, u"blur"_q, u"стрим"_q, u"стример"_q, u"блюр"_q, u"аватарки"_q, u"приватность"_q },
	});

	builder.addButton({
		.title = Tr::Value(QString::fromUtf8("О клиенте MakuGramR"), u"About MakuGramR"_q),
		.icon = { &st::menuIconInfo },
		.onClick = [=] { ShowAboutBox(controller); },
		.keywords = { u"about"_q, u"makugram"_q, u"makugramr"_q, u"version"_q, u"версия"_q, u"информация"_q },
	});

	builder.addDivider();
	builder.addDividerText(Tr::Value(
		QString::fromUtf8("Нажмите на категорию для настройки параметров MakuGramR."),
		u"Click a category to configure MakuGramR settings."_q));
}

const auto kMeta = BuildHelper({
	.id = MakuGramSection::Id(),
	.parentId = MainId(),
	.title = &tr::lng_settings_makugram,
	.icon = &st::menuIconManage,
}, [](SectionBuilder &builder) {
	BuildMakuGramSection(builder);
});

MakuGramSection::MakuGramSection(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

rpl::producer<QString> MakuGramSection::title() {
	return Tr::Value(QString::fromUtf8("Функции MakuGramR"), u"MakuGramR Features"_q);
}

void MakuGramSection::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	build(content, kMeta.build);
	Ui::ResizeFitChild(this, content);
}

} // namespace

Type MakuGramId() {
	return MakuGramSection::Id();
}

} // namespace Settings
