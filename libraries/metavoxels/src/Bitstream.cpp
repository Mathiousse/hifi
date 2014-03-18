//
//  Bitstream.cpp
//  metavoxels
//
//  Created by Andrzej Kapolka on 12/2/13.
//  Copyright (c) 2013 High Fidelity, Inc. All rights reserved.
//

#include <cstring>

#include <QCryptographicHash>
#include <QDataStream>
#include <QMetaType>
#include <QUrl>
#include <QtDebug>

#include <RegisteredMetaTypes.h>
#include <SharedUtil.h>

#include "AttributeRegistry.h"
#include "Bitstream.h"
#include "ScriptCache.h"

REGISTER_SIMPLE_TYPE_STREAMER(bool)
REGISTER_SIMPLE_TYPE_STREAMER(int)
REGISTER_SIMPLE_TYPE_STREAMER(uint)
REGISTER_SIMPLE_TYPE_STREAMER(float)
REGISTER_SIMPLE_TYPE_STREAMER(QByteArray)
REGISTER_SIMPLE_TYPE_STREAMER(QColor)
REGISTER_SIMPLE_TYPE_STREAMER(QString)
REGISTER_SIMPLE_TYPE_STREAMER(QUrl)
REGISTER_SIMPLE_TYPE_STREAMER(QVariantList)
REGISTER_SIMPLE_TYPE_STREAMER(QVariantHash)
REGISTER_SIMPLE_TYPE_STREAMER(SharedObjectPointer)

// some types don't quite work with our macro
static int vec3Streamer = Bitstream::registerTypeStreamer(qMetaTypeId<glm::vec3>(), new SimpleTypeStreamer<glm::vec3>());
static int metaObjectStreamer = Bitstream::registerTypeStreamer(qMetaTypeId<const QMetaObject*>(),
    new SimpleTypeStreamer<const QMetaObject*>());

IDStreamer::IDStreamer(Bitstream& stream) :
    _stream(stream),
    _bits(1) {
}

void IDStreamer::setBitsFromValue(int value) {
    _bits = 1;
    while (value >= (1 << _bits) - 1) {
        _bits++;
    }
}

IDStreamer& IDStreamer::operator<<(int value) {
    _stream.write(&value, _bits);
    if (value == (1 << _bits) - 1) {
        _bits++;
    }
    return *this;
}

IDStreamer& IDStreamer::operator>>(int& value) {
    value = 0;
    _stream.read(&value, _bits);
    if (value == (1 << _bits) - 1) {
        _bits++;
    }
    return *this;
}

int Bitstream::registerMetaObject(const char* className, const QMetaObject* metaObject) {
    getMetaObjects().insert(className, metaObject);
    
    // register it as a subclass of itself and all of its superclasses
    for (const QMetaObject* superClass = metaObject; superClass; superClass = superClass->superClass()) {
        getMetaObjectSubClasses().insert(superClass, metaObject);
    }
    return 0;
}

int Bitstream::registerTypeStreamer(int type, TypeStreamer* streamer) {
    streamer->setType(type);
    getTypeStreamers().insert(type, streamer);
    return 0;
}

const TypeStreamer* Bitstream::getTypeStreamer(int type) {
    return getTypeStreamers().value(type);
}

const QMetaObject* Bitstream::getMetaObject(const QByteArray& className) {
    return getMetaObjects().value(className);
}

QList<const QMetaObject*> Bitstream::getMetaObjectSubClasses(const QMetaObject* metaObject) {
    return getMetaObjectSubClasses().values(metaObject);
}

Bitstream::Bitstream(QDataStream& underlying, MetadataType metadataType, QObject* parent) :
    QObject(parent),
    _underlying(underlying),
    _byte(0),
    _position(0),
    _metadataType(metadataType),
    _metaObjectStreamer(*this),
    _typeStreamerStreamer(*this),
    _attributeStreamer(*this),
    _scriptStringStreamer(*this),
    _sharedObjectStreamer(*this) {
}

void Bitstream::addMetaObjectSubstitution(const QByteArray& className, const QMetaObject* metaObject) {
    _metaObjectSubstitutions.insert(className, metaObject);
}

void Bitstream::addTypeSubstitution(const QByteArray& typeName, int type) {
    _typeStreamerSubstitutions.insert(typeName, getTypeStreamers().value(type));
}

