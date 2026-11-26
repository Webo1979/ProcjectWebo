#include <QApplication>
#include <QByteArray>
#include <QCheckBox>
#include <QComboBox>
#include <QFontDatabase>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSpinBox>
#include <QTime>
#include <QTimer>
#include <gensio/gensio.h>
#include <gensio/gensio_os_funcs_public.h>
#include <map>

class NetDebug : public QWidget {
public:
    NetDebug()
    {
        setWindowTitle(QStringLiteral("网络调试助手 · gensio"));
        resize(980, 650);
        setMinimumSize(720, 480);
        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(22, 18, 22, 18);
        root->setSpacing(12);

        auto *title = new QLabel(QStringLiteral("网络调试助手"));
        title->setObjectName("title");
        root->addWidget(title);

        mode.addItems({QStringLiteral("TCP 客户端"), QStringLiteral("TCP 服务端"), QStringLiteral("UDP")});
        local.setText("127.0.0.1");
        remote.setText("127.0.0.1");
        for (auto *port : {&localPort, &remotePort}) {
            port->setRange(1, 65535);
            port->setValue(9000);
        }
        auto *config = new QGridLayout;
        config->addWidget(new QLabel(QStringLiteral("模式")), 0, 0);
        config->addWidget(&mode, 1, 0);
        config->addWidget(new QLabel(QStringLiteral("本地 IPv4")), 0, 1);
        config->addWidget(&local, 1, 1);
        config->addWidget(&localPort, 1, 2);
        config->addWidget(new QLabel(QStringLiteral("目标 IPv4")), 0, 3);
        config->addWidget(&remote, 1, 3);
        config->addWidget(&remotePort, 1, 4);
        config->addWidget(&toggle, 1, 5);
        root->addLayout(config);

        auto *bar = new QHBoxLayout;
        bar->addWidget(&status);
        bar->addStretch();
        bar->addWidget(&counts);
        bar->addWidget(&hexReceive);
        bar->addWidget(&clear);
        root->addLayout(bar);

        log.setReadOnly(true);
        log.setMaximumBlockCount(2000);
        log.setPlaceholderText(QStringLiteral("连接状态和收发记录"));
        root->addWidget(&log, 4);

        auto *sendBar = new QHBoxLayout;
        sendBar->addWidget(new QLabel(QStringLiteral("发送到")));
        sendBar->addWidget(&targets, 1);
        sendBar->addWidget(&hexSend);
        sendBar->addWidget(&newline);
        sendBar->addWidget(&send);
        root->addLayout(sendBar);
        input.setPlaceholderText(QStringLiteral("输入 UTF-8 文本，或勾选 HEX 后输入十六进制字节"));
        root->addWidget(&input, 1);

        setStyleSheet(QStringLiteral(
            "QWidget { background:#f3f6fa; color:#18304a; font-size:14px }"
            "QLabel#title { font-size:27px; font-weight:700 }"
            "QPlainTextEdit,QLineEdit,QSpinBox,QComboBox { background:white; border:1px solid #bac8d7; border-radius:5px; padding:6px }"
            "QPushButton { background:#1760ad; color:white; border:0; border-radius:5px; padding:8px 17px }"
            "QPushButton:hover { background:#104b8c }"
            "QPushButton:disabled { background:#dbe3ec; color:#637589 }"
            "QLineEdit:disabled,QSpinBox:disabled,QComboBox:disabled { background:#e7edf4; color:#66798e }"));
        log.setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        input.setFont(log.font());

        connect(&mode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { updateControls(); });
        connect(&targets, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { updateControls(); });
        connect(&toggle, &QPushButton::clicked, this, [this] { active ? stop() : start(); });
        connect(&send, &QPushButton::clicked, this, [this] { queueWrite(); });
        connect(&clear, &QPushButton::clicked, &log, &QPlainTextEdit::clear);
        connect(&hexSend, &QCheckBox::toggled, &newline, &QCheckBox::setDisabled);
        connect(&pump, &QTimer::timeout, this, [this] { service(); });

        const int error = gensio_alloc_os_funcs(0, &os, 0);
        if (error)
            reportError(QStringLiteral("初始化 gensio"), error);
        updateControls();
        updateCounts();
    }

