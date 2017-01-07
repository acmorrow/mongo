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
#include "mongo/executor/async_stream.h"
#include "mongo/util/log.h"
#include "mongo/stdx/memory.h"
#include "mongo/transport/service_entry_point.h"

namespace mongo {
namespace transport {

TransportLayerASIO::TransportLayerASIO(ServiceEntryPoint* sep) : _sep(sep), _running(false), _io_service() {}

TransportLayerASIO::~TransportLayerASIO() = default;

transport::Ticket TransportLayerASIO::sourceMessage(const SessionHandle& session,
                                                    Message* message,
                                                    Date_t expiration) {
    auto asioSession = checked_pointer_cast<ASIOSession>(session);
    std::cout << "BEGIN SOURCE\n" << std::endl;
    asioSession->beginRead(asioSession, message);
    auto ticket = stdx::make_unique<ASIOTicket>(asioSession, expiration);
    return {this, std::move(ticket)};
}

transport::Ticket TransportLayerASIO::sinkMessage(const SessionHandle& session,
                                                  const Message& message,
                                                  Date_t expiration) {
    auto asioSession = checked_pointer_cast<ASIOSession>(session);
    std::cout << "BEGIN SINK\n" << std::endl;
    asioSession->beginWrite(asioSession, message);
    auto ticket = stdx::make_unique<ASIOTicket>(asioSession, expiration);
    return {this, std::move(ticket)};
}

Status TransportLayerASIO::wait(Ticket&& ticket) {

    std::cout << "BEGIN WAIT\n" << std::endl;

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

    //if (session->closed())
    //    return TransportLayer::TicketSessionClosedStatus;

    return session->wait();
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

    session->wait(std::move(ownedTicket), std::move(callback));
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

    _permanent_workers.push_back(stdx::thread([this] {
        try {
            asio::io_service::work work(_io_service);
            std::error_code ec;
            _io_service.run(ec);
            if (ec) {
                severe() << "Failure in _io_service.run(): " << ec.message();
                fassertFailed(40367);
            }
        } catch (...) {
            severe() << "Uncaught exception in NetworkInterfaceASIO IO "
                "worker thread of type: "
                     << exceptionToStatus();
            fassertFailed(40368);
        }
    }));

    try {
        asio::ip::tcp::acceptor tcp(_io_service);
        const asio::ip::tcp::endpoint endpoint(asio::ip::tcp::v4(), 28017);
        tcp.open(endpoint.protocol());
        tcp.set_option(asio::ip::tcp::acceptor::reuse_address(true));
        tcp.bind(endpoint);
        tcp.listen();
        _acceptors.emplace_back(std::move(tcp));
    } catch(...) {
        return exceptionToStatus();
    }

    try {
        asio::local::stream_protocol::acceptor local(_io_service);
        const asio::local::stream_protocol::endpoint endpoint("/tmp/mongodb-28017.sock");
        local.open(endpoint.protocol());
        local.bind(endpoint);
        local.listen();
        _acceptors.emplace_back(std::move(local));
    }
    catch(...) {
        return exceptionToStatus();
    }

    for (auto&& acceptor : _acceptors)
        _begin_accept(acceptor);

    return Status::OK();
}

void TransportLayerASIO::_begin_accept(generic_acceptor& acceptor) {
    auto socket = std::make_shared<asio::generic::stream_protocol::socket>(_io_service);
    acceptor.async_accept(*socket, [this, &acceptor, socket](std::error_code ec) {
        socket->non_blocking(true);
        auto session = std::make_shared<ASIOSession>(this, std::move(*socket));

        stdx::list<std::weak_ptr<ASIOSession>> list;
        auto it = list.emplace(list.begin(), session);

        {
            // Add the new session to our list
            stdx::lock_guard<stdx::mutex> lk(_sessionsMutex);
            session->setIter(it);
            _sessions.splice(_sessions.begin(), list, it);
        }

        invariant(_sep);
        _sep->startSession(std::move(session));

        this->_begin_accept(acceptor);
    });
}

void TransportLayerASIO::shutdown() {
    MONGO_UNREACHABLE;
}

TransportLayerASIO::ASIOSession::ASIOSession(TransportLayerASIO* tl, stream_socket socket) : _tl(tl), _strand(tl->_io_service) {
    _stream = stdx::make_unique<executor::AsyncStream>(&_strand, std::move(socket));
}

const HostAndPort& TransportLayerASIO::ASIOSession::remote() const {
    static const HostAndPort result{"127.0.0.1", 28017};
    return result;
    MONGO_UNREACHABLE;
}

const HostAndPort& TransportLayerASIO::ASIOSession::local() const {
    static const HostAndPort result{"127.0.0.1", 28017};
    return result;
    MONGO_UNREACHABLE;
}

const int kHeaderLen = sizeof(MSGHEADER::Value);
const int kInitialMessageSize = 1024;

void TransportLayerASIO::ASIOSession::beginRead(const ASIOSessionHandle& self, Message* message) {

    std::cout << "BR0\n" << std::endl;
    SharedBuffer buf = SharedBuffer::allocate(kInitialMessageSize);
    MsgData::View md = buf.get();

    asio::error_code ec;

    // TODO: SSL
    const std::size_t bytesRead = self->_stream->read(asio::buffer(md.view2ptr(), kHeaderLen), ec);

    if (!ec && (bytesRead == kHeaderLen)) {
        std::cout << "BR1\n" << std::endl;
        continueRead(self, message, std::move(buf));
    } else if (ec != asio::error::would_block) {
        std::cout << "BR2\n" << std::endl;
        self->complete(ErrorCodes::BadValue, "failed header read");
    } else {
        std::cout << "BR3\n" << std::endl;
        self->_posted();

        auto asyncBuffer = asio::buffer(md.view2ptr(), kHeaderLen);

        // TODO::SSL, TIMERS
        self->_stream->read(asyncBuffer, [message, buf=std::move(buf), self](std::error_code ec, std::size_t read) mutable {
            std::cout << "BR4\n" << std::endl;
            invariant(ec || (kHeaderLen == read));
            if (ec)
                return self->complete(ErrorCodes::BadValue, "failed header async read");
            continueRead(self, message, std::move(buf));
        });
    }
}

void TransportLayerASIO::ASIOSession::continueRead(const ASIOSessionHandle& self, Message* message, SharedBuffer&& buf) {

    std::cout << "CR0\n" << std::endl;

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
        std::cout << "CR1\n" << std::endl;

        message->setData(std::move(buf));
        self->complete(Status::OK());
    } else if (ec != asio::error::would_block) {
        std::cout << "CR2\n" << std::endl;

        self->complete(ErrorCodes::BadValue, "failed body read");
    } else {
        std::cout << "CR3\n" << std::endl;
        self->_posted();

        auto asyncBuffer = asio::buffer(md.data() + bytesRead, msgLen - kHeaderLen - bytesRead);

        // TODO: SSL, TIMERS
        self->_stream->read(asyncBuffer, [self, message, buf=std::move(buf), msgLen, bytesRead](std::error_code ec, std::size_t read) mutable {
            std::cout << "CR4\n" << std::endl;
            invariant(ec || ((msgLen - kHeaderLen - bytesRead) == read));
            if (ec)
                return self->complete(ErrorCodes::BadValue, "failed body async read");
            message->setData(std::move(buf));
            self->complete(Status::OK());
        });
    }
}

void TransportLayerASIO::ASIOSession::beginWrite(const ASIOSessionHandle& self, Message const& message) {

    std::cout << "BW0\n" << std::endl;

    const auto msgbuf = message.buf();
    const std::size_t msglen = MsgData::ConstView(msgbuf).getLen();

    asio::error_code ec;
    // TODO: SSL
    const std::size_t bytesWritten = self->_stream->write(asio::buffer(msgbuf, msglen), ec);

    if (!ec && (bytesWritten == msglen)) {
        std::cout << "BW1\n" << std::endl;
        self->complete(Status::OK());
    } else if (ec != asio::error::would_block) {
        std::cout << "BW2\n" << std::endl;
        self->complete(ErrorCodes::BadValue, "failed write");
    } else {
        std::cout << "BW3\n" << std::endl;
        self->_posted();

        auto asyncBuffer = asio::buffer(msgbuf + bytesWritten, msglen - bytesWritten);

        // TODO: SSL, TIMERS
        self->_stream->write(asyncBuffer, [self, msglen, bytesWritten](std::error_code ec, std::size_t written) {

            std::cout << "BW4\n" << std::endl;
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
        result.swap(_status);
        invariant(!_status);
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

status TransportLayerASIO::ASIOSession::wait() {
    // If we call run_one here, then we may block forever, because one of the 'intrinsic' threads
    // may have run the last of the pending work.

    // If we call poll_one here, then we may spin forever, because there may be no work to do.

    // How can we know which one to call?
    static_cast<TransportLayerASIO*>(getTransportLayer())->_io_service.poll_one();
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
