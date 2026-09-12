/*
 *  This file is part of Dune Legacy.
 *
 *  Dune Legacy is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  Dune Legacy is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with Dune Legacy.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef ENETPACKETISTREAM_H
#define ENETPACKETISTREAM_H

#include <misc/InputStream.h>
#include <misc/exceptions.h>

#include <enet/enet.h>

#include <cstring>
#include <string>

/// Upper bound for a single string field inside a network packet.
/// Legitimate senders stay far below this: mod chunks are MOD_CHUNK_SIZE (64 KiB) and the
/// largest other string is a multiplayer map file. The bound exists so a malformed length
/// can never drive an allocation that is unrelated to the bytes actually received.
#define ENETPACKET_MAX_STRING_LENGTH (4 * 1024 * 1024)

class ENetPacketIStream : public InputStream
{
public:
    explicit ENetPacketIStream(ENetPacket* pPacket)
     : currentPos(0), packet(pPacket) {
        ;
    }

    explicit ENetPacketIStream(const ENetPacketIStream& p)
     : currentPos(0), packet(nullptr) {
        *this = p;
    }

    ~ENetPacketIStream() {
        if(packet != nullptr) {
            enet_packet_destroy(packet);
        }
    }

    ENetPacketIStream& operator=(const ENetPacketIStream& p) {
        if(this != &p) {
            ENetPacket* packetCopy = enet_packet_create(p.packet->data,p.packet->dataLength,p.packet->flags);
            if(packetCopy == nullptr) {
                THROW(InputStream::error, "ENetPacketIStream::operator=(): enet_packet_create() failed!");
            }

            if(packet != nullptr) {
                enet_packet_destroy(packet);
            }

            packet = packetCopy;
            currentPos = p.currentPos;
        }

        return *this;
    }

    /**
        \return the number of bytes that have not been consumed yet
    */
    size_t getRemainingLength() const override {
        const size_t dataLength = (packet != nullptr) ? packet->dataLength : 0;
        return (currentPos >= dataLength) ? 0 : (dataLength - currentPos);
    }

    std::string readString() override
    {
        Uint32 length = readUint32();

        // Subtraction form: currentPos is never allowed past dataLength, so this cannot wrap.
        // The additive form (currentPos + length) overflows on 32-bit size_t targets (wasm32).
        if(length > ENETPACKET_MAX_STRING_LENGTH || length > getRemainingLength()) {
            THROW(InputStream::eof, "ENetPacketIStream::readString(): End-of-File reached!");
        }

        std::string resultString((const char*) (packet->data + currentPos), (size_t) length);
        currentPos += (size_t) length;
        return resultString;
    }

    Uint8 readUint8() override
    {
        Uint8 tmp;
        readRaw(&tmp, sizeof(tmp), "readUint8");
        return tmp;
    }

    Uint16 readUint16() override
    {
        Uint16 tmp;
        readRaw(&tmp, sizeof(tmp), "readUint16");
        return SDL_SwapLE16(tmp);
    }

    Uint32 readUint32() override
    {
        Uint32 tmp;
        readRaw(&tmp, sizeof(tmp), "readUint32");
        return SDL_SwapLE32(tmp);
    }

    Uint64 readUint64() override
    {
        Uint64 tmp;
        readRaw(&tmp, sizeof(tmp), "readUint64");
        return SDL_SwapLE64(tmp);
    }

    bool readBool() override
    {
        // A boolean on the wire is exactly 0 or 1; anything else is a malformed packet.
        const Uint8 value = readUint8();
        if(value > 1) {
            THROW(InputStream::error, "ENetPacketIStream::readBool(): Invalid boolean encoding!");
        }
        return value == 1;
    }

    float readFloat() override
    {
        Uint32 tmp = readUint32();
        float tmp2;
        memcpy(&tmp2,&tmp,sizeof(Uint32)); // workaround for a strange optimization in gcc 4.1
        return tmp2;
    }

private:
    /**
        Copies size bytes out of the packet without ever dereferencing an unaligned typed
        pointer (undefined behaviour, and a real fault on strict-alignment targets).
    */
    void readRaw(void* destination, size_t size, const char* what) {
        if(size > getRemainingLength()) {
            THROW(InputStream::eof, "ENetPacketIStream::%s(): End-of-File reached!", what);
        }
        memcpy(destination, packet->data + currentPos, size);
        currentPos += size;
    }

    size_t  currentPos;
    ENetPacket* packet;
};

#endif // ENETPACKETISTREAM_H