    ~NetDebug() override
    {
        stop();
        if (os)
            gensio_os_funcs_free(os);
    }

private:
    struct Peer {
        gensio *io;
        QString name;
        QByteArray pending;
        bool ready = false;
        bool remove = false;
    };

    gensio_os_funcs *os = nullptr;
    gensio_accepter *listener = nullptr;
    std::map<int, Peer> peers;
    int nextId = 1;
    bool active = false;
    quint64 received = 0, transmitted = 0;
    QTimer pump;
    QComboBox mode, targets;
    QLineEdit local, remote;
    QSpinBox localPort, remotePort;
    QLabel status{QStringLiteral("未启动")}, counts;
    QPushButton toggle, clear{QStringLiteral("清空记录")}, send{QStringLiteral("发送")};
    QCheckBox hexReceive{QStringLiteral("接收 HEX")}, hexSend{QStringLiteral("发送 HEX")},
        newline{QStringLiteral("添加 CRLF")};
    QPlainTextEdit log, input;

    bool server() const { return mode.currentIndex() == 1; }
    bool udp() const { return mode.currentIndex() == 2; }

    static bool validAddress(const QString &address)
    {
        const auto parts = address.split('.');
        static const QRegularExpression octet(QStringLiteral("^(0|[1-9][0-9]{0,2})$"));
        if (parts.size() != 4)
            return false;
        for (const auto &part : parts)
            if (!octet.match(part).hasMatch() || part.toInt() > 255)
                return false;
        return true;
    }

    Peer *selected()
    {
        const auto it = peers.find(targets.currentData().toInt());
        return it == peers.end() ? nullptr : &it->second;
    }

    void updateControls()
    {
        mode.setEnabled(!active);
        local.setEnabled(!active && (server() || udp()));
        localPort.setEnabled(local.isEnabled());
        remote.setEnabled(!active && !server());
        remotePort.setEnabled(remote.isEnabled());
        toggle.setEnabled(os != nullptr);
        toggle.setText(active ? QStringLiteral("停止") : server() ? QStringLiteral("监听") :
                       udp() ? QStringLiteral("绑定") : QStringLiteral("连接"));
        const auto *peer = selected();
        send.setEnabled(peer && peer->ready && !peer->remove && peer->pending.isEmpty());
    }

    void append(const QString &message)
    {
        log.appendPlainText(QTime::currentTime().toString("HH:mm:ss.zzz") + "  " + message);
    }

    void reportError(const QString &where, int error)
    {
        append(where + QStringLiteral("：") + QString::fromUtf8(gensio_err_to_str(error)));
    }

    void updateCounts()
    {
        counts.setText(QStringLiteral("接收 %1 B   发送 %2 B").arg(received).arg(transmitted));
    }

    void record(bool rx, const QString &name, const QByteArray &data)
    {
        (rx ? received : transmitted) += data.size();
        const bool hex = rx ? hexReceive.isChecked() : hexSend.isChecked();
        constexpr int previewLimit = 4096;
        const QByteArray preview = data.left(previewLimit);
        QString payload = hex ? QString::fromLatin1(preview.toHex(' ').toUpper()) : QString::fromUtf8(preview);
        if (!hex) {
            payload.replace('\r', QStringLiteral("\\r"));
            payload.replace('\n', QStringLiteral("\\n"));
            payload.replace(QChar(0), QStringLiteral("\\0"));
        }
        if (data.size() > previewLimit)
            payload += QStringLiteral(" …（仅显示前 %1 B）").arg(previewLimit);
        append(QStringLiteral("%1  %2  [%3 B]  %4").arg(rx ? "RX" : "TX", name).arg(data.size()).arg(payload));
        updateCounts();
    }