const int LAST_BIT_POSITION = BITS_IN_BYTE - 1;

Bitstream& Bitstream::write(const void* data, int bits, int offset) {
    const quint8* source = (const quint8*)data;
    while (bits > 0) {
        int bitsToWrite = qMin(BITS_IN_BYTE - _position, qMin(BITS_IN_BYTE - offset, bits));
        _byte |= ((*source >> offset) & ((1 << bitsToWrite) - 1)) << _position;
        if ((_position += bitsToWrite) == BITS_IN_BYTE) {
            flush();
        }
        if ((offset += bitsToWrite) == BITS_IN_BYTE) {
            source++;
            offset = 0;
        }
        bits -= bitsToWrite;
    }
    return *this;
}

Bitstream& Bitstream::read(void* data, int bits, int offset) {
    quint8* dest = (quint8*)data;
    while (bits > 0) {
        if (_position == 0) {
            _underlying >> _byte;
        }
        int bitsToRead = qMin(BITS_IN_BYTE - _position, qMin(BITS_IN_BYTE - offset, bits));
        int mask = ((1 << bitsToRead) - 1) << offset;
        *dest = (*dest & ~mask) | (((_byte >> _position) << offset) & mask);
        _position = (_position + bitsToRead) & LAST_BIT_POSITION;
        if ((offset += bitsToRead) == BITS_IN_BYTE) {
            dest++;
            offset = 0;
        }
        bits -= bitsToRead;
    }
    return *this;
}

void Bitstream::flush() {
    if (_position != 0) {
        _underlying << _byte;
        reset();
    }
}

void Bitstream::reset() {
    _byte = 0;
    _position = 0;
}

Bitstream::WriteMappings Bitstream::getAndResetWriteMappings() {
    WriteMappings mappings = { _metaObjectStreamer.getAndResetTransientOffsets(),
        _typeStreamerStreamer.getAndResetTransientOffsets(),
        _attributeStreamer.getAndResetTransientOffsets(),
        _scriptStringStreamer.getAndResetTransientOffsets(),
        _sharedObjectStreamer.getAndResetTransientOffsets() };
    return mappings;
}

void Bitstream::persistWriteMappings(const WriteMappings& mappings) {
    _metaObjectStreamer.persistTransientOffsets(mappings.metaObjectOffsets);
    _typeStreamerStreamer.persistTransientOffsets(mappings.typeStreamerOffsets);
    _attributeStreamer.persistTransientOffsets(mappings.attributeOffsets);
    _scriptStringStreamer.persistTransientOffsets(mappings.scriptStringOffsets);
    _sharedObjectStreamer.persistTransientOffsets(mappings.sharedObjectOffsets);
    
    // find out when shared objects are deleted in order to clear their mappings
    for (QHash<SharedObjectPointer, int>::const_iterator it = mappings.sharedObjectOffsets.constBegin();
            it != mappings.sharedObjectOffsets.constEnd(); it++) {
        if (it.key()) {
            connect(it.key().data(), SIGNAL(destroyed(QObject*)), SLOT(clearSharedObject(QObject*)));
        }
    }
}

void Bitstream::persistAndResetWriteMappings() {
    persistWriteMappings(getAndResetWriteMappings());
}

Bitstream::ReadMappings Bitstream::getAndResetReadMappings() {
    ReadMappings mappings = { _metaObjectStreamer.getAndResetTransientValues(),
        _typeStreamerStreamer.getAndResetTransientValues(),
        _attributeStreamer.getAndResetTransientValues(),
        _scriptStringStreamer.getAndResetTransientValues(),
        _sharedObjectStreamer.getAndResetTransientValues() };
    return mappings;
}

void Bitstream::persistReadMappings(const ReadMappings& mappings) {
    _metaObjectStreamer.persistTransientValues(mappings.metaObjectValues);
    _typeStreamerStreamer.persistTransientValues(mappings.typeStreamerValues);
    _attributeStreamer.persistTransientValues(mappings.attributeValues);
    _scriptStringStreamer.persistTransientValues(mappings.scriptStringValues);
    _sharedObjectStreamer.persistTransientValues(mappings.sharedObjectValues);
}

