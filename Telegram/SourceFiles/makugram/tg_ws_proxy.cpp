/*
This file is part of MakuGram,
a custom Telegram Desktop client fork.
*/
#include "makugram/tg_ws_proxy.h"
#include "makugram/makugram_settings.h"
#include "makugram/makugram_lang.h"
#include "core/websocket_client.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "core/core_settings_proxy.h"
#include "mtproto/mtproto_proxy_data.h"
#include "base/random.h"
#include <crl/crl_on_main.h>

#include <QtCore/QSettings>
#include <QtCore/QtEndian>
#include <QtCore/QRegularExpression>
#include <QtNetwork/QNetworkRequest>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QHostAddress>

namespace MakuGram {

namespace {

const QStringList kDefaultDomains = {
	u"virkgj.com"_q,
	u"vmmzovy.com"_q,
	u"mkuosckvso.com"_q,
	u"zaewayzmplad.com"_q,
	u"twdmbzcm.com"_q,
	u"awzwsldi.com"_q,
	u"clngqrflngqin.com"_q,
	u"tjacxbqtj.com"_q,
	u"bxaxtxmrw.com"_q,
	u"dmohrsgmohcrwb.com"_q,
	u"vwbmtmoi.com"_q,
	u"khgrre.com"_q,
	u"ulihssf.com"_q,
	u"tmhqsdqmfpmk.com"_q,
	u"xwuwoqbm.com"_q,
	u"orgcnunpj.com"_q,
	u"zhkuldz.com"_q,
	u"zypoljnslxa.com"_q,
	u"efabnxaowuzs.com"_q,
	u"zaftuzsftqdq.com"_q,
};

int DcFromIp(const QHostAddress &addr) {
	const auto str = addr.toString();
	if (str.startsWith(u"149.154.175."_q)) {
		if (str.endsWith(u".100"_q) || str.endsWith(u".117"_q)) {
			return 3;
		}
		return 1;
	} else if (str.startsWith(u"149.154.167."_q)) {
		if (str.endsWith(u".91"_q)) {
			return 4;
		}
		return 2;
	} else if (str.startsWith(u"149.154.171."_q) || str.startsWith(u"91.108.56."_q)) {
		return 5;
	}
	return 2;
}

QString OfficialTelegramWebGateway(int dc) {
	switch (dc) {
	case 1: return u"venus.web.telegram.org"_q;
	case 2: return u"aurora.web.telegram.org"_q;
	case 3: return u"vesta.web.telegram.org"_q;
	case 4: return u"pluto.web.telegram.org"_q;
	case 5: return u"flora.web.telegram.org"_q;
	default: return u"aurora.web.telegram.org"_q;
	}
}

} // namespace

TgWsProxy &TgWsProxy::instance() {
	static TgWsProxy inst;
	return inst;
}

TgWsProxy::TgWsProxy() {
	loadDomains();
}

TgWsProxy::~TgWsProxy() {
	stop();
}

void TgWsProxy::loadDomains() {
	QSettings s("MakuGram", "MakuGram");
	_domains = s.value("tgWsProxyDomains").toStringList();
	if (_domains.isEmpty()) {
		_domains = kDefaultDomains;
	}
}

void TgWsProxy::saveDomains() {
	QSettings s("MakuGram", "MakuGram");
	s.setValue("tgWsProxyDomains", _domains);
}

void TgWsProxy::start() {
	if (isRunning()) {
		return;
	}
	_port = Settings::instance().tgWsProxyPort();
	if (_port == 0) {
		_port = 1443;
	}

	_server = std::make_unique<QTcpServer>(this);
	connect(_server.get(), &QTcpServer::newConnection, this, [=] {
		onNewConnection();
	});

	if (!_server->listen(QHostAddress::LocalHost, _port)) {
		// Port may be busy, try alternate port 14430 or any free port
		if (!_server->listen(QHostAddress::LocalHost, 14430)) {
			_server->listen(QHostAddress::LocalHost, 0);
		}
		_port = _server->serverPort();
		Settings::instance().setTgWsProxyPort(_port);
	}
}

void TgWsProxy::stop() {
	_bridges.clear();
	if (_server) {
		_server->close();
		_server.reset();
	}
}

bool TgWsProxy::isRunning() const {
	return _server && _server->isListening();
}

uint16 TgWsProxy::port() const {
	return _port;
}

int TgWsProxy::domainCount() const {
	return _domains.size();
}

QStringList TgWsProxy::domains() const {
	return _domains;
}

void TgWsProxy::onNewConnection() {
	while (_server && _server->hasPendingConnections()) {
		if (auto socket = _server->nextPendingConnection()) {
			_bridges.push_back(std::make_unique<TgWsProxyBridge>(socket, this));
		}
	}
}

void TgWsProxy::removeBridge(TgWsProxyBridge *bridge) {
	crl::on_main([this, bridge] {
		for (auto it = _bridges.begin(); it != _bridges.end(); ++it) {
			if (it->get() == bridge) {
				_bridges.erase(it);
				break;
			}
		}
	});
}

QString TgWsProxy::selectHostForDc(int dc) {
	const auto worker = Settings::instance().tgWsProxyWorker().trimmed();
	if (!worker.isEmpty()) {
		return worker;
	}
	if (!_domains.isEmpty()) {
		const auto idx = int(base::RandomIndex(_domains.size()));
		const auto baseDomain = _domains[idx];
		return u"kws%1.%2"_q.arg(dc).arg(baseDomain);
	}
	return OfficialTelegramWebGateway(dc);
}

void TgWsProxy::updateDomainsFromGitHub(std::function<void(bool success, int count)> callback) {
	if (_updating) {
		if (callback) {
			callback(false, _domains.size());
		}
		return;
	}
	_updating = true;

	if (!_network) {
		_network = std::make_unique<QNetworkAccessManager>(this);
	}

	const auto url = QUrl(u"https://raw.githubusercontent.com/Flowseal/tg-ws-proxy/main/.github/cfproxy-domains.txt"_q);
	auto request = QNetworkRequest(url);
	request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
	request.setRawHeader("User-Agent", "MakuGram-App");

	const auto reply = _network->get(request);
	connect(reply, &QNetworkReply::finished, this, [=] {
		_updating = false;
		reply->deleteLater();

		if (reply->error() != QNetworkReply::NoError) {
			if (callback) {
				callback(false, _domains.size());
			}
			return;
		}

		const auto content = QString::fromUtf8(reply->readAll());
		const auto lines = content.split('\n');
		QStringList list;
		for (auto line : lines) {
			line = line.trimmed();
			if (!line.isEmpty() && !line.startsWith('#') && line.contains('.')) {
				list.append(line);
			}
		}

		if (!list.isEmpty()) {
			_domains = list;
			saveDomains();
			if (callback) {
				callback(true, _domains.size());
			}
		} else {
			if (callback) {
				callback(false, _domains.size());
			}
		}
	});
}

void TgWsProxy::applyToProxySettings(bool enabled) {
	auto &proxy = Core::App().settings().proxy();
	auto list = proxy.list();
	const auto localHost = u"127.0.0.1"_q;
	const auto localPort = uint32(port());

	int existingIdx = -1;
	for (int i = 0; i < int(list.size()); ++i) {
		if (list[i].host == localHost && list[i].port == localPort) {
			existingIdx = i;
			break;
		}
	}

	if (enabled) {
		start();
		MTP::ProxyData wsProxy;
		wsProxy.type = MTP::ProxyData::Type::Socks5;
		wsProxy.host = localHost;
		wsProxy.port = localPort;

		if (existingIdx >= 0) {
			list[existingIdx] = wsProxy;
		} else {
			list.insert(list.begin(), wsProxy);
		}
		proxy.setList(list);
		proxy.setSelected(wsProxy);
		proxy.setSettings(MTP::ProxyData::Settings::Enabled);
	} else {
		if (proxy.selected().host == localHost && proxy.selected().port == localPort) {
			proxy.setSettings(MTP::ProxyData::Settings::Disabled);
		}
		stop();
	}
	Core::App().saveSettingsDelayed();
}

// ---------------- TgWsProxyBridge ----------------

TgWsProxyBridge::TgWsProxyBridge(QTcpSocket *socket, TgWsProxy *parent)
: _client(socket)
, _parent(parent) {
	_client->setParent(this);

	connect(_client, &QTcpSocket::readyRead, this, [=] {
		onClientReadyRead();
	});
	connect(_client, &QTcpSocket::disconnected, this, [=] {
		close();
	});
}

TgWsProxyBridge::~TgWsProxyBridge() {
	if (_client) {
		_client->abort();
	}
	if (_ws) {
		_ws->close();
	}
}

void TgWsProxyBridge::close() {
	if (_stage == Stage::Closed) {
		return;
	}
	_stage = Stage::Closed;
	if (_client) {
		_client->disconnectFromHost();
	}
	if (_ws) {
		_ws->close();
	}
	if (_parent) {
		_parent->removeBridge(this);
	}
}

void TgWsProxyBridge::onClientReadyRead() {
	if (!_client) {
		return;
	}
	const auto data = _client->readAll();
	if (data.isEmpty()) {
		return;
	}

	if (_stage == Stage::Piping) {
		if (_wsConnected && _ws) {
			_ws->sendBinary(data);
		} else {
			_pendingClientData.append(data);
		}
		return;
	}

	_pendingClientData.append(data);

	if (_stage == Stage::Initial) {
		if (_pendingClientData.isEmpty()) {
			return;
		}
		const auto firstByte = quint8(_pendingClientData[0]);
		if (firstByte == 0x05) {
			// SOCKS5 handshake
			_stage = Stage::Socks5Greeting;
			handleSocks5Handshake();
		} else if (_pendingClientData.size() >= 64) {
			// Direct MTProto obfs2 handshake
			const auto dcBytes = _pendingClientData.mid(60, 2);
			if (dcBytes.size() == 2) {
				const auto dcIdx = qFromLittleEndian<qint16>(dcBytes.constData());
				_dc = std::clamp(std::abs(int(dcIdx)), 1, 5);
			}
			_stage = Stage::Piping;
			connectWebSocket(_dc);
		}
	} else if (_stage == Stage::Socks5Request) {
		handleSocks5Handshake();
	}
}

void TgWsProxyBridge::handleSocks5Handshake() {
	if (_stage == Stage::Socks5Greeting) {
		if (_pendingClientData.size() < 2) {
			return;
		}
		const auto nmethods = quint8(_pendingClientData[1]);
		if (_pendingClientData.size() < 2 + nmethods) {
			return;
		}
		_pendingClientData.remove(0, 2 + nmethods);

		// Reply: version 5, no authentication (0x00)
		const char reply[2] = { 0x05, 0x00 };
		_client->write(reply, 2);

		_stage = Stage::Socks5Request;
	}

	if (_stage == Stage::Socks5Request) {
		if (_pendingClientData.size() < 10) {
			return;
		}
		// 0x05, 0x01 (CONNECT), 0x00 (RSV), 0x01 (IPv4)
		const auto cmd = quint8(_pendingClientData[1]);
		const auto atyp = quint8(_pendingClientData[3]);

		if (cmd == 0x01) { // CONNECT
			if (atyp == 0x01) { // IPv4
				if (_pendingClientData.size() < 10) {
					return;
				}
				const auto ipBytes = _pendingClientData.mid(4, 4);
				const auto ip = qFromBigEndian<quint32>(ipBytes.constData());
				_dc = DcFromIp(QHostAddress(ip));
				_pendingClientData.remove(0, 10);
			} else if (atyp == 0x03) { // Domain name
				const auto len = quint8(_pendingClientData[4]);
				if (_pendingClientData.size() < 7 + len) {
					return;
				}
				_dc = 2; // default to DC 2
				_pendingClientData.remove(0, 7 + len);
			}

			// Reply SOCKS5 Success
			const char successReply[10] = {
				0x05, 0x00, 0x00, 0x01,
				0x00, 0x00, 0x00, 0x00,
				0x00, 0x00
			};
			_client->write(successReply, 10);

			_stage = Stage::Piping;
			connectWebSocket(_dc);
		} else {
			close();
		}
	}
}

void TgWsProxyBridge::connectWebSocket(int dc) {
	const auto host = _parent->selectHostForDc(dc);

	_ws = std::make_unique<Core::WebSocketClient>(this);
	_ws->onConnected = [=] {
		_wsConnected = true;
		flushBuffer();
	};
	_ws->onBinary = [=](QByteArray data) {
		if (_client) {
			_client->write(data);
		}
	};
	_ws->onClosed = [=] {
		close();
	};

	_ws->connectTo(host, 443, u"/apiws"_q, u"binary"_q);
}

void TgWsProxyBridge::flushBuffer() {
	if (_wsConnected && _ws && !_pendingClientData.isEmpty()) {
		_ws->sendBinary(_pendingClientData);
		_pendingClientData.clear();
	}
}

} // namespace MakuGram