    void start()
    {
        const QString bind = local.text().trimmed();
        const QString host = remote.text().trimmed();
        if (((server() || udp()) && !validAddress(bind)) || (!server() && !validAddress(host))) {
            append(QStringLiteral("请输入有效的 IPv4 地址，例如 127.0.0.1"));
            return;
        }

        const QByteArray target = host.toLatin1() + ',' + QByteArray::number(remotePort.value());
        const QByteArray source = bind.toLatin1() + ',' + QByteArray::number(localPort.value());
        int error;
        if (server()) {
            const QByteArray spec = "tcp,ipv4," + source;
            error = str_to_gensio_accepter(spec.constData(), os, acceptEvent, this, &listener);
            if (!error) {
                error = gensio_acc_startup(listener);
                if (error) {
                    gensio_acc_free(listener);
                    listener = nullptr;
                }
            }
        } else {
            gensio *io = nullptr;
            const QByteArray spec = udp()
                ? "udp(nocon,readbuf=65536,laddr=\"ipv4," + source + "\"),ipv4," + target
                : "tcp,ipv4," + target;
            error = str_to_gensio(spec.constData(), os, ioEvent, this, &io);
            if (!error) {
                addPeer(io, host + ':' + QString::number(remotePort.value()), false);
                error = gensio_open(io, opened, this);
                if (error)
                    erasePeer(peers.begin(), false);
            }
        }

        if (error) {
            reportError(QStringLiteral("启动失败"), error);
            status.setText(QStringLiteral("启动失败"));
        } else {
            active = true;
            status.setText(server() ? QStringLiteral("正在监听") : QStringLiteral("正在打开…"));
            if (server())
                append(QStringLiteral("监听 %1:%2").arg(bind).arg(localPort.value()));
            pump.start(8);
        }
        updateControls();
    }

    void stop()
    {
        pump.stop();
        const bool wasActive = active;
        active = false;
        if (listener) {
            gensio_acc_shutdown_s(listener);
            gensio_acc_free(listener);
            listener = nullptr;
        }
        while (!peers.empty())
            erasePeer(peers.begin(), true);
        if (wasActive)
            append(QStringLiteral("已停止"));
        status.setText(QStringLiteral("未启动"));
        updateControls();
    }

    void service()
    {
        for (int i = 0; i < 24; ++i) {
            gensio_time timeout{0, 0};
            if (gensio_os_funcs_service(os, &timeout))
                break;
        }
        for (auto it = peers.begin(); it != peers.end();) {
            if (it->second.remove) {
                const bool ready = it->second.ready;
                auto closing = it++;
                erasePeer(closing, ready);
            } else {
                ++it;
            }
        }
        if (!server() && peers.empty()) {
            pump.stop();
            active = false;
            if (!status.text().contains(QStringLiteral("失败")))
                status.setText(QStringLiteral("连接已关闭"));
            updateControls();
        }
    }

    void addPeer(gensio *io, const QString &name, bool ready)
    {
        const auto id = nextId++;
        peers.emplace(id, Peer{io, name, {}, ready, false});
        targets.addItem(name, id);
        if (ready)
            gensio_set_read_callback_enable(io, true);
    }

    void erasePeer(std::map<int, Peer>::iterator it, bool close)
    {
        gensio *io = it->second.io;
        if (close) {
            gensio_set_read_callback_enable(io, false);
            gensio_set_write_callback_enable(io, false);
            gensio_close_s(io);
        }
        const int index = targets.findData(it->first);
        peers.erase(it);
        targets.removeItem(index);
        gensio_free(io);
        updateControls();
    }

    Peer *findPeer(gensio *io)
    {
        for (auto &entry : peers)
            if (entry.second.io == io)
                return &entry.second;
        return nullptr;
    }

    static void opened(gensio *io, int error, void *data)
    {
        auto *self = static_cast<NetDebug *>(data);
        auto *peer = self->findPeer(io);
        if (error) {
            self->reportError(QStringLiteral("打开失败"), error);
            self->status.setText(QStringLiteral("打开失败"));
            peer->remove = true;
        } else {
            peer->ready = true;
            gensio_set_read_callback_enable(io, true);
            self->status.setText(self->udp() ? QStringLiteral("UDP 已绑定") : QStringLiteral("TCP 已连接"));
            self->append(self->status.text() + QStringLiteral(" · ") + peer->name);
        }
        self->updateControls();
    }

