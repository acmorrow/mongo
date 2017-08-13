/**
 *    Copyright (C) 2017 MongoDB Inc.
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

#include "mongo/platform/basic.h"

#include "mongo/transport/service_entry_point.h"
#include "mongo/transport/transport_layer_asio.h"
#include "mongo/unittest/unittest.h"
#include "mongo/util/assert_util.h"

namespace mongo {
namespace {

class ServiceEntryPointUtil : public ServiceEntryPoint {
public:
    void startSession(transport::SessionHandle session) override {
        Message m;
        Status s = session->sourceMessage(&m).wait();

        ASSERT_NOT_OK(s);

        tll->end(session);
    }

    void endAllSessions(transport::Session::TagMask tags) override {
    }

    DbResponse handleRequest(OperationContext* opCtx, const Message& request) override {
        MONGO_UNREACHABLE;
    }

    transport::TransportLayerASIO* tll = nullptr;
};

TEST(TransportLayerASIO, TestShutdownDoesNotHang) {
    ServiceEntryPointUtil sepu;

    transport::TransportLayerASIO::Options opts;
    opts.port = 0;  // Pick any port

#ifndef _WIN32
    opts.useUnixSockets = false;
#endif

    transport::TransportLayerASIO tll(opts, &sepu);

    sepu.tll = &tll;

    ASSERT_OK(tll.setup());
    ASSERT_OK(tll.start());

    tll.shutdown();
}

}  // namespace
}  // namespace mongo