void Bitstream::persistAndResetReadMappings() {
    persistReadMappings(getAndResetReadMappings());
}

void Bitstream::clearSharedObject(int id) {
    SharedObjectPointer object = _sharedObjectStreamer.takePersistentValue(id);
    if (object) {
        _weakSharedObjectHash.remove(object->getRemoteID());
    }
}

Bitstream& Bitstream::operator<<(bool value) {
    if (value) {
        _byte |= (1 << _position);
    }
    if (++_position == BITS_IN_BYTE) {
        flush();
    }
    return *this;
}

Bitstream& Bitstream::operator>>(bool& value) {
    if (_position == 0) {
        _underlying >> _byte;
    }
    value = _byte & (1 << _position);
    _position = (_position + 1) & LAST_BIT_POSITION;
    return *this;
}

Bitstream& Bitstream::operator<<(int value) {
    return write(&value, 32);
}

Bitstream& Bitstream::operator>>(int& value) {
    qint32 sizedValue;
    read(&sizedValue, 32);
    value = sizedValue;
    return *this;
}

Bitstream& Bitstream::operator<<(uint value) {
    return write(&value, 32);
}

Bitstream& Bitstream::operator>>(uint& value) {
    quint32 sizedValue;
    read(&sizedValue, 32);
    value = sizedValue;
    return *this;
}

Bitstream& Bitstream::operator<<(float value) {
    return write(&value, 32);
}

Bitstream& Bitstream::operator>>(float& value) {
    return read(&value, 32);
}

Bitstream& Bitstream::operator<<(const glm::vec3& value) {
    return *this << value.x << value.y << value.z;
}

Bitstream& Bitstream::operator>>(glm::vec3& value) {
    return *this >> value.x >> value.y >> value.z;
}

Bitstream& Bitstream::operator<<(const QByteArray& string) {
    *this << string.size();
    return write(string.constData(), string.size() * BITS_IN_BYTE);
}

Bitstream& Bitstream::operator>>(QByteArray& string) {
    int size;
    *this >> size;
    string.resize(size);
    return read(string.data(), size * BITS_IN_BYTE);
}

Bitstream& Bitstream::operator<<(const QColor& color) {
    return *this << (int)color.rgba();
}

Bitstream& Bitstream::operator>>(QColor& color) {
    int rgba;
    *this >> rgba;
    color.setRgba(rgba);
    return *this;
}

Bitstream& Bitstream::operator<<(const QString& string) {
    *this << string.size();    
    return write(string.constData(), string.size() * sizeof(QChar) * BITS_IN_BYTE);
}

Bitstream& Bitstream::operator>>(QString& string) {
    int size;
    *this >> size;
    string.resize(size);
    return read(string.data(), size * sizeof(QChar) * BITS_IN_BYTE);
}

Bitstream& Bitstream::operator<<(const QUrl& url) {
    return *this << url.toString();
}

Bitstream& Bitstream::operator>>(QUrl& url) {
    QString string;
    *this >> string;
    url = string;
    return *this;
}

Bitstream& Bitstream::operator<<(const QVariant& value) {
    const TypeStreamer* streamer = getTypeStreamers().value(value.userType());
    if (streamer) {
        _typeStreamerStreamer << streamer;
        streamer->write(*this, value);
    } else {
        qWarning() << "Non-streamable type: " << value.typeName() << "\n";
    }
    return *this;
}

Bitstream& Bitstream::operator>>(QVariant& value) {
    TypeReader reader;
    _typeStreamerStreamer >> reader;
    value = reader.read(*this);
    return *this;
}

Bitstream& Bitstream::operator<<(const AttributeValue& attributeValue) {
    _attributeStreamer << attributeValue.getAttribute();
    if (attributeValue.getAttribute()) {
        attributeValue.getAttribute()->write(*this, attributeValue.getValue(), true);
    }
    return *this;
}

Bitstream& Bitstream::operator>>(OwnedAttributeValue& attributeValue) {
    AttributePointer attribute;
    _attributeStreamer >> attribute;
    if (attribute) {
        void* value = attribute->create();
        attribute->read(*this, value, true);
        attributeValue = AttributeValue(attribute, value);
        attribute->destroy(value);
        
    } else {
        attributeValue = AttributeValue();
    }
    return *this;
}