    static int acceptEvent(gensio_accepter *, void *data, int event, void *eventData)
    {
        if (event != GENSIO_ACC_EVENT_NEW_CONNECTION)
            return GE_NOTSUP;
        auto *self = static_cast<NetDebug *>(data);
        auto *io = static_cast<gensio *>(eventData);
        if (!self->active || self->peers.size() >= 64) {
            gensio_free(io);
            return 0;
        }
        char address[256] = {};
        gensiods length = sizeof(address);
        const int error = gensio_control(io, 0, true, GENSIO_CONTROL_RADDR, address, &length);
        const auto name = QStringLiteral("#%1 %2").arg(self->nextId)
            .arg(error ? QString() : QString::fromUtf8(address, qMin<int>(length, sizeof(address) - 1)));
        gensio_set_callback(io, ioEvent, self);
        self->addPeer(io, name, true);
        self->append(QStringLiteral("客户端已连接 · ") + name);
        self->updateControls();
        return 0;
    }

    static int ioEvent(gensio *io, void *data, int event, int error,
                       unsigned char *buffer, gensiods *length, const char *const *aux)
    {
        if (event != GENSIO_EVENT_READ && event != GENSIO_EVENT_WRITE_READY)
            return GE_NOTSUP;
        auto *self = static_cast<NetDebug *>(data);
        auto *peer = self->findPeer(io);
        if (!peer || peer->remove) {
            if (length)
                *length = 0;
            return 0;
        }
        if (error) {
            self->reportError(peer->name, error);
            peer->remove = true;
            gensio_set_read_callback_enable(io, false);
            gensio_set_write_callback_enable(io, false);
        } else if (event == GENSIO_EVENT_READ) {
            QString sender = peer->name;
            for (auto item = aux; item && *item; ++item) {
                if (qstrncmp(*item, "addr:", 5) == 0)
                    sender = QString::fromUtf8(*item + 5);
            }
            self->record(true, sender, QByteArray(reinterpret_cast<const char *>(buffer), int(*length)));
        } else {
            self->flush(*peer);
        }
        return 0;
    }

    void queueWrite()
    {
        auto *peer = selected();
        if (!peer || !peer->ready || peer->remove || !peer->pending.isEmpty())
            return;
        const auto content = input.toPlainText();
        QByteArray bytes;
        if (hexSend.isChecked()) {
            auto hex = content;
            hex.remove(QRegularExpression(QStringLiteral("\\s+")));
            static const QRegularExpression valid(QStringLiteral("^(?:[0-9a-fA-F]{2})+$"));
            if (!valid.match(hex).hasMatch()) {
                append(QStringLiteral("HEX 格式有误，每个字节需要两个十六进制字符"));
                return;
            }
            bytes = QByteArray::fromHex(hex.toLatin1());
        } else {
            bytes = content.toUtf8();
            if (newline.isChecked())
                bytes += "\r\n";
        }
        if (bytes.isEmpty() || bytes.size() > (udp() ? 65507 : 1024 * 1024)) {
            append(QStringLiteral("发送内容为空或超过单次发送上限"));
            return;
        }
        peer->pending = bytes;
        flush(*peer);
    }

    void flush(Peer &peer)
    {
        if (!peer.pending.isEmpty()) {
            gensiods count = 0;
            const int error = gensio_write(peer.io, &count,
                reinterpret_cast<const unsigned char *>(peer.pending.constData()), peer.pending.size(), nullptr);
            if (error) {
                reportError(QStringLiteral("发送失败"), error);
                peer.pending.clear();
            } else if (count) {
                record(false, peer.name, peer.pending.left(int(count)));
                peer.pending.remove(0, int(count));
                if (udp() && !peer.pending.isEmpty()) {
                    append(QStringLiteral("UDP 短写，已取消剩余数据"));
                    peer.pending.clear();
                }
            }
        }
        gensio_set_write_callback_enable(peer.io, !peer.pending.isEmpty());
        updateControls();
    }
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    NetDebug window;
    window.show();
    return app.exec();
}
