/**
 *    Copyright (C) 2016 MongoDB Inc.
 *
 *    This program is free software: you can redistribute it and/or  modify
 *    it under the terms of the GNU Affero General Public License, version 3,
 *    as published by the Free Software Foundation.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU Affero General Public License for more details.
 *
 *    You should have received a copy of the GNU Affero General Public License
 *    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the GNU Affero General Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */

#pragma once

#include <string>

#include "mongo/transport/ticket_impl.h"
#include "mongo/transport/transport_layer.h"

namespace mongo {

class AbstractMessagingPort;
class ServiceEntryPoint;

namespace transport {

/**
 * A TransportLayer implementation based on ASIO networking primitives.
 */
class TransportLayerASIO final : public TransportLayer {
    MONGO_DISALLOW_COPYING(TransportLayerASIO);

public:
    TransportLayerASIO(ServiceEntryPoint* sep);

    ~TransportLayerASIO();

    Ticket sourceMessage(const SessionHandle& session,
                         Message* message,
                         Date_t expiration = Ticket::kNoExpirationDate) override;

    Ticket sinkMessage(const SessionHandle& session,
                       const Message& message,
                       Date_t expiration = Ticket::kNoExpirationDate) override;

    Status wait(Ticket&& ticket) override;

    void asyncWait(Ticket&& ticket, TicketCallback callback) override;

    SSLPeerInfo getX509PeerInfo(const ConstSessionHandle& session) const override;

    Stats sessionStats() override;

    void end(const SessionHandle& session) override;

    void endAllSessions(transport::Session::TagMask tags) override;

    Status start() override;

    void shutdown() override;

private:
    // Our private vocabulary types.
    class Connection;
    class Session;
    class Ticket;

    using SessionHandle = std::shared_ptr<Session>;
    using ConstSessionHandle = std::shared_ptr<const Session>;

    class Connection {
    };

    class Session : public transport::Session {
        MONGO_DISALLOW_COPYING(Session);

    public:
        TransportLayer* getTransportLayer() const override {
            return _tl;
        }

        const HostAndPort& remote() const override;

        const HostAndPort& local() const override;

    private:
        TransportLayerLegacy* _tl;
    };

    class Ticket : public TicketImpl {
        MONGO_DISALLOW_COPYING(Ticket);

    public:
        SessionId sessionId() const override;
        Date_t expiration() const override;
    };

    ServiceEntryPoint* const _sep;
};

}  // namespace transport
}  // namespace mongo
