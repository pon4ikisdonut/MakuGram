/*
This file is part of MakuGramR,
a custom Telegram Desktop client fork.
*/
#pragma once

#include <QtCore/QString>

namespace Window {
class SessionController;
} // namespace Window

namespace MakuGram {

inline constexpr auto kAppName = "MakuGramR";
inline constexpr auto kVersion = "1.2.0";
inline constexpr auto kGitHubRepo = "pon4ikisdonut/MakuGramR";

#ifdef MAKUGRAM_BUILD_NUMBER
inline constexpr auto kBuildNumber = MAKUGRAM_BUILD_NUMBER;
#else
inline constexpr auto kBuildNumber = 1;
#endif

void CheckForUpdates(Window::SessionController *controller, bool silent = false);
void CleanOldUpdateFiles();

} // namespace MakuGram
