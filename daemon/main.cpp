/*
    SPDX-FileCopyrightText: 2026 Leonardo Nobre <info@nobrehd.pt>
    SPDX-License-Identifier: LGPL-2.0-or-later
*/

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDir>
#include <QFile>
#include <QFileSystemWatcher>
#include <QFutureWatcher>
#include <QSet>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QtConcurrentRun>
#include <QtEndian>

struct Options {
    bool once = false;
    bool dryRun = false;
};
static Options g_opts;

static QByteArray inflateZlib(const QByteArray &zdata)
{
    QByteArray buf(4, '\0');
    qToBigEndian<quint32>(256 * 1024, buf.data());
    buf.append(zdata);
    return qUncompress(buf);
}

struct ThumbMeta {
    QString mime;
    QString uri;
};

static ThumbMeta readThumbMeta(const QString &filePath)
{
    ThumbMeta meta;
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return meta;
    }

    static const char pngSig[8] = {'\x89', 'P', 'N', 'G', '\r', '\n', '\x1a', '\n'};
    if (file.read(8) != QByteArray::fromRawData(pngSig, 8)) {
        return meta;
    }

    while (!file.atEnd() && (meta.mime.isEmpty() || meta.uri.isEmpty())) {
        const QByteArray header = file.read(8);
        if (header.size() < 8) break;

        const quint32 length = qFromBigEndian<quint32>(header.constData());
        const QByteArray type = header.mid(4, 4);

        if (type == "IDAT" || type == "IEND") {
            break;
        }

        if (type == "tEXt" || type == "zTXt") {
            const QByteArray body = file.read(length);
            file.skip(4);

            const qsizetype nul = body.indexOf('\0');
            if (nul > 0) {
                const QByteArray key = body.left(nul);
                const QByteArray rest = body.mid(nul + 1);
                QString val;

                if (type == "tEXt") {
                    val = QString::fromUtf8(rest);
                } else if (type == "zTXt" && rest.size() > 1 && rest.at(0) == 0) {
                    val = QString::fromUtf8(inflateZlib(rest.mid(1)));
                }

                if (key == "Thumb::Mimetype") {
                    meta.mime = val;
                } else if (key == "Thumb::URI") {
                    meta.uri = val;
                }
            }
        } else {
            file.skip(length + 4);
        }
    }
    return meta;
}

static bool isSvgThumbnail(const ThumbMeta &m)
{
    return m.mime.startsWith(QLatin1String("image/svg"))
        || m.uri.endsWith(QLatin1String(".svg"), Qt::CaseInsensitive)
        || m.uri.endsWith(QLatin1String(".svgz"), Qt::CaseInsensitive);
}

static QString kdeglobalsPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QLatin1String("/kdeglobals");
}

static QString themeSignature()
{
    QFile file(kdeglobalsPath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QStringLiteral("none");
    }

    QCryptographicHash hash(QCryptographicHash::Sha1);
    bool inColorGroup = false;

    while (!file.atEnd()) {
        const QByteArray line = file.readLine().trimmed();
        if (line.startsWith('[')) {
            inColorGroup = line.startsWith("[Colors:") || line.startsWith("[ColorEffects:");
            if (inColorGroup) hash.addData(line);
        } else if (inColorGroup || line.startsWith("ColorScheme=") || line.startsWith("AccentColor=")) {
            hash.addData(line);
        }
    }
    return QString::fromLatin1(hash.result().toHex());
}

static QStringList purgeSvgThumbnails(bool dryRun)
{
    QStringList purgedUris;
    const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + QLatin1String("/thumbnails");

    for (const QString &sub : {QStringLiteral("normal"), QStringLiteral("large"), QStringLiteral("x-large"), QStringLiteral("xx-large")}) {
        const QFileInfoList entries = QDir(base + QLatin1Char('/') + sub).entryInfoList({QStringLiteral("*.png")}, QDir::Files);
        for (const QFileInfo &info : entries) {
            const ThumbMeta meta = readThumbMeta(info.absoluteFilePath());
            if (isSvgThumbnail(meta)) {
                if (!meta.uri.isEmpty()) {
                    purgedUris.append(meta.uri);
                }
                if (!dryRun) {
                    QFile::remove(info.absoluteFilePath());
                }
            }
        }
    }
    purgedUris.removeDuplicates();
    return purgedUris;
}

