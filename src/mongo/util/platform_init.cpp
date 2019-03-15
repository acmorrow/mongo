/**
 *    Copyright (C) 2018-present MongoDB, Inc.
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the Server Side Public License, version 1,
 *    as published by MongoDB, Inc.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    Server Side Public License for more details.
 *
 *    You should have received a copy of the Server Side Public License
 *    along with this program. If not, see
 *    <http://www.mongodb.com/licensing/server-side-public-license>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the Server Side Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kControl

#include "mongo/platform/basic.h"

#ifdef _WIN32
#include <appmodel.h>
#include <crtdbg.h>
#include <mmsystem.h>
#include <stdio.h>
#include <stdlib.h>
#endif

#include "mongo/base/init.h"
#include "mongo/util/log.h"
#include "mongo/util/scopeguard.h"
#include "mongo/util/stacktrace.h"

#ifdef _WIN32

namespace mongo {

MONGO_INITIALIZER(Behaviors_Win32)(InitializerContext*) {
    // do not display dialog on abort()
    _set_abort_behavior(0, _CALL_REPORTFAULT | _WRITE_ABORT_MSG);

    // hook the C runtime's error display
    _CrtSetReportHook(crtDebugCallback);

    if (_setmaxstdio(2048) == -1) {
        warning() << "Failed to increase max open files limit from default of 512 to 2048";
    }

    // Let's try to set minimum Windows Kernel quantum length to smallest viable timer resolution in
    // order to allow sleepmillis() to support waiting periods below Windows default quantum length
    // (which can vary per Windows version)
    // See https://msdn.microsoft.com/en-us/library/windows/desktop/dd743626(v=vs.85).aspx
    TIMECAPS tc;
    int targetResolution = 1;
    int timerResolution;

    if (timeGetDevCaps(&tc, sizeof(TIMECAPS)) != TIMERR_NOERROR) {
        warning() << "Failed to read timer resolution range.";
        if (timeBeginPeriod(1) != TIMERR_NOERROR) {
            warning() << "Failed to set minimum timer resolution to 1 millisecond.";
        }
    } else {
        timerResolution =
            std::min(std::max(int(tc.wPeriodMin), targetResolution), int(tc.wPeriodMax));
        invariant(timeBeginPeriod(timerResolution) == TIMERR_NOERROR);
    }

    // https://jira.mongodb.org/browse/SERVER-39728
    //
    // It appears that during process shutdown, the CRT attempts to
    // call AppPolicyGetProcessTerminationMethod during last
    // rites. That function in turn wants to load a DLL, and we crash.
    //
    // By calling AppPolicyGetProcessTerminationMethod here, we force
    // whatever support code is needed by that entry point to be
    // loaded early in the process lifetime so that we don't need to
    // do that load at shutdown.
#if 0
    warning() << "XXX ACM START";

    HANDLE accessToken;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_READ, &accessToken)) {
        const auto str = errnoWithPrefix("Failed to OpenProcessToken");
        return Status(ErrorCodes::InternalError, str);
    }
    const auto accessTokenGuard = makeGuard([&] { CloseHandle(accessToken); });

    AppPolicyProcessTerminationMethod value;
    const auto result = AppPolicyGetProcessTerminationMethod(accessToken, &value);
    if (result != ERROR_SUCCESS) {
        // TODO: Is there something more we can do with the error code?
        return Status(ErrorCodes::InternalError, "Failed to call AppPolicyGetProcessTerminationMethod");
    }

    if ((value != AppPolicyProcessTerminationMethod_ExitProcess) && (value != AppPolicyProcessTerminationMethod_TerminateProcess)) {
        return Status(ErrorCodes::InternalError, "Process has an unknown termination method");
    }

    warning() << "XXX ACM FINISH";
#endif

    return Status::OK();
}

}  // namespace mongo

#endif  // _WIN32
