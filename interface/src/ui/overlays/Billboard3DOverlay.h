//
//  Billboard3DOverlay.h
//  hifi/interface/src/ui/overlays
//
//  Created by Zander Otavka on 8/4/15.
//  Copyright 2014 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_Billboard3DOverlay_h
#define hifi_Billboard3DOverlay_h

#include "Planar3DOverlay.h"
#include "PanelAttachable.h"

class Billboard3DOverlay : public Planar3DOverlay, public PanelAttachable {
    Q_OBJECT

public:
    Billboard3DOverlay();
    Billboard3DOverlay(const Billboard3DOverlay* billboard3DOverlay);

    bool getIsFacingAvatar() const { return _isFacingAvatar; }
    void setIsFacingAvatar(bool isFacingAvatar) { _isFacingAvatar = isFacingAvatar; }

    virtual void setProperties(const QScriptValue& properties);
    virtual QScriptValue getProperty(const QString& property);

protected:
    virtual void setTransforms(Transform& transform);

private:
    bool _isFacingAvatar;
};

#endif // hifi_Billboard3DOverlay_h