Bitstream& Bitstream::operator<<(const QObject* object) {
    if (!object) {
        _metaObjectStreamer << NULL;
        return *this;
    }
    const QMetaObject* metaObject = object->metaObject();
    _metaObjectStreamer << metaObject;
    for (int i = 0; i < metaObject->propertyCount(); i++) {
        QMetaProperty property = metaObject->property(i);
        if (!property.isStored(object)) {
            continue;
        }
        const TypeStreamer* streamer = getTypeStreamers().value(property.userType());
        if (streamer) {
            streamer->write(*this, property.read(object));
        }
    }
    return *this;
}

Bitstream& Bitstream::operator>>(QObject*& object) {
    ObjectReader objectReader;
    _metaObjectStreamer >> objectReader;
    object = objectReader.read(*this);
    return *this;
}

Bitstream& Bitstream::operator<<(const QMetaObject* metaObject) {
    _metaObjectStreamer << metaObject;
    return *this;
}

Bitstream& Bitstream::operator>>(const QMetaObject*& metaObject) {
    ObjectReader objectReader;
    _metaObjectStreamer >> objectReader;
    metaObject = objectReader.getMetaObject();
    return *this;
}

Bitstream& Bitstream::operator>>(ObjectReader& objectReader) {
    _metaObjectStreamer >> objectReader;
    return *this;
}

Bitstream& Bitstream::operator<<(const TypeStreamer* streamer) {
    _typeStreamerStreamer << streamer;    
    return *this;
}

Bitstream& Bitstream::operator>>(const TypeStreamer*& streamer) {
    TypeReader typeReader;
    _typeStreamerStreamer >> typeReader;
    streamer = typeReader.getStreamer();
    return *this;
}

Bitstream& Bitstream::operator>>(TypeReader& reader) {
    _typeStreamerStreamer >> reader;
    return *this;
}

Bitstream& Bitstream::operator<<(const AttributePointer& attribute) {
    _attributeStreamer << attribute;
    return *this;
}

Bitstream& Bitstream::operator>>(AttributePointer& attribute) {
    _attributeStreamer >> attribute;
    return *this;
}

Bitstream& Bitstream::operator<<(const QScriptString& string) {
    _scriptStringStreamer << string;
    return *this;
}

Bitstream& Bitstream::operator>>(QScriptString& string) {
    _scriptStringStreamer >> string;
    return *this;
}

Bitstream& Bitstream::operator<<(const SharedObjectPointer& object) {
    _sharedObjectStreamer << object;
    return *this;
}

Bitstream& Bitstream::operator>>(SharedObjectPointer& object) {
    _sharedObjectStreamer >> object;
    return *this;
}

Bitstream& Bitstream::operator<(const QMetaObject* metaObject) {
    if (!metaObject) {
        return *this << QByteArray();
    }
    *this << QByteArray::fromRawData(metaObject->className(), strlen(metaObject->className()));
    if (_metadataType == NO_METADATA) {
        return *this;
    }
    int storedPropertyCount = 0;
    for (int i = 0; i < metaObject->propertyCount(); i++) {
        QMetaProperty property = metaObject->property(i);
        if (property.isStored() && getTypeStreamers().contains(property.userType())) {
            storedPropertyCount++;
        }    
    }
    *this << storedPropertyCount;
    QCryptographicHash hash(QCryptographicHash::Md5);
    for (int i = 0; i < metaObject->propertyCount(); i++) {
        QMetaProperty property = metaObject->property(i);
        if (!property.isStored()) {
            continue;
        }
        const TypeStreamer* typeStreamer = getTypeStreamers().value(property.userType());
        if (!typeStreamer) {
            continue;
        }
        _typeStreamerStreamer << typeStreamer;
        if (_metadataType == FULL_METADATA) {
            *this << QByteArray::fromRawData(property.name(), strlen(property.name()));
        } else {
            hash.addData(property.name(), strlen(property.name()) + 1);
        }
    }
    if (_metadataType == HASH_METADATA) {
        QByteArray hashResult = hash.result();
        write(hashResult.constData(), hashResult.size() * BITS_IN_BYTE);
    }
    return *this;
}

