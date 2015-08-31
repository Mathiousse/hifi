//
//  SendAssetTask.h
//  assignment-client/src/assets
//
//  Created by Ryan Huffman on 2015/08/26
//  Copyright 2015 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_SendAssetTask_h
#define hifi_SendAssetTask_h

#include <QtCore/QByteArray>
#include <QtCore/QSharedPointer>
#include <QtCore/QString>
#include <QtCore/QRunnable>

#include "AssetUtils.h"
#include "AssetServer.h"
#include "Node.h"

namespace udt {
    class Packet;
}

class SendAssetTask : public QRunnable {
public:
    SendAssetTask(QSharedPointer<udt::Packet> packet, const SharedNodePointer& sendToNode, const QDir& resourcesDir);

    void run();

private:
    QSharedPointer<udt::Packet> _packet;
    MessageID _messageID;
    QByteArray _assetHash;
    QString _filePath;
    DataOffset _start;
    DataOffset _end;
    SharedNodePointer _senderNode;
    QDir _resourcesDir;
};

#endif