static void notifyViews(const QStringList &uris)
{
    if (uris.isEmpty()) return;

    QSet<QString> dirs;
    for (const QString &uri : uris) {
        dirs.insert(QUrl(uri).adjusted(QUrl::RemoveFilename | QUrl::StripTrailingSlash).toString(QUrl::FullyEncoded));
    }

    auto send = [](const QString &signal, const QVariant &arg) {
        QDBusMessage msg = QDBusMessage::createSignal(QStringLiteral("/"), QStringLiteral("org.kde.KDirNotify"), signal);
        msg << arg;
        QDBusConnection::sessionBus().send(msg);
    };

    send(QStringLiteral("FilesRemoved"), uris);
    for (const QString &dir : std::as_const(dirs)) {
        send(QStringLiteral("FilesAdded"), dir);
    }
}

class ThemeSyncDaemon : public QObject
{
    Q_OBJECT
public:
    explicit ThemeSyncDaemon(QObject *parent = nullptr)
        : QObject(parent)
        , m_lastSig(themeSignature())
        , m_configDir(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation))
    {
        qInfo().noquote() << "tasvg-daemon: started (theme signature:" << m_lastSig << ")";

        m_debounce.setSingleShot(true);
        m_debounce.setInterval(400);
        connect(&m_debounce, &QTimer::timeout, this, &ThemeSyncDaemon::checkTheme);
        connect(&m_watcher, &QFutureWatcher<QStringList>::finished, this, &ThemeSyncDaemon::onPurgeFinished);

        setupDbus();
        setupFileWatcher();
    }

public Q_SLOTS:
    void triggerCheck()
    {
        m_debounce.start();
    }

    void onDbusSignal(const QDBusMessage &)
    {
        triggerCheck();
    }

private:
    void setupDbus()
    {
        auto bus = QDBusConnection::sessionBus();
        bus.connect(QString(), QString(), QStringLiteral("org.freedesktop.portal.Settings"), QStringLiteral("SettingChanged"), this, SLOT(onDbusSignal(QDBusMessage)));
        bus.connect(QString(), QStringLiteral("/kdeglobals"), QStringLiteral("org.kde.kconfig.notify"), QStringLiteral("ConfigChanged"), this, SLOT(onDbusSignal(QDBusMessage)));
    }

    void setupFileWatcher()
    {
        m_fileWatcher.addPath(m_configDir);
        rearmFileWatcher();

        auto onFsEvent = [this] {
            rearmFileWatcher();
            triggerCheck();
        };
        connect(&m_fileWatcher, &QFileSystemWatcher::fileChanged, this, onFsEvent);
        connect(&m_fileWatcher, &QFileSystemWatcher::directoryChanged, this, onFsEvent);
    }

    void rearmFileWatcher()
    {
        const QString path = kdeglobalsPath();
        if (m_fileWatcher.files().contains(path)) {
            m_fileWatcher.removePath(path); // Clear old watch after atomic rename
        }
        if (QFile::exists(path)) {
            m_fileWatcher.addPath(path);
        }
    }

    void checkTheme()
    {
        const QString current = themeSignature();
        if (current == m_lastSig) return;

        qInfo().noquote() << "tasvg-daemon: theme change detected (" << m_lastSig << "->" << current << ")";
        m_lastSig = current;

        if (m_isRunning) {
            m_needsRerun = true;
            return;
        }

        m_isRunning = true;
        m_watcher.setFuture(QtConcurrent::run([] { return purgeSvgThumbnails(g_opts.dryRun); }));
    }

    void onPurgeFinished()
    {
        const QStringList uris = m_watcher.result();
        qInfo().noquote() << "tasvg-daemon: purged" << uris.size() << "SVG thumbnails";

        if (!g_opts.dryRun) {
            notifyViews(uris);
        }
        m_isRunning = false;

        if (m_needsRerun) {
            m_needsRerun = false;
            checkTheme();
        }
    }

    QString m_lastSig;
    QString m_configDir;
    QTimer m_debounce;
    QFileSystemWatcher m_fileWatcher;
    QFutureWatcher<QStringList> m_watcher;
    bool m_isRunning = false;
    bool m_needsRerun = false;
};

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("tasvg-daemon"));

    const QStringList args = app.arguments();
    g_opts.once = args.contains(QLatin1String("--once"));
    g_opts.dryRun = args.contains(QLatin1String("--dry-run"));

    if (g_opts.once || g_opts.dryRun) {
        const QStringList uris = purgeSvgThumbnails(g_opts.dryRun);
        qInfo().noquote() << "tasvg-daemon: found" << uris.size() << "SVG thumbnails";
        if (!g_opts.dryRun) {
            notifyViews(uris);
        }
        QTimer::singleShot(250, &app, &QCoreApplication::quit);
        return app.exec();
    }

    ThemeSyncDaemon daemon(&app);
    return app.exec();
}

#include "main.moc"