Bitstream& Bitstream::operator>(ObjectReader& objectReader) {
    QByteArray className;
    *this >> className;
    if (className.isEmpty()) {
        objectReader = ObjectReader();
        return *this;
    }
    const QMetaObject* metaObject = _metaObjectSubstitutions.value(className);
    if (!metaObject) {
        metaObject = getMetaObjects().value(className);
    }
    if (!metaObject) {
        qWarning() << "Unknown class name: " << className << "\n";
    }
    if (_metadataType == NO_METADATA) {
        objectReader = ObjectReader(className, metaObject, getPropertyReaders(metaObject));
        return *this;
    }
    int storedPropertyCount;
    *this >> storedPropertyCount;
    QVector<PropertyReader> properties(storedPropertyCount);
    for (int i = 0; i < storedPropertyCount; i++) {
        TypeReader typeReader;
        *this >> typeReader;
        QMetaProperty property = QMetaProperty();
        if (_metadataType == FULL_METADATA) {
            QByteArray propertyName;
            *this >> propertyName;
            if (metaObject) {
                property = metaObject->property(metaObject->indexOfProperty(propertyName));
            }
        }
        properties[i] = PropertyReader(typeReader, property);
    }
    // for hash metadata, check the names/types of the properties as well as the name hash against our own class
    if (_metadataType == HASH_METADATA) {
        QCryptographicHash hash(QCryptographicHash::Md5);
        bool matches = true;
        if (metaObject) {
            int propertyIndex = 0;
            for (int i = 0; i < metaObject->propertyCount(); i++) {
                QMetaProperty property = metaObject->property(i);
                if (!property.isStored()) {
                    continue;
                }
                const TypeStreamer* typeStreamer = getTypeStreamers().value(property.userType());
                if (!typeStreamer) {
                    continue;
                }
                if (propertyIndex >= properties.size() ||
                        !properties.at(propertyIndex).getReader().matchesExactly(typeStreamer)) {
                    matches = false;
                    break;
                }
                hash.addData(property.name(), strlen(property.name()) + 1); 
                propertyIndex++;
            }
            if (propertyIndex != properties.size()) {
                matches = false;
            }
        }
        QByteArray localHashResult = hash.result();
        QByteArray remoteHashResult(localHashResult.size(), 0);
        read(remoteHashResult.data(), remoteHashResult.size() * BITS_IN_BYTE);
        if (metaObject && matches && localHashResult == remoteHashResult) {
            objectReader = ObjectReader(className, metaObject, getPropertyReaders(metaObject));
            return *this;
        }
    }
    objectReader = ObjectReader(className, metaObject, properties);
    return *this;
}

Bitstream& Bitstream::operator<(const TypeStreamer* streamer) {
    const char* typeName = QMetaType::typeName(streamer->getType());
    *this << QByteArray::fromRawData(typeName, strlen(typeName));
    if (_metadataType == NO_METADATA) {
        return *this;
    }
    const QVector<MetaField>& metaFields = streamer->getMetaFields();
    *this << metaFields.size();
    if (metaFields.isEmpty()) {
        return *this;
    }
    QCryptographicHash hash(QCryptographicHash::Md5);
    foreach (const MetaField& metaField, metaFields) {
        _typeStreamerStreamer << metaField.getStreamer();
        if (_metadataType == FULL_METADATA) {
            *this << metaField.getName();
        } else {
            hash.addData(metaField.getName().constData(), metaField.getName().size() + 1);
        }
    }
    if (_metadataType == HASH_METADATA) {
        QByteArray hashResult = hash.result();
        write(hashResult.constData(), hashResult.size() * BITS_IN_BYTE);
    }
    return *this;
}

