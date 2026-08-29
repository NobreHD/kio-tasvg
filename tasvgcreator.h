/*
    SPDX-FileCopyrightText: 2006 Pascal Létourneau <pascal.letourneau@kdemail.net>
    SPDX-FileCopyrightText: 2026 Leonardo Nobre <info@nobrehd.pt>

    SPDX-License-Identifier: LGPL-2.0-or-later
*/

#pragma once

#include <KIO/ThumbnailCreator>

class TaSvgCreator : public KIO::ThumbnailCreator {
    Q_OBJECT
public:
  TaSvgCreator(QObject *parent, const QVariantList &args);
  ~TaSvgCreator() override = default;

  KIO::ThumbnailResult create(const KIO::ThumbnailRequest &request) override;
};