/*
This file is part of MakuGram,
a custom Telegram Desktop client fork.
*/
#pragma once

class HistoryItem;
class PeerData;

namespace MakuGram {

[[nodiscard]] bool IsBlockedMessageHidden(const HistoryItem *item);
[[nodiscard]] bool IsSpamKeywordMessageHidden(const HistoryItem *item);
[[nodiscard]] bool IsSearchMessageHidden(const HistoryItem *item);
[[nodiscard]] bool IsUserHidden(const PeerData *peer);

} // namespace MakuGram
