/*
This file is part of MakuGram,
a custom Telegram Desktop client fork.
*/
#include "makugram/makugram_filter.h"

#include "makugram/makugram_lang.h"
#include "makugram/makugram_settings.h"
#include "history/history.h"
#include "history/history_item.h"
#include "data/data_peer.h"
#include "data/data_user.h"
#include "ui/text/text_entity.h"
#include <QtCore/QStringView>

namespace MakuGram {
namespace {

bool MatchesPattern(const QString &text, const QString &normText, const SpamPattern &p) {
	if (text.isEmpty()) {
		return false;
	}
	if (p.type == PatternType::Regex) {
		return p.regex.isValid() && p.regex.match(text).hasMatch();
	}
	if (normText.isEmpty()) {
		return false;
	}

	bool anySubstr = false;
	for (const auto &v : p.variants) {
		if (normText.contains(v)) {
			anySubstr = true;
			break;
		}
	}
	if (!anySubstr) {
		return false;
	}

	if (p.type == PatternType::Contains) {
		return true;
	}

	const int n = normText.size();
	for (int i = 0; i < n;) {
		while (i < n && !normText[i].isLetterOrNumber()) {
			++i;
		}
		if (i >= n) break;
		const int start = i;
		while (i < n && normText[i].isLetterOrNumber()) {
			++i;
		}
		const auto word = normText.mid(start, i - start);
		const auto wordStem = RussianStem(word);

		for (const auto &v : p.variants) {
			if (p.type == PatternType::Prefix) {
				if (word.startsWith(v) || wordStem.startsWith(v)) {
					return true;
				}
			} else if (p.type == PatternType::Suffix) {
				if (word.endsWith(v) || wordStem.endsWith(v)) {
					return true;
				}
			} else if (p.type == PatternType::Exact) {
				if (word == v || wordStem == v || word == p.clean) {
					return true;
				}
			}
		}
	}
	return false;
}

} // namespace

bool IsBlockedMessageHidden(const HistoryItem *item) {
	if (!item) {
		return false;
	}
	if (!Settings::instance().hideMessagesFromBlocked()) {
		return false;
	}
	if (item->out()) {
		return false;
	}
	const auto history = item->history();
	if (!history) {
		return false;
	}
	const auto peer = history->peer;
	if (!peer || peer->isUser()) {
		return false;
	}

	const auto from = item->from();
	if (from && from->isUser() && from->asUser()->isBlocked()) {
		return true;
	}
	if (const auto bot = item->viaBot()) {
		if (bot->isBlocked()) {
			return true;
		}
	}
	return false;
}

bool IsSpamKeywordMessageHidden(const HistoryItem *item) {
	if (!item) {
		return false;
	}
	const auto &settings = Settings::instance();
	if (!settings.spamFilterEnabled()) {
		return false;
	}
	const auto &patterns = settings.spamPatterns();
	if (patterns.empty()) {
		return false;
	}
	if (item->out()) {
		return false;
	}
	const auto &text = item->originalText().text;
	if (text.isEmpty()) {
		return false;
	}
	QString normText;
	bool normComputed = false;
	for (const auto &p : patterns) {
		if (p.type != PatternType::Regex && !normComputed) {
			normText = NormalizeRussianText(text);
			normComputed = true;
		}
		if (MatchesPattern(text, normText, p)) {
			return true;
		}
	}
	return false;
}

bool IsSearchMessageHidden(const HistoryItem *item) {
	if (!item) {
		return false;
	}
	if (IsFullHiddenMessageHidden(item)) {
		return true;
	}
	const auto &settings = Settings::instance();
	if (!settings.filterSpamInSearch()) {
		return false;
	}
	const auto &patterns = settings.spamPatterns();
	if (patterns.empty()) {
		return false;
	}

	const auto checkText = [&](const QString &text) {
		if (text.isEmpty()) {
			return false;
		}
		QString normText;
		bool normComputed = false;
		for (const auto &p : patterns) {
			if (p.type != PatternType::Regex && !normComputed) {
				normText = NormalizeRussianText(text);
				normComputed = true;
			}
			if (MatchesPattern(text, normText, p)) {
				return true;
			}
		}
		return false;
	};

	if (checkText(item->originalText().text)) {
		return true;
	}
	if (checkText(item->notificationText().text)) {
		return true;
	}
	if (checkText(item->translatedText().text)) {
		return true;
	}
	return false;
}

bool IsUserHidden(const PeerData *peer) {
	return Settings::instance().isUserHidden(peer);
}

bool IsUserFullHidden(const PeerData *peer) {
	return Settings::instance().isUserFullHidden(peer);
}

bool IsFullHiddenMessageHidden(const HistoryItem *item) {
	if (!item) {
		return false;
	}
	if (IsUserFullHidden(item->from()) || IsUserFullHidden(item->author())) {
		return true;
	}
	if (const auto bot = item->viaBot()) {
		if (IsUserFullHidden(bot)) {
			return true;
		}
	}
	if (const auto history = item->history()) {
		if (IsUserFullHidden(history->peer)) {
			return true;
		}
	}
	return false;
}

TextWithEntities MaskHiddenUserMentions(const TextWithEntities &text) {
	if (text.text.isEmpty() || !Settings::instance().hiddenUsersEnabled()) {
		return text;
	}
	const auto &settings = Settings::instance();
	const auto &hiddenUsers = settings.hiddenUsersList();
	if (hiddenUsers.isEmpty()) {
		return text;
	}

	auto result = text;

	// 1. Check existing entities: Mention, MentionName, CustomUrl
	for (int i = int(result.entities.size()) - 1; i >= 0; --i) {
		const auto &entity = result.entities[i];
		bool isHiddenMention = false;

		if (entity.type() == EntityType::Mention) {
			auto mention = result.text.mid(entity.offset(), entity.length()).trimmed();
			if (mention.startsWith('@')) {
				mention = mention.mid(1);
			}
			if (settings.isMentionHidden(mention)) {
				isHiddenMention = true;
			}
		} else if (entity.type() == EntityType::MentionName) {
			const auto userId = entity.data().toULongLong();
			if (userId && settings.isMentionHidden(QString(), userId)) {
				isHiddenMention = true;
			}
		} else if (entity.type() == EntityType::CustomUrl) {
			const auto url = entity.data().toLower();
			for (const auto &entry : hiddenUsers) {
				auto clean = entry.toLower();
				if (clean.startsWith('@')) clean = clean.mid(1);
				if (!clean.isEmpty() && (url.endsWith('/' + clean) || url.contains('/' + clean + '?'))) {
					isHiddenMention = true;
					break;
				}
			}
		}

		if (isHiddenMention) {
			const auto offset = entity.offset();
			const auto oldLength = entity.length();
			const auto &replacement = HiddenUserName();
			const auto newLength = int(replacement.size());
			const auto delta = newLength - oldLength;

			result.text.replace(offset, oldLength, replacement);
			result.entities[i] = EntityInText(EntityType::Spoiler, offset, newLength);

			for (int j = i + 1; j < int(result.entities.size()); ++j) {
				result.entities[j] = EntityInText(
					result.entities[j].type(),
					result.entities[j].offset() + delta,
					result.entities[j].length(),
					result.entities[j].data());
			}
		}
	}

	// 2. Scan text for any un-entitied @username mentions of hidden users
	for (const auto &entry : hiddenUsers) {
		auto clean = entry.trimmed();
		if (clean.startsWith('@')) clean = clean.mid(1);
		if (clean.isEmpty() || clean.toULongLong() != 0) {
			continue;
		}
		const auto mentionPattern = '@' + clean;
		int searchFrom = 0;
		while (true) {
			const auto pos = result.text.indexOf(mentionPattern, searchFrom, Qt::CaseInsensitive);
			if (pos < 0) {
				break;
			}
			const auto afterPos = pos + mentionPattern.size();
			if (afterPos < result.text.size() && (result.text[afterPos].isLetterOrNumber() || result.text[afterPos] == '_')) {
				searchFrom = afterPos;
				continue;
			}

			bool alreadyCovered = false;
			for (const auto &e : result.entities) {
				if (e.offset() <= pos && (e.offset() + e.length()) >= afterPos) {
					alreadyCovered = true;
					break;
				}
			}
			if (alreadyCovered) {
				searchFrom = afterPos;
				continue;
			}

			const auto &replacement = HiddenUserName();
			const auto oldLength = int(mentionPattern.size());
			const auto newLength = int(replacement.size());
			const auto delta = newLength - oldLength;

			result.text.replace(pos, oldLength, replacement);
			result.entities.push_back(EntityInText(EntityType::Spoiler, pos, newLength));

			for (auto &e : result.entities) {
				if (e.offset() > pos) {
					e = EntityInText(e.type(), e.offset() + delta, e.length(), e.data());
				}
			}

			searchFrom = pos + newLength;
		}
	}

	return result;
}

} // namespace MakuGram
