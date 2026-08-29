/*
    SPDX-FileCopyrightText: 2006 Pascal Létourneau <pascal.letourneau@kdemail.net>
    SPDX-FileCopyrightText: 2026 Leonardo Nobre <info@nobrehd.pt>

    SPDX-License-Identifier: LGPL-2.0-or-later
*/
#include "tasvgcreator.h"

#include <memory>

#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QPalette>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QSvgRenderer>

#include <KCompressionDevice>
#include <KPluginFactory>
#include <KColorScheme>
#include <KSharedConfig>

K_PLUGIN_CLASS_WITH_JSON(TaSvgCreator, "tasvgthumbnail.json")

TaSvgCreator::TaSvgCreator(QObject *parent, const QVariantList &args)
    : KIO::ThumbnailCreator(parent, args)
{
}

static QByteArray currentTextColor()
{
    KSharedConfig::Ptr config = KSharedConfig::openConfig();
    config->reparseConfiguration();
    KColorScheme scheme(QPalette::Active, KColorScheme::View, config);
    return scheme.foreground(KColorScheme::NormalText).color().name().toLatin1();
}

static void applyThemeColor(QByteArray &svgData, const QByteArray &themeColor)
{
    // 1. Support KDE Breeze stylesheets (.ColorScheme-Text { color:#... })
    if (svgData.contains("ColorScheme-Text")) {
        QString text = QString::fromUtf8(svgData);
        static const QRegularExpression breezeRe(
            QStringLiteral("(\\.ColorScheme-Text\\s*\\{[^}]*?\\bcolor\\s*:\\s*)([^;\\}]+)"),
            QRegularExpression::CaseInsensitiveOption
        );
        text.replace(breezeRe, QStringLiteral("\\1") + QString::fromLatin1(themeColor));
        svgData = text.toUtf8();
    }

    // 2. Locate root <svg> tag
    qsizetype svgStart = svgData.indexOf("<svg");
    if (svgStart == -1) svgStart = svgData.indexOf("<SVG");
    if (svgStart == -1) return;

    const qsizetype tagEnd = svgData.indexOf('>', svgStart);
    if (tagEnd == -1) return;

    const QByteArray rootTagBytes = svgData.sliced(svgStart, tagEnd - svgStart);
    QString rootTag = QString::fromUtf8(rootTagBytes);

    // 3. Skip if an explicit fixed color is defined (e.g. color="red" or style="color: blue")
    static const QRegularExpression attrColorRe(
        QStringLiteral("\\s+color\\s*=\\s*[\"']([^\"']+)[\"']"),
        QRegularExpression::CaseInsensitiveOption
    );
    const auto attrMatch = attrColorRe.match(rootTag);
    if (attrMatch.hasMatch()) {
        const QString val = attrMatch.captured(1).trimmed();
        if (val.compare(QLatin1String("currentColor"), Qt::CaseInsensitive) != 0
            && val.compare(QLatin1String("inherit"), Qt::CaseInsensitive) != 0) {
            return;
        }
    }

    static const QRegularExpression cssColorRe(
        QStringLiteral("[;\"\\s]color\\s*:\\s*([^;\"'>\\s]+)"),
        QRegularExpression::CaseInsensitiveOption
    );
    const auto cssMatch = cssColorRe.match(rootTag);
    if (cssMatch.hasMatch()) {
        const QString val = cssMatch.captured(1).trimmed();
        if (val.compare(QLatin1String("currentColor"), Qt::CaseInsensitive) != 0
            && val.compare(QLatin1String("inherit"), Qt::CaseInsensitive) != 0) {
            return;
        }
    }

    // 4. Update existing color="currentColor" or inject color="..."
    static const QRegularExpression currentColorAttrRe(
        QStringLiteral("\\s+color\\s*=\\s*([\"'])currentColor\\1"),
        QRegularExpression::CaseInsensitiveOption
    );

    if (rootTag.contains(currentColorAttrRe)) {
        rootTag.replace(currentColorAttrRe, QStringLiteral(" color=\"%1\"").arg(QString::fromLatin1(themeColor)));
        svgData.replace(svgStart, tagEnd - svgStart, rootTag.toUtf8());
    } else {
        svgData.insert(svgStart + 4, " color=\"" + themeColor + "\"");
    }
}

KIO::ThumbnailResult TaSvgCreator::create(const KIO::ThumbnailRequest &request)
{
    const QString filePath = request.url().toLocalFile();
    std::unique_ptr<QIODevice> device;

    if (filePath.endsWith(QLatin1String(".svgz"), Qt::CaseInsensitive)) {
        device = std::make_unique<KCompressionDevice>(filePath, KCompressionDevice::GZip);
    } else {
        device = std::make_unique<QFile>(filePath);
    }

    if (!device->open(QIODevice::ReadOnly)) {
        return KIO::ThumbnailResult::fail();
    }

    QByteArray svgData = device->readAll();
    applyThemeColor(svgData, currentTextColor());

    QSvgRenderer renderer(svgData);
    if (!renderer.isValid()) {
        return KIO::ThumbnailResult::fail();
    }

    const qreal dpr = request.devicePixelRatio() > 0.0 ? request.devicePixelRatio() : 1.0;
    const QSize targetSize = request.targetSize() * dpr;
    if (targetSize.isEmpty()) {
        return KIO::ThumbnailResult::fail();
    }

    QSize renderSize = targetSize;
    const QSize defaultSize = renderer.defaultSize();
    if (defaultSize.isValid() && !defaultSize.isEmpty()) {
        const QSize scaled = defaultSize.scaled(targetSize, Qt::KeepAspectRatio);
        if (!scaled.isEmpty()) {
            renderSize = scaled;
        }
    }

    QImage img(renderSize, QImage::Format_ARGB32_Premultiplied);
    img.setDevicePixelRatio(dpr);
    img.fill(Qt::transparent);

    QPainter painter(&img);
    renderer.render(&painter, QRectF(QPointF(0, 0), QSizeF(renderSize) / dpr));

    return KIO::ThumbnailResult::pass(img);
}

#include "tasvgcreator.moc"