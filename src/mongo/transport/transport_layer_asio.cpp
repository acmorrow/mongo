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

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kNetwork

#include "mongo/platform/basic.h"

#include "mongo/transport/transport_layer_asio.h"

#include <boost/optional.hpp>

#include "mongo/base/checked_cast.h"
#include "mongo/executor/async_stream_interface.h"
#include "mongo/stdx/memory.h"


namespace mongo {
namespace transport {

TransportLayerASIO::TransportLayerASIO(ServiceEntryPoint* sep) : _sep(sep), _running(false), _io_service() {}

TransportLayerASIO::~TransportLayerASIO() = default;

transport::Ticket TransportLayerASIO::sourceMessage(const SessionHandle& session,
                                                    Message* message,
                                                    Date_t expiration) {
    auto asioSession = checked_pointer_cast<ASIOSession>(session);
    asioSession->beginRead(asioSession, message);
    auto ticket = stdx::make_unique<ASIOTicket>(asioSession, expiration);
    return {this, std::move(ticket)};
}

transport::Ticket TransportLayerASIO::sinkMessage(const SessionHandle& session,
                                                  const Message& message,
                                                  Date_t expiration) {
    auto asioSession = checked_pointer_cast<ASIOSession>(session);
    asioSession->beginWrite(asioSession, message);
    auto ticket = stdx::make_unique<ASIOTicket>(asioSession, expiration);
    return {this, std::move(ticket)};
}

Status TransportLayerASIO::wait(Ticket&& ticket) {

    // Take ownership, since we may.
    Ticket ownedTicket(std::move(ticket));

    if (!_running.load()) {
        return TransportLayer::ShutdownStatus;
    }

    if (ownedTicket.expiration() < Date_t::now()) {
        return Ticket::ExpiredStatus;
    }

    auto asioTicket = checked_cast<ASIOTicket*>(getTicketImpl(ownedTicket));
    auto session = asioTicket->getSession();

    if (!session) {
        return TransportLayer::TicketSessionClosedStatus;
    }

    if (session->closed())
        return TransportLayer::TicketSessionClosedStatus;

    boost::optional<Status> sessionStatus;

    try {
        while (!(sessionStatus = session->getOperationStatus())) {
            session->work();
        }
    } catch(...) {
        sessionStatus = exceptionToStatus();
    }

    // TODO: note X509 subject if available???

    return sessionStatus.get();
}

void TransportLayerASIO::asyncWait(Ticket&& ticket, TicketCallback callback) {

    // Take ownership, since we may.
    Ticket ownedTicket(std::move(ticket));

    if (!_running.load()) {
        return callback(TransportLayer::ShutdownStatus);
    }

    if (ticket.expiration() < Date_t::now()) {
        return callback(Ticket::ExpiredStatus);
    }

    auto asioTicket = checked_cast<ASIOTicket*>(getTicketImpl(ticket));
    auto session = asioTicket->getSession();

    if (!session) {
        return callback(TransportLayer::TicketSessionClosedStatus);
    }

    if (session->closed())
        return callback(TransportLayer::TicketSessionClosedStatus);

    session->getOperationStatus(std::move(ownedTicket), std::move(callback));
}

TransportLayer::Stats TransportLayerASIO::sessionStats() {
    // TODO: Stats
    return {};
}

void TransportLayerASIO::end(const SessionHandle& session) {
    MONGO_UNREACHABLE;
}

void TransportLayerASIO::endAllSessions(Session::TagMask tags) {
    MONGO_UNREACHABLE;
}

Status TransportLayerASIO::start() {
    if (_running.swap(true)) {
        return {ErrorCodes::InternalError, "TransportLayer is already running"};
    }

    // TODO: AF_UNIX, parameters, ipv6
    _tcp_acceptor = stdx::make_unique<asio::ip::tcp::acceptor>(
        asio::ip::tcp::endpoint(asio::ip::tcp::v4(), 27017));

    _begin_accept(_tcp_acceptor);

    return Status::OK();
}

void TransportLayerASIO::_begin_accept() {
    
}

void TransportLayerASIO::shutdown() {
    MONGO_UNREACHABLE;
}

const HostAndPort& TransportLayerASIO::ASIOSession::remote() const {
    MONGO_UNREACHABLE;
}

const HostAndPort& TransportLayerASIO::ASIOSession::local() const {
    MONGO_UNREACHABLE;
}

const int kHeaderLen = sizeof(MSGHEADER::Value);
const int kInitialMessageSize = 1024;

void TransportLayerASIO::ASIOSession::beginRead(const ASIOSessionHandle& self, Message* message) {
    SharedBuffer buf = SharedBuffer::allocate(kInitialMessageSize);
    MsgData::View md = buf.get();

    asio::error_code ec;

    // TODO: SSL
    const std::size_t bytesRead = self->_stream->read(asio::buffer(md.view2ptr(), kHeaderLen), ec);

    if (!ec && (bytesRead == kHeaderLen)) {
        continueRead(self, message, buf);
    } else if (ec != asio::error::would_block) {
        self->complete(ErrorCodes::BadValue, "failed header read");
    } else {
        self->_posted();

        auto asyncBuffer = asio::buffer(md.view2ptr(), kHeaderLen);

        // TODO::SSL, TIMERS
        self->_stream->read(asyncBuffer, [message, buf, self](std::error_code ec, std::size_t read) {
            invariant(ec || (kHeaderLen == read));
            if (ec)
                return self->complete(ErrorCodes::BadValue, "failed header async read");
            continueRead(self, message, buf);
        });
    }
}

void TransportLayerASIO::ASIOSession::continueRead(const ASIOSessionHandle& self, Message* message, SharedBuffer buf) {
    MsgData::View md = buf.get();
    const size_t msgLen = md.getLen();

    // TODO: Validate msgLen

    if (msgLen > kInitialMessageSize) {
        buf.realloc(msgLen);
        md = buf.get();
    }

    asio::error_code ec;

    // TODO: SSL
    const std::size_t bytesRead = self->_stream->read(asio::buffer(md.data(), msgLen - kHeaderLen), ec);

    if (!ec && (bytesRead == (msgLen - kHeaderLen))) {
        message->setData(std::move(buf));
        self->complete(Status::OK());
    } else if (ec != asio::error::would_block) {
        self->complete(ErrorCodes::BadValue, "failed body read");
    } else {
        self->_posted();

        auto asyncBuffer = asio::buffer(md.data() + bytesRead, msgLen - kHeaderLen - bytesRead);

        // TODO: SSL, TIMERS
        self->_stream->read(asyncBuffer, [self, msgLen, bytesRead](std::error_code ec, std::size_t read) {
            invariant(ec || ((msgLen - kHeaderLen - bytesRead) == read));
            if (ec)
                return self->complete(ErrorCodes::BadValue, "failed body async read");
            self->complete(Status::OK());
        });
    }
}


void TransportLayerASIO::ASIOSession::beginWrite(const ASIOSessionHandle& self, Message const& message) {
    const auto msgbuf = message.buf();
    const std::size_t msglen = MsgData::ConstView(msgbuf).getLen();

    asio::error_code ec;
    // TODO: SSL
    const std::size_t bytesWritten = self->_stream->write(asio::buffer(msgbuf, msglen), ec);

    if (!ec && (bytesWritten == msglen)) {
        self->complete(Status::OK());
    } else if (ec != asio::error::would_block) {
        self->complete(ErrorCodes::BadValue, "failed write");
    } else {
        self->_posted();

        auto asyncBuffer = asio::buffer(msgbuf + bytesWritten, msglen - bytesWritten);

        // TODO: SSL, TIMERS
        self->_stream->write(asyncBuffer, [self, msglen, bytesWritten](std::error_code ec, std::size_t written) {
            invariant(ec || (msglen - bytesWritten) == written);
            if (ec)
                return self->complete(ErrorCodes::BadValue, "failed async write");
            self->complete(Status::OK());
        });
    }
}

bool TransportLayerASIO::ASIOSession::closed() const {
    MONGO_UNREACHABLE;
}

boost::optional<Status> TransportLayerASIO::ASIOSession::getOperationStatus() {
    boost::optional<Status> result;

    {
        const stdx::lock_guard<stdx::mutex> lock(_mutex);
        // We shouldn't have a callback if we are here.
        invariant(!_callback);
        result = std::move(_status);
    }

    return result;
}

void TransportLayerASIO::ASIOSession::getOperationStatus(Ticket&& ticket, TicketCallback&& callback) {
    const stdx::lock_guard<stdx::mutex> lock(_mutex);
    if (_status) {
        return callback(_status.get());
    }
    _callback = callback;
}

void TransportLayerASIO::ASIOSession::work() {
    static_cast<TransportLayerASIO*>(getTransportLayer())->_io_service.run_one();
}

void TransportLayerASIO::ASIOSession::_posted() {
    // TODO: Can we leverage this data to eliminate locks in getOperationStatus and _complete?
}

void TransportLayerASIO::ASIOSession::_complete(Status&& status) {
    TicketCallback callback;
    {
        const stdx::lock_guard<stdx::mutex> lock(_mutex);

        // Someone beat us here with another status. Probably, they cancelled
        // the operation. In any event, there is nothing more for us to do.
        if (_status)
            return;

        if (!_callback) {
            _status = std::move(status);
        } else {
            callback = std::move(_callback);
        }
    }
    if (callback)
        callback(status);
}

}  // namespace transport
}  // namespace mongo