Bitstream& Bitstream::operator>(TypeReader& reader) {
    QByteArray typeName;
    *this >> typeName;
    const TypeStreamer* streamer = _typeStreamerSubstitutions.value(typeName);
    if (!streamer) {
        streamer = getTypeStreamers().value(QMetaType::type(typeName.constData()));
    }
    if (!streamer) {
        qWarning() << "Unknown type name: " << typeName << "\n";
    }
    if (_metadataType == NO_METADATA) {
        reader = TypeReader(typeName, streamer);
        return *this;
    }
    int fieldCount;
    *this >> fieldCount;
    QVector<FieldReader> fields(fieldCount);
    for (int i = 0; i < fieldCount; i++) {
        TypeReader typeReader;
        *this >> typeReader;
        int index = -1;
        if (_metadataType == FULL_METADATA) {
            QByteArray fieldName;
            *this >> fieldName;
            if (streamer) {
                index = streamer->getFieldIndex(fieldName);
            }
        }
        fields[i] = FieldReader(typeReader, index);
    }
    // for hash metadata, check the names/types of the fields as well as the name hash against our own class
    if (_metadataType == HASH_METADATA) {
        QCryptographicHash hash(QCryptographicHash::Md5);
        bool matches = true;
        if (streamer) {
            const QVector<MetaField>& localFields = streamer->getMetaFields();
            if (fieldCount != localFields.size()) {
                matches = false;
                
            } else {
                if (fieldCount == 0) {
                    reader = TypeReader(typeName, streamer);
                    return *this;
                }
                for (int i = 0; i < fieldCount; i++) {
                    const MetaField& localField = localFields.at(i);
                    if (!fields.at(i).getReader().matchesExactly(localField.getStreamer())) {
                        matches = false;
                        break;
                    }
                    hash.addData(localField.getName().constData(), localField.getName().size() + 1);
                }   
            }
        }
        QByteArray localHashResult = hash.result();
        QByteArray remoteHashResult(localHashResult.size(), 0);
        read(remoteHashResult.data(), remoteHashResult.size() * BITS_IN_BYTE);
        if (streamer && matches && localHashResult == remoteHashResult) {
            // since everything is the same, we can use the default streamer
            reader = TypeReader(typeName, streamer);
            return *this;
        }
    } else if (streamer) {
        // if all fields are the same type and in the right order, we can use the (more efficient) default streamer
        const QVector<MetaField>& localFields = streamer->getMetaFields();
        if (fieldCount != localFields.size()) {
            reader = TypeReader(typeName, streamer, false, fields);
            return *this;
        }
        for (int i = 0; i < fieldCount; i++) {
            const FieldReader& fieldReader = fields.at(i);
            if (!fieldReader.getReader().matchesExactly(localFields.at(i).getStreamer()) || fieldReader.getIndex() != i) {
                reader = TypeReader(typeName, streamer, false, fields);
                return *this;
            }
        }
        reader = TypeReader(typeName, streamer);
        return *this;
    }
    reader = TypeReader(typeName, streamer, false, fields);
    return *this;
}

Bitstream& Bitstream::operator<(const AttributePointer& attribute) {
    return *this << (QObject*)attribute.data();
}

Bitstream& Bitstream::operator>(AttributePointer& attribute) {
    QObject* object;
    *this >> object;
    attribute = AttributeRegistry::getInstance()->registerAttribute(static_cast<Attribute*>(object));
    return *this;
}

Bitstream& Bitstream::operator<(const QScriptString& string) {
    return *this << string.toString();
}

Bitstream& Bitstream::operator>(QScriptString& string) {
    QString rawString;
    *this >> rawString;
    string = ScriptCache::getInstance()->getEngine()->toStringHandle(rawString);
    return *this;
}

Bitstream& Bitstream::operator<(const SharedObjectPointer& object) {
    if (!object) {
        return *this << (int)0;
    }
    return *this << object->getID() << (QObject*)object.data();
}

Bitstream& Bitstream::operator>(SharedObjectPointer& object) {
    int id;
    *this >> id;
    if (id == 0) {
        object = SharedObjectPointer();
        return *this;
    }
    QPointer<SharedObject>& pointer = _weakSharedObjectHash[id];
    if (pointer) {
        ObjectReader objectReader;
        _metaObjectStreamer >> objectReader;
        objectReader.read(*this, pointer.data());
    
    } else {
        QObject* rawObject;
        *this >> rawObject;
        pointer = static_cast<SharedObject*>(rawObject);
        pointer->setRemoteID(id);
    }
    object = static_cast<SharedObject*>(pointer.data());
    return *this;
}

void Bitstream::clearSharedObject(QObject* object) {
    int id = _sharedObjectStreamer.takePersistentID(static_cast<SharedObject*>(object));
    if (id != 0) {
        emit sharedObjectCleared(id);
    }
}

QHash<QByteArray, const QMetaObject*>& Bitstream::getMetaObjects() {
    static QHash<QByteArray, const QMetaObject*> metaObjects;
    return metaObjects;
}

