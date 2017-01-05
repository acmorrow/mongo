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

#include <asio.hpp>

#include "mongo/transport/ticket_impl.h"
#include "mongo/transport/transport_layer.h"
#include "mongo/util/net/ssl_types.h"
#include "mongo/stdx/mutex.h"

namespace mongo {

class ServiceEntryPoint;

namespace executor {
class AsyncStreamInterface;
}  // namespace executor

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

    Stats sessionStats() override;

    void end(const SessionHandle& session) override;

    void endAllSessions(Session::TagMask tags) override;

    Status start() override;

    void shutdown() override;

private:
    // Our private vocabulary types.
    class ASIOConnection;
    class ASIOSession;
    friend class ASIOSession;

    class ASIOTicket;

    using ASIOSessionHandle = std::shared_ptr<ASIOSession>;
    using ConstASIOSessionHandle = std::shared_ptr<const ASIOSession>;

    class ASIOSession : public Session {
        MONGO_DISALLOW_COPYING(ASIOSession);

    public:
        TransportLayer* getTransportLayer() const override {
            return _tl;
        }

        const HostAndPort& remote() const override;

        const HostAndPort& local() const override;

        /**
         * Starts the process of reading a new message, and returns a callback to be associated with
         * the Ticket.
         */
        static void beginRead(const ASIOSessionHandle& self, Message* message);

        /**
         * Continues a read once we have obtained a header in buffer.
         */
        static void continueRead(const ASIOSessionHandle& self, Message* message, SharedBuffer buf);

        /**
         * Starts the process of writing a new message, and returns a callback to be associated with
         * the Ticket.
         */
        static void beginWrite(const ASIOSessionHandle& self, const Message& message);

        template<typename ...Args>
        void complete(Args&&...args) {
            Status status(std::forward<Args>(args)...);
            _complete(std::move(status));
        }

        /**
         * Returns true if this session has been closed.
         */
        bool closed() const;

        /**
         * Returns the completion status of the currently pending operation. If the operation is not
         * complete, then a disengaged optional is returned. The caller should call 'work' to
         * advance the IO loop before checking again.
         */
        boost::optional<Status> getOperationStatus();

        /**
         * If the currently pending operation has completed, invoke 'callback' with the associated
         * status. Otherwise, enqueue 'callback' to be invoked later when the operation completes.
         */
        void getOperationStatus(Ticket&& ticket, TicketCallback&& callback);

        /**
         * Perform work on behalf of this or other sessions to advance the state of pending
         * operations. If the zero-argument form of getOperationStatus returns a disengaged
         * optional, this method must be called at least once before retrying getOperationStatus to
         * avoid potential busy-waiting.
         */
        void work();

    private:
        void _posted();
        void _complete(Status&& status);

        TransportLayerASIO* const _tl;
        std::unique_ptr<executor::AsyncStreamInterface> _stream;

        stdx::mutex _mutex;
        boost::optional<Status> _status;
        TicketCallback _callback;
    };

    class ASIOTicket : public TicketImpl {
        MONGO_DISALLOW_COPYING(ASIOTicket);

    public:
        explicit ASIOTicket(const ASIOSessionHandle& session, Date_t expiration)
            : _session(session), _sessionId(session->id()), _expiration(expiration) {}

        SessionId sessionId() const override {
            return _sessionId;
        }

        Date_t expiration() const override {
            return _expiration;
        }

        /**
         * If this ticket's session is still alive, return a shared_ptr. Otherwise,
         * return nullptr.
         */
        ASIOSessionHandle getSession() {
            return _session.lock();
        }

    private:
        const std::weak_ptr<ASIOSession> _session;
        const SessionId _sessionId;
        const Date_t _expiration;
    };

    void _begin_accept();

    ServiceEntryPoint* const _sep;
    AtomicWord<bool> _running;
    asio::io_service _io_service;
    std::unique_ptr<asio::ip::tcp::acceptor> _tcp_acceptor;
};

}  // namespace transport
}  // namespace mongo
