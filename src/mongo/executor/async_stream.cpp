/**
 *    Copyright (C) 2015 MongoDB Inc.
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

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kASIO

#include "mongo/platform/basic.h"

#include "mongo/executor/async_stream.h"
#include "mongo/executor/async_stream_common.h"
#include "mongo/util/assert_util.h"
#include "mongo/util/log.h"

namespace mongo {
namespace executor {

using asio::ip::tcp;

AsyncStream::AsyncStream(asio::io_service::strand* strand)
    : _strand(strand), _stream(_strand->get_io_service()) {}

AsyncStream::AsyncStream(asio::io_service::strand* strand, stream_socket socket)
    : _strand(strand), _stream(std::move(socket)), _connected(true) {}

AsyncStream::~AsyncStream() {
    destroyStream(&_stream, _connected);
}

void AsyncStream::connect(tcp::resolver::iterator iter, ConnectHandler&& connectHandler) {

    auto stream = std::make_shared<asio::ip::tcp::socket>(_strand->get_io_service());

    asio::async_connect(
        *stream,
        std::move(iter),
        // We need to wrap this with a lambda of the right signature so it compiles, even
        // if we don't actually use the resolver iterator.
        _strand->wrap([this, connectHandler, stream](std::error_code ec, tcp::resolver::iterator iter) {
            if (ec) {
                return connectHandler(ec);
            }

            // We assume that our owner is responsible for keeping us alive until we call
            // connectHandler, so _connected should always be a valid memory location.
            ec = setStreamNonBlocking(stream.get());
            if (ec) {
                return connectHandler(ec);
            }

            ec = setStreamNoDelay(stream.get());
            if (ec) {
                return connectHandler(ec);
            }

            _stream = std::move(*stream);
            _connected = true;

            return connectHandler(ec);
        }));
}

std::size_t AsyncStream::write(asio::const_buffer buffer, std::error_code& ec) {
    return writeStream(&_stream, _connected, buffer, ec);
}

void AsyncStream::write(asio::const_buffer buffer, StreamHandler&& streamHandler) {
    writeStream(&_stream, _strand, _connected, buffer, std::move(streamHandler));
}

std::size_t AsyncStream::read(asio::mutable_buffer buffer, std::error_code& ec) {
    return readStream(&_stream, _connected, buffer, ec);
}

void AsyncStream::read(asio::mutable_buffer buffer, StreamHandler&& streamHandler) {
    readStream(&_stream, _strand, _connected, buffer, std::move(streamHandler));
}

void AsyncStream::cancel() {
    cancelStream(&_stream);
}

bool AsyncStream::isOpen() {
    return checkIfStreamIsOpen(&_stream, _connected);
}

}  // namespace executor
}  // namespace mongo