QMultiHash<const QMetaObject*, const QMetaObject*>& Bitstream::getMetaObjectSubClasses() {
    static QMultiHash<const QMetaObject*, const QMetaObject*> metaObjectSubClasses;
    return metaObjectSubClasses;
}

QHash<int, const TypeStreamer*>& Bitstream::getTypeStreamers() {
    static QHash<int, const TypeStreamer*> typeStreamers;
    return typeStreamers;
}

QVector<PropertyReader> Bitstream::getPropertyReaders(const QMetaObject* metaObject) {
    QVector<PropertyReader> propertyReaders;
    if (!metaObject) {
        return propertyReaders;
    }
    for (int i = 0; i < metaObject->propertyCount(); i++) {
        QMetaProperty property = metaObject->property(i);
        if (!property.isStored()) {
            continue;
        }
        const TypeStreamer* typeStreamer = getTypeStreamers().value(property.userType());
        if (typeStreamer) {
            propertyReaders.append(PropertyReader(TypeReader(QByteArray(), typeStreamer), property));
        }
    }
    return propertyReaders;
}

TypeReader::TypeReader(const QByteArray& typeName, const TypeStreamer* streamer,
        bool exactMatch, const QVector<FieldReader>& fields) :
    _typeName(typeName),
    _streamer(streamer),
    _exactMatch(exactMatch),
    _fields(fields) {
}

QVariant TypeReader::read(Bitstream& in) const {
    if (_exactMatch) {
        return _streamer->read(in);
    }
    QVariant object = _streamer ? QVariant(_streamer->getType(), 0) : QVariant();
    foreach (const FieldReader& field, _fields) {
        field.read(in, _streamer, object);
    }
    return object;
}

bool TypeReader::matchesExactly(const TypeStreamer* streamer) const {
    return _exactMatch && _streamer == streamer;
}

uint qHash(const TypeReader& typeReader, uint seed) {
    return qHash(typeReader.getTypeName(), seed);
}

FieldReader::FieldReader(const TypeReader& reader, int index) :
    _reader(reader),
    _index(index) {
}

void FieldReader::read(Bitstream& in, const TypeStreamer* streamer, QVariant& object) const {
    QVariant value = _reader.read(in);
    if (_index != -1 && streamer) {
        streamer->setField(_index, object, value);
    }    
}

ObjectReader::ObjectReader(const QByteArray& className, const QMetaObject* metaObject,
        const QVector<PropertyReader>& properties) :
    _className(className),
    _metaObject(metaObject),
    _properties(properties) {
}

QObject* ObjectReader::read(Bitstream& in, QObject* object) const {
    if (!object && _metaObject) {
        object = _metaObject->newInstance();
    }
    foreach (const PropertyReader& property, _properties) {
        property.read(in, object);
    }
    return object;
}

uint qHash(const ObjectReader& objectReader, uint seed) {
    return qHash(objectReader.getClassName(), seed);
}

PropertyReader::PropertyReader(const TypeReader& reader, const QMetaProperty& property) :
    _reader(reader),
    _property(property) {
}

void PropertyReader::read(Bitstream& in, QObject* object) const {
    QVariant value = _reader.read(in);
    if (_property.isValid() && object) {
        _property.write(object, value);
    }
}

MetaField::MetaField(const QByteArray& name, const TypeStreamer* streamer) :
    _name(name),
    _streamer(streamer) {
}

TypeStreamer::~TypeStreamer() {
}

const QVector<MetaField>& TypeStreamer::getMetaFields() const {
    static QVector<MetaField> emptyMetaFields;
    return emptyMetaFields;
}

int TypeStreamer::getFieldIndex(const QByteArray& name) const {
    return -1;
}

void TypeStreamer::setField(int index, QVariant& object, const QVariant& value) const {
    // nothing by default
}

const TypeStreamer* TypeStreamer::getKeyStreamer() const {
    return NULL;
}

const TypeStreamer* TypeStreamer::getValueStreamer() const {
    return NULL;
}

void TypeStreamer::append(QVariant& object, const QVariant& element) const {
    // nothing by default
}

void TypeStreamer::insert(QVariant& object, const QVariant& key, const QVariant& value) const {
    // nothing by default
}
