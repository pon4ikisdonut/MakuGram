/*
This file is part of MakuGram,
a custom Telegram Desktop client fork.
*/
#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QByteArray>
#include <QtCore/QPointer>
#include <QtNetwork/QTcpServer>
#include <QtNetwork/QTcpSocket>
#include <QtNetwork/QNetworkAccessManager>
#include "base/basic_types.h"
#include <functional>
#include <memory>
#include <vector>

namespace Core {
class WebSocketClient;
} // namespace Core

namespace MakuGram {

class TgWsProxyBridge;

class TgWsProxy final : public QObject {
public:
	static TgWsProxy &instance();

	void start();
	void stop();
	[[nodiscard]] bool isRunning() const;
	[[nodiscard]] uint16 port() const;
	[[nodiscard]] int domainCount() const;
	[[nodiscard]] QStringList domains() const;

	void updateDomainsFromGitHub(std::function<void(bool success, int count)> callback = nullptr);
	void applyToProxySettings(bool enabled);

	[[nodiscard]] QString selectHostForDc(int dc);

private:
	TgWsProxy();
	~TgWsProxy();

	void onNewConnection();
	void loadDomains();
	void saveDomains();
	void removeBridge(TgWsProxyBridge *bridge);

	std::unique_ptr<QTcpServer> _server;
	std::unique_ptr<QNetworkAccessManager> _network;
	QStringList _domains;
	uint16 _port = 1443;
	bool _updating = false;
	std::vector<std::unique_ptr<TgWsProxyBridge>> _bridges;

	friend class TgWsProxyBridge;
};

class TgWsProxyBridge final : public QObject {
public:
	TgWsProxyBridge(QTcpSocket *socket, TgWsProxy *parent);
	~TgWsProxyBridge();

private:
	void onClientReadyRead();
	void handleSocks5Handshake();
	void connectWebSocket(int dc);
	void flushBuffer();
	void close();

	enum class Stage {
		Initial,
		Socks5Greeting,
		Socks5Request,
		Piping,
		Closed,
	};

	QPointer<QTcpSocket> _client;
	TgWsProxy *_parent = nullptr;
	std::unique_ptr<Core::WebSocketClient> _ws;
	QByteArray _pendingClientData;
	Stage _stage = Stage::Initial;
	int _dc = 2;
	bool _wsConnected = false;
};

} // namespace MakuGram